#ifndef muggin_h
#define muggin_h
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "str.h"
#include "alloc.h"

typedef enum m_AttrType {
  AT_STRING, // a str struct value
  AT_INT,    // 64bit signed integer
  AT_DOUBLE, // 64bit double
  AT_BOOL,   // boolean, if 0 the attribute is omitted when rendering
  AT_CONSTANTS, // list of constants, separated by space
} m_AttrType;

typedef struct m_AttrConstants {
  size_t constants_count;
  size_t *constants;
} m_AttrConstants;

/* An HTML attribute */
typedef struct m_Attr {
  size_t name; // the attribute name constant idx, like "class"
  m_AttrType type;
  union {
    str str_value;
    int64_t int_value;
    bool bool_value;
    double double_value;
    m_AttrConstants constants_value;
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

/* An HTML element or text node, has attributes and children */
typedef struct m_Node {
  m_NodeType type;
  union {
    m_Elt elt;
    str content; // for text nodes
  };
} m_Node;

typedef struct m_Template {
  size_t *constant_idx; // constant order number to array index
  str *constants;
  size_t constants_count;
  size_t constants_capacity;
  m_Node *root;
} m_Template;

/**
 * Parse the given input, returns the root node.
 * Allocates fresh strings so input string can be
 * freed after parse.
 *
 */
m_Template *muggin_parse(Alloc *a, str input);

/**
 * Write template as HTML to given file.
 */
void muggin_print_html(FILE *to, m_Template *t);

/**
 * Render to string.
 */
void muggin_render(m_Template *t, Alloc *a, str *to);

#endif //muggin_h
