/* PostgreSQL Wire Protocol over TCP/IP socket */
#include <ctype.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <stdbool.h>

#define USE_MALLOC
#include "pgwire.h"
#include "util.h"
#include "../str.h"

/* Read to given memory */
static bool read_to(PgConn *c, char *to, size_t bytes) {
  ssize_t count = read(c->sockfd, to, bytes);
  if(count != bytes) {
    err("Could not read from socket, expected %zu bytes, got %zu.", bytes, count);
    return false;
  }
  return true;
}

/* Read to connection buffer, incrementing its position.
 * Sets *data_out to start of read bytes.
 */
static bool read_buf(PgConn *c, size_t bytes, char **data_out) {
  expect(pg_ensure_buf(c, bytes));
  char *data = &c->buf[c->buf_pos];
  expect(read_to(c, data, bytes));
  //dbg("  read %zd bytes to %s", bytes, data);
  c->buf_pos += bytes;
  *data_out = data;
  return true;
}

/* Read message from connection socket into *msg.
 * The payload data is stored in connection buffer.
 */
static bool read_msg(PgConn *c, PgMessage *msg) {
  char hdr[5];
  expect(read_to(c, hdr, 5));
  msg->type = hdr[0];
  msg->len = ntohl(*((int32_t*)&hdr[1])) - 4;
  char *data;
  msg->data = c->buf_pos;
  msg->read = c->buf_pos;
  expect(read_buf(c, msg->len, &data));
  return true;
}

bool read_startup_messages(PgConn *c) {
  bool auth_ok = false;
  PgMessage m;
  size_t buf_pos = c->buf_pos;
  while(m.type != 'Z') {
    expect(read_msg(c, &m));
    switch(m.type) {
    case 'R': { // auth status
      int status;
      get_i32(c, m.read, status);
      if(status == 0) auth_ok = true;
      break;
    }

    case 'K': // cancellation key data
      get_i32(c, m.read, c->pid);
      get_i32(c, m.read, c->secret_key);
      dbg("cid: %d, secret key: %d", c->pid, c->secret_key);
      break;
      // S=parameter status, Z=ready for query
    case 'S':
      //dbg("%s = %s", &c->buf[m.data], &c->buf[m.data]+(strlen(&c->buf[m.data])+1));
      break;
    default: break;
    }
  }
  c->buf_pos = buf_pos; // discard messages
  return auth_ok;
}


static void extract_val(char **start, char *to) {
  char *at = *start;
  while(*at != 0 && *at != ' ') {
    *to = *at;
    to++; at++;
  }
  *to = 0;
  while(*at == ' ') at++; // skip the whitespace
  *start = at;
}

/* Send current buffer */
static bool pg_send(PgConn *c) {
  dbg("Sending %zu bytes", c->buf_pos);
  /*for (int i = 0; i < c->buf_pos; i++) {
    if (isprint(c->buf[i]))
      printf("%c", c->buf[i]);
    else printf(" ");
    printf(" (%d)\n", c->buf[i], c->buf[i]);
    }*/
  if(!write(c->sockfd, c->buf, c->buf_pos)) {
    err("Unable to write %zu bytes to socket.", c->buf_size);
    return false;
  }
  c->buf_pos = 0; // reset buffer position
  return true;
}


