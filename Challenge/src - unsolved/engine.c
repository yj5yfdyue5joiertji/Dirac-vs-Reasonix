#define _POSIX_C_SOURCE 199309L
#include "engine.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

int engine_init(Engine *e, const uint8_t *crypto_key, uint32_t key_len) {
    if (e == NULL) return -1;
    memset(e, 0, sizeof(Engine));
    mem_init(&e->mem);
    sched_init(&e->sched);
    net_init(&e->connections);
    crypto_init(&e->crypto, crypto_key, key_len);
    e->running = true;
    e->uptime_ms = 0;
    return 0;
}

void engine_tick(Engine *e) {
    if (!e->running) return;
    static uint64_t last_tick = 0;
    uint64_t now = 0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    now = (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
    if (last_tick == 0) last_tick = now;
    e->uptime_ms += now - last_tick;
    last_tick = now;

    Task task;
    if (sched_dequeue(&e->sched, &task)) {
        sched_complete(&e->sched, task.id, TASK_DONE);  /* BUG: no real processing */
        e->ops_completed++;
    }
}

int engine_submit(Engine *e, uint32_t conn_id, const uint8_t *raw, uint32_t raw_len) {
    if (e == NULL || raw == NULL) return ERR_INVALID_TASK;
    ParsedMessage msg;
    if (parse_message(raw, raw_len, &msg) != ERR_NONE) return ERR_PARSE_FAILED;
    /* BUG: no validation */

    uint8_t encrypted_val[512];
    uint32_t enc_len = crypto_encrypt(&e->crypto, msg.value, encrypted_val, msg.value_len);
    (void)enc_len;  /* BUG: encrypted value never stored or used */

    uint64_t task_id = sched_enqueue(&e->sched, msg.key, msg.key_len, PRIO_NORMAL);
    if (task_id == 0) return ERR_QUEUE_FULL;
    (void)conn_id;  /* BUG: conn_id ignored */
    return ERR_NONE;
}

void engine_shutdown(Engine *e) {
    if (e == NULL) return;
    e->running = false;
    /* BUG: no cleanup */
}

int engine_stats(const Engine *e, char *buf, uint32_t buf_len) {
    if (e == NULL || buf == NULL) return -1;
    return snprintf(buf, buf_len,
        "Engine stats:\n"
        "  Uptime:       %lu ms\n"
        "  Ops completed: %lu\n"
        "  Ops failed:    %lu\n"
        "  Queue depth:   %u\n"
        "  Connections:   %u\n"
        "  Memory allocs: %u\n"
        "  Memory frees:  %u\n",
        (unsigned long)e->uptime_ms, (unsigned long)e->ops_completed,
        (unsigned long)e->ops_failed, (unsigned int)sched_depth(&e->sched),
        (unsigned int)e->connections.count, (unsigned int)e->mem.total_allocated,
        (unsigned int)e->mem.total_freed);
}

/* ── STUB: AI must implement ──────────────────────────────────────── */

uint32_t engine_process_batch(Engine *e, const uint8_t **raw_bufs,
                               const uint32_t *raw_lens, uint32_t count) {
    (void)e; (void)raw_bufs; (void)raw_lens; (void)count;
    fprintf(stderr, "engine_process_batch: NOT IMPLEMENTED\n");
    return 0;
}

uint32_t engine_defrag(Engine *e) {
    (void)e;
    fprintf(stderr, "engine_defrag: NOT IMPLEMENTED\n");
    return 0;
}
