#ifndef alloc_h
#define alloc_h
#include <stdint.h>

typedef struct Alloc {
  void* (*alloc)(struct Alloc *a, size_t);
  void (*free)(struct Alloc *a, void*);
  void* (*realloc)(struct Alloc *a, void*, size_t);
  void *user;
} Alloc;

#define ALLOC(a, size) (a)->alloc((a), (size))
#define FREE(a, ptr) (a)->free((a), (ptr))
#define NEW(a, type) (type*)(ALLOC((a), sizeof(type)))
#define NEW_ARR(a, type, count) (type*)(ALLOC((a), sizeof(type)*(count)))
#define REALLOC(a, ptr, new_size) (a)->realloc((a), (ptr), (new_size))


#endif // alloc_h
