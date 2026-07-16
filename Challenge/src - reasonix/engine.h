#ifndef ENGINE_H
#define ENGINE_H

#include "common.h"
#include "memory.h"
#include "scheduler.h"
#include "parser.h"
#include "crypto.h"
#include "net.h"

typedef struct {
    MemoryPool      mem;
    Scheduler       sched;
    ConnectionPool  connections;
    CryptoContext   crypto;
    uint64_t        uptime_ms;
    uint64_t        ops_completed;
    uint64_t        ops_failed;
    bool            running;
    /* BUG: no shutdown synchronization */
} Engine;

/* Initialize the engine with a crypto key */
int  engine_init(Engine *e, const uint8_t *crypto_key, uint32_t key_len);

/* Main processing loop — call repeatedly or in a thread */
void engine_tick(Engine *e);

/* Submit work from raw network data */
int  engine_submit(Engine *e, uint32_t conn_id, const uint8_t *raw, uint32_t raw_len);

/* Graceful shutdown */
void engine_shutdown(Engine *e);

/* Get engine statistics as a formatted string */
int  engine_stats(const Engine *e, char *buf, uint32_t buf_len);

/* --- NEW: AI must implement --- */

/* Process a batch of encrypted tasks:
 *   1. Decrypt each raw buffer using the engine's crypto context
 *   2. Parse the decrypted message
 *   3. Enqueue the resulting task
 *   4. If any step fails, record the error and continue with next
 * Returns number of successfully enqueued tasks. */
uint32_t engine_process_batch(Engine *e, const uint8_t **raw_bufs,
                              const uint32_t *raw_lens, uint32_t count);

/* Run defragmentation on the memory pool and return freed blocks count */
uint32_t engine_defrag(Engine *e);

#endif /* ENGINE_H */
