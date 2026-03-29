#ifndef alloc_h
#define alloc_h

#ifdef USE_MALLOC

// Use regular stdlib malloc
#include <stdlib.h>
#define ALLOC(size) malloc((size))
#define FREE(ptr) free((ptr))
#define NEW(type) (type*)(malloc(sizeof(type)))
#define NEW_ARR(type, count) (type*)(malloc(sizeof(type)*(count)))
#define REALLOC(ptr, new_size) realloc((ptr), (new_size))
#define ARR_RESIZE(arr, new_count)                                             \
  realloc((arr), sizeof((arr)[0]) * (new_count))

#endif

#ifndef USE_MALLOC

// Use PostgreSQL palloc and friends when running inside extension

#include "postgres.h"
#include "utils/palloc.h"

#define ALLOC(size) palloc((size))
#define FREE(ptr) pfree((ptr))
#define NEW(type) (type*)(palloc_object(type))
#define NEW_ARR(type, count) (type*)(palloc(sizeof(type)*(count)))
#define REALLOC(ptr, new_size) repalloc((ptr), (new_size))
#define ARR_RESIZE(arr, new_count)                                             \
  repalloc_array((arr), typeof((arr)[0]), (new_count))

#endif

#endif // alloc_h
