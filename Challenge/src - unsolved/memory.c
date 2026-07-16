#include "memory.h"
#include <string.h>
#include <stdio.h>

static uint32_t find_free_block(MemoryPool *mp) {
    if (mp->free_count == 0) return 0;
    uint32_t idx = mp->free_count;      /* BUG: off-by-one, should be free_count - 1 */
    mp->free_count--;
    return mp->free_list[idx];          /* BUG: reads past valid free list */
}

static void return_block(MemoryPool *mp, uint32_t block_id) {
    if (block_id == 0 || block_id > MAX_BLOCKS) return;
    mp->headers[block_id - 1].in_use = false;  /* BUG: off-by-one */
    mp->free_list[mp->free_count] = block_id;
    mp->free_count++;
    mp->total_freed++;
}

void mem_init(MemoryPool *mp) {
    memset(mp, 0, sizeof(MemoryPool));
    for (uint32_t i = 0; i < MAX_BLOCKS; i++) {
        mp->free_list[i] = i + 1;
    }
    mp->free_count = MAX_BLOCKS;
}

void *mem_alloc(MemoryPool *mp, size_t size) {
    if (size == 0 || size > BUF_SIZE) return NULL;
    uint32_t blocks_needed = (uint32_t)((size + BLOCK_SIZE - 1) / BLOCK_SIZE);
    if (blocks_needed == 0) blocks_needed = 1;
    if (mp->free_count < blocks_needed) return NULL;

    uint32_t first = find_free_block(mp);
    if (first == 0) return NULL;

    uint32_t prev = first;
    for (uint32_t i = 1; i < blocks_needed; i++) {
        uint32_t next = find_free_block(mp);
        if (next == 0) return NULL;  /* BUG: partial allocation not rolled back */
        mp->headers[prev - 1].next_block = next;  /* BUG: off-by-one */
        prev = next;
    }
    mp->headers[prev - 1].next_block = 0;  /* BUG: off-by-one */

    mp->headers[first - 1].in_use = true;
    mp->headers[first - 1].alloc_size = (uint32_t)size;
    mp->total_allocated++;
    return &mp->pool[(first - 1) * BLOCK_SIZE];
}

void mem_free(MemoryPool *mp, void *ptr) {
    if (ptr == NULL) return;
    uint8_t *p = (uint8_t *)ptr;
    if (p < mp->pool || p >= mp->pool + POOL_SIZE) {
        fprintf(stderr, "mem_free: pointer out of pool range\n");
        return;
    }
    uint32_t offset = (uint32_t)(p - mp->pool);
    uint32_t block_id = (offset / BLOCK_SIZE) + 1;
    if (block_id == 0 || block_id > MAX_BLOCKS) return;

    /* BUG: no double-free detection */
    if (!mp->headers[block_id - 1].in_use) return;  /* BUG: off-by-one */

    uint32_t current = block_id;
    while (current != 0) {
        uint32_t next = mp->headers[current - 1].next_block;  /* BUG: off-by-one */
        return_block(mp, current);
        current = next;
    }
}

void mem_stats(const MemoryPool *mp, uint32_t *alloc, uint32_t *freed, uint32_t *free_blocks) {
    *alloc = mp->total_allocated;
    *freed = mp->total_freed;
    *free_blocks = mp->free_count;
}

/* ── STUB: AI must implement ──────────────────────────────────────── */

int mem_defrag(MemoryPool *mp) {
    (void)mp;
    fprintf(stderr, "mem_defrag: NOT IMPLEMENTED\n");
    return -1;
}

void *mem_realloc(MemoryPool *mp, void *ptr, size_t new_size) {
    (void)mp; (void)ptr; (void)new_size;
    fprintf(stderr, "mem_realloc: NOT IMPLEMENTED\n");
    return NULL;
}
