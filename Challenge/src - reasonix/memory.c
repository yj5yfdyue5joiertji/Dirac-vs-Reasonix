#include "memory.h"
#include <string.h>
#include <stdio.h>

/* ── Internal helpers ─────────────────────────────────────────────── */

static uint32_t find_free_block(MemoryPool *mp) {
    if (mp->free_count == 0) return 0;  /* 0 = no block */
    uint32_t idx = mp->free_count - 1;  /* FIXED: was mp->free_count (off-by-one) */
    mp->free_count--;
    return mp->free_list[idx];
}

static void return_block(MemoryPool *mp, uint32_t block_id) {
    if (block_id == 0 || block_id > MAX_BLOCKS) return;
    /* block_id is 1-indexed, headers is 0-indexed */
    BlockHeader *h = &mp->headers[block_id - 1];
    h->in_use = false;
    h->alloc_size = 0;       /* FIXED: clear stale data */
    h->next_block = 0;       /* FIXED: clear stale data */
    mp->free_list[mp->free_count] = block_id;
    mp->free_count++;
    mp->total_freed++;
}

/* ── Public API ───────────────────────────────────────────────────── */

void mem_init(MemoryPool *mp) {
    memset(mp, 0, sizeof(MemoryPool));
    for (uint32_t i = 0; i < MAX_BLOCKS; i++) {
        mp->free_list[i] = i + 1;  /* 1-indexed block IDs */
    }
    mp->free_count = MAX_BLOCKS;
}

void *mem_alloc(MemoryPool *mp, size_t size) {
    if (size == 0 || size > BUF_SIZE) return NULL;

    uint32_t blocks_needed = (uint32_t)((size + BLOCK_SIZE - 1) / BLOCK_SIZE);
    if (blocks_needed == 0) blocks_needed = 1;

    if (mp->free_count < blocks_needed) return NULL;

    /* Save state for rollback */
    uint32_t saved_free_count = mp->free_count;
    uint32_t saved_list[MAX_BLOCKS];
    memcpy(saved_list, mp->free_list, sizeof(mp->free_list));

    uint32_t first = find_free_block(mp);
    if (first == 0) return NULL;

    /* Track allocated blocks for potential rollback */
    uint32_t allocated[MAX_BLOCKS];
    uint32_t alloc_count = 1;
    allocated[0] = first;

    uint32_t prev = first;
    for (uint32_t i = 1; i < blocks_needed; i++) {
        uint32_t next = find_free_block(mp);
        if (next == 0) {
            /* FIXED: rollback partial allocation */
            for (uint32_t j = 0; j < alloc_count; j++) {
                return_block(mp, allocated[j]);
            }
            /* Restore free list to pre-allocation state */
            mp->free_count = saved_free_count;
            memcpy(mp->free_list, saved_list, sizeof(mp->free_list));
            return NULL;
        }
        mp->headers[prev - 1].next_block = next;
        allocated[alloc_count++] = next;
        prev = next;
    }
    mp->headers[prev - 1].next_block = 0;

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

    /* FIXED: double-free detection */
    if (!mp->headers[block_id - 1].in_use) {
        fprintf(stderr, "mem_free: double-free detected at block %u\n", block_id);
        return;
    }

    /* Free the chain */
    uint32_t current = block_id;
    while (current != 0) {
        uint32_t next = mp->headers[current - 1].next_block;
        return_block(mp, current);
        current = next;
    }
}

void mem_stats(const MemoryPool *mp, uint32_t *alloc, uint32_t *freed, uint32_t *free_blocks) {
    if (alloc) *alloc = mp->total_allocated;
    if (freed) *freed = mp->total_freed;
    if (free_blocks) *free_blocks = mp->free_count;
}

/* ── IMPLEMENTED: mem_defrag ──────────────────────────────────────── */

