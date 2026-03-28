#ifndef str_h
#define str_h
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

#include "alloc.h"
#include "env.h"

typedef struct str {
  size_t len;
  char *data;
} str;

typedef struct strbuf {
  size_t capacity;
  str str;
} strbuf;

// for passing to printf
#define STR_FMT "%.*s"
#define STR_ARG(x) (int)(x).len, (x).data

// if str x equals constant C string
#define str_eq_constant(x, constant)                                           \
  ((x).len == strlen(constant) &&                                        \
   memcmp((x).data, constant, strlen(constant)) == 0)

#define STR_EMPTY ((str){.len = 0})

#define str_constant(x) ((str){.len = sizeof(x)-1, .data = x})

#define cstr(c) ((str){.len=strlen((c)),.data=(c)})

bool str_splitat(str in, const char *chars, str *split, str *rest);
int str_indexof(str in, char ch);
bool str_each_line(str *lines, str *line);
bool is_space(char ch);
str str_ltrim(str in);
str str_rtrim(str in);
str str_trim(str in);
bool str_to_long(str s, long *value);
bool str_startswith(str haystack, str needle);
str str_drop(str haystack, size_t len);
str str_take(str big, size_t len);
str str_from_cstr(const char *in);
str str_dup(str in);
void str_reverse(str mut);
void str_free(str *s);
bool str_eq(str a, str b);
bool str_eq_cstr(str str, const char *cstring);
char str_char_at(str str, size_t idx);
char *str_to_cstr(str str);
str str_from_file(const char *filename);
uint64_t str_hash(str in);
bool str_urldecode(str *mut);
bool str_urldecode_to(str in, str *to);

strbuf *strbuf_new(void);
bool strbuf_ensure(strbuf *buf, size_t space_needed);
bool strbuf_append_str(strbuf *buf, str data);
bool strbuf_append_int(strbuf *buf, int i);
bool strbuf_append_char(strbuf *buf, char ch);
bool strbuf_append_str_escaped(strbuf *buf, str data);



#endif

#ifdef STR_IMPLEMENTATION

#include <stdio.h>
#include <string.h>
#include "alloc.h"

str str_from_file(const char *filename) {
  FILE *f;
  size_t size;
  str out;

  f = fopen(filename, "r");
  if(!f) {
    fprintf(stderr, "Can't open file: %s\n", filename);
    goto fail;
  }
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  rewind(f);

  out = (str) {.len = size, .data = ALLOC(size)};
  if(!out.data) {
    fprintf(stderr, "Alloc failed\n");
    goto fail;
  }

  if(size != fread(out.data, 1, size, f)) {
    fprintf(stderr, "Read failed\n");
    FREE(out.data);
    goto fail;
  }
  return out;

 fail:
  return (str){.len=0};
}

bool str_splitat(str in, const char *chars, str *split, str *rest) {
  size_t at;
  int chs;

  if(in.len == 0) return false;
  at = 0;
  chs = strlen(chars);
  while(at < in.len) {
    for(int i=0;i<chs;i++) {
      if(in.data[at] == chars[i]) {
        // found split here
        split->len = at;
        split->data = in.data;
        rest->len = in.len - at - 1;
        rest->data = in.data + at + 1;
        return true;
      }
    }
    at++;
  }
  *split = in;
  *rest = (str){.len = 0};
  return true;
}


int str_indexof(str in, char ch) {
  for(int i=0;i<in.len;i++) {
    if(in.data[i] == ch) return i;
  }
  return -1;
}


bool str_each_line(str *lines, str *line) {
  if(lines->len == 0) return false;
  if(str_splitat(*lines, "\n", line, lines))
    return true;
  if(lines->len) {
    // last line
    line->len = lines->len;
    line->data = lines->data;
    return true;
  }
  return false;
}

bool is_space(char ch) {
  return ch == ' ' || ch == '\t' || ch == '\n';
}

str str_ltrim(str in) {
  while(in.len && is_space(in.data[0])) {
    in.len--;
    in.data = &in.data[1];
  }
  return in;
}

