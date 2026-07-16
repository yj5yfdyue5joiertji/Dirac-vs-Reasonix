#include "memory.h"
#include <string.h>
#include <stdio.h>

static uint32_t find_free_block(MemoryPool *mp) {
    if (mp->free_count == 0) return 0;
    uint32_t idx = mp->free_count - 1;   /* FIXED: off-by-one */
    mp->free_count--;
    return mp->free_list[idx];
}

static void return_block(MemoryPool *mp, uint32_t block_id) {
    if (block_id == 0 || block_id > MAX_BLOCKS) return;
    /* Prevent double-free: check if already in free list by scanning */
    for (uint32_t i = 0; i < mp->free_count; i++) {
        if (mp->free_list[i] == block_id) return; /* already free */
    }
    mp->headers[block_id - 1].in_use = false;
    mp->headers[block_id - 1].next_block = 0;
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

    /* Save state for rollback */
    uint32_t saved_free_count = mp->free_count;
    uint32_t saved_list[MAX_BLOCKS];
    memcpy(saved_list, mp->free_list, sizeof(saved_list));

    uint32_t first = find_free_block(mp);
    if (first == 0) return NULL;

    uint32_t prev = first;
    for (uint32_t i = 1; i < blocks_needed; i++) {
        uint32_t next = find_free_block(mp);
        if (next == 0) {
            /* Rollback partial allocation */
            mp->free_count = saved_free_count;
            memcpy(mp->free_list, saved_list, sizeof(saved_list));
            return NULL;
        }
        mp->headers[prev - 1].next_block = next;
        mp->headers[next - 1].in_use = true;
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

    /* Double-free detection */
    if (!mp->headers[block_id - 1].in_use) return;

    uint32_t current = block_id;
    while (current != 0) {
        uint32_t next = mp->headers[current - 1].next_block;
        /* Check in_use on each block in chain to prevent double-free */
        if (!mp->headers[current - 1].in_use) break;
        return_block(mp, current);
        current = next;
    }
}

void mem_stats(const MemoryPool *mp, uint32_t *alloc, uint32_t *freed, uint32_t *free_blocks) {
    *alloc = mp->total_allocated;
    *freed = mp->total_freed;
    *free_blocks = mp->free_count;
}

/* ── mem_defrag ──────────────────────────────────────────────────── */

int mem_defrag(MemoryPool *mp) {
    if (mp == NULL) return 0;

    /* Collect all free block IDs and sort them */
    uint32_t free_ids[MAX_BLOCKS];
    uint32_t nfree = mp->free_count;
    memcpy(free_ids, mp->free_list, nfree * sizeof(uint32_t));

    /* Simple insertion sort by block_id */
    for (uint32_t i = 1; i < nfree; i++) {
        uint32_t key = free_ids[i];
        int32_t j = (int32_t)i - 1;
        while (j >= 0 && free_ids[j] > key) {
            free_ids[j + 1] = free_ids[j];
            j--;
        }
        free_ids[j + 1] = key;
    }

    int coalesced = 0;
    uint32_t new_free_list[MAX_BLOCKS];
    uint32_t new_count = 0;

    uint32_t run_start = 0;
    for (uint32_t i = 0; i < nfree; i++) {
        if (i == 0 || free_ids[i] != free_ids[i - 1] + 1) {
            /* Start a new run */
            if (run_start < i && i > 0) {
                /* Close previous run: chain blocks together via next_block */
                uint32_t run_len = i - run_start;
                for (uint32_t j = run_start; j < i - 1; j++) {
                    mp->headers[free_ids[j] - 1].next_block = free_ids[j + 1];
                }
                mp->headers[free_ids[i - 1] - 1].next_block = 0;
                coalesced += (int)(run_len - 1);
            }
            run_start = i;
        }
    }
    /* Handle last run */
    if (run_start < nfree) {
        uint32_t run_len = nfree - run_start;
        for (uint32_t j = run_start; j < nfree - 1; j++) {
            mp->headers[free_ids[j] - 1].next_block = free_ids[j + 1];
        }
        mp->headers[free_ids[nfree - 1] - 1].next_block = 0;
        coalesced += (int)(run_len - 1);
    }

    /* Rebuild free_list: keep only first block of each run */
    for (uint32_t i = 0; i < nfree; i++) {
        if (i == 0 || free_ids[i] != free_ids[i - 1] + 1) {
            new_free_list[new_count++] = free_ids[i];
        }
    }

    mp->free_count = new_count;
    memcpy(mp->free_list, new_free_list, new_count * sizeof(uint32_t));

    return coalesced;
}

/* ── mem_realloc ─────────────────────────────────────────────────── */

void *mem_realloc(MemoryPool *mp, void *ptr, size_t new_size) {
    if (mp == NULL) return NULL;
    if (ptr == NULL) return mem_alloc(mp, new_size);
    if (new_size == 0) {
        mem_free(mp, ptr);
        return NULL;
    }

    uint8_t *p = (uint8_t *)ptr;
    if (p < mp->pool || p >= mp->pool + POOL_SIZE) return NULL;

    uint32_t offset = (uint32_t)(p - mp->pool);
    uint32_t block_id = (offset / BLOCK_SIZE) + 1;
    if (block_id == 0 || block_id > MAX_BLOCKS) return NULL;
    if (!mp->headers[block_id - 1].in_use) return NULL;

    uint32_t new_blocks = (uint32_t)((new_size + BLOCK_SIZE - 1) / BLOCK_SIZE);
    if (new_blocks == 0) new_blocks = 1;

    /* Count current blocks in chain */
    uint32_t cur_blocks = 0;
    uint32_t cur = block_id;
    while (cur != 0) {
        cur_blocks++;
        cur = mp->headers[cur - 1].next_block;
    }

    if (new_blocks == cur_blocks) {
        /* Same size: just update alloc_size */
        mp->headers[block_id - 1].alloc_size = (uint32_t)new_size;
        return ptr;
    }

    if (new_blocks < cur_blocks) {
        /* Shrink: free extra blocks at end of chain */
        uint32_t cur2 = block_id;
        for (uint32_t i = 1; i < new_blocks; i++) {
            cur2 = mp->headers[cur2 - 1].next_block;
        }
        uint32_t extra_start = mp->headers[cur2 - 1].next_block;
        mp->headers[cur2 - 1].next_block = 0;
        /* Free the extra chain */
        cur = extra_start;
        while (cur != 0) {
            uint32_t next = mp->headers[cur - 1].next_block;
            return_block(mp, cur);
            cur = next;
        }
        mp->headers[block_id - 1].alloc_size = (uint32_t)new_size;
        return ptr;
    }

    /* Grow: try to extend from the last block */
    uint32_t last = block_id;
    while (mp->headers[last - 1].next_block != 0) {
        last = mp->headers[last - 1].next_block;
    }

    uint32_t extra_needed = new_blocks - cur_blocks;
    /* Check if the next consecutive blocks are free */
    uint32_t next_block_id = last + 1;
    uint32_t extend_count = 0;

    /* See how many consecutive free blocks follow 'last' */
    {
        uint32_t probe = next_block_id;
        while (probe <= MAX_BLOCKS && extend_count < extra_needed) {
            /* Check if probe is in free_list */
            bool is_free = false;
            for (uint32_t i = 0; i < mp->free_count; i++) {
                if (mp->free_list[i] == probe) {
                    is_free = true;
                    break;
                }
            }
            if (!is_free) break;
            extend_count++;
            probe++;
        }
    }

    if (extend_count >= extra_needed) {
        /* Remove the needed blocks from free_list */
        for (uint32_t j = 0; j < extra_needed; j++) {
            uint32_t target = next_block_id + j;
            bool found = false;
            for (uint32_t i = 0; i < mp->free_count; i++) {
                if (mp->free_list[i] == target) {
                    /* Remove by swapping with last */
                    mp->free_list[i] = mp->free_list[mp->free_count - 1];
                    mp->free_count--;
                    found = true;
                    break;
                }
            }
            if (!found) {
                /* Should not happen; rollback anything? */
                break;
            }
        }
        /* Chain the new blocks */
        uint32_t prev = last;
        for (uint32_t j = 0; j < extra_needed; j++) {
            uint32_t blk = next_block_id + j;
            mp->headers[blk - 1].in_use = true;
            mp->headers[prev - 1].next_block = blk;
            prev = blk;
        }
        mp->headers[prev - 1].next_block = 0;
        mp->headers[block_id - 1].alloc_size = (uint32_t)new_size;
        return ptr;
    }

    /* Fallback: allocate new, copy old, free old */
    void *new_ptr = mem_alloc(mp, new_size);
    if (new_ptr == NULL) return NULL;

    uint32_t old_size = mp->headers[block_id - 1].alloc_size;
    uint32_t copy_size = old_size < (uint32_t)new_size ? old_size : (uint32_t)new_size;
    memcpy(new_ptr, ptr, copy_size);
    mem_free(mp, ptr);
    return new_ptr;
}
