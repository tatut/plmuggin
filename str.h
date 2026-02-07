#ifndef str_h
#define str_h
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "alloc.h"

typedef struct str {
  size_t len;
  char *data;
} str;

typedef struct strbuf {
  Alloc *alloc;
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

#define str_constant(x) ((str){.len = sizeof(x), .data = x})

bool str_splitat(str in, const char *chars, str *split, str *rest);
int str_indexof(str in, char ch);
bool str_each_line(str *lines, str *line);
bool is_space(char ch);
str str_ltrim(str in);
str str_rtrim(str in);
str str_trim(str in);
long str_to_long(str s);
bool str_startswith(str haystack, str needle);
str str_drop(str haystack, size_t len);
str str_take(str big, size_t len);
str str_from_cstr(const char *in);
str str_dup(Alloc *a, str in);
void str_reverse(str mut);
void str_free(Alloc *a, str *s);
bool str_eq(str a, str b);
bool str_eq_cstr(str str, const char *cstring);
char str_char_at(str str, size_t idx);
char *str_to_cstr(str str);
str str_from_file(Alloc *a, const char *filename);
uint64_t str_hash(str in);

strbuf *strbuf_new(Alloc *a);
bool strbuf_ensure(strbuf *buf, size_t space_needed);
bool strbuf_append_str(strbuf *buf, str data);
bool strbuf_append_int(strbuf *buf, int i);
bool strbuf_append_char(strbuf *buf, char ch);

#endif

#ifdef STR_IMPLEMENTATION

#include <stdio.h>
#include <string.h>
#include "alloc.h"

str str_from_file(Alloc *a, const char *filename) {
  FILE *f = fopen(filename, "r");
  if(!f) {
    fprintf(stderr, "Can't open file: %s\n", filename);
    goto fail;
  }
  fseek(f, 0, SEEK_END);
  size_t size = ftell(f);
  rewind(f);

  str out = {.len = size, .data = ALLOC(a, size)};
  if(!out.data) {
    fprintf(stderr, "Alloc failed\n");
    goto fail;
  }

  if(size != fread(out.data, 1, size, f)) {
    fprintf(stderr, "Read failed\n");
    FREE(a, out.data);
    goto fail;
  }
  return out;

 fail:
  return (str){.len=0};
}

bool str_splitat(str in, const char *chars, str *split, str *rest) {
  if(in.len == 0) return false;
  size_t at = 0;
  int chs = strlen(chars);
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
  return false;
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

long str_to_long(str s) {
  long l=0;
  for(size_t i=0; i < s.len && s.data[i] >= '0' && s.data[i] <= '9'; i++) {
    l = (l*10) + (s.data[i]-48);
  }
  return l;
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
  size_t len = strlen(in);
  char *data = malloc(len);
  if(!data) fprintf(stderr, "Malloc failed!");
  memcpy(data, in, len);
  return (str){.data = data, .len = len};
}

str str_dup(Alloc *a, str in) {
  char *data = ALLOC(a, in.len);
  if(!data) fprintf(stderr, "Aalloc failed!");
  memcpy(data, in.data, in.len);
  return (str) {.len = in.len, .data = data};
}

void str_reverse(str mut) {
  size_t l=0,r=mut.len-1;
  while(l<r) {
    char tmp = mut.data[l];
    mut.data[l] = mut.data[r];
    mut.data[r] = tmp;
    l++; r--;
  }
}

void str_free(Alloc *a, str *s) {
  if(s->data) FREE(a, s->data);
  s->len = 0;
  s->data = NULL;
}

bool str_eq(str a, str b) {
  if(a.len != b.len) return false;
  return strncmp(a.data, b.data, a.len) == 0;
}

bool str_eq_cstr(str str, const char *cstring) {
  int len = strlen(cstring);
  if(len != str.len) return false;
  return strncmp(str.data, cstring, len) == 0;
}

char str_char_at(str str, size_t idx) {
  if(idx < 0 || idx >= str.len) return 0;
  return str.data[idx];
}

char *str_to_cstr(str str) {
  char *cstr = malloc(str.len+1);
  if(!cstr) {
    printf("malloc failed");
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

strbuf *strbuf_new(Alloc *a) {
  strbuf *sb = NEW(a, strbuf);
  if(!sb) return NULL;
  char *initial = ALLOC(a, 64);
  if(!initial) {
    FREE(a, sb);
    return NULL;
  }
  sb->str = (str){.len = 0, .data = initial};
  sb->capacity = 64;
  sb->alloc = a;
  return sb;
}

bool strbuf_ensure(strbuf *buf, size_t space_needed) {
  if(buf->capacity - buf->str.len < space_needed) {
    size_t new_capacity = buf->capacity * 1.618; // grow by golden ratio
    char *new_data = REALLOC(buf->alloc, buf->str.data, buf->capacity);
    if(!new_data) return false;
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

bool strbuf_append_int(strbuf *buf, int i) {
  if(!strbuf_ensure(buf, 12)) return false;
  size_t len = snprintf(&buf->str.data[buf->str.len], 12, "%d", i);
  buf->str.len += len;
  return true;
}

bool strbuf_append_char(strbuf *buf, char ch) {
  if(!strbuf_ensure(buf, 1)) return false;
  buf->str.data[buf->str.len++] = ch;
  return true;
}

#endif
