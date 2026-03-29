#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "muggin.h"
#include "postgres.h"
#include "executor/spi.h"
#include "utils/lsyscache.h"

#define STR_IMPLEMENTATION
#include "str.h"

#include "alloc.h"
#include "env.h"

/* context to pass around all parsing functions */
#define MAX_ERROR_SIZE 1024
typedef struct _ctx {
  m_Template *t;
  int indent;
  str input;
  char error[MAX_ERROR_SIZE];
  str error_at; // location where error occurred
} _ctx;

/* render context passed around all rendering functions */
typedef struct _rctx {
  m_Template *t;
  m_Scope *scope;
  bool has_query; // if "m:q" attribute is present
  size_t mq_idx; // "m:q" attribute index
  m_Node *mq; // recursive call handling query, don't handle it again
} _rctx;

#define CONSTANT(t, idx) ((t)->constants[(t)->constant_idx[(idx)]])
#define NAME(t, idx) ((t)->names[(t)->name_idx[(idx)]])

/* Signatures */


static bool muggin_contents_from_str(_ctx *ctx, str s, m_Contents *contents);
static bool _grow(_ctx *ctx, size_t *count, size_t *capacity, void **array,
                  size_t sz);
static size_t string_pool_idx(_ctx *ctx, str *pool, size_t *idxs, size_t count,
                              size_t capacity, str s);
static size_t string_pool_lookup(str *pool, size_t *idxs, size_t count, size_t capacity,
                                 str s, bool *found);
static size_t string_pool_use(_ctx *ctx, size_t **idx, str **pool,
                              size_t *count, size_t *capacity, str s);
static size_t muggin_use_constant(_ctx *ctx, str constant);
static size_t muggin_use_name(_ctx *ctx, str name);
static size_t muggin_lookup_name(m_Template *tpl, str name, bool *found);

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

void muggin_render_contents(_rctx *ctx, m_Contents *contents, strbuf *sb);
void muggin_render_node(_rctx *ctx, m_Node *n, strbuf *sb);
static int query(_rctx *ctx, m_Contents *contents);
static str muggin_skip_metadata(str input);




// grow dynamic array if needed
static bool _grow(_ctx *ctx, size_t *count, size_t *capacity, void **array, size_t sz) {
  if(*count == *capacity) {
    size_t new_capacity = *capacity ? *capacity * 1.618 : 64;
    void *new_array = *capacity
      ? REALLOC(*array, new_capacity * sz)
      : ALLOC(new_capacity * sz);
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
    LOG_ERROR("Bug in string pool hashing!, existing entry idx not found: "STR_FMT,
              STR_ARG(s));
  } else if(!pool[idx].len) {
    // free slot
    idxs[num] = idx;
    pool[idx] = str_dup(s);
    return num;
  }
  idx = (idx + 1) % capacity;
  goto again;
}

static size_t string_pool_lookup(str *pool, size_t *idxs, size_t count, size_t capacity, str s,
                                 bool *found) {
  uint64_t start_idx, idx;
  start_idx = str_hash(s) % capacity;
  idx = start_idx;
  do {
    if(str_eq(pool[idx], s)) {
      LOG_DEBUG("pool[%llu] equals "STR_FMT, idx, STR_ARG(s));
      for(size_t i=0;i<count;i++) {
        if(idxs[i] == idx) {
          *found = true;
          return i;
        }
      }
      LOG_ERROR("Bug in string pool hashing!, existing entry idx not found: "STR_FMT,
                STR_ARG(s));
    }
    idx = (idx + 1) % capacity;
  } while(idx != start_idx);
  *found = false;
  return 0;

}

