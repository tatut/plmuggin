/* Mugshot: serve PL/Muggin templates via HTTP. */
const char *mugshot_logo =
    " ███▄ ▄███▓ █    ██   ▄████   ██████  ██░ ██  ▒█████  ▄▄▄█████▓\n"
    "▓██▒▀█▀ ██▒ ██  ▓██▒ ██▒ ▀█▒▒██    ▒ ▓██░ ██▒▒██▒  ██▒▓  ██▒ ▓▒\n"
    "▓██    ▓██░▓██  ▒██░▒██░▄▄▄░░ ▓██▄   ▒██▀▀██░▒██░  ██▒▒ ▓██░ ▒░\n"
    "▒██    ▒██ ▓▓█  ░██░░▓█  ██▓  ▒   ██▒░▓█ ░██ ▒██   ██░░ ▓██▓ ░ \n"
    "▒██▒   ░██▒▒▒█████▓ ░▒▓███▀▒▒██████▒▒░▓█▒░██▓░ ████▓▒░  ▒██▒ ░ \n"
    "░ ▒░   ░  ░░▒▓▒ ▒ ▒  ░▒   ▒ ▒ ▒▓▒ ▒ ░ ▒ ░░▒░▒░ ▒░▒░▒░   ▒ ░░   \n"
    "░  ░      ░░░▒░ ░ ░   ░   ░ ░ ░▒  ░ ░ ▒ ░▒░ ░  ░ ▒ ▒░     ░    \n"
    "░      ░    ░░░ ░ ░ ░ ░   ░ ░  ░  ░   ░  ░░ ░░ ░ ░ ▒    ░      \n"
    "       ░      ░           ░       ░   ░  ░  ░    ░ ░           \n";




/* Definitions */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdatomic.h>
#include <assert.h>

#ifdef __linux__
#include <sys/sendfile.h>
#endif

#define USE_MALLOC
#include "pgwire.h"

#define STR_IMPLEMENTATION
#include "../str.h"
#include "util.h"

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

void log_error(const char *error) { fprintf(stderr, "\e[31m[ERROR]\e[0m %s", error); }
void log_notice(const char *notice) { fprintf(stdout, "%s", notice); }
void log_debug(const char *debug) { fprintf(stdout, "%s", debug); }

/* == Logging == */

// add timestamp?
#define VA_ARGS(...) , ##__VA_ARGS__
#define mugshot_error(fmt, ...) \
  fprintf(stderr, "[ERROR] " fmt "\n" VA_ARGS(__VA_ARGS__))
#define mugshot_info(fmt, ...)                                                     \
  fprintf(stdout, "[INFO] " fmt "\n" VA_ARGS(__VA_ARGS__))
#ifdef DEBUG
#define mugshot_debug(fmt, ...) \
  fprintf(stdout, "[DEBUG] " fmt "\n" VA_ARGS(__VA_ARGS__))
#else
#define mugshot_debug(...)
#endif

/* == Base64 == */

/* Encode input as base64 to output. Returns true on success or false
 * if there is not enough space or some other error.
 */
bool base64_encode(const uint8_t *input, size_t input_len, uint8_t *output, size_t output_len);

/* Decode base64 by mutating input in place. Returns true on success
 * or false on any error. */
bool base64_decode_mut(uint8_t *input);

/* == SHA1 == */

void SHA1(const uint8_t *data, size_t len, uint8_t *digest);


/* == Arena allocator == */
typedef struct mugshot_arena {
  uint8_t *start;   // where allocated memory starts
  uint8_t *cur;     // current start of unused memory
  uint8_t *end;     // start of memory after slab
} mugshot_arena;

#define MUGSHOT_ARENA_DEFAULT_SIZE (8 * 1024 * 1024) // 8mb default size

#ifdef C23
#define mugshot_new(arena, type, count)                                            \
  (type *)mugshot_arena_alloc(arena, sizeof(type), alignof(type), count)
#else
#define mugshot_new(arena, type, count)                                            \
  (type *)mugshot_arena_alloc(arena, sizeof(type), 1, count)
#endif

/* Allocate memory with alignment */
void *mugshot_arena_alloc(mugshot_arena *arena, ptrdiff_t size, ptrdiff_t align,
                      ptrdiff_t count);
/* Initialize arena with given capacity */
void mugshot_arena_init(mugshot_arena *arena, size_t capacity);

/* Free the arena memory */
void mugshot_arena_free(mugshot_arena *arena);

/* Clear arena so the memory can be reused */
void mugshot_arena_clear(mugshot_arena *arena);

/* Copy string to arena */
char *mugshot_arena_str(mugshot_arena *arena, char *str);

/* == HTTP enum definitions == */
typedef enum {
  MUGSHOT_METHOD_GET,
  MUGSHOT_METHOD_HEAD,
  MUGSHOT_METHOD_POST,
  MUGSHOT_METHOD_PUT,
  MUGSHOT_METHOD_DELETE,
  MUGSHOT_METHOD_OPTIONS,
  MUGSHOT_METHOD_PATCH
  // OPTIONS, TRACE not supported
} mugshot_http_method;

const char *mugshot_http_method_string[] = {
    [MUGSHOT_METHOD_GET] = "GET",
    [MUGSHOT_METHOD_HEAD] = "HEAD",
    [MUGSHOT_METHOD_POST] = "POST",
    [MUGSHOT_METHOD_PUT] = "PUT",
    [MUGSHOT_METHOD_DELETE] = "DELETE",
    [MUGSHOT_METHOD_OPTIONS] = "OPTIONS",
    [MUGSHOT_METHOD_PATCH] = "PATCH"
};

typedef enum {
  MUGSHOT_TEXT_HTML,
  MUGSHOT_APPLICATION_JSON,
  MUGSHOT_TEXT_PLAIN,
  MUGSHOT_APPLICATION_OCTET_STREAM
} mugshot_http_content_type;

str mugshot_http_content_type_str[] = {
  [MUGSHOT_TEXT_HTML] = str_constant("text/html"),
  [MUGSHOT_APPLICATION_JSON] = str_constant("application/json"),
  [MUGSHOT_TEXT_PLAIN] = str_constant("text/plain"),
  [MUGSHOT_APPLICATION_OCTET_STREAM] = str_constant("application/octet-stream")};


/* == Trie based endpoint routing == */

