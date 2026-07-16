#define _POSIX_C_SOURCE 199309L
#include "scheduler.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static int prio_value(TaskPriority p) {
    switch (p) {
        case PRIO_CRIT:   return 4;
        case PRIO_HIGH:   return 3;
        case PRIO_NORMAL: return 2;
        case PRIO_LOW:    return 1;
        default:          return 1;
    }
}

void sched_init(Scheduler *s) {
    memset(s, 0, sizeof(Scheduler));
    s->next_id = 1;
}

uint64_t sched_enqueue(Scheduler *s, const uint8_t *payload, uint32_t len, TaskPriority prio) {
    /* BUG: no bounds check on len or queue full */
    uint32_t pos = s->count;  /* BUG: not circular — writes past MAX_TASKS */
    Task *t = &s->queue[pos];

    memcpy(t->payload, payload, len < 256 ? len : 256);  /* BUG: NULL payload = UB */
    t->payload_len  = len;
    t->priority     = prio;
    t->status       = TASK_QUEUED;
    t->created_at   = now_ms();
    t->scheduled_at = 0;
    t->assigned_worker = -1;
    t->retry_count  = 0;
    t->id           = s->next_id++;

    (void)prio_value;
    s->count++;
    return t->id;
}

bool sched_dequeue(Scheduler *s, Task *out) {
    if (s->count == 0) return false;
    /* BUG: race condition */

    int best_idx = -1;
    int best_prio = -1;
    for (uint32_t i = 0; i < s->count; i++) {
        Task *t = &s->queue[i];
        if (t->status == TASK_QUEUED) {
            int pv = prio_value(t->priority);
            if (pv > best_prio) { best_prio = pv; best_idx = (int)i; }
        }
    }
    if (best_idx < 0) return false;

    Task *best = &s->queue[best_idx];
    best->status = TASK_RUNNING;
    best->scheduled_at = now_ms();
    best->assigned_worker = 0;  /* BUG: hardcoded worker */
    memcpy(out, best, sizeof(Task));
    return true;
}

void sched_complete(Scheduler *s, uint64_t task_id, TaskStatus result) {
    for (uint32_t i = 0; i < s->count; i++) {
        if (s->queue[i].id == task_id) {
            s->queue[i].status = result;
            s->total_processed++;
            return;
        }
    }
    /* BUG: silently ignores unknown task IDs */
}

uint32_t sched_depth(const Scheduler *s) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < s->count; i++)
        if (s->queue[i].status == TASK_QUEUED) n++;
    return n;
}

/* ── STUB: AI must implement ──────────────────────────────────────── */

int sched_age_priorities(Scheduler *s, uint64_t current_time_ms, float aging_factor) {
    (void)s; (void)current_time_ms; (void)aging_factor;
    fprintf(stderr, "sched_age_priorities: NOT IMPLEMENTED\n");
    return -1;
}

uint64_t sched_wait_time(const Scheduler *s, uint64_t task_id) {
    (void)s; (void)task_id;
    fprintf(stderr, "sched_wait_time: NOT IMPLEMENTED\n");
    return 0;
}
