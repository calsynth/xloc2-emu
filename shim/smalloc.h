// smalloc stub: special-pool allocator mapped to plain malloc.
#pragma once

#include <cstdlib>
#include <cstring>

struct smalloc_pool {
  int dummy;
};

extern struct smalloc_pool extmem_smalloc_pool;

static inline void* sm_malloc_pool(struct smalloc_pool*, size_t n) { return malloc(n); }
static inline void* sm_zalloc_pool(struct smalloc_pool*, size_t n) { return calloc(1, n); }
static inline void sm_free_pool(struct smalloc_pool*, void* p) { free(p); }
static inline void* sm_malloc(size_t n) { return malloc(n); }
static inline void* sm_zalloc(size_t n) { return calloc(1, n); }
static inline void sm_free(void* p) { free(p); }