typedef struct mugshot_endpoint_arg {
  str name;
  long oid; // OID type, eg 25 = TEXT
  bool path;
} mugshot_endpoint_arg;

typedef struct mugshot_endpoint {
  mugshot_http_method method;
  str name; // name of the function (might not be the same as path)
  str schema; // the schema where the function exists
  str path; // the defined path after the method, like /foo/:id/bar
  mugshot_http_content_type content_type; // returned content type
  size_t nargs; // how many arguments there are
  mugshot_endpoint_arg *args;
} mugshot_endpoint;



typedef struct mugshot_trie {
  struct mugshot_trie *children;
  uint8_t num_children;
  uint8_t children_capacity;
  char ch;
  bool leaf;
  void *data; // arbitrary user data
} mugshot_trie;

int mugshot_trie_node_compare(const void *a, const void *b) {
  return ((mugshot_trie *)a)->ch - ((mugshot_trie *)b)->ch;
}

// bsearch over sorted children
mugshot_trie *_trie_find_child(mugshot_trie *node, char ch) {
  uint8_t n = node->num_children;
  if(n == 0) {
    return NULL;
  } else if(n == 1) {
    return (node->children[0].ch == ch) ? &node->children[0] : NULL;
  }

  int l=0, r=n-1, i;
  while(l <= r) {
    i = l + ((r-l)/2);
    int d = node->children[i].ch - ch;
    if(d == 0) {
      return &node->children[i];
    } else if(d < 0) {
      l = i+1;
    } else if(d > 0) {
      r = i-1;
    }
  }
  return NULL;
}

/* Insert into the trie, returning the leaf node.
 * If already exists, returns the existing node.
 */
mugshot_trie *_trie_insert(mugshot_trie *root, char *word, mugshot_arena *arena) {
  char ch;
  mugshot_trie *at = root;
  mugshot_trie *node = root;
  while((ch = *word++)) {
    node = NULL;
    // find child in current children
    node = _trie_find_child(at, ch);
    if(node == NULL) {
      if(!arena) return NULL; // don't insert
      if(at->num_children == 255) {
        mugshot_error("Trie node already at max children.");
        return NULL;
      }
      if(at->children_capacity < at->num_children+1) {
        size_t new_size = at->children_capacity ? at->children_capacity * 2 : 4;
        if(new_size > 255) {
          new_size = 255;
        }
        mugshot_debug("Reallocing trie children from %d => %zu", at->children_capacity, new_size);
        mugshot_trie *old_children = at->children;
        at->children = mugshot_new(arena, mugshot_trie, new_size);
        if(!at->children) {
          mugshot_error("Could not reallocate Trie children from arena");
          return NULL;
        }
        at->children_capacity = new_size;
        if(old_children) {
          memcpy(at->children, old_children, at->num_children);
        }
        // NOTE: old children will be orphaned, in arena
      }

      node = &at->children[at->num_children++];
      node->ch = ch;
      node->num_children = 0;
      node->children = NULL;
      node->data = NULL;
      qsort(at->children, at->num_children, sizeof(mugshot_trie), mugshot_trie_node_compare);
      // refind the node after sort
      node = _trie_find_child(at, ch);
    }
    at = node;
  }
  assert(node != NULL);
  if(arena) {
    node->leaf = true;
  }
  return node;
}


/* == HTTP server == */

typedef enum {
  MUGSHOT_STATE_INITIAL = 0,
  MUGSHOT_STATE_STARTED = 1,
  MUGSHOT_STATE_STOPPED = 2
} mugshot_server_state;

// State describing the current connection state
// REQ_P_* = request parse position
typedef enum {
  MUGSHOT_CS_REQ_START = 0, // method, path and version
  MUGSHOT_CS_REQ_HEADERS,
  MUGSHOT_CS_REQ_BODY, // reading body
  MUGSHOT_CS_REQ_DONE, // request parsing done
  MUGSHOT_CS_RES_STATUS, // status sent, can send headers
  MUGSHOT_CS_RES_BODY_STARTED, // started sending body
  MUGSHOT_CS_RES_DONE, // response sent, close connection or read another for keepalive

  MUGSHOT_CS_WS_HANDSHAKE, // sending ws handshake
  MUGSHOT_CS_WS_IDLE, // waiting for message
  MUGSHOT_CS_WS_READ, // reading a message
  MUGSHOT_CS_WS_WRITE, // sending a message
  MUGSHOT_CS_WS_CLOSED, // ws closed,

  MUGSHOT_CS_ERR // in some error state, close this socket immediately
} mugshot_conn_state;



typedef struct {
  char *name;
  char *value;
} mugshot_header;

/* HTTP connection, can be a regular request or WebSocket */
typedef struct mugshot_conn {

  mugshot_arena arena;
  mugshot_http_method method;
  const char* path;
  int headers_size;
  mugshot_header *headers;

  // internal, not meant for applications to directly use
  int _socket; // HTTP connection socket
  mugshot_conn_state state; // current state
  char *buf; // read/write buffer depending on state
  size_t buf_len; // how much has been read
  size_t buf_capacity; // how much capacity in the buffer
  size_t read_pos; // currently reading from
  size_t append_pos; // position append newly received data

  atomic_bool processing; // 1 if this is being processed

  // to maintain free list
  struct mugshot_conn *_next;
} mugshot_conn;


struct mugshot_server;

typedef struct {
  int id; // worker number for logging
  pthread_t thread;

  struct mugshot_server *server;
} mugshot_worker;

typedef struct mugshot_server {
  str connection; // pg connection string
  str schema; // name of schema to publish
  mugshot_server_state state;
  uint16_t threads;
  uint16_t backlog;
  uint16_t port;
  int server_fd;

  mugshot_arena arena;

  // dynamic array of routes (PENDING: use a trie instead)
  mugshot_endpoint *routes;

  // threads count of workers
  mugshot_worker *workers;

  // active connections
  mugshot_conn *connections;
  size_t connections_count;
  size_t connections_capacity;

  // ring buffer queue of connections waiting to be taken be a worker
  pthread_cond_t has_waiting_clients;
  pthread_mutex_t acquire_client;
  pthread_mutex_t release_client;

  mugshot_conn **waiting_client;
  int waiting_client_count; // number of waiting clients
  int first_waiting_client;
  int last_waiting_client;
  mugshot_conn *free_client; // free list of connections

} mugshot_server;

void mugshot_print_endpoint(mugshot_endpoint *e);
void mugshot_add_endpoint(mugshot_server *s, mugshot_endpoint route);

