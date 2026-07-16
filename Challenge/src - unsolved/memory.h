#ifndef MEMORY_H
#define MEMORY_H

#include "common.h"

typedef struct {
    uint32_t block_id;
    bool     in_use;
    uint32_t alloc_size;   /* requested size (may be < BLOCK_SIZE) */
    uint32_t next_block;   /* for chained allocations; 0 = none */
    uint8_t  padding[4];   /* alignment padding */
} BlockHeader;

typedef struct {
    uint8_t      pool[POOL_SIZE];
    BlockHeader  headers[MAX_BLOCKS];
    uint32_t     free_list[MAX_BLOCKS];
    uint32_t     free_count;
    uint32_t     total_allocated;
    uint32_t     total_freed;
    /* BUG: missing mutex for thread safety */
} MemoryPool;

/* Initialize the memory subsystem */
void  mem_init(MemoryPool *mp);

/* Allocate 'size' bytes. Returns pointer or NULL. */
void *mem_alloc(MemoryPool *mp, size_t size);

/* Free a previously allocated pointer. */
void  mem_free(MemoryPool *mp, void *ptr);

/* Get statistics */
void  mem_stats(const MemoryPool *mp, uint32_t *alloc, uint32_t *freed, uint32_t *free_blocks);

/* --- NEW: AI must implement these --- */

/* Defragment the pool: coalesce adjacent free blocks into larger contiguous runs. */
int   mem_defrag(MemoryPool *mp);

/* Resize an existing allocation. May move the block. */
void *mem_realloc(MemoryPool *mp, void *ptr, size_t new_size);

#endif /* MEMORY_H */