int mem_defrag(MemoryPool *mp) {
    if (mp == NULL) return -1;

    int coalesced = 0;

    /* Iterate through all blocks, find adjacent free blocks */
    for (uint32_t i = 0; i < MAX_BLOCKS; i++) {
        BlockHeader *h = &mp->headers[i];
        if (h->in_use) continue;

        /* Look ahead for consecutive free blocks */
        uint32_t next_id = h->next_block;
        while (next_id != 0 && next_id <= MAX_BLOCKS) {
            BlockHeader *nh = &mp->headers[next_id - 1];
            if (nh->in_use) break;

            /* Coalesce: skip the next block and point to the one after */
            uint32_t after_next = nh->next_block;
            h->next_block = after_next;

            /* Mark the skipped block as nothing (it's now part of the current chain) */
            nh->next_block = 0;
            nh->alloc_size = 0;

            /* Remove the coalesced block from the free list */
            for (uint32_t j = 0; j < mp->free_count; j++) {
                if (mp->free_list[j] == next_id) {
                    /* Shift remaining entries down */
                    for (uint32_t k = j; k < mp->free_count - 1; k++) {
                        mp->free_list[k] = mp->free_list[k + 1];
                    }
                    mp->free_count--;
                    coalesced++;
                    break;
                }
            }

            next_id = after_next;
        }
    }

    return coalesced;
}

/* ── IMPLEMENTED: mem_realloc ─────────────────────────────────────── */

void *mem_realloc(MemoryPool *mp, void *ptr, size_t new_size) {
    if (mp == NULL || ptr == NULL) return NULL;
    if (new_size == 0) {
        mem_free(mp, ptr);
        return NULL;
    }
    if (new_size > BUF_SIZE) return NULL;

    uint8_t *p = (uint8_t *)ptr;
    if (p < mp->pool || p >= mp->pool + POOL_SIZE) return NULL;

    uint32_t offset = (uint32_t)(p - mp->pool);
    uint32_t block_id = (offset / BLOCK_SIZE) + 1;

    if (block_id == 0 || block_id > MAX_BLOCKS) return NULL;
    if (!mp->headers[block_id - 1].in_use) return NULL;

    uint32_t blocks_needed = (uint32_t)((new_size + BLOCK_SIZE - 1) / BLOCK_SIZE);
    if (blocks_needed == 0) blocks_needed = 1;

    /* Count current chain length */
    uint32_t current_blocks = 0;
    uint32_t cur = block_id;
    while (cur != 0 && cur <= MAX_BLOCKS) {
        current_blocks++;
        cur = mp->headers[cur - 1].next_block;
    }

    uint32_t old_alloc_size = mp->headers[block_id - 1].alloc_size;

    if (blocks_needed <= current_blocks) {
        /* Shrink: free excess blocks */
        uint32_t ci = block_id;
        for (uint32_t b = 1; b < blocks_needed; b++) {
            ci = mp->headers[ci - 1].next_block;
        }
        uint32_t excess_start = mp->headers[ci - 1].next_block;
        mp->headers[ci - 1].next_block = 0;

        /* Free excess chain */
        uint32_t ec = excess_start;
        while (ec != 0) {
            uint32_t next = mp->headers[ec - 1].next_block;
            return_block(mp, ec);
            ec = next;
        }
        mp->headers[block_id - 1].alloc_size = (uint32_t)new_size;
        return ptr;  /* Same pointer */
    }

    /* Need more blocks: try to extend in place */
    if (mp->free_count >= (blocks_needed - current_blocks)) {
        /* Find the last block in chain */
        uint32_t last = block_id;
        while (mp->headers[last - 1].next_block != 0) {
            last = mp->headers[last - 1].next_block;
        }

        uint32_t extend_count = blocks_needed - current_blocks;
        uint32_t prev = last;
        for (uint32_t i = 0; i < extend_count; i++) {
            uint32_t next = find_free_block(mp);
            if (next == 0) break;
            mp->headers[prev - 1].next_block = next;
            prev = next;
        }
        mp->headers[prev - 1].next_block = 0;
        mp->headers[block_id - 1].alloc_size = (uint32_t)new_size;
        return ptr;
    }

    /* Allocate new, copy, free old */
    void *new_ptr = mem_alloc(mp, new_size);
    if (new_ptr == NULL) return NULL;

    uint32_t copy_size = old_alloc_size < new_size ? old_alloc_size : (uint32_t)new_size;
    memcpy(new_ptr, ptr, copy_size);
    mem_free(mp, ptr);

    return new_ptr;
}