static size_t string_pool_use(_ctx *ctx, size_t **idx, str **pool, size_t *count,
                              size_t *capacity, str s) {
  size_t id;
  if(*count == *capacity) {
    size_t *old_idx = *idx;
    str *old_pool = *pool;
    size_t old_count = *count;
    size_t new_capacity = *capacity ? *capacity * 1.618 : 64;
    size_t *new_idx = NEW_ARR(size_t, new_capacity);
    str *new_pool = NEW_ARR(str, new_capacity);
    if(!new_idx || !new_pool) {
      LOG_ERROR("String pool rehash alloc failed, new_capacity=%zu", new_capacity);
    }
    bzero(new_pool, sizeof(str)*new_capacity);
    *idx = new_idx;
    *pool = new_pool;
    *capacity = new_capacity;
    for(size_t i=0;i<old_count;i++) {
      string_pool_idx(ctx, *pool, *idx, i, *capacity, old_pool[old_idx[i]]);
      FREE(old_pool[old_idx[i]].data);
    }
    if(old_pool) FREE(old_pool);
    if(old_idx) FREE(old_idx);
    *count = old_count;
  }

  id = string_pool_idx(ctx, *pool, *idx, *count, *capacity, s);
  *count += 1;
  LOG_DEBUG("string pool (%zu) = %.*s", id, (int) s.len, s.data);
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

static size_t muggin_lookup_name(m_Template *tpl, str name, bool *found) {
  return string_pool_lookup(tpl->names, tpl->name_idx, tpl->names_count, tpl->names_capacity,
                            name, found);
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
  if(ch == ':' && !first) return true; // allow ':' in attr name (for namespace)
  return valid_tag(ch, first); // so far they are the same
}

static bool valid_id(char ch, bool first) { return valid_tag(ch, first); }
static bool valid_class(char ch, bool first) { return valid_tag(ch, first); }

static str void_elements[] = {
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
    LOG_DEBUG("adding attr "STR_FMT, STR_ARG(CONSTANT(ctx->t, attr.name)));
    n->elt.attributes[n->elt.attributes_count++] = attr;
  }
}