str str_rtrim(str in) {
  while(in.len && is_space(in.data[in.len-1])) in.len--;
  return in;
}

str str_trim(str in) {
  return str_ltrim(str_rtrim(in));
}

bool str_to_long(str s, long *value) {
  long l = 0;
  if(s.len == 0) return false;
  for (size_t i = 0; i < s.len; i++) {
    if(s.data[i] >= '0' && s.data[i] <= '9') {
      l = (l * 10) + (s.data[i] - 48);
    } else {
      return false;
    }
  }
  *value = l;
  return true;
}
bool str_startswith(str haystack, str needle) {
  if(needle.len > haystack.len) return false;
  return strncmp(haystack.data,needle.data,needle.len) == 0;
}

str str_drop(str haystack, size_t len) {
  if(len > haystack.len) return (str) {.len = 0, .data=NULL};
  return (str) {.len = haystack.len - len,
                .data = &haystack.data[len]};
}

str str_take(str big, size_t len) {
  if(len > big.len) return big;
  return (str) {.len = len, .data = big.data};
}

str str_from_cstr(const char *in) {
  size_t len;
  char *data;

  len = strlen(in);
  data = ALLOC(len);
  if(!data) LOG_ERROR("Alloc string of len %zu failed!", len);
  memcpy(data, in, len);
  return (str){.data = data, .len = len};
}

str str_dup(str in) {
  char *data;
  data = ALLOC(in.len);
  if(!data) fprintf(stderr, "Aalloc failed!");
  memcpy(data, in.data, in.len);
  return (str) {.len = in.len, .data = data};
}

void str_reverse(str mut) {
  size_t l, r;
  l=0;
  r=mut.len-1;
  while(l<r) {
    char tmp;
    tmp = mut.data[l];
    mut.data[l] = mut.data[r];
    mut.data[r] = tmp;
    l++; r--;
  }
}

void str_free(str *s) {
  if(s->data) FREE(s->data);
  s->len = 0;
  s->data = NULL;
}

bool str_eq(str a, str b) {
  if(a.len != b.len) return false;
  return strncmp(a.data, b.data, a.len) == 0;
}

bool str_eq_cstr(str str, const char *cstring) {
  int len;
  len = strlen(cstring);
  if(len != str.len) return false;
  return strncmp(str.data, cstring, len) == 0;
}

char str_char_at(str str, size_t idx) {
  if(idx < 0 || idx >= str.len) return 0;
  return str.data[idx];
}

char *str_to_cstr(str str) {
  char *cstr;
  cstr = ALLOC(str.len+1);
  if(!cstr) {
    printf("palloc failed");
    exit(1);
  }
  memcpy(cstr, str.data, str.len); // FIXME: check
  cstr[str.len] = 0;
  return cstr;
}

// djb2 hash
uint64_t str_hash(str in) {
  uint64_t hash = 5381;
  for(int i=0; i<in.len;i++) {
    hash = ((hash << 5) + hash) + in.data[i]; /* hash * 33 + c */
  }
  return hash;
}

/* URL decode to existing string, which must have enough space.
 * The output string length is adjusted on success to the actual
 * decoded length.
 * Returns true on success, false otherwise.
 */
