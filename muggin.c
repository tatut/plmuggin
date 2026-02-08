#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "muggin.h"

#define STR_IMPLEMENTATION
#include "str.h"

#include "alloc.h"
#include "env.h"

/* context to pass around all parsing functions */
#define MAX_ERROR_SIZE 1024
typedef struct _ctx {
  Alloc *a;
  m_Template *t;
  int indent;
  str input;
  char error[MAX_ERROR_SIZE];
  str error_at; // location where error occurred
} _ctx;

#define CONSTANT(t, idx) ((t)->constants[(t)->constant_idx[(idx)]])
#define NAME(t, idx) ((t)->names[(t)->name_idx[(idx)]])

/* Signatures */


static bool muggin_contents_from_str(_ctx *ctx, str s, m_Contents *contents);
static bool _grow(_ctx *ctx, size_t *count, size_t *capacity, void **array,
                  size_t sz);
static size_t string_pool_idx(_ctx *ctx, str *pool, size_t *idxs, size_t count, size_t capacity, str s);
static size_t string_pool_use(_ctx *ctx, size_t **idx, str **pool,
                              size_t *count, size_t *capacity, str s);
static size_t muggin_use_constant(_ctx *ctx, str constant);
static size_t muggin_use_name(_ctx *ctx, str name);
static int muggin_read_indent(_ctx *ctx);
static bool muggin_check_indent(_ctx *ctx, int expected);
static bool valid_tag(char ch, bool first);
static bool valid_attr(char ch, bool first);
static bool valid_id(char ch, bool first);
static bool valid_class(char ch, bool first);
static bool is_void_element(str tag);
static bool muggin_read_constant(_ctx *ctx, bool (*valid)(char, bool),
                                 size_t *ref);
static void muggin_add_attribute(_ctx *ctx, m_Node *n, m_Attr attr);
static void muggin_add_child(_ctx *ctx, m_Node *n, m_Node child);
static bool muggin_read_attribute(_ctx *ctx, m_Attr *attr);
static bool muggin_contents_add(_ctx *ctx, m_Contents *contents,
                                m_ContentType type, size_t id);
static bool muggin_contents_add_str(_ctx *ctx, m_Contents *contents,
                                    m_ContentType type, str content);
static bool muggin_contents_from_str(_ctx *ctx, str s, m_Contents *contents);
static bool muggin_parse_node(_ctx *ctx, m_Node **to);
m_Template *muggin_parse(Alloc *a, str input);
void muggin_render_contents(m_Template *t, m_Contents *contents, strbuf *sb);
void muggin_render_node(m_Template *t, m_Node *n, strbuf *sb);
void muggin_render(m_Template *t, Alloc *a, str *to);






// grow dynamic array if needed
static bool _grow(_ctx *ctx, size_t *count, size_t *capacity, void **array, size_t sz) {
  if(*count == *capacity) {
    size_t new_capacity = *capacity ? *capacity * 1.618 : 64;
    void *new_array = REALLOC(ctx->a, *array, new_capacity * sz);
    if(!new_array) {
      LOG_ERROR("Failed to reallocate dynamic array, capacity %zu => %zu", *capacity, new_capacity);
      return false;
    }
    *capacity = new_capacity;
    *array = new_array;
  }
  return true;
}

