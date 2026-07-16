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
} Engine;

int      engine_init(Engine *e, const uint8_t *crypto_key, uint32_t key_len);
void     engine_tick(Engine *e);
int      engine_submit(Engine *e, uint32_t conn_id, const uint8_t *raw, uint32_t raw_len);
void     engine_shutdown(Engine *e);
int      engine_stats(const Engine *e, char *buf, uint32_t buf_len);

/* --- NEW: AI must implement --- */
uint32_t engine_process_batch(Engine *e, const uint8_t **raw_bufs, const uint32_t *raw_lens, uint32_t count);
uint32_t engine_defrag(Engine *e);

#endif /* ENGINE_H */
