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
    e->running  = true;
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
    uint64_t delta = now - last_tick;
    e->uptime_ms += delta;
    last_tick = now;

    /* Process one task from the scheduler */
    Task task;
    if (sched_dequeue(&e->sched, &task)) {
        sched_complete(&e->sched, task.id, TASK_DONE);
        e->ops_completed++;
    }
}

int engine_submit(Engine *e, uint32_t conn_id, const uint8_t *raw, uint32_t raw_len) {
    if (e == NULL || raw == NULL) return ERR_INVALID_TASK;

    /* Parse the message */
    ParsedMessage msg;
    int parse_rc = parse_message(raw, raw_len, &msg);
    if (parse_rc != ERR_NONE) return ERR_PARSE_FAILED;

    /* FIXED: validate the parsed message */
    if (!validate_message(&msg, NULL, 0)) return ERR_PARSE_FAILED;

    /* Encrypt the value for storage */
    uint8_t encrypted_val[512];
    uint32_t enc_len = crypto_encrypt(&e->crypto, msg.value, encrypted_val, msg.value_len);

    /* FIXED: use encrypted value as task payload */
    uint64_t task_id;
    if (enc_len > 0 && enc_len <= 256) {
        task_id = sched_enqueue(&e->sched, encrypted_val, enc_len, PRIO_NORMAL);
    } else {
        task_id = sched_enqueue(&e->sched, msg.key, msg.key_len, PRIO_NORMAL);
    }

    if (task_id == 0) return ERR_QUEUE_FULL;

    (void)conn_id;

    return ERR_NONE;
}

void engine_shutdown(Engine *e) {
    if (e == NULL) return;
    e->running = false;
    /* FIXED: close all connections */
    for (uint32_t i = 0; i < MAX_CONNECTIONS; i++) {
        if (e->connections.pool[i].in_use) {
            net_close(&e->connections, i);
        }
    }
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
        (unsigned long)e->uptime_ms,
        (unsigned long)e->ops_completed,
        (unsigned long)e->ops_failed,
        (unsigned int)sched_depth(&e->sched),
        (unsigned int)e->connections.count,
        (unsigned int)e->mem.total_allocated,
        (unsigned int)e->mem.total_freed);
}

/* ── IMPLEMENTED: engine_process_batch ────────────────────────────── */

uint32_t engine_process_batch(Engine *e, const uint8_t **raw_bufs,
                               const uint32_t *raw_lens, uint32_t count) {
    if (e == NULL || raw_bufs == NULL || raw_lens == NULL) return 0;

    uint32_t success = 0;

    for (uint32_t i = 0; i < count; i++) {
        if (raw_bufs[i] == NULL) continue;

        /* Step 1: Decrypt the raw buffer */
        uint32_t raw_len = raw_lens[i];
        uint8_t decrypted[BUF_SIZE];
        uint32_t dec_len = crypto_decrypt(&e->crypto, raw_bufs[i], decrypted, raw_len);

        if (dec_len == 0 && raw_len > 0) {
            e->ops_failed++;
            continue;
        }

        /* Use decrypted data (or original if decryption returned 0 for empty) */
        const uint8_t *data = (dec_len > 0) ? decrypted : raw_bufs[i];
        uint32_t data_len = (dec_len > 0) ? dec_len : raw_len;

        /* Step 2: Parse the message */
        ParsedMessage msg;
        int parse_rc = parse_message(data, data_len, &msg);
        if (parse_rc != ERR_NONE) {
            e->ops_failed++;
            continue;
        }

        /* Step 3: Validate the message */
        if (!validate_message(&msg, NULL, 0)) {
            e->ops_failed++;
            continue;
        }

        /* Step 4: Enqueue the task at appropriate priority */
        uint64_t task_id = sched_enqueue(&e->sched, msg.key, msg.key_len, PRIO_NORMAL);
        if (task_id == 0) {
            e->ops_failed++;
            continue;
        }

        success++;
    }

    return success;
}

/* ── IMPLEMENTED: engine_defrag ───────────────────────────────────── */

uint32_t engine_defrag(Engine *e) {
    if (e == NULL) return 0;

    /* Pause task processing */
    bool was_running = e->running;
    e->running = false;

    /* Trigger defragmentation */
    int coalesced = mem_defrag(&e->mem);

    /* Resume task processing */
    e->running = was_running;

    return (uint32_t)(coalesced >= 0 ? coalesced : 0);
}