static size_t string_pool_idx(_ctx *ctx, str *pool, size_t *idxs, size_t count, size_t capacity, str s) {
  size_t num = count;
  uint64_t idx = str_hash(s) % capacity;
 again:
  if(str_eq(pool[idx], s)) {
    // we already have an entry, return idx
    for(size_t i=0;i<count;i++) {
      if(idxs[i] == idx) return i;
    }
    LOG_ERROR("Bug in string pool hashing!, existing entry idx not found");
  } else if(!pool[idx].len) {
    // free slot
    idxs[num] = idx;
    pool[idx] = str_dup(ctx->a, s);
    return num;
  }
  idx = (idx + 1) % capacity;
  goto again;
}
 
 
static size_t string_pool_use(_ctx *ctx, size_t **idx, str **pool, size_t *count,
                              size_t *capacity, str s) {
  size_t id;
  if(*count == *capacity) {
    size_t *old_idx = *idx;
    str *old_pool = *pool;
    size_t old_count = *count;
    size_t new_capacity = *capacity ? *capacity * 1.618 : 64;
    size_t *new_idx = NEW_ARR(ctx->a, size_t, new_capacity);
    str *new_pool = NEW_ARR(ctx->a, str, new_capacity);
    if(!new_idx || !new_pool) {
      LOG_ERROR("String pool rehash alloc failed, new_capacity=%zu", new_capacity);
    }
    bzero(new_pool, sizeof(str)*new_capacity);
    *idx = new_idx;
    *pool = new_pool;
    *capacity = new_capacity;
    for(size_t i=0;i<old_count;i++) {
      string_pool_idx(ctx, *pool, *idx, i, *capacity, old_pool[old_idx[i]]);
      FREE(ctx->a, old_pool[old_idx[i]].data);
    }
    FREE(ctx->a, old_pool);
    FREE(ctx->a, old_idx);
    *count = old_count;
  }
 
  id = string_pool_idx(ctx, *pool, *idx, *count, *capacity, s);
  *count += 1;
  //LOG_NOTICE("string pool (%zu) = %.*s", id, (int) s.len, s.data);
  return id;
}

/* (re)use constant, returns index */
static size_t muggin_use_constant(_ctx *ctx, str constant) {
  return string_pool_use(ctx, &ctx->t->constant_idx, &ctx->t->constants,
                         &ctx->t->constants_count, &ctx->t->constants_capacity,
                         constant);
}

static size_t muggin_use_name(_ctx *ctx, str name) {
  return string_pool_use(ctx, &ctx->t->name_idx, &ctx->t->names,
                         &ctx->t->names_count, &ctx->t->names_capacity,
                         name);
}


static int muggin_read_indent(_ctx *ctx) {
  int indent = 0;
  char ch;
 next:
  if (!ctx->input.len) return indent;
  ch = ctx->input.data[0];
  switch (ch) {
  case ' ': indent += 1; break;
  case '\t': indent += 8; break;
  default: return indent;
  }
  ctx->input.len--;
  ctx->input.data++;
  goto next;
}

static bool muggin_check_indent(_ctx *ctx, int expected) {
  str orig_input;
  int actual;
  
  orig_input = ctx->input;
  actual = muggin_read_indent(ctx);
  if(expected == actual) {
    return true;
  } else {
    ctx->input = orig_input;
    return false;
  }
}

static bool valid_tag(char ch, bool first) {
  if(ch >= 'a' && ch <= 'z') return true;
  if(ch >= 'A' && ch <= 'Z') return true;
  if(!first) {
    if(ch >= '0' && ch <= '9') return true;
    if(ch == ':') return true;
    if(ch == '-') return true;
  }
  return false;
}

static bool valid_attr(char ch, bool first) {
  return valid_tag(ch, first); // so far they are the same
}

static bool valid_id(char ch, bool first) { return valid_tag(ch, first); }
static bool valid_class(char ch, bool first) { return valid_tag(ch, first); }

str void_elements[] = {
    str_constant("area"),  str_constant("base"),  str_constant("br"),
    str_constant("col"),   str_constant("embed"), str_constant("hr"),
    str_constant("img"),   str_constant("input"), str_constant("link"),
    str_constant("meta"),  str_constant("param"), str_constant("source"),
    str_constant("track"), str_constant("wbr")};


static bool is_void_element(str tag) {
  for(int i=0; i<(sizeof(void_elements)/sizeof(str));i++) {
    if(str_eq(tag, void_elements[i])) return true;
  }
  return false;
}

static bool muggin_read_constant(_ctx *ctx, bool (*valid)(char,bool), size_t *ref) {
  str orig;
  int len;
  bool first;
  str constant;
  
  orig = ctx->input;
  len = 0;
  first = true;
  while(ctx->input.len && valid(ctx->input.data[0], first)) {
    first = false;
    len++;
    ctx->input = str_drop(ctx->input, 1);
  }
  if(len > 0) {
    constant = str_take(orig, len);
    *ref = muggin_use_constant(ctx, constant);
    //printf("CONSTANT(#%zu): '"STR_FMT"'\n", *ref, STR_ARG(constant));
    return true;
  }
  return false;
}