static void muggin_add_child(_ctx *ctx, m_Node *n, m_Node child) {
  if(n->type == NODE_TEXT) {
    LOG_ERROR("Text nodes can't have children.");
  } else {
    if(n->elt.children_capacity == n->elt.children_count) {
      size_t new_capacity = n->elt.children_capacity ? n->elt.children_capacity * 2 : 8;
      m_Node *new_children = NEW_ARR(m_Node, new_capacity);
      if(!new_children) {
        LOG_ERROR("Unable to add child node, alloc failed.");
      }
      if(n->elt.children) {
        memcpy(new_children, n->elt.children, n->elt.children_capacity * sizeof(m_Node));
        FREE(n->elt.children);
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
      attr->type = AT_CONTENTS;
      attr->contents = NEW(m_Contents);
      if(!muggin_contents_from_str(ctx, val, attr->contents))
        return false;
      ctx->input = rest;
      return true;
    } else {
      PARSE_ERROR(ctx, "Unterminated quoted string");
    }
  } else {
    size_t len=0;
    char ch;
    do {
      ch = str_char_at(ctx->input, len++);
    } while(ch != ' ' && ch != '\t' && ch != '\n' && ch != ')');
    val = str_take(ctx->input, len-1);
    ctx->input = str_drop(ctx->input, ch == ')' ? len-1 : len); // leave ')' in input
    attr->type = AT_CONTENTS;
    attr->contents = NEW(m_Contents);
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
  LOG_DEBUG("str cont (%d) => "STR_FMT, type, STR_ARG(content));
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
    LOG_DEBUG("found '{{' at idx: %d", idx);
    if(idx > 0) {
      LOG_DEBUG("before var: '"STR_FMT"'", STR_ARG(str_take(s,idx)));
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
    LOG_DEBUG("var name '"STR_FMT"'", STR_ARG(name));
    s = str_drop(s, idx+2);
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
    n = NEW(m_Node);
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
    n = NEW(m_Node);
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
        m_Contents *contents = NEW(m_Contents);
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
      attr = (m_Attr){0};
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
      m_Contents *contents = NEW(m_Contents);
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
    FREE(n); // free, if we already allocated
  }
  return false;
}

/* Skip metadata section, separated by "---" plus whitespace */
str muggin_skip_metadata(str input) {
  str orig;
  int index;

  orig = input;
  index = str_indexof(input, '-');
  while (index != -1) {
    input = str_drop(input, index);
    if (str_char_at(input, 0) == '-' && str_char_at(input, 1) == '-' &&
        str_char_at(input, 2) == '-') {
      return str_ltrim(str_drop(input, 3));
    }
  }
  return orig;
}


m_Template *muggin_parse(str input, str (*load_template)(str)) {
  m_Template *t;
  _ctx ctx;

  input = str_ltrim(muggin_skip_metadata(input));

  t = NEW(m_Template);
  t->constant_idx = NEW_ARR(size_t, 64);
  t->constants = NEW_ARR(str, 64);
  bzero(t->constants, sizeof(str)*64);
  t->constants_capacity = 64;
  t->constants_count = 0;

  t->name_idx = NEW_ARR(size_t, 64);
  t->names = NEW_ARR(str, 64);
  bzero(t->names, sizeof(str)*64);
  t->names_capacity = 64;
  t->names_count = 0;

  ctx = (_ctx){.t = t, .indent = 0, .input = input};
  if(muggin_parse_node(&ctx, &t->root)) {
    return t;
  }

  if(t) FREE(t);
  return NULL;
}

void muggin_render_contents(_rctx *ctx, m_Contents *contents, strbuf *sb) {
  for(size_t i=0;i < contents->contents_count; i++) {
    if(i && contents->separator) {
      strbuf_append_char(sb, contents->separator);
    }
    switch(contents->contents[i].type) {
      // FIXME: escape HTML entities!
    case CT_CONSTANT:
      strbuf_append_str(sb, CONSTANT(ctx->t, contents->contents[i].id));
      break;
    case CT_NAME: {
      m_Binding b;
      b = ctx->scope->values[contents->contents[i].id];
      LOG_DEBUG("idx: %zu, b.flags = %d", contents->contents[i].id, b.flags);
      if (b.flags & BF_HAS_VALUE) {
        // We need to stringify here
        str value;
        Oid typoutput;
        bool typisvarlena;

        getTypeOutputInfo(b.oid, &typoutput, &typisvarlena);
        value.data = OidOutputFunctionCall(typoutput, b.value);
        value.len = strlen(value.data);

        if(b.flags & BF_NEED_ESCAPE) {
          strbuf_append_str_escaped(sb, value);
        } else {
          strbuf_append_str(sb, value);
        }
      } else {
        LOG_NOTICE("Missing value for: "STR_FMT, STR_ARG(NAME(ctx->t, contents->contents[i].id)));

      }
      break;
    }
    }
  }
}

/* Run a PostgreSQL query via SPI.
 * Converts {{name}} arguments to PostgreSQL query arguments, to avoid string
 * interpolation problems.
 */
static int query(_rctx *ctx, m_Contents *contents) {
  strbuf *sb;
  int nargs;
  Datum *values;
  Oid *argtypes;
  int result;

  // we don't know the exact argument count, but it's at most the contents count
  nargs = 0;
  values = NEW_ARR(Datum, contents->contents_count);
  argtypes = NEW_ARR(Oid, contents->contents_count);

  sb = strbuf_new();

  for(size_t i=0;i < contents->contents_count; i++) {
    if(i && contents->separator) {
      strbuf_append_char(sb, contents->separator);
    }
    switch(contents->contents[i].type) {
    case CT_CONSTANT:
      strbuf_append_str(sb, CONSTANT(ctx->t, contents->contents[i].id));
      break;
    case CT_NAME: {
      m_Binding b;
      b = ctx->scope->values[contents->contents[i].id];
      LOG_DEBUG("idx: %zu, b.flags = %d", contents->contents[i].id, b.flags);
      if(b.flags & BF_HAS_VALUE) {
        argtypes[nargs] = b.oid;
        values[nargs] = b.value;
        strbuf_append_char(sb, '$');
        nargs += 1;
        strbuf_append_int(sb, nargs);
      } else {
        LOG_ERROR("Missing value needed in query for: "STR_FMT, STR_ARG(NAME(ctx->t, contents->contents[i].id)));
      }
      break;
    }
    }
  }
  strbuf_append_char(sb, 0); // NUL terminate
  LOG_DEBUG("Running query: %s", sb->str.data);
  // max 65536 rows
  result = SPI_execute_with_args(sb->str.data, nargs, argtypes, values, NULL, true, 1<<16);
  FREE(sb->str.data);
  FREE(sb);
  return result;
}

void muggin_render_node(_rctx *ctx, m_Node *n, strbuf *sb) {
  switch(n->type) {
  case NODE_TEXT:
    muggin_render_contents(ctx, n->contents, sb); break;
  case NODE_ELEMENT: {
    int mq_attr;
    mq_attr = -1;
    strbuf_append_char(sb, '<');
    strbuf_append_str(sb, CONSTANT(ctx->t, n->elt.tag));
    for(size_t i=0;i<n->elt.attributes_count;i++) {
      m_Attr a;
      a = n->elt.attributes[i];
      if(ctx->has_query && n->elt.attributes[i].name == ctx->mq_idx) {
        // "m:q" is not rendered as an attr, it does a query to loop children
        LOG_DEBUG("we have a query!");
        mq_attr = (int) i;
        continue;
      }
      LOG_DEBUG("(%zu/%zu) attr "STR_FMT" has type %d",
                 i, n->elt.attributes_count,
                 STR_ARG(CONSTANT(ctx->t, a.name)), a.type);
      switch(a.type) {
      case AT_BOOL: {
        if(a.bool_value) {
          strbuf_append_char(sb, ' ');
          strbuf_append_str(sb, CONSTANT(ctx->t, a.name));
        }
        break;
      }
      case AT_STRING: {
        strbuf_append_char(sb, ' ');
        strbuf_append_str(sb, CONSTANT(ctx->t, a.name));
        strbuf_append_str(sb, str_constant("=\""));
        strbuf_append_str(sb, a.str_value);
        strbuf_append_char(sb, '"');
        break;
      }
      case AT_INT: {
        strbuf_append_char(sb, ' ');
        strbuf_append_str(sb, CONSTANT(ctx->t, a.name));
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
        LOG_DEBUG("render constant attr '%.*s' value '%.*s'",
                   STR_ARG(CONSTANT(ctx->t,a.name)),
                   STR_ARG(CONSTANT(ctx->t, a.constant)));
        strbuf_append_char(sb, ' ');
        strbuf_append_str(sb, CONSTANT(ctx->t, a.name));
        strbuf_append_str(sb, str_constant("=\""));
        strbuf_append_str(sb, CONSTANT(ctx->t, a.constant));
        strbuf_append_char(sb, '"');
        break;
      }
      case AT_VARIABLE: {
        // FIXME: render actual variable!
        strbuf_append_char(sb, ' ');
        strbuf_append_str(sb, CONSTANT(ctx->t, a.name));
        strbuf_append_str(sb, str_constant("=\""));
        strbuf_append_str(sb, str_constant("MUUTTUJA!"));
        strbuf_append_str(sb, NAME(ctx->t, a.variable));
        strbuf_append_char(sb, '"');
        break;
      }
      case AT_CONTENTS: {
        strbuf_append_char(sb, ' ');
        strbuf_append_str(sb, CONSTANT(ctx->t, a.name));
        strbuf_append_str(sb, str_constant("=\""));
        muggin_render_contents(ctx, a.contents, sb);
        strbuf_append_char(sb, '"');
        break;
      }
    }
    }// end attributes

    strbuf_append_char(sb, '>');
    if(!(n->elt.flags & EF_VOID)) {
      if(mq_attr != -1) {
        // execute query, render children for each row
        int result;
        result = query(ctx, n->elt.attributes[mq_attr].contents);
        if(result < 1 || !SPI_tuptable) {
          LOG_ERROR("Query failed, result: %d", result);
        } else {
          TupleDesc tupdesc;
          int *varidx;

          // Bind selected items and recurse into children
          tupdesc = SPI_tuptable->tupdesc;
          varidx = NEW_ARR(int, tupdesc->natts);

          /* Resolve columns to variables */
          for(int col = 0; col < tupdesc->natts; col++) {
              Form_pg_attribute attr;
              char *name;
              bool found;
              size_t idx;
              attr = TupleDescAttr(tupdesc, col);
              name = NameStr(attr->attname);
              idx = muggin_lookup_name(ctx->t, cstr(name), &found);
              if(found) {
                varidx[col] = (int) idx;
              } else {
                varidx[col] = -1;
                LOG_NOTICE("Query has column that is not used in template: %s", name);
              }
          }

          for(int row = 0; row < SPI_tuptable->numvals; row++) {
            HeapTuple tuple;
            tuple = SPI_tuptable->vals[row];

            for (int col = 1; col <= tupdesc->natts; col++) {
              Oid type;
              Datum value;
              bool isnull;
              int idx;
              idx = varidx[col-1];
              if (idx != -1) {
                type = SPI_gettypeid(tupdesc, col);
                value = SPI_getbinval(tuple, tupdesc, col, &isnull);
                muggin_scope_bind_idx(ctx->scope, idx, type, value, BF_NEED_ESCAPE);

              }
            }
            /* Render all children */
            for(int i = 0; i < n->elt.children_count; i++) {
              muggin_render_node(ctx, &n->elt.children[i], sb);
            }

          }
        }
      } else {
        // recurse into children, unless this is a void element
        for(int i = 0; i < n->elt.children_count; i++) {
          muggin_render_node(ctx, &n->elt.children[i], sb);
        }
      }
      strbuf_append_str(sb, str_constant("</"));
      strbuf_append_str(sb, CONSTANT(ctx->t, n->elt.tag));
      strbuf_append_char(sb, '>');
    }
  }
  }
}

m_Scope *muggin_scope_new(m_Template *tpl) {
  m_Scope *scope;
  scope = NEW(m_Scope);
  scope->template = tpl;
  scope->values = NEW_ARR(m_Binding, tpl->names_count);
  return scope;
}

void muggin_scope_bind(m_Scope *scope, str name, Oid type, Datum value, uint8_t flags) {
  bool found;
  size_t idx;
  idx = muggin_lookup_name(scope->template, name, &found);
  if(!found) {
    LOG_NOTICE("Template has no variable named: %.*s", STR_ARG(name));
  } else {
    muggin_scope_bind_idx(scope, idx, type, value, flags);
  }
}

void muggin_scope_bind_idx(m_Scope *scope, size_t idx, Oid type, Datum value, uint8_t flags) {
  LOG_DEBUG("idx %zu bound to "STR_FMT, idx, STR_ARG(value));
  scope->values[idx] = (m_Binding) {
    .oid = type,
    .value = value,
    .flags = flags | BF_HAS_VALUE
  };
}

void muggin_scope_unbind(m_Scope *scope, str name) {
  /* FIXME: does nothing, last binding stays in effect even after scope */
}


bool muggin_render(m_Template *t, m_Scope *scope, strbuf *to) {
  _rctx ctx;

  ctx = (_rctx){.t = t, .scope = scope};
  ctx.mq_idx = string_pool_lookup(t->constants, t->constant_idx, t->constants_count, t->constants_capacity,
                                  cstr("m:q"), &ctx.has_query);

  strbuf_append_str(to, str_constant("<!DOCTYPE html>\n"));
  muggin_render_node(&ctx, t->root, to);
  strbuf_append_char(to, 0); // NUL terminate to play nice as C string

  return true;
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
