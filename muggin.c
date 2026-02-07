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

/* (re)use constant, returns index */
static size_t muggin_use_constant(_ctx *ctx, str constant) {
  size_t constant_num = ctx->t->constants_count;
  //printf(STR_FMT" constant num: %ld\n", STR_ARG(constant), constant_num);
  if(ctx->t->constants_count == ctx->t->constants_capacity) {
    //printf("REHASHING!\n");
    size_t *old_idx = ctx->t->constant_idx;
    str *old_str = ctx->t->constants;
    size_t old_count = ctx->t->constants_count;
    size_t new_capacity = ctx->t->constants_capacity ? ctx->t->constants_capacity * 2 : 64;
    size_t *new_idx = NEW_ARR(ctx->a, size_t, new_capacity);
    str *new_str = NEW_ARR(ctx->a, str, new_capacity);
    if(!new_idx || !new_str) {
      LOG_ERROR("Constant hashtable rehash alloc failed, new_capacity=%zu", new_capacity);
    }
    bzero(new_str, sizeof(str)*new_capacity);
    ctx->t->constant_idx = new_idx;
    ctx->t->constants = new_str;
    ctx->t->constants_capacity = new_capacity;
    ctx->t->constants_count = 0;

    for(size_t i=0;i<old_count;i++) {
      muggin_use_constant(ctx, old_str[old_idx[i]]);
      FREE(ctx->a, &old_str[old_idx[i]]);
    }
    FREE(ctx->a, old_str);
    FREE(ctx->a, old_idx);

  }
  uint64_t idx = str_hash(constant) % ctx->t->constants_capacity;
 again:
  if(str_eq(ctx->t->constants[idx], constant)) {
    // we already have this entry, return constant_idx that has this idx
    for(size_t i=0;i < ctx->t->constants_count; i++) {
      if(ctx->t->constant_idx[i] == idx) return i;
    }
    LOG_ERROR("Bug in constant hashtable!");
  } else if(!ctx->t->constants[idx].len) {

    // free slot!
    // FIXME: ideally we should rehash here, if ht is full
    ctx->t->constant_idx[constant_num] = idx;
    ctx->t->constants[idx] = str_dup(ctx->a, constant);
    ctx->t->constants_count++;
    return constant_num;
  }
  idx = (idx + 1) % ctx->t->constants_capacity;
  goto again;

}