#define PARSE_ERROR(ctx, ...) do { snprintf((ctx)->error, MAX_ERROR_SIZE, __VA_ARGS__); (ctx)->error_at = (ctx)->input; goto fail; } while(false)

static void muggin_add_attribute(_ctx *ctx, m_Node *n, m_Attr attr) {
  if(n->type == NODE_TEXT) {
    LOG_ERROR("Can't add attributes to text nodes!");
  } else {
    if(!_grow(ctx, &n->elt.attributes_count, &n->elt.attributes_capacity,
              (void**) &n->elt.attributes, sizeof(m_Attr))) return;
    LOG_NOTICE("adding attr "STR_FMT, STR_ARG(CONSTANT(ctx->t, attr.name)));
    n->elt.attributes[n->elt.attributes_count++] = attr;
  }
}

static void muggin_add_child(_ctx *ctx, m_Node *n, m_Node child) {
  if(n->type == NODE_TEXT) {
    LOG_ERROR("Text nodes can't have children.");
  } else {
    if(n->elt.children_capacity == n->elt.children_count) {
      size_t new_capacity = n->elt.children_capacity ? n->elt.children_capacity * 2 : 8;
      m_Node *new_children = NEW_ARR(ctx->a, m_Node, new_capacity);
      if(!new_children) {
        LOG_ERROR("Unable to add child node, alloc failed.");
      }
      if(n->elt.children) {
        memcpy(new_children, n->elt.children, n->elt.children_capacity * sizeof(m_Node));
        FREE(ctx->a, n->elt.children);
      }
      n->elt.children = new_children;
      n->elt.children_capacity = new_capacity;
    }
    n->elt.children[n->elt.children_count++] = child;
  }
}

static bool muggin_read_attribute(_ctx *ctx, m_Attr *attr) {
  str val, rest;
  size_t name;
  
  ctx->input = str_ltrim(ctx->input);
  if(str_char_at(ctx->input, 0) == ')') return false;
  
  if(!muggin_read_constant(ctx, valid_attr, &name)) {
    PARSE_ERROR(ctx, "Expected attribute name.");
  }
  ctx->input = str_ltrim(ctx->input);
  if(str_char_at(ctx->input, 0) != '=') {
    PARSE_ERROR(ctx, "Expected '=' after attribute name");
  }
  ctx->input = str_ltrim(str_drop(ctx->input,1));
  attr->name = name;
  // if '"' read quoted string, otherwise raw string
  // PENDING: should read numbers as well
  if(str_char_at(ctx->input, 0) == '"') {
    ctx->input = str_drop(ctx->input, 1);
    
    if(str_splitat(ctx->input, "\"", &val, &rest)) {
      attr->type = AT_STRING;
      attr->str_value = str_dup(ctx->a, val);
      ctx->input = rest;
      return true;
    } else {
      PARSE_ERROR(ctx, "Unterminated quoted string");
    }
  } else {
    str val;
    size_t len=0;
    char ch;
    do {
      ch = str_char_at(ctx->input, len++);
    } while(ch != ' ' && ch != '\t' && ch != '\n' && ch != ')');
    val = str_take(ctx->input, len-1);
    ctx->input = str_drop(ctx->input, ch == ')' ? len-1 : len); // leave ')' in input
    attr->type = AT_CONTENTS;
    muggin_contents_from_str(ctx, val, attr->contents);
    
    return true;
  }

 fail:
  return false;
 }

static bool muggin_contents_add(_ctx *ctx, m_Contents *contents,
                                m_ContentType type, size_t id) {
  m_Content add;
  if(!_grow(ctx, &contents->contents_count, &contents->contents_capacity,
            (void**)&contents->contents, sizeof(m_Content))) {
    return false;
  }
  add = (m_Content){.type = type, .id = id};
  contents->contents[contents->contents_count++] = add;
  return true;
  
}

