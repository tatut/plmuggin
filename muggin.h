#ifndef muggin_h
#define muggin_h
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "str.h"
#include "alloc.h"
#include "postgres.h"

typedef enum m_AttrType {
  AT_STRING,    // a str struct value
  AT_INT,       // 64bit signed integer
  AT_DOUBLE,    // 64bit double
  AT_BOOL,      // boolean, if 0 the attribute is omitted when rendering
  AT_CONSTANT,  // a single constant value
  AT_VARIABLE,  // a single variable reference
  AT_CONTENTS,  // list of constants and variable names
} m_AttrType;

/* An HTML attribute */
typedef struct m_Attr {
  size_t name; // the attribute name constant idx, like "class"
  m_AttrType type;
  union {
    str str_value;
    int64_t int_value;
    bool bool_value;
    double double_value;
    size_t variable; // id of variable name
    size_t constant; // id of constant
    struct m_Contents *contents;
  };
} m_Attr;

typedef enum m_NodeType { NODE_ELEMENT, NODE_TEXT } m_NodeType;

struct m_Node;

// element flags
#define EF_VOID 1

typedef struct m_Elt {
  size_t tag; // the tagname constant idx, like "div" or "x-foobar"
  size_t attributes_count; // how many attributes this element has
  size_t attributes_capacity; // how many are allocated
  m_Attr *attributes; // the attributes array

  size_t children_count;
  size_t children_capacity;
  struct m_Node *children;

  uint8_t flags;
} m_Elt;

typedef enum m_ContentType {
  CT_CONSTANT, CT_NAME
} m_ContentType;


/* Text content that is either a constant, or a name */
typedef struct m_Content {
  size_t id;
  uint8_t type;
} m_Content;

/* Dynamic array of contents */
typedef struct m_Contents {
  size_t contents_count;
  size_t contents_capacity;
  m_Content *contents;
  char separator; // char to separate, like ' ' for class list, 0 for none
} m_Contents;

/* An HTML element or text node, has attributes and children */
typedef struct m_Node {
  m_NodeType type;
  union {
    m_Elt elt;
    m_Contents *contents; // for text nodes
  };
} m_Node;

typedef struct m_Template {
  size_t *constant_idx; // constant order number to array index
  str *constants;
  size_t constants_count;
  size_t constants_capacity;

  // all variable names, you can refer to variables via idx
  size_t *name_idx;
  str *names;
  size_t names_count;
  size_t names_capacity;


  m_Node *root;
} m_Template;

typedef struct m_Binding {
  Datum data;
  Oid type;
} m_Binding;


/* Render time bindings for different names. */
typedef struct m_Scope {
  m_Binding *values; // binding by name idx
} m_Scope;

/**
 * Parse the given input, returns the root node.
 * Allocates fresh strings so input string can be
 * freed after parse.
 *
 */
m_Template *muggin_parse(Alloc *a, str input);

/**
 * Render to string.
 */
void muggin_render(m_Template *t, Alloc *a, str *to);

/**
 * Free all allocations in the template.
 */
//void muggin_free(m_Template *t, Alloc *a);

#endif //muggin_h