bool mugshot_server_start(mugshot_server *s);
void mugshot_server_accept_loop(mugshot_server *s);
void mugshot_server_wait(mugshot_server *s);

bool mugshot_path_match(mugshot_endpoint *route, const char *path);

/* Start server and enter accept loop. */
void mugshot_serve(mugshot_server *s);

bool mugshot_response_static(mugshot_conn *req, const char *content_type, size_t content_length, const char *content);

/* == Base64 == */

static const char base64_enc[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// base64 decode ascii, [0] ('+') == 62, [79] ('z') = 51
static const uint8_t base64_dec[] = {
  62, 64, 64, 64, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60,
  61, 64, 64, 64, 64, 64, 64, 64, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
  11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 64, 64, 64, 64,
  64, 64, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42,
  43, 44, 45, 46, 47, 48, 49, 50, 51};

bool base64_encode(const uint8_t *input, size_t input_len, uint8_t *output, size_t output_len) {
  #define out(c) do { if(j >= output_len) { return false; } else { output[j++] = (c); } } while(false)
  if(!input || input_len == 0 || output == false || output_len == 0) return false;
    size_t i = 0, j = 0;
  while (i < input_len) {
    // how many bits can we take from char?
    uint8_t octets[3];
    uint8_t num_octets=0;
    for(int o=0;o<3;o++) {
      if(i < input_len) {
        num_octets++;
        octets[o] = input[i++];
      }
    }
    switch(num_octets) {
    case 1:
      out(base64_enc[ octets[0] >> 2 ] );
      out(base64_enc[ (octets[0] & 0b00000011) << 4 ]);
      out('=');
      out('=');
      break;
    case 2:
      out(base64_enc[ octets[0] >> 2 ]);
      out(base64_enc[ ((octets[0] & 0b00000011) << 4) +
                        (octets[1] >> 4) ]);
      out(base64_enc[ ((octets[1] & 0b00001111) << 2) ]);
      out('=');
      break;
    case 3:
      out(base64_enc[ octets[0] >> 2 ]);
      out(base64_enc[ ((octets[0] & 0b00000011) << 4) +
                        (octets[1] >> 4) ]);
      out(base64_enc[ ((octets[1] & 0b00001111) << 2) +
                        (octets[2] >> 6) ]);
      out(base64_enc[ (octets[2] & 0b00111111) ]);
      break;
      default: return false;
    }
  }
  out(0);
  return true;
#undef out
}

/* Decode base64 input in place (mutating it) */
bool base64_decode_mut(uint8_t *input) {
#define out(x) { *outptr = x; outptr++; }
  if(!input) return false;
  uint8_t *outptr = input;
  while(*input != 0) {
    uint8_t c=0;
    uint8_t in[4] = {0,0,0,0};
    for(int i=0;i<4;i++) {
      if(*input < '+' || *input > 'z') { printf("invalid char: %c\n", *input); return false; }
      if(*input == 0) break;
      if(*input != '=') {
        c++;
        in[i] = *input - '+';
      }
      //printf(" | %c ", in[i]+'+');
      input++;
    }
    //printf(" | c: %d\n", c);
    // how many non padding bytes we got
    switch(c) {
    case 2: // output 1 byte
      out( (base64_dec[in[0]] << 2) + (base64_dec[in[1]] >> 4) );
      break;
    case 3: // output 2 bytes
      out( (base64_dec[in[0]] << 2) + (base64_dec[in[1]] >> 4) );
      out( ((base64_dec[in[1]] & 0b00001111)<<4) + (base64_dec[in[2]] >> 2));
      break;
    case 4: // output 3 bytes
      out( (base64_dec[in[0]] << 2) + (base64_dec[in[1]] >> 4) );
      out( ((base64_dec[in[1]] & 0b00001111)<<4) + (base64_dec[in[2]] >> 2));
      out( ((base64_dec[in[2]] & 0b00000011)<<6) + (base64_dec[in[3]]));
      break;
    default:
      return false;
    }
  }
  out(0);
  return true;
  #undef out
}

/* == Arena allocator == */
void *mugshot_arena_alloc(mugshot_arena *arena, ptrdiff_t size, ptrdiff_t align,
                      ptrdiff_t count) {
  if(!arena->start) {
    mugshot_arena_init(arena, MUGSHOT_ARENA_DEFAULT_SIZE);
  }
  ptrdiff_t pad = -(uintptr_t)arena->cur & (align - 1);
  if(arena->cur + pad + (count * size) >= arena->end) {
    mugshot_error("Out of arena memory, tried to allocate %ld (pad %ld)", (count*size), pad);
    return NULL;
  } else {
    void *p = arena->cur + pad;
    memset(p, 0, count * size);
    arena->cur += pad + (count * size);
    return (void *)p;
  }
}

/**
 * Init a new arena by allocating memory for it via malloc.
 */
void mugshot_arena_init(mugshot_arena *arena, size_t capacity) {
  arena->start = (uint8_t *) malloc(capacity);
  arena->cur = arena->start;
  arena->end = arena->start + capacity;
}

void mugshot_arena_free(mugshot_arena *arena) {
  if(arena->start != NULL) {
    free(arena->start);
    arena->start = NULL;
    arena->cur = NULL;
    arena->end = NULL;
  }
}

void mugshot_arena_clear(mugshot_arena *arena) {
  arena->cur = arena->start;
}

char *mugshot_arena_str(mugshot_arena *arena, char *str) {
  if(str == NULL) return NULL;
  size_t len = strlen(str)+1;
  char *out = (char*) mugshot_arena_alloc(arena, len, 1, 1);
  if(!out) return NULL;
  memcpy(out, str, len);
  return out;
}

/* == HTTP server == */

// define this before including, to increase
#ifndef MUGSHOT_HTTP_MAX_REQUEST_HEADER_SIZE
#define MUGSHOT_HTTP_MAX_REQUEST_HEADER_SIZE 8192
#endif

#ifndef MUGSHOT_MAX_HEADERS
#define MUGSHOT_MAX_HEADERS 64
#endif

void mugshot_add_endpoint(mugshot_server *s, mugshot_endpoint route) {
  arrput(s->routes, route);
}

int _mugshot_http_getch(mugshot_conn *r) {
  if(r->read_pos >= r->buf_len) return -1;
  return r->buf[r->read_pos++];
}

/* Take a slice of the buffer until endch is reached (turning that into \0).
 * Returns false if there was not enough data in the buffer or
 * true if endch was reach and *to was set.
 *
 * When returning false, buffer read position is not changed.
 **/
bool _mugshot_http_read_until(mugshot_conn *r, char **to, char endch) {
  size_t start = r->read_pos;
  size_t pos = r->read_pos;
  while(pos < r->buf_len) {
    if(r->buf[pos] == endch) {
      r->buf[pos] = 0;
      r->read_pos = pos+1;
      *to = &r->buf[start];
      return true;
    }
    pos++;
  }
  return false; // not encountered yet
}

bool _mugshot_http_read_line(mugshot_conn *r, char **to) {
  size_t start = r->read_pos;
  size_t pos = r->read_pos;
  while(pos + 1 < r->buf_len) {
    if(r->buf[pos] == '\r' && r->buf[pos+1] == '\n') {
      r->buf[pos] = 0;
      r->read_pos = pos+2;
      *to = &r->buf[start];
      return true;
    }
    pos++;
  }
  return false;
}

#define out_constant(r, c) write((r)->_socket, (c), sizeof((c))-1)
#define out_str(r, s) write((r)->_socket, (s).data, (s).len)

void _mugshot_http_fail(mugshot_conn *r, int status, const char *msg) {
  r->state = MUGSHOT_CS_ERR;
  char response[1024];
  size_t len = snprintf(response, 1024, "HTTP/1.1 %d %s\r\nConnection: close\r\n\r\n", status, msg);
  write(r->_socket, response, len);
}


/* Output the OK status line */
void mugshot_http_ok(mugshot_conn *r) {
  out_constant(r, "HTTP/1.1 200 OK\r\n");
}

/* Output an HTTP header line */
void mugshot_http_header(mugshot_conn *r, str header, str value) {
  out_str(r,header);
  out_constant(r,": ");
  out_str(r,value);
  out_constant(r,"\r\n");
}

void mugshot_http_header_sz(mugshot_conn *r, str header, size_t value) {
  out_str(r,header);
  out_constant(r, ": ");
  char sz[32];
  size_t len = snprintf(sz, 32, "%zu", value);
  write(r->_socket, sz, len);
  out_constant(r,"\r\n");
}

/* Output body, ends headers */
void mugshot_http_body(mugshot_conn *r, str body) {
  out_constant(r, "\r\n");
  write(r->_socket, body.data, body.len);
}


void _mugshot_http_read(mugshot_conn *r) {
  char *line;
  while(_mugshot_http_read_line(r, &line)) {
    mugshot_debug("<< %s", line);
    mugshot_debug("state: %d", r->state);
    switch(r->state) {
    case MUGSHOT_CS_REQ_START: {
      char *path;
      mugshot_debug("method 1st char: %c'", line[0]);
      switch(line[0]) {
      case 'G':
        r->method = MUGSHOT_METHOD_GET;
        if(strncmp(&line[1], "ET ", 3) != 0) goto fail;
        path = &line[4];
        break;
      case 'P':
        if(line[1] == 'O') {
          r->method = MUGSHOT_METHOD_POST;
          if(strncmp(&line[2], "ST ", 3) != 0) goto fail;
          path = &line[5];
        } else if(line[1] == 'U') {
          r->method = MUGSHOT_METHOD_PUT;
          if(strncmp(&line[2], "T ", 2) != 0) goto fail;
          path = &line[4];
        } else if(line[1] == 'A') {
          r->method = MUGSHOT_METHOD_PATCH;
          if(strncmp(&line[2], "TCH ", 4) != 0) goto fail;
          path = &line[6];
        } else goto fail;
        break;

      case 'H':
        r->method = MUGSHOT_METHOD_HEAD;
        if(strncmp(&line[1], "EAD ", 4) != 0) goto fail;
        path = &line[5];
        break;

      case 'D':
        r->method = MUGSHOT_METHOD_DELETE;
        if(strncmp(&line[1], "ELETE ", 6) != 0) goto fail;
        path = &line[7];
        break;

      case 'O':
        r->method = MUGSHOT_METHOD_OPTIONS;
        if(strncmp(&line[1], "PTIONS ", 7) != 0) goto fail;
        path = &line[8];
        break;

      default: goto fail;
    }

      // path and version
      char *space = strchr(path, ' ');
      mugshot_debug("space found: %s", space);
      if(!space) goto fail;
      *space = 0;
      r->path = path; // we can reuse buffer memory in the same arena
      if(strncmp(space+1, "HTTP/", 5)!=0) goto fail;
      r->state = MUGSHOT_CS_REQ_HEADERS;
      break;
    }
    case MUGSHOT_CS_REQ_HEADERS: {
      if(line[0] == 0) {
        // PENDING: do we need body?
        r->state = MUGSHOT_CS_REQ_DONE;
      } else {
        char *name = line;
        char *split = strstr(line, ": ");
        if(!split) goto fail;
        *split = 0;
        if(r->headers_size == MUGSHOT_MAX_HEADERS) {
          mugshot_error("Too many headers in request, max is %d.", MUGSHOT_MAX_HEADERS);
          goto fail;
        } else {
          r->headers[r->headers_size].name = name;
          r->headers[r->headers_size].value = name+2;
          r->headers_size++;
        }
      }
      break;
    }

    default:
      goto fail;
    }

    // FIXME: upgrade to ws
    /*if(strcmp(name, "Upgrade") == 0 && strcmp(value, "websocket")==0) {
      printf("upgrading to ws!");
      c->type = HTTP_WEBSOCKET;
      } else if(strcmp(name, "Sec-WebSocket-Version")==0) {
      c->data.ws.version = atoi(value);
      } else if(strcmp(name, "Sec-WebSocket-Key")==0) {
      c->data.ws.key = arena_str(&c->arena, value);
      }*/

  }
  return;
 fail:
  r->state = MUGSHOT_CS_ERR;
  _mugshot_http_fail(r, 400, "Bad Request");
}

bool mugshot_response_static(mugshot_conn *req, const char *content_type,
                         const size_t content_length, const char *content) {
  char head[512];
  const size_t len = snprintf(head, 512,
                              "HTTP/1.1 200 OK\r\n"
                              "Content-Type: %s\r\n"
                              "Content-Length: %lu\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              content_type, content_length);
  write(req->_socket, head, len);
  write(req->_socket, content, content_length);
  req->state = MUGSHOT_CS_RES_DONE;
  return true;
}

void mugshot_response_file(mugshot_conn *r) {

  // FIXME: unsecure!
  FILE* f = fopen(&r->path[1], "rb");
  if(!f) {
    _mugshot_http_fail(r, 404, "Not found");
    return;
  }
  char header[512];

  struct stat s;
  int fd = fileno(f);
  fstat(fd, &s);
  const size_t hlen = snprintf(header, 512,
                               "HTTP/1.1 200 OK\r\n"
                               "Content-Length: %llu\r\n"
                               "Connection: close\r\n"
                               "\r\n", s.st_size);
  write(r->_socket, header, hlen);
  #ifdef __linux__
  sendfile(r->_socket, fd, 0, s.st_size);
  #else
  sendfile(fd, r->_socket, 0, &s.st_size, NULL, 0);
  #endif
  fclose(f);
}

bool mugshot_path_match(mugshot_endpoint *r, const char *path) {
  const char *at = path;
  str route_path = r->path;
loop:
  printf("matc path: '%c' vs '%c'\n",  *at, str_char_at(route_path, 0));
    if(*at == 0 && route_path.len == 0) return true; // both exhausted, match!
    if (*at == 0 && route_path.len)
      return false; // route path is longer than given
    char ch = str_char_at(route_path, 0);
    if (*at == ch) {
      at++;
      route_path = str_drop(route_path, 1);
    } else if (ch == ':') {
      // consume parameter in route path
      route_path = str_drop(route_path, 1); // drop ':'
      while (route_path.len && str_char_at(route_path, 0) != '/')
        route_path = str_drop(route_path, 1);
      // consume parameter in given
      while(*at && *at != '/') at++;
    }
    goto loop; // feels more honest than while(1) ¯\_(ツ)_/¯
}

/* Serve a single HTTP client connection.
 * Binds parameters from HTTP request and calls the PostgreSQL function.
 *
 * Expects 1 single row response, which is the payload (HTML or JSON etc).
 * The response is copied into the HTTP output buffer as is.
 *
 * PENDING: if PG connection failed, should we retry?
 * PENDING: support Connection: Keep-Alive
 */
void mugshot_serve_pg(mugshot_endpoint *endpoint, mugshot_conn *client,
                      PgConn *conn) {
  size_t cap = 512;
  char sql[512]; // pg identifier max is 63 chars, this is enough
  size_t len = snprintf(sql, cap, "SELECT \"" STR_FMT "\".\"" STR_FMT "\"",
                        STR_ARG(endpoint->schema), STR_ARG(endpoint->name));
  for (int a = 0; a < endpoint->nargs; a++) {
    len += snprintf(&sql[len], cap - len, "%c$%d", a ? ',' : '(', a+1);
  }
  len += snprintf(&sql[len], cap - len, ");");
  printf("SQL(%zu): '%s'\n", len, sql);

  // PostgreSQL max function parameter number is 100
  // https://www.postgresql.org/docs/current/limits.html
  int argtypes[endpoint->nargs];
  str argvals[endpoint->nargs];

  // Bind parameters, from HTTP path and form/query parameters
  argtypes[0] = OID_TEXT;
  argvals[0] = str_constant("maailma!");
  PgResult res = pg_query(conn, sql, endpoint->nargs, argtypes, argvals);

  if (!res.success) {
    mugshot_error("PostgreSQL function call query failed, see database logs.");
    goto fail;
  } else {
    PgRow row = pg_next_row(conn, &res);
    if (!row.has_row) {
      _mugshot_http_fail(client, 204, "No Content");
    } else {
      PgVal v = pg_value(conn, &res, 0);
      mugshot_http_ok(client);
      mugshot_http_header(
          client, str_constant("Content-Type"),
          mugshot_http_content_type_str[endpoint->content_type]);
      mugshot_http_header_sz(client, str_constant("Content-Length"),
                             v.len);
      mugshot_http_body(client, (str){.len=v.len, .data=v.data});

    }
    row = pg_next_row(conn, &res);
    if (!row.success || row.has_row) {
      mugshot_error("Unexpected extra row in PostgreSQL function call.");
      goto fail;
    }
  }

 fail:
  _mugshot_http_fail(client, 501, "Internal server error ¯\\_(ツ)_/¯");
}

void _mugshot_server_worker(mugshot_worker *w) {
  mugshot_server *s = w->server;
  PgConn *conn = NULL;
  int sleep_seconds = 5;
  while (!conn) {
    conn = pg_connect(s->connection);
    printf("buf pos/size after connect: %zu/%zu (%p)\n", conn->buf_pos, conn->buf_size, conn->buf);
    if (!conn) {
      mugshot_error("Worker(%d) failed to get PostgreSQL connection, trying again "
                    "in %d seconds.", w->id, sleep_seconds);
      sleep(5);
      sleep_seconds = sleep_seconds * 2;
      if(sleep_seconds > 60) sleep_seconds = 60;
    }
  }

  while (1) {
    pthread_mutex_lock(&s->acquire_client);
    while(s->waiting_client_count == 0) {
      pthread_cond_wait(&s->has_waiting_clients, &s->acquire_client);
    }
    mugshot_debug("Have %d waiting clients", s->waiting_client_count);
    mugshot_conn *c = s->waiting_client[s->first_waiting_client];
    s->first_waiting_client = (s->first_waiting_client + 1) % 1024;
    s->waiting_client_count--;
    pthread_mutex_unlock(&s->acquire_client);
    mugshot_debug("thread: %d, GOT client with socket %d", w->id, c->_socket);

    const ssize_t read = recv(c->_socket, &c->buf[c->buf_len], c->buf_capacity - c->buf_len,
                              MSG_DONTWAIT);
    if(read == 0) {
      mugshot_debug("connection closed, socket: %d", c->_socket);
      close(c->_socket);
      c->_socket = 0;
      c->state = MUGSHOT_CS_ERR;
      goto done;
    } else if (read > 0) {
      // handle new data
      mugshot_debug("read %ld bytes from socket %d", read, c->_socket);
      c->buf_len += read;
      _mugshot_http_read(c);
      mugshot_debug("state is now: %d", c->state);
    }

    if(c->state == MUGSHOT_CS_REQ_DONE) {
      // Done reading request, time to run handler

      // Go through handlers to see
      // PENDING: make this a Trie for fast routing
      size_t nroutes = arrlen(s->routes);
      bool handled = false;
      for (size_t i = 0; !handled && i < nroutes; i++) {
        mugshot_endpoint route = s->routes[i];
        if (mugshot_path_match(&route, c->path)) {
          printf("path matched! \n");
          mugshot_serve_pg(&route, c, conn);
          //mugshot_print_endpoint(&route);
          handled = true;
        }
      }
      if(!handled)
        _mugshot_http_fail(c, 404, "Not Found");
      if(c->state == MUGSHOT_CS_RES_DONE || c->state == MUGSHOT_CS_ERR) {
        // PENDING: what about keepalive?
        close(c->_socket);
        c->_socket = 0;
        pthread_mutex_lock(&s->release_client);
        c->_next = s->free_client;
        s->free_client = c;
        pthread_mutex_unlock(&s->release_client);
      }

    }

  done:
    atomic_store(&c->processing, false);

  }
}


bool mugshot_server_start(mugshot_server *s) {

  if(s->state != MUGSHOT_STATE_INITIAL) {
    mugshot_error("Server is not in initial state, can't start it.");
    return false;
  }

  /* Ensure some default options */
  if(s->threads == 0) s->threads = 4;
  if(s->backlog == 0) s->backlog = 10;

  s->has_waiting_clients = (pthread_cond_t) PTHREAD_COND_INITIALIZER;
  s->acquire_client = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;
  s->release_client = (pthread_mutex_t) PTHREAD_MUTEX_INITIALIZER;

  struct sockaddr_in server_addr, client_addr;

  const int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (!server_fd) {
    mugshot_error("Failed to create server socket.");
    return false;
  }
  mugshot_debug("server socket fd: %d", server_fd);

  // Set socket options
  const int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
    mugshot_error("Failed to set socket options");
    return false;
  }

  // Configure server address
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(s->port);

  // Bind socket
  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    mugshot_error("Bind failed to port: %d", s->port);
    return false;
  }

  // Listen for connections
  if (listen(server_fd, s->backlog) < 0) {
    mugshot_error("Listen failed");
    return false;
  }
  s->server_fd = server_fd;
  s->state = MUGSHOT_STATE_STARTED;

  // Start listening workers
  s->workers = mugshot_new(&s->arena, mugshot_worker, s->threads);
  for(size_t t=0; t < s->threads; t++) {
    s->workers[t] = (mugshot_worker) {
      .id = t+1,
      .server = s
    };

    pthread_create(&s->workers[t].thread, NULL, (void*) _mugshot_server_worker, &s->workers[t]);
  }

  return true;
}