static bool muggin_contents_add_str(_ctx *ctx, m_Contents *contents,
                                    m_ContentType type, str content) {
  switch(type) {
  case CT_CONSTANT: return muggin_contents_add(ctx, contents, type,
                                               muggin_use_constant(ctx, content));
  case CT_NAME: return muggin_contents_add(ctx, contents, type,
                                           muggin_use_name(ctx, content));
  }
  return false;
}
/* Read contents, that can contain {{ mustache }} references to variables. */ 
static bool muggin_contents_from_str(_ctx *ctx, str s, m_Contents *contents) {
  int idx;
  str name;
  
  *contents = (m_Contents){0};
 again:
  idx = str_indexof(s, '{');
  if(idx != -1 && str_char_at(s, idx+1) == '{') {
    // found "{{" append content before it as constant
    if(!idx) {
      if(!muggin_contents_add_str(ctx, contents, CT_CONSTANT, str_take(s, idx)))
        return false;
      s = str_drop(s, idx);
    }
    // drop the "{{" and trim it
    s = str_ltrim(str_drop(s, 2));
    // find the end "}}"
    idx = str_indexof(s, '}');
    if(idx == -1 || str_char_at(s, idx+1) != '}') {
      LOG_ERROR("Expected '}}' end in variable reference");
      return false;
    }
    name = str_rtrim(str_take(s, idx));
    s = str_drop(s, idx+1);
    if(!muggin_contents_add_str(ctx, contents, CT_NAME, name))
      return false;

    goto again;
  } else if(s.len) {
    if(!muggin_contents_add_str(ctx, contents, CT_CONSTANT, s))
      return false;
  }  
  return true;
}
 
static bool muggin_parse_node(_ctx *ctx, m_Node **to) {
  str line, rest;
  bool has_id;
  size_t id_constant;
  int classes;
  m_Node *n;
  size_t tag;
  size_t class_constants[64]; // 64 ought to be enough, even for tailwind users? ;)
  size_t class, id;
  int current_indent;
  m_Node *child;
  str content;
  m_Attr attr;
  
  // new node starts on a fresh line, we expect indent
  if(!muggin_check_indent(ctx, ctx->indent)) return false;

  n = NULL;

  if(str_char_at(ctx->input, 0) == '|') {
    ctx->input = str_trim(str_drop(ctx->input, 1));
    // text node
    n = NEW(ctx->a, m_Node);
    n->type = NODE_TEXT;
    if(str_splitat(ctx->input, "\n", &line, &rest)) {
      line = str_trim(line);
      ctx->input = rest;
    } else {
      line = str_trim(ctx->input);
      ctx->input = STR_EMPTY;
    }
    if(!muggin_contents_from_str(ctx, line, n->contents)) return false;
    *to = n;
    return true;
  } else if(muggin_read_constant(ctx, valid_tag, &tag)) {
    n = NEW(ctx->a, m_Node);
    n->type = NODE_ELEMENT;
    n->elt = (m_Elt){.tag = tag};
    if(is_void_element(CONSTANT(ctx->t, tag))) {
      n->elt.flags |= EF_VOID;
    }
    // check #id or .class definitions
    has_id = false;
    classes = 0;
    
  id_or_class:
    switch(str_char_at(ctx->input, 0)) {
    case '#': {
      if(has_id) {
        PARSE_ERROR(ctx, "Parse error: can't have #<id> twice in element name.");
      }
      ctx->input = str_drop(ctx->input, 1);
      if(!muggin_read_constant(ctx, valid_id, &id_constant)) {
        PARSE_ERROR(ctx, "Parse error: can't read #<id> in element name.");
      }
      has_id = true;
      goto id_or_class;
    }
    case '.': {
      ctx->input = str_drop(ctx->input, 1);
      if(!muggin_read_constant(ctx, valid_class, &class)) {
        PARSE_ERROR(ctx, "Parse error: cant read .<class> in element name.");
      }
      if(classes == 64) {
        PARSE_ERROR(ctx, "Parse error: too many classes, only 64 supported ¯\\_(ツ)_/¯");
      }
      class_constants[classes++] = class;
      goto id_or_class;
    }
    default: break;
    }

    if(has_id) {
      id = muggin_use_constant(ctx, str_constant("id"));
      muggin_add_attribute(ctx, n, (m_Attr) {.name = id, .type = AT_CONSTANT,
                                             .constant = id_constant});
    }
    if(classes) {
      // add attribute
      class = muggin_use_constant(ctx, str_constant("class"));
      if(classes == 1) {
        // only 1 constant
        muggin_add_attribute(ctx, n, (m_Attr) {.name = class, .type = AT_CONSTANT,
                                               .constant = class_constants[0]});
      } else {
        // multiple classes separated by ' '
        m_Contents *contents = NEW(ctx->a, m_Contents);
        *contents = (m_Contents){.separator = ' '};
        for(size_t i=0; i < classes; i++) {
          muggin_contents_add(ctx, contents, CT_CONSTANT, class_constants[i]);
        }
        muggin_add_attribute(ctx, n, (m_Attr) {.name = class, .type = AT_CONTENTS,
                                               .contents = contents});
      }
    }

    // parse attributes, if any
    if(str_char_at(ctx->input, 0) == '(') {
      ctx->input = str_drop(ctx->input, 1);
      while(muggin_read_attribute(ctx, &attr)) {
        muggin_add_attribute(ctx, n, attr);
      }
      if(str_char_at(ctx->input, 0) != ')') {
        PARSE_ERROR(ctx, "Expected ')' after attributes.");
      }
      ctx->input = str_drop(ctx->input, 1);
    }
    // rest of the line is simple content
    if(str_splitat(ctx->input, "\n", &content, &rest)) {
      content = str_trim(content);
      ctx->input = rest;
    } else {
      content = str_trim(ctx->input); // no more newlines, rest is content
      ctx->input = STR_EMPTY;
    }
    if(content.len) {
      //printf("add text child: "STR_FMT"\n", STR_ARG(content));
      m_Contents *contents = NEW(ctx->a, m_Contents);
      muggin_contents_from_str(ctx, content, contents);
      muggin_add_child(ctx, n, (m_Node){
          .type = NODE_TEXT,
          .contents = contents
        });
    }


    // parse child elements at indent+2
    current_indent = ctx->indent;
    ctx->indent += 2;
    while(muggin_parse_node(ctx, &child)) {
      //printf("adding ELEMENT child: "STR_FMT"\n", STR_ARG(CONSTANT(ctx->t, child->elt.tag)));
      muggin_add_child(ctx, n, *child);
    }
    ctx->indent = current_indent;

    //if(str_char_at(ctx->input, 0)) startswith(ctx->input, str_constant("("))
    // parse text content, if any
    *to = n;
    return true;
  }

 fail:
  if(n) {
    FREE(ctx->a, n); // free, if we already allocated
  }
  return false;
}