PgConn *pg_connect(str ci) {
  str user = {0}, database = {0}, password = {0};
  struct sockaddr_in to;
  memset(&to, 0, sizeof(to));

  to.sin_family = AF_INET;
  to.sin_port = htons(5432);

  str field;
  while (str_splitat(ci, " ", &field, &ci)) {
    str key, value;
    str_splitat(field, "=", &key, &value);
    if(str_eq_constant(key, "host")) {
      char host[128];
      snprintf(host,128,STR_FMT, STR_ARG(value));
      dbg("host: %s", host);
      struct hostent *h = gethostbyname(host);
      if(!h) {
        err("Unable to resolve host: %s", host);
        return NULL;
      }

      if(h->h_addrtype == AF_INET) {
        dbg("got AF_INET address");
        to.sin_addr = *((struct in_addr **)h->h_addr_list)[0];
      } else {
        err("Unable to resolve host %s, got type %d", host, h->h_addrtype);
        return NULL;
      }
    } else if (str_eq_constant(key, "port")) {
      long port;

      if(!str_to_long(value, &port)) {
        err("Unable to extract port");
        return NULL;
      }
      dbg("port: %ld", port);
      to.sin_port = htons(port);
    } else if (str_eq_constant(key, "database")) {
      database = value;
    } else if (str_eq_constant(key, "user")) {
      user = value;
    } else if (str_eq_constant(key, "password")) {
      password = value;
    } else {
      err("Unsupported connection info key: "STR_FMT, STR_ARG(key));
      return NULL;
    }
  }

  // connect and send startup message, expect auth ok response
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if(sockfd < 0) {
    err("couldn't create socket");
    return NULL;
  }
   struct timeval tv;
   tv.tv_sec = 60;
   tv.tv_usec = 0;
   setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(struct timeval));
   setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(struct timeval));

  if(connect(sockfd, (struct sockaddr *)&to, sizeof(to)) < 0) {
    err("connect failed");
    return NULL;
  }

  PgConn *c = malloc(sizeof(PgConn));
  c->sockfd = sockfd;
  c->buf = malloc(MIN_BUFFER_SIZE);
  if(c->buf == NULL) goto fail;
  c->buf_pos = 0;
  c->buf_size = MIN_BUFFER_SIZE;


  // len (i32), proto ver (i32), "user\0", username, "database\0", dbname, \0
  mark_len(c);
  put_i32(c, 196608); // always this version
  if (user.len) {
    put_string(c, "user");
    put_str(c, user);
  }
  if (database.len) {
    put_string(c, "database");
    put_str(c, database);
  }
  if (password.len) {
    put_string(c, "password");
    put_str(c, password);
  }
  put_ch(c, 0); // end message
  update_len(c);

  if(!pg_send(c)) {
    err("Couldn't write startup message");
    goto fail;
  }

  // last thing, malloc a buffer for writing/reading
  if(!read_startup_messages(c)) goto fail;
  return c;

 fail:
  if(c && c->buf) free(c->buf);
  close(sockfd);
  if(c) free(c);
  return NULL;
}

void pg_close(PgConn *c) {
  close(c->sockfd);
  free(c->buf);
  free(c);
}

bool pg_ensure_buf(PgConn *c, size_t extra) {
  //printf("pg_ensure_buf(%p), pos: %zu, size: %zu, extra: %zu\n", c->buf, c->buf_pos, c->buf_size, extra);
  size_t wanted = c->buf_pos + extra;
  size_t size = c->buf_size;
  if(wanted > size) {
    dbg("wanted %zd, current %zd", wanted, size);
    size_t new_size = c->buf_size * BUFFER_INCREASE_FACTOR;
    size_t increase = new_size - size;
    if(increase < MIN_BUFFER_INCREASE) {
      new_size = size + MIN_BUFFER_INCREASE;
    } else if(increase > MAX_BUFFER_INCREASE) {
      // if some huge increase is coming, only increase by wanted amount
      new_size = wanted;
    }
    dbg("realloc from %zd to %zd", size, new_size);
    char *new_buf = realloc(c->buf, new_size);
    if(new_buf == NULL) {
      err("Unable to allocate more buffer space, at: %zu, need: %zu",
              size, new_size);
      return false;
    }
    c->buf = new_buf;
    c->buf_size = new_size;
  }
  return true;
}


/* put Parse message to buffer */
static bool put_parse(PgConn *c, const char* sql, int num_params, int *param_oids) {
  put_ch(c,'P');
  mark_len(c);
  put_ch(c,0); // no name for prepared statement
  put_string(c,sql);
  put_i16(c,num_params);
  for(int i=0;i<num_params;i++) put_i32(c,param_oids[i]);
  update_len(c);
  return true;
}

/* put Bind message to buffer */
static bool put_bind(PgConn *c, int num_params, str *param_data) {
  put_ch(c, 'B');
  mark_len(c);
  // 'B', i32 len, portal name (none), prepared name (none), i16 (num param formats: 0 (text)),
  // for each parameter:
  // - i32 len
  // - bytes
  // i16 result column format codes: 1
  // i16 result column format: 1 (binary)
  put_ch(c,0); // portal name (none)
  put_ch(c,0); // prepared stmt name (none)
  put_i16(c,0); // text format for all params (text)
  put_i16(c,num_params);
  for(int p = 0; p < num_params; p++) {
    put_len_str(c, param_data[p]);
  }
  put_i16(c,1); // format for all results
  put_i16(c,1); // binary
  update_len(c);
  return true;
}