static int muggin_read_indent(_ctx *ctx) {
  int indent = 0;
 next:
  if (!ctx->input.len) return indent;
  char ch = ctx->input.data[0];
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
  str orig_input = ctx->input;
  int actual = muggin_read_indent(ctx);
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
  str orig = ctx->input;
  int len = 0;
  bool first = true;
  while(ctx->input.len && valid(ctx->input.data[0], first)) {
    first = false;
    len++;
    ctx->input = str_drop(ctx->input, 1);
  }
  if(len > 0) {
    str constant = str_take(orig, len);
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
    if(n->elt.attributes_count == n->elt.attributes_capacity) {
      size_t new_capacity = n->elt.attributes_capacity ? n->elt.attributes_capacity * 2 : 8;
      m_Attr *new_attrs = NEW_ARR(ctx->a, m_Attr, new_capacity);
      if(!new_attrs) {
        LOG_ERROR("Can't add attribute: allocation failed");
      }
      if(n->elt.attributes) {
        memcpy(new_attrs, n->elt.attributes, n->elt.attributes_capacity);
        FREE(ctx->a, n->elt.attributes);
      }
      n->elt.attributes_capacity = new_capacity;
      n->elt.attributes = new_attrs;
    }
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
  ctx->input = str_ltrim(ctx->input);
  if(str_char_at(ctx->input, 0) == ')') return false;
  size_t name;
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
    str val, rest;
    if(str_splitat(ctx->input, "\"", &val, &rest)) {
      attr->type = AT_STRING;
      attr->str_value = str_dup(ctx->a, val);
      ctx->input = rest;
      return true;
    } else {
      PARSE_ERROR(ctx, "Unterminated quoted string");
    }
  } else {
    str val, rest;
    size_t len=0;
    char ch;
    do {
      ch = str_char_at(ctx->input, len++);
    } while(ch != ' ' && ch != '\t' && ch != '\n' && ch != ')');
    val = str_take(ctx->input, len-1);
    ctx->input = str_drop(ctx->input, ch == ')' ? len-1 : len); // leave ')' in input
    attr->type = AT_STRING;
    attr->str_value = str_dup(ctx->a, val);
    return true;
  }

 fail:
  return false;
}
static bool muggin_parse_node(_ctx *ctx, m_Node **to) {
  // new node starts on a fresh line, we expect indent
  if(!muggin_check_indent(ctx, ctx->indent)) return false;

  m_Node *n = NULL;
  size_t tag;
  if(str_char_at(ctx->input, 0) == '|') {
    ctx->input = str_trim(str_drop(ctx->input, 1));
    // text node
    n = NEW(ctx->a, m_Node);
    n->type = NODE_TEXT;
    str line, rest;
    if(str_splitat(ctx->input, "\n", &line, &rest)) {
      line = str_trim(line);
      ctx->input = rest;
    } else {
      line = str_trim(ctx->input);
      ctx->input = STR_EMPTY;
    }
    n->content = str_dup(ctx->a, line);
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
    bool has_id = false;
    size_t id_constant;
    int classes = 0;
    size_t class_constants[64]; // 64 ought to be enough, even for tailwind users? ;)
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
      size_t class;
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
      size_t id = muggin_use_constant(ctx, str_constant("id"));
      size_t *ids = NEW(ctx->a, size_t);
      ids[0] = id_constant;
      muggin_add_attribute(ctx, n, (m_Attr) {.name = id, .type = AT_CONSTANTS,
                                             .constants_value = {.constants_count = 1, .constants = ids}});
    }
    if(classes) {
      // add attribute
      size_t class = muggin_use_constant(ctx, str_constant("class"));
      size_t *class_names = NEW_ARR(ctx->a, size_t, classes);
      memcpy(class_names, class_constants, sizeof(size_t)*classes);
      muggin_add_attribute(ctx, n, (m_Attr) {.name = class, .type = AT_CONSTANTS,
                                             .constants_value = {.constants_count = classes, .constants = class_names }});
    }

    // parse attributes, if any
    if(str_char_at(ctx->input, 0) == '(') {
      ctx->input = str_drop(ctx->input, 1);
      m_Attr attr;
      while(muggin_read_attribute(ctx, &attr)) {
        muggin_add_attribute(ctx, n, attr);
      }
      if(str_char_at(ctx->input, 0) != ')') {
        PARSE_ERROR(ctx, "Expected ')' after attributes.");
      }
      ctx->input = str_drop(ctx->input, 1);
    }
    // rest of the line is simple content
    str content, rest;
    if(str_splitat(ctx->input, "\n", &content, &rest)) {
      content = str_trim(content);
      ctx->input = rest;
    } else {
      content = str_trim(ctx->input); // no more newlines, rest is content
      ctx->input = STR_EMPTY;
    }
    if(content.len) {
      //printf("add text child: "STR_FMT"\n", STR_ARG(content));
      muggin_add_child(ctx, n, (m_Node){.type = NODE_TEXT, .content = content});
    }


    // parse child elements at indent+2
    int current_indent = ctx->indent;
    ctx->indent += 2;
    m_Node *child;
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
  m_Template *t = NEW(a, m_Template);
  t->constant_idx = NEW_ARR(a, size_t, 64);
  t->constants = NEW_ARR(a, str, 64);
  bzero(t->constants, sizeof(str)*64);
  t->constants_capacity = 64;
  t->constants_count = 0;

  int indent = 0;
  _ctx ctx = (_ctx){.a = a, .t = t, .indent = 0, .input = input};
  m_Node *root;
  if(muggin_parse_node(&ctx, &t->root)) {
    return t;
  }

 fail:
  if(t) FREE(a, t);
  return NULL;
}