m_Template *muggin_parse(Alloc *a, str input) {
  m_Template *t;
  _ctx ctx;
  
  t = NEW(a, m_Template);
  t->constant_idx = NEW_ARR(a, size_t, 64);
  t->constants = NEW_ARR(a, str, 64);
  bzero(t->constants, sizeof(str)*64);
  t->constants_capacity = 64;
  t->constants_count = 0;


  ctx = (_ctx){.a = a, .t = t, .indent = 0, .input = input};
  if(muggin_parse_node(&ctx, &t->root)) {
    return t;
  }

  if(t) FREE(a, t);
  return NULL;
}

void muggin_render_contents(m_Template *t, m_Contents *contents, strbuf *sb) {
  for(size_t i=0;i < contents->contents_count; i++) {
    if(i && contents->separator) {
      strbuf_append_char(sb, contents->separator);
    }
    switch(contents->contents[i].type) {
      // FIXME: escape HTML entities!
    case CT_CONSTANT: strbuf_append_str(sb, CONSTANT(t, contents->contents[i].id)); break;
    case CT_NAME: strbuf_append_str(sb, str_constant("MUUTTUJA:"));
      strbuf_append_str(sb, NAME(t, contents->contents[i].id));
      break;
    }
  }
}

void muggin_render_node(m_Template *t, m_Node *n, strbuf *sb) {
  switch(n->type) {
  case NODE_TEXT:
    muggin_render_contents(t, n->contents, sb); break; 
  case NODE_ELEMENT: {
    strbuf_append_char(sb, '<');
    strbuf_append_str(sb, CONSTANT(t, n->elt.tag));
    for(size_t i=0;i<n->elt.attributes_count;i++) {
      m_Attr a = n->elt.attributes[i];
      LOG_NOTICE("(%zu/%zu) attr "STR_FMT" has type %d",
                 i, n->elt.attributes_count,
                 STR_ARG(CONSTANT(t, a.name)), a.type);
      switch(a.type) {
      case AT_BOOL: {
        if(a.bool_value) {
          strbuf_append_char(sb, ' ');
          strbuf_append_str(sb, CONSTANT(t, a.name));
        }
        break;
      }
      case AT_STRING: {
        strbuf_append_char(sb, ' ');
        strbuf_append_str(sb, CONSTANT(t, a.name));
        strbuf_append_str(sb, str_constant("=\""));
        strbuf_append_str(sb, a.str_value);
        strbuf_append_char(sb, '"');
        break;
      }
      case AT_INT: {
        strbuf_append_char(sb, ' ');
        strbuf_append_str(sb, CONSTANT(t, a.name));
        strbuf_append_char(sb, '=');
        strbuf_append_int(sb, a.int_value);
        break;
      }
      case AT_DOUBLE: {
        char dbl[64]; // PENDING: what's a reasonable max len?
        size_t len = snprintf(dbl, 64, "%.2f", a.double_value);
        strbuf_append_str(sb, (str){.len = len, .data = dbl});
        break;
      }
      case AT_CONSTANT: {
        LOG_NOTICE("render constant attr '%.*s' value '%.*s'", STR_ARG(CONSTANT(t,a.name)),
               STR_ARG(CONSTANT(t, a.constant)));
        strbuf_append_char(sb, ' ');
        strbuf_append_str(sb, CONSTANT(t, a.name));
        strbuf_append_str(sb, str_constant("=\""));
        strbuf_append_str(sb, CONSTANT(t, a.constant));
        strbuf_append_char(sb, '"');
        break;
      }
      case AT_VARIABLE: {
        // FIXME: render actual variable!
        strbuf_append_char(sb, ' ');
        strbuf_append_str(sb, CONSTANT(t, a.name));
        strbuf_append_str(sb, str_constant("=\""));
        strbuf_append_str(sb, str_constant("MUUTTUJA!"));
        strbuf_append_str(sb, NAME(t, a.variable));
        strbuf_append_char(sb, '"');
        break;
      }
      case AT_CONTENTS: {
        strbuf_append_char(sb, ' ');
        strbuf_append_str(sb, CONSTANT(t, a.name));
        strbuf_append_str(sb, str_constant("=\""));
        muggin_render_contents(t, a.contents, sb);
        strbuf_append_char(sb, '"');
        break;
      }
    }
    }// end attributes

    strbuf_append_char(sb, '>');
    if(!(n->elt.flags & EF_VOID)) {
      // recurse into children, unless this is a void element
      for(int i = 0; i < n->elt.children_count; i++) {
        muggin_render_node(t, &n->elt.children[i], sb);
      }
      strbuf_append_str(sb, str_constant("</"));
      strbuf_append_str(sb, CONSTANT(t, n->elt.tag));
      strbuf_append_char(sb, '>');
    }
  }
  }
}
  
void muggin_render(m_Template *t, Alloc *a, str *to) {
  strbuf *sb = strbuf_new(a);
  strbuf_append_str(sb, str_constant("<!DOCTYPE html>\n"));
  muggin_render_node(t, t->root, sb);
  strbuf_append_char(sb, 0); // NUL terminate to play nice as C string
  *to = sb->str;
  FREE(a, sb); // free buffer struct
}

/*
static void muggin_free_node(Alloc *a, m_Node *n) {
  switch(n->type) {
  case NODE_TEXT: {
    FREE(a, n->contents->contents);
    FREE(a, n->contents);
    break;
  }
  case NODE_ELEMENT: {
    for(size_t i=0;i<n->elt.attributes_count; i++) {
      muggin_free_attr(a, n->elt.attributes[i]);
    }
    FREE(a, n->elt.attributes);
  }


  }
}
void muggin_free(m_Template *t, Alloc *a) {
   string_pool_free(a, t->constants, t->constants_count);
   string_pool_free(a, t->names, t->names_count);
   muggin_free_node(a, t->root);
 }
*/