static bool put_describe_portal(PgConn *c) {
  put_ch(c, 'D');
  mark_len(c);
  put_ch(c, 'P');
  put_ch(c, 0); // empty named portal
  update_len(c);
  return true;
}

static bool put_execute(PgConn *c) {
  put_ch(c, 'E');
  mark_len(c);
  put_ch(c, 0); // empty named portal
  put_i32(c, 0); // unlimited rows
  update_len(c);
  return true;
}

static bool put_sync(PgConn *c) {
  put_ch(c, 'S');
  put_i32(c, 4); // length only
  return true;
}

static bool handle_notice(PgConn *c, int size) {
  if(!pg_ensure_buf(c, size)) return false;
  char *notice = &c->buf[c->buf_pos];
  if(read(c->sockfd, notice, size) != size) {
    err("Unable to read %d bytes for notice.", size);
    return false;
  }
  while(*notice) {
    if(*notice == 'V' || *notice == 'M') {
      fprintf(stdout, "NOTICE: %s ", notice+1);
    }
    notice += 2 + strlen(notice+1);
  }
  fprintf(stdout, "\n");
  return true;
}

static bool expect_msg(PgConn *c, char msg, int expected_size) {
  char hdr[5];
 start:
  if(read(c->sockfd, hdr, 5) != 5) {
    err("Could not read from socket.");
    return false;
  }
  int size = ntohl(*((int32_t*)&hdr[1]));
  if('N' == hdr[0]) {
    if(!handle_notice(c, size-4)) return false;
    goto start;
  }
  if(msg != hdr[0]) {
    err("Expected '%c' message from server, got %c.", msg, hdr[0]);
    return false;
  }

  if(expected_size != -1 && size != expected_size) {
    err("Unexpected size in '%c' message, expected %d, got: %d", msg, expected_size, size);
    return false;
  }
  // Read rest of message
  if(size > 4) {
    if(!pg_ensure_buf(c, size - 4)) return false;
    if(read(c->sockfd, &c->buf[c->buf_pos], size-4) != size-4) {
      err("Couldn't read %d bytes from socket.", size-4);
      return false;
    }
  }
  return true;
}

static bool expect_simple(PgConn *c, char msg) { return expect_msg(c, msg, 4); }

static bool expect_ready(PgConn *c) {
  return expect_msg(c, 'Z', 5);
}

void pg_clear(PgConn *c) {
  c->buf_pos = 0;
  if(c->buf_size > 10*MIN_BUFFER_SIZE) {
    // if we have an overly large buffer, realloc it to smaller
    c->buf = realloc(c->buf, MIN_BUFFER_SIZE);
    if(!c->buf) err("Unable to reallocate buffer!");
    c->buf_size = MIN_BUFFER_SIZE;
  }
}

/* Issue a query, sends parse and bind messages. */
PgResult pg_query(PgConn *c, const char *sql, int num_params, int *param_oids,
                  str *param_data) {
  if (!put_parse(c, sql, num_params, param_oids)) goto fail;
  if (!put_bind(c, num_params, param_data)) goto fail;
  if (!put_describe_portal(c)) goto fail;
  if (!put_execute(c)) goto fail;
  if (!put_sync(c)) goto fail;
  if(!pg_send(c)) goto fail;

  c->buf_pos = 0;

  // expect ParseComplete ('1') and BindComplete ('2') messages
  if(!expect_simple(c, '1')) goto fail;
  if(!expect_simple(c, '2')) goto fail;

  PgMessage msg;
  if(!read_msg(c, &msg)) goto fail;
  if(msg.type == 'n') {
    /* got NoData, this executed ok */
    if(!expect_msg(c, 'C', -1)) goto fail; // expect command complete
    if(!expect_ready(c)) goto fail;
    return (PgResult) { true, 0, 0, 0 };
  } else if(msg.type != 'T') {
    err("Expected RowDescription ('B') message, got: %c", msg.type);
    goto fail;
  }
  if(msg.len < 2) {
    err("Expected RowDescription len >= 2, got: %u", msg.len);
    goto fail;
  }

  PgResult res = (PgResult) { true, msg.data, 0, -1 };
  int pos = msg.data;
  get_i16(c, pos, res.fields);

  return res;

 fail:
  c->buf_pos = 0;
  return (PgResult) { false, 0, 0 };
}