void mugshot_server_accept_loop(mugshot_server *s) {
  struct sockaddr_in client_addr;
  int addr_len = sizeof(client_addr);
  s->waiting_client_count = 0;
  s->first_waiting_client = 0;
  s->last_waiting_client = 0;
  s->waiting_client = mugshot_new(&s->arena, mugshot_conn*, 1024);
  s->connections = mugshot_new(&s->arena, mugshot_conn, 1023); // fd_set max - 1
  if(!s->connections) {
    mugshot_error("Alloc failed!");
  }
  //s->connections_count = 0;
  s->connections_capacity = 1023;

  // add each connection into free list
  s->free_client = NULL;
  size_t i = s->connections_capacity - 1;
  for(size_t i=0; i < s->connections_capacity; i++) {
    //mugshot_debug("free client %zu", i);
    s->connections[i]._next = s->free_client;
    s->free_client = &s->connections[i];
  }

  fd_set ready;
  //struct timeval wait_time = {0, 1000}; // wait at most 1000 microseconds (1ms)
  while(1) {

    FD_ZERO(&ready);

    // Add listen socket
    FD_SET(s->server_fd, &ready);

    // Add all connections that are currently not being processed
    int max_fd = s->server_fd;
    for(size_t i=0; i < s->connections_capacity; i++) {
      //mugshot_debug("   connection %zu", i);
      if(s->connections[i]._socket && !atomic_load(&s->connections[i].processing)) {
        //mugshot_debug("   client %zu with socket %d selectable", i, s->connections[i]._socket);
        FD_SET(s->connections[i]._socket, &ready);
        if(s->connections[i]._socket > max_fd)
          max_fd = s->connections[i]._socket;
      }
    }

    const int activity = select(max_fd + 1, &ready, NULL, NULL, NULL/*&wait_time*/);
    if(activity < 0 && errno != EINTR) {
      perror("Select error");
    }

    const size_t current_connections = s->connections_capacity;
    if(FD_ISSET(s->server_fd, &ready)) {
      const int client_socket = accept(s->server_fd, (struct sockaddr *) &client_addr, (socklen_t *) &addr_len);
      if (client_socket < 0) {
        mugshot_error("Accept failed, errno: %d", errno);
        sleep(1);
        continue;
      }

      // Print client info
      char client_ip[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
      mugshot_debug("New connection from %s:%d", client_ip, ntohs(client_addr.sin_port));
      bool got_conn = true;
      pthread_mutex_lock(&s->release_client);
      mugshot_conn *c = s->free_client;
      if(!c) {
        got_conn = false;
      } else {
        s->free_client = c->_next;
      }
      pthread_mutex_unlock(&s->release_client);


      if(!got_conn) {
         mugshot_error("No free connections, sending 503");
         // FIXME: do something smarter
         close(client_socket);
      } else {
        mugshot_debug("Got client!");
        if(c->arena.cur != NULL) {
          mugshot_arena_clear(&c->arena);
        }
        c->state = MUGSHOT_CS_REQ_START;
        c->_socket = client_socket;
        c->buf = (char*) mugshot_arena_alloc(&c->arena, MUGSHOT_HTTP_MAX_REQUEST_HEADER_SIZE, 1, 1);
        c->buf_capacity = MUGSHOT_HTTP_MAX_REQUEST_HEADER_SIZE;
        c->buf_len = 0;
        c->read_pos = 0;
        c->headers = mugshot_new(&c->arena, mugshot_header, MUGSHOT_MAX_HEADERS);
        c->headers_size = 0;
      }
    }

    // Check all active
    for(size_t i=0; i < current_connections; i++) {
      if(s->connections[i]._socket && FD_ISSET(s->connections[i]._socket, &ready)) {
        // Offload this to task queue
        pthread_mutex_lock(&s->acquire_client);
        bool work = false;
        if(s->waiting_client_count == 1024) {
          mugshot_error("Waiting client pool is full (1024)");
        } else {
          // PENDING: instead of queueing, should we just assign a thread?
          // each worker could have it's own work queue with mutex
          atomic_store(&s->connections[i].processing, true);
          s->waiting_client[s->last_waiting_client] = &s->connections[i];
          s->last_waiting_client = (s->last_waiting_client + 1) % 1024;
          s->waiting_client_count++;
          work = true;
        }
        // notify some thread that there's a new client waiting
        pthread_mutex_unlock(&s->acquire_client);
        if(work) pthread_cond_signal(&s->has_waiting_clients);
      }
    }
  }
}

void mugshot_server_wait(mugshot_server *s) {
  if(s->state != MUGSHOT_STATE_STARTED) {
    mugshot_error("Can't wait for server that is not started.");
  } else {
    for(size_t t=0; t < s->threads; t++) {
      pthread_join(s->workers[t].thread, NULL);
    }
  }
}

void mugshot_serve(mugshot_server *s) {
  if(mugshot_server_start(s)) {
    mugshot_info("Server started on port %d (backlog %d, worker threads %d)", s->port,
             s->backlog, s->threads);
    mugshot_server_accept_loop(s);
  } else {
    mugshot_error("Failed to start server.");
  }
}

/* == SHA1 == */

// from Grok

// Rotate left operation
#define ROL(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

// SHA-1 constants
#define K0 0x5A827999
#define K1 0x6ED9EBA1
#define K2 0x8F1BBCDC
#define K3 0xCA62C1D6

// SHA-1 context
typedef struct {
    uint32_t state[5];  // State (A, B, C, D, E)
    uint64_t count;     // Number of bits, modulo 2^64
    uint8_t buffer[64]; // Input buffer
} SHA1_CTX;

// Initialize SHA-1 context
void SHA1_Init(SHA1_CTX *ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count = 0;
    memset(ctx->buffer, 0, 64);
}


// Process a block of data
void SHA1_Transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t a, b, c, d, e, temp;
    uint32_t w[80];

    // Copy buffer to w and expand
    for (int i = 0; i < 16; i++) {
        w[i] = (buffer[i * 4] << 24) | (buffer[i * 4 + 1] << 16) |
               (buffer[i * 4 + 2] << 8) | buffer[i * 4 + 3];
    }
    for (int i = 16; i < 80; i++) {
        w[i] = ROL(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    }

    // Initialize hash value for this chunk
    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];

    // Main loop
    for (int i = 0; i < 80; i++) {
        if (i < 20) {
            temp = ROL(a, 5) + ((b & c) | (~b & d)) + e + w[i] + K0;
        } else if (i < 40) {
            temp = ROL(a, 5) + (b ^ c ^ d) + e + w[i] + K1;
        } else if (i < 60) {
            temp = ROL(a, 5) + ((b & c) | (b & d) | (c & d)) + e + w[i] + K2;
        } else {
            temp = ROL(a, 5) + (b ^ c ^ d) + e + w[i] + K3;
        }
        e = d;
        d = c;
        c = ROL(b, 30);
        b = a;
        a = temp;
    }

    // Update state
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

// Update context with input data
void SHA1_Update(SHA1_CTX *ctx, const uint8_t *data, size_t len) {
    size_t i = 0;
    size_t j = (ctx->count / 8) % 64;

    ctx->count += len * 8;

    // Fill buffer if there's partial data
    if (j + len >= 64) {
        memcpy(&ctx->buffer[j], data, 64 - j);
        SHA1_Transform(ctx->state, ctx->buffer);
        i = 64 - j;
        j = 0;
    }

    // Process full blocks
    while (i + 63 < len) {
        SHA1_Transform(ctx->state, &data[i]);
        i += 64;
    }

    // Copy remaining data to buffer
    if (i < len) {
        memcpy(&ctx->buffer[j], &data[i], len - i);
    }
}

// Finalize and get the hash
void SHA1_Final(uint8_t digest[20], SHA1_CTX *ctx) {
    uint8_t finalcount[8];
    // Store bit count in big-endian format
    for (int i = 0; i < 8; i++) {
        finalcount[i] = (uint8_t)(ctx->count >> (56 - (i * 8)) & 255);
    }

    // Pad with 1 bit and zeros
    uint8_t pad = 0x80;
    SHA1_Update(ctx, &pad, 1);

    // Pad with zeros until 448 bits (mod 512)
    uint8_t zero = 0;
    while ((ctx->count % 512) != 448) {
        SHA1_Update(ctx, &zero, 1);
    }

    // Append length
    SHA1_Update(ctx, finalcount, 8);

    // Output final digest
    for (int i = 0; i < 20; i++) {
        digest[i] = (uint8_t)(ctx->state[i >> 2] >> (24 - (i % 4) * 8)) & 255;
    }
}

// Convenience function to get SHA-1 hash
void SHA1(const uint8_t *data, size_t len, uint8_t digest[20]) {
    SHA1_CTX ctx;
    SHA1_Init(&ctx);
    SHA1_Update(&ctx, data, len);
    SHA1_Final(digest, &ctx);
}
#undef ROL

void load_config(int argc, char **argv, mugshot_server *s) {
  str conf;
  if (argc != 2) {
    err("Expected 1 argument: config file, got: %d", (argc-1));
    exit(1);
  }
  /* read file, we consume the parts and never free this */
  conf = str_from_file(argv[1]);
  if (conf.len == 0) {
    err("Couldn't read configuration file: %s", argv[1]);
    exit(1);
  }

  /* Apply defaults */
  s->port = 3000;
  s->threads = 1;
  s->backlog = 10;

  str line;
  int linenum=0;
  while (str_each_line(&conf, &line)) {
    linenum++;
    line = str_trim(line);
    if (line.len == 0 || str_char_at(line, 0) == '#')
      continue;
    str key, value;
    if (str_indexof(line, ':') < 1 || !str_splitat(line, ":", &key, &value)) {
      goto parse_error;
    }
    key = str_trim(key);
    value = str_trim(value);
    if (str_eq_constant(key, "connection")) {
      s->connection = value;
    } else if (str_eq_constant(key, "schema")) {
      s->schema = value;
    } else if (str_eq_constant(key, "port")) {
      long v;
      if (!str_to_long(value, &v)) {
        goto parse_error;
      }
      if (v <= 0 || v > 1 << 16) {
        err("Invalid port: %ld", v);
        exit(1);
      }
      s->port = v;

    }
    continue;
  parse_error:
    err("Configuration parse error on line %d\n>> " STR_FMT, linenum, STR_ARG(line));
      exit(1);

  }

}

const char *query_endpoints =
    "SELECT array_length(proargtypes,1), proargtypes, proargnames, typname as \"content-type\", "
    "       COALESCE(obj_description(t.oid,'pg_proc'), proname) AS route, proname "
    "  FROM pg_proc p "
    "  JOIN pg_type t ON p.prorettype = t.oid "
    " WHERE pronamespace = (SELECT oid FROM pg_namespace WHERE nspname=$1)";

bool mugshot_read_content_type(str ct, mugshot_http_content_type *type) {
  if (str_eq_constant(ct, "text") || str_eq_constant(ct, "text/html")) {
    *type = MUGSHOT_TEXT_HTML;
  } else if (str_eq_constant(ct, "bytea") ||
             str_eq_constant(ct,"application/octet-stream")) {
    *type = MUGSHOT_APPLICATION_OCTET_STREAM;
  } else if (str_eq_constant(ct, "application/json")) {
    *type = MUGSHOT_APPLICATION_JSON;
  } else {
    err("Unsupported content type: " STR_FMT, STR_ARG(ct));
    return false;
  }
  return true;
}

bool mugshot_read_method(str *route, mugshot_http_method *method) {
  for (int m = MUGSHOT_METHOD_GET; m <= MUGSHOT_METHOD_PATCH; m++) {
    const char *mstr = mugshot_http_method_string[m];
    int len = strlen(mstr);
    if (str_startswith(*route, (str){.len = len, .data = (char*)mstr})) {
      *method = m;
      *route = str_drop(*route, len);
      return true;
    }
  }
  err("Unsupported HTTP method in route: " STR_FMT, STR_ARG(*route));
  return false;
}

bool mugshot_read_endpoint(PgConn *conn, PgResult *result, mugshot_endpoint *e) {
  PgVal v;

  v = pg_value(conn, result, 0); // arg len
  e->nargs = ntohl(*((int32_t*)v.data)); // atoi(v.data);
  e->args = NEW_ARR(mugshot_endpoint_arg, e->nargs);
  if (!e->args) {
    err("Out of memory for endpoint arguments");
    exit(1);
  }

  v = pg_value(conn, result, 1); // arg types
  PgArray arr = pg_value_arr(v);
  int *value = (int32_t*)arr.data;
  for (int i = 0; i < e->nargs; i++) {
    e->args[i].oid = ntohl(value[i*2 + 1]); // skip len for each entry, we know its 4
  }

  /* Parse the route, which contains HTTP method and path.
   * Path can contain parameters. Parameters that are not in the path are
   * expected to be passed in as query/form parameters.
   *
   * Example:
   * GET/todos/:id
   * PATCH/my/:id/status
   *
   * Any :param reference ends in the next '/' character or end.
   */
  v = pg_value(conn, result, 2); // argnames
  arr = pg_value_arr(v);
  for (int i = 0; i < e->nargs; i++) {
    int32_t len = ntohl(*((int32_t *)arr.data));
    arr.data += 4;
    str name = (str){.len = len, .data = arr.data}; // dup
    e->args[i].name = str_dup(name);
    //printf("arg %i name %d: %.*s\n", i, len, STR_ARG(name));
    arr.data += len;
  }
  v = pg_value(conn, result, 3); // content type
  //printf(" CONTENT TYPE: '%.*s'\n", (int)v.len, v.data);
  if (!mugshot_read_content_type((str){.len = v.len, .data = v.data},
                                 &e->content_type)) {
    return false;
  }

  v = pg_value(conn, result, 4); // route
  str route = (str){.len = v.len, .data = v.data};
  if (!mugshot_read_method(&route, &e->method)) {
    return false;
  }
  e->path = str_dup(route);
  // go through parameters in path
  str part;
  while (str_splitat(route, "/", &part, &route)) {
    if (str_char_at(part, 0) == ':') {
      str name = str_drop(part, 1);
      for (int i = 0; i < e->nargs; i++) {
        if (str_eq(name, e->args[i].name)) {
          e->args[i].path = true;
        }
      }
    }
  }

  v = pg_value(conn, result, 5); // name
  e->name = str_dup((str){.len = v.len, .data = v.data});

  return true;

}

void mugshot_print_endpoint(mugshot_endpoint *e) {
  printf("\e[0;34m%7s\e[0m \e[0;95m" STR_FMT "\e[0m", mugshot_http_method_string[e->method],
         STR_ARG(e->path));
  if (!str_eq(e->path, e->name)) {
    printf(" (function: " STR_FMT ")", STR_ARG(e->name));
  }
  printf(" ["STR_FMT"]", STR_ARG(mugshot_http_content_type_str[e->content_type]));
  printf("\n  %zu argument%s ", e->nargs,
         e->nargs == 0 ? "." :
         (e->nargs == 1 ? ":" : "s:"));
  for (int i = 0; i < e->nargs; i++) {
    if (i)
      printf(", ");
    printf("\e[0;32m"STR_FMT"\e[0m (%s)", STR_ARG(e->args[i].name), pg_oid_name(e->args[i].oid));
  }
  printf("\n");
}

int main(int argc, char **argv) {

  printf("\n\e[1;91m%s\e[0m\nMugshot starting up.\n", mugshot_logo);

  mugshot_server server = {0};
  load_config(argc, argv, &server);

  PgConn *conn = pg_connect(server.connection);
  int param_oids[] = {OID_TEXT}; // TEXT
  str param_data[] = {server.schema};

  PgResult result = pg_query(conn, query_endpoints, 1, param_oids, param_data);
  if (!result.success) {
    err("Failed to query for endpoints, check database access.");
    goto fail;
  }
  PgRow row = pg_next_row(conn, &result);
  while (row.has_row) {
    mugshot_endpoint e = {.schema = server.schema};
    printf("-------------\n");
    mugshot_read_endpoint(conn, &result, &e);
    mugshot_print_endpoint(&e);

    mugshot_add_endpoint(&server, e);
    row = pg_next_row(conn, &result);
  }

  mugshot_serve(&server);

  return 0;

fail:
  pg_close(conn);
  return 1;
}
