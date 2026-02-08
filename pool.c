/* A memory allocator with an autorelease pool.
 * All allocations are added to a linked list and can
 * be released with single call.
 */
#include "postgres.h"
#include "env.h"
#include "pool.h"
#include "utils/palloc.h"

typedef struct PEntry {
  struct PEntry *prev, *next;
} PEntry;

typedef struct Pool {
  PEntry *last; // last allocation
} Pool;

static void *pool_alloc(Alloc *a, size_t sz) {
  void *mem;
  Pool *p;
  PEntry *entry;

  mem = palloc0(sz + sizeof(PEntry));
  if(!mem) {
    LOG_ERROR("Unable to allocate %zu bytes", sz);
    return NULL;
  }
  p = (Pool*) a->user;
  entry = mem;
  entry->prev = p->last;
  entry->next = NULL;
  if(p->last)
    p->last->next = entry;
  p->last = entry;
  return (void*)( (char*)mem + sizeof(PEntry) );
}

static void *pool_realloc(Alloc *a, void *mem, size_t sz) {
  Pool *p;
  PEntry *entry, *prev, *next;
  bool is_last;

  if(!mem) {
    return pool_alloc(a, sz);
  }
  // get the entry
  p = (Pool*) a->user;
  LOG_NOTICE("pool_realloc %zu, pool=%p, mem=%p", sz, p, mem);
  entry = (PEntry*)( (char*)mem - sizeof(PEntry) );
  prev = entry->prev;
  next = entry->next;
  is_last = p->last == entry;

  entry = repalloc(entry, sz + sizeof(PEntry));
  if(!entry) {
    LOG_ERROR("Unable to realloc pointer to new size %zu", sz);
    return NULL;
  }
  if(prev) prev->next = entry;
  if(next) next->prev = entry;
  if(is_last) p->last = entry;
  return (void*)( (char*)entry + sizeof(PEntry) );
}

static void pool_free(Alloc *a, void *mem) {
  // DO NOTHING, everything is freed at release
}

void pool_release(Alloc *a) {
  LOG_NOTICE("pool_release");
  Pool *p = a->user;
  PEntry *entry = p->last;
  while(entry) {
    PEntry *prev = entry->prev;
    pfree(entry);
    entry = prev;
  }
  pfree(a);
}

Alloc *pool_new(void) {
  void *mem = palloc0( sizeof(Alloc) + sizeof(Pool));
  Alloc *a = mem;
  Pool *p = (Pool*)( (char*)mem + sizeof(Alloc) );
  p->last = NULL;
  a->alloc = pool_alloc;
  a->realloc = pool_realloc;
  a->free = pool_free;
  a->user = p;
  LOG_NOTICE("pool alloc %p, pool %p", a, p);

  return a;
}
