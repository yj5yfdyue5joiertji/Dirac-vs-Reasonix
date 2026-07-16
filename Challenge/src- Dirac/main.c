#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "engine.h"

typedef struct {
    Engine *engine;
    int     worker_id;
    bool    running;
} WorkerCtx;

static void *worker_thread(void *arg) {
    WorkerCtx *ctx = (WorkerCtx *)arg;
    printf("[Worker %d] Started\n", ctx->worker_id);
    while (ctx->running) {
        engine_tick(ctx->engine);
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
        nanosleep(&ts, NULL);
    }
    printf("[Worker %d] Stopped\n", ctx->worker_id);
    return NULL;
}

/* ── Test helpers ─────────────────────────────────────────────────── */

static void test_basic_submit(Engine *e) {
    printf("\n── Test: Basic Submit ──\n");
    const char *key = "hello";
    const char *val = "world";
    uint32_t key_len = (uint32_t)strlen(key);
    uint32_t val_len = (uint32_t)strlen(val);
uint32_t total = 1 + 4 + 2 + 4 + key_len + 4 + val_len;

    uint8_t raw[256];
    raw[0] = MSG_PUT;
    *(uint32_t *)(raw + 1) = htonl(total);
    *(uint16_t *)(raw + 5) = 0;  /* placeholder */
    *(uint32_t *)(raw + 7) = htonl(key_len);
    memcpy(raw + 11, key, key_len);
    *(uint32_t *)(raw + 11 + key_len) = htonl(val_len);
    memcpy(raw + 15 + key_len, val, val_len);

    /* Compute CRC16 over correct range (same as serialize_message): bytes 0-4 and 7..total-1 */
    uint16_t crc = 0xFFFF;
    /* byte 0 */
    crc ^= (uint16_t)raw[0] << 8;
    for (int _j = 0; _j < 8; _j++) {
        if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
        else crc = (uint16_t)(crc << 1);
    }
    /* bytes 1-4 */
    for (uint32_t _i = 1; _i <= 4; _i++) {
        crc ^= (uint16_t)raw[_i] << 8;
        for (int _j = 0; _j < 8; _j++) {
            if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
            else crc = (uint16_t)(crc << 1);
        }
    }
    /* bytes 7..total-1 */
    for (uint32_t _i = 7; _i < total; _i++) {
        crc ^= (uint16_t)raw[_i] << 8;
        for (int _j = 0; _j < 8; _j++) {
            if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
            else crc = (uint16_t)(crc << 1);
        }
    }
    *(uint16_t *)(raw + 5) = htons(crc);
    int rc = engine_submit(e, 0, raw, total);
    printf("  Submit result: %d (expected: 0 = ERR_NONE)\n", rc);
    for (int i = 0; i < 5; i++) engine_tick(e);
    char stats[512];
    engine_stats(e, stats, sizeof(stats));
    printf("%s", stats);
}

static void test_memory_stress(Engine *e) {
    printf("\n── Test: Memory Stress ──\n");
    void *ptrs[100];
    int allocs = 0;
    for (int i = 0; i < 100; i++) {
        size_t sz = (size_t)((i % 10) + 1) * 32;
        ptrs[i] = mem_alloc(&e->mem, sz);
        if (ptrs[i]) allocs++;
        else { printf("  Allocation %d (size %zu) failed\n", i, sz); break; }
    }
    printf("  Allocated: %d blocks\n", allocs);
    for (int i = 0; i < allocs; i++)
        if (i % 2 == 1) mem_free(&e->mem, ptrs[i]);
    uint32_t a, f, fb;
    mem_stats(&e->mem, &a, &f, &fb);
    printf("  After free: allocated=%u freed=%u free_blocks=%u\n", a, f, fb);
}

static void test_crypto_roundtrip(Engine *e) {
    printf("\n── Test: Crypto Roundtrip ──\n");
    const char *plain = "Secret message for encryption test!";
    uint32_t len = (uint32_t)strlen(plain);
    uint8_t cipher[128], decrypted[128];
    uint32_t enc_len = crypto_encrypt(&e->crypto, (const uint8_t *)plain, cipher, len);
    printf("  Encrypted %u bytes\n", enc_len);
    uint32_t dec_len = crypto_decrypt(&e->crypto, cipher, decrypted, enc_len);
    printf("  Decrypted %u bytes\n", dec_len);
    decrypted[dec_len] = '\0';
    printf("  Original:  '%s'\n  Decrypted: '%s'\n", plain, decrypted);
    printf("  Match: %s\n", strcmp(plain, (char *)decrypted) == 0 ? "YES" : "NO");
}

static void test_scheduler_overflow(Engine *e) {
    printf("\n── Test: Scheduler Overflow ──\n");
    uint8_t payload[32];
    memset(payload, 0xAA, sizeof(payload));
    for (int i = 0; i < MAX_TASKS + 20; i++) {
        uint64_t id = sched_enqueue(&e->sched, payload, sizeof(payload), PRIO_NORMAL);
        if (id == 0 && i < MAX_TASKS)
            printf("  Enqueue %d failed unexpectedly (id=0)\n", i);
    }
    printf("  Enqueued %d tasks, queue count=%u\n", MAX_TASKS + 20, e->sched.count);
    Task t;
    int dequeued = 0;
    while (sched_dequeue(&e->sched, &t)) {
        dequeued++;
        if (dequeued >= MAX_TASKS + 30) break;
    }
    printf("  Dequeued %d tasks\n", dequeued);
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    setbuf(stdout, NULL);
    printf("=== NexusDB — Distributed Task Engine ===\n");
    printf("Build: %s %s\n", __DATE__, __TIME__);
    printf("Arch: %zu-bit, MAX_TASKS=%d, POOL_SIZE=%d\n",
           sizeof(void*) * 8, MAX_TASKS, POOL_SIZE);

    Engine engine;
    uint8_t key[17] = "nexusdb-key-128!";
    if (engine_init(&engine, key, 16) != 0) {
        fprintf(stderr, "FATAL: engine_init failed\n");
        return 1;
    }

    test_basic_submit(&engine);
    test_memory_stress(&engine);
    test_crypto_roundtrip(&engine);
    test_scheduler_overflow(&engine);

    WorkerCtx wctx = { .engine = &engine, .worker_id = 1, .running = true };
    pthread_t worker;
    pthread_create(&worker, NULL, worker_thread, &wctx);
    struct timespec ts_sleep = { .tv_sec = 0, .tv_nsec = 50000000 };
    nanosleep(&ts_sleep, NULL);
    wctx.running = false;
    pthread_join(worker, NULL);
    engine_shutdown(&engine);

    printf("\n=== NexusDB Shutdown Complete ===\n");
    printf("Ops: completed=%lu failed=%lu\n",
           (unsigned long)engine.ops_completed, (unsigned long)engine.ops_failed);
    return 0;
}
