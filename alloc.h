#ifndef alloc_h
#define alloc_h

#include "postgres.h"
#include "utils/palloc.h"

#define ALLOC(size) palloc((size))
#define FREE(ptr) pfree((ptr))
#define NEW(type) (type*)(palloc_object(type))
#define NEW_ARR(type, count) (type*)(palloc(sizeof(type)*(count)))
#define REALLOC(ptr, new_size) repalloc((ptr), (new_size))
#define ARR_RESIZE(arr, new_count)                                             \
  repalloc_array((arr), typeof((arr)[0]), (new_count))



#endif // alloc_h