bool pg_field(PgConn *c, PgResult res, int field_num, int *oid_out, char **name_out) {
  if(field_num < 0 || field_num >= res.fields) {
    return false;
  } else {
    size_t pos = 2;
    char *name;
    int _tbl, _attr, oid, _typlen, _typmod, _fmt;

    for(int i=0;i<=field_num;i++) {
      // read the oid
      get_string(c, pos, name);
      get_i32(c, pos, _tbl);
      get_i16(c, pos, _attr);
      get_i32(c, pos, oid);
      get_i16(c, pos, _typlen);
      get_i32(c, pos, _typmod);
      get_i16(c, pos, _fmt);
    }
    *oid_out = oid;
    *name_out = name;
    return true;
  }
}

PgRow pg_next_row(PgConn *c, PgResult *res) {
  if(res->row_start != -1) {
    // invalidate old row, read on top of it
    c->buf_pos = res->row_start;
  }
  PgMessage m;
 message:
   if (!read_msg(c, &m))
     goto fail;
   switch (m.type) {
   case 'D': { // DataRow
     res->row_start = m.data;
     return (PgRow){true, true};
   }
   case 'C': { // CommandComplete
     if(!expect_ready(c)) goto fail;
     return (PgRow){true, false};
   }
   case 'N': { // Notice (PENDING: should we have some "notice handler" callback
               // option?)
     size_t len = m.len;
     char *notice = &c->buf[m.read];
     while (len > 1) {
       char field = *notice;
       len--;
       notice++;
       if(field == 'M') // only show message
         fprintf(stdout, "NOTICE: %s\n", notice);
       size_t l = strlen(notice) + 1;
       len -= l;
       notice += l;
     }
     goto message; // read again
   }
   case 'E': { // ErrorResponse
     // Z sync should come after this, but we could just close the connection
     // on error
     size_t len = m.len - 1;
     char type = c->buf[m.read];
     str msg = (str){.data = &c->buf[m.read + 1], .len = len - 1};
     fprintf(stderr, "ERROR(%c): "STR_FMT"\n", type, STR_ARG(msg));
     goto fail;
   }
   default:
     err("Unexpected message from server: %c", m.type);
     goto fail;
  }

 fail:
  return (PgRow) { false, false };
 }


PgVal pg_value(PgConn *c, PgResult *res, int field) {
  size_t pos = res->row_start;
  if(pos == -1) goto fail;
  short fields;
  get_i16(c, pos, fields);
  if(field < 0 || field >= fields) goto fail;
  PgVal v;
  int len;
  for(int i=0;i<=field;i++) {
    get_i32(c, pos, len);
    if(len == -1) {
      v.is_null = true;
      v.len = 0;
      v.data = NULL;
    } else {
      v.is_null = false;
      v.len = len;
      v.data = &c->buf[pos];
      pos += len;
    }
  }
  v.success = true;
  return v;
 fail:
    return (PgVal) { false, false, 0, NULL };
}

PgArray pg_value_arr(PgVal val) {
  PgArray a = {0};
  int32_t *values = (int32_t *)val.data;
  a.ndim = ntohl(values[0]);
  a.has_null = ntohl(values[1]);
  a.element_type = ntohl(values[2]);
  a.dim_size = ntohl(values[3]);
  a.lower_bound = ntohl(values[4]);
  a.data = val.data + 20;
  return a;
}

static char _oid_name[12];
const char *pg_oid_name(int oid) {
  switch (oid) {
  case OID_TEXT:
    strcpy(_oid_name, "text");
    break;
  case OID_BOOL:
    strcpy(_oid_name, "bool");
    break;
  case OID_INT2:
    strcpy(_oid_name, "int2");
    break;
  case OID_INT4:
    strcpy(_oid_name, "int4");
    break;
  case OID_INT8:
    strcpy(_oid_name, "int8");
    break;
  case OID_OID:
    strcpy(_oid_name, "oid");
    break;
  default:
    snprintf(_oid_name, 12, "%d", oid);
  }
  return _oid_name;
}