void muggin_render_node(m_Template *t, m_Node *n, strbuf *sb) {
  switch(n->type) {
  case NODE_TEXT: strbuf_append_str(sb, n->content); break; // FIXME: escape HTML entities!
  case NODE_ELEMENT: {
    strbuf_append_char(sb, '<');
    strbuf_append_str(sb, CONSTANT(t, n->elt.tag));
    for(int i=0;i<n->elt.attributes_count;i++) {
      m_Attr a = n->elt.attributes[i];
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
      case AT_CONSTANTS: {
        strbuf_append_char(sb, ' ');
        strbuf_append_str(sb, CONSTANT(t, a.name));
        strbuf_append_str(sb, str_constant("=\""));
        for(int i=0;i< a.constants_value.constants_count; i++) {
          if(i) strbuf_append_char(sb, ' ');
          strbuf_append_str(sb, CONSTANT(t, a.constants_value.constants[i]));
        }
        strbuf_append_char(sb, '"');
        break;
      }
        //case AT_MUSTACHE:
        // expanded values like "width: {{ width }};"
        // consisting of constants and names!
      }
    } // end attributes
    
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
    break;
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

void muggin_print_node(FILE *to, m_Template *t, m_Node *n) {
  switch(n->type) {
  case NODE_TEXT: fprintf(to, STR_FMT, STR_ARG(n->content)); break; // FIXME: escape HTML entities in parse phase?
  case NODE_ELEMENT:
    fprintf(to, "<"STR_FMT, STR_ARG(CONSTANT(t, n->elt.tag)));
    for(int i = 0; i < n->elt.attributes_count; i++) {
      m_Attr a = n->elt.attributes[i];
      switch(a.type) {
      case AT_BOOL: if(a.bool_value) {
          fprintf(to, " "STR_FMT, STR_ARG(CONSTANT(t, a.name)));
          break;
        }
      case AT_STRING: fprintf(to, " "STR_FMT"=\""STR_FMT"\"", STR_ARG(CONSTANT(t, a.name)), STR_ARG(a.str_value)); break;
      case AT_INT: fprintf(to, " "STR_FMT"=\"%lld\"", STR_ARG(CONSTANT(t, a.name)), a.int_value); break;
      case AT_DOUBLE: fprintf(to, " "STR_FMT"=\"%lf\"", STR_ARG(CONSTANT(t, a.name)), a.double_value); break;
      case AT_CONSTANTS: {
        fprintf(to, " "STR_FMT"=\"", STR_ARG(CONSTANT(t, a.name)));
        for(int i=0;i< a.constants_value.constants_count; i++) {
          if(i) fprintf(to, " ");
          fprintf(to, STR_FMT, STR_ARG(CONSTANT(t, a.constants_value.constants[i])));
        }
        fprintf(to, "\"");
        break;
      }
      }
    }
    fprintf(to, ">");
    if(!(n->elt.flags & EF_VOID)) {
      for(int i = 0; i < n->elt.children_count; i++) {
        muggin_print_node(to, t, &n->elt.children[i]);
      }
      fprintf(to, "</"STR_FMT">", STR_ARG(CONSTANT(t, n->elt.tag)));
    }
    break;
  }
}

void muggin_print_html(FILE *to, m_Template *t) {
  fprintf(to, "<!DOCTYPE html>\n");
  muggin_print_node(to, t, t->root);
  fflush(to);
}

/*
int main(int argc, char **argv) {
  str f = str_from_file(&a, "test/simple.mug");
  printf("GOT: "STR_FMT"\n", STR_ARG(f));
  m_Template *t = muggin_parse(&a, f);
  muggin_print_html(stdout, t);
  return 0;
}
*/