bool str_urldecode_to(str in, str *out) {
  size_t p,o;
  p = 0; // in position
  o = 0; // out position
  while (p < in.len && o < out->len) {
    char at;
    at = str_char_at(in, p);
    if (at == '%') {
      char d1, d2;
      int n1, n2;

      if ((in.len - p) < 2)
        return false; // must have at least 2 digits left

      d1 = str_char_at(in, p + 1);
      d2 = str_char_at(in, p + 2);

      if (d1 >= '0' && d1 <= '9') {
        n1 = d1 - '0';
      } else if (d1 >= 'a' && d1 <= 'f') {
        n1 = 10 + (d1 - 'a');
      } else if (d1 >= 'A' && d1 <= 'F') {
        n1 = 10 + (d1 - 'A');
      } else {
        goto fail;
      }

      if (d2 >= '0' && d2 <= '9') {
        n2 = d2 - '0';
      } else if (d2 >= 'a' && d2 <= 'f') {
        n2 = 10 + (d2 - 'a');
      } else if (d2 >= 'A' && d2 <= 'F') {
        n2 = 10 + (d2 - 'A');
      } else {
        goto fail;
      }

      out->data[o++] = (n1 << 4) + n2;
      p += 3; // skip '%' and 2 hex digits
    } else if (at < 32) {
      // disallow ASCII control characters
      goto fail;
    } else {
      out->data[o++] = at;
      p += 1;
    }
  }

  out->len = o;
  return true;
fail:
  return false;
}

/* Decode in place to same buffer, we can do it
 * becose decoding always is shorter.
 */
bool str_urldecode(str *encoded) { return str_urldecode_to(*encoded, encoded); }

strbuf *strbuf_new() {
  strbuf *sb;
  char *initial;

  sb = NEW(strbuf);
  if(!sb) return NULL;
  initial = ALLOC(64);
  if(!initial) {
    FREE(sb);
    return NULL;
  }
  sb->str = (str){.len = 0, .data = initial};
  sb->capacity = 64;
  return sb;
}

bool strbuf_ensure(strbuf *buf, size_t space_needed) {
  size_t min_capacity, new_capacity;
  char *new_data;

  min_capacity = buf->str.len + space_needed;
  if(buf->capacity < min_capacity) {
    new_capacity = buf->capacity * 1.618; // grow by golden ratio
    if(new_capacity < min_capacity) {
      new_capacity = min_capacity;
    }
    //LOG_NOTICE("%ld - %ld < %ld, realloc to %zu", buf->capacity, buf->str.len, space_needed, new_capacity);

    new_data = REALLOC(buf->str.data, new_capacity);
    if(!new_data) {
      LOG_ERROR("Failed to realloc string buffer from size %zu to %zu", buf->capacity, new_capacity);
      return false;
    }
    buf->str.data = new_data;
    buf->capacity = new_capacity;
  }
  return true;
}

bool strbuf_append_str(strbuf *buf, str data) {
  size_t len = data.len;
  if(!strbuf_ensure(buf, len)) return false;
  memcpy(&buf->str.data[buf->str.len], data.data, len);
  buf->str.len += len;
  return true;
}

// append HTML escaped
bool strbuf_append_str_escaped(strbuf *buf, str data) {
  size_t *len;
  strbuf_ensure(buf, data.len); // might need more, but good estimate
  len = &buf->str.len;
  for(size_t i = 0; i < data.len; i++) {
    // check we have enough space for the longest replacement
    if(buf->capacity - buf->str.len < 6) {
      if(!strbuf_ensure(buf, 16)) return false;
    }

    switch(data.data[i]) {
    case '&': memcpy(&buf->str.data[*len], "&amp;", 5);  *len += 5; break;
    case '<': memcpy(&buf->str.data[*len], "&lt;", 4);   *len += 4; break;
    case '>': memcpy(&buf->str.data[*len], "&gt;", 4);   *len += 4; break;
    case '"': memcpy(&buf->str.data[*len], "&quot;", 6); *len += 6; break;
    case '\'':memcpy(&buf->str.data[*len], "&#39;", 5);  *len += 5; break;
    default: buf->str.data[*len] = data.data[i]; *len += 1;
    }
  }
  return true;
}

bool strbuf_append_int(strbuf *buf, int i) {
  size_t len;
  if(!strbuf_ensure(buf, 12)) return false;
  len = snprintf(&buf->str.data[buf->str.len], 12, "%d", i);
  buf->str.len += len;
  return true;
}

bool strbuf_append_char(strbuf *buf, char ch) {
  if(!strbuf_ensure(buf, 1)) return false;
  buf->str.data[buf->str.len++] = ch;
  return true;
}

#endif
