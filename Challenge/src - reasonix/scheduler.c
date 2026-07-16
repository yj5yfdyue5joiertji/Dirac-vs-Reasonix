#define _POSIX_C_SOURCE 199309L
#include "scheduler.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

/* ── Helpers ──────────────────────────────────────────────────────── */

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

/* ── Public API ───────────────────────────────────────────────────── */

void sched_init(Scheduler *s) {
    memset(s, 0, sizeof(Scheduler));
    s->next_id = 1;
}

uint64_t sched_enqueue(Scheduler *s, const uint8_t *payload, uint32_t len,
                       TaskPriority prio) {
    /* FIXED: check queue depth (only QUEUED tasks count against capacity) */
    if (sched_depth(s) >= MAX_TASKS) return 0;

    /* FIXED: validate payload */
    if (payload == NULL && len > 0) return 0;

    /* Clamp len to payload size */
    uint32_t copy_len = (len < 256) ? len : 255;
    if (copy_len > 255) copy_len = 255;

    /* Find a free slot (non-QUEUED, or use a slot with DONE/FAILED task) */
    int32_t pos = -1;
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (s->queue[i].status != TASK_QUEUED) {
            pos = (int32_t)i;
            break;
        }
    }
    /* If all slots are QUEUED (shouldn't happen due to depth check), use count */
    if (pos < 0) {
        pos = (int32_t)((s->head + s->count) % MAX_TASKS);
    }

    Task *t = &s->queue[pos];

    if (payload && copy_len > 0) {
        memcpy(t->payload, payload, copy_len);
    }
    t->payload_len  = copy_len;
    t->priority     = prio;
    t->status       = TASK_QUEUED;
    t->created_at   = now_ms();
    t->scheduled_at = 0;
    t->assigned_worker = -1;
    t->retry_count  = 0;
    t->id           = s->next_id++;

    s->count++;
    return t->id;
}

bool sched_dequeue(Scheduler *s, Task *out) {
    if (s->count == 0 || out == NULL) return false;

    /* Find highest priority QUEUED task (linear scan) */
    int best_idx = -1;
    int best_prio = -1;
    uint64_t oldest_ts = UINT64_MAX;

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        Task *t = &s->queue[i];
        if (t->status == TASK_QUEUED) {
            int pv = prio_value(t->priority);
            /* Prefer higher priority; break ties with older timestamp */
            if (pv > best_prio || (pv == best_prio && t->created_at < oldest_ts)) {
                best_prio = pv;
                best_idx = (int)i;
                oldest_ts = t->created_at;
            }
        }
    }

    if (best_idx < 0) return false;

    Task *best = &s->queue[best_idx];
    best->status = TASK_RUNNING;
    best->scheduled_at = now_ms();
    best->assigned_worker = 0;

    memcpy(out, best, sizeof(Task));
    return true;
}

void sched_complete(Scheduler *s, uint64_t task_id, TaskStatus result) {
    /* FIXED: scan bounded by MAX_TASKS, not count */
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (s->queue[i].id == task_id) {
            s->queue[i].status = result;
            s->total_processed++;
            return;
        }
    }
}

uint32_t sched_depth(const Scheduler *s) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (s->queue[i].status == TASK_QUEUED) n++;
    }
    return n;
}

/* ── IMPLEMENTED: sched_age_priorities ────────────────────────────── */

int sched_age_priorities(Scheduler *s, uint64_t current_time_ms, float aging_factor) {
    if (s == NULL) return -1;

    int changed = 0;

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        Task *t = &s->queue[i];
        if (t->status != TASK_QUEUED) continue;

        /* Calculate wait time */
        uint64_t wait_ms = current_time_ms - t->created_at;
        if ((int64_t)wait_ms < 0) continue;

        /* Aging: effective priority boost = wait_ms * aging_factor / 1000 */
        float boost = (float)wait_ms * aging_factor / 1000.0f;

        TaskPriority old_prio = t->priority;

        if (boost >= 1.0f && t->priority == PRIO_LOW) {
            t->priority = PRIO_NORMAL;
        }
        if (boost >= 2.0f && t->priority <= PRIO_NORMAL) {
            t->priority = PRIO_HIGH;
        }
        if (boost >= 3.0f && t->priority <= PRIO_HIGH) {
            t->priority = PRIO_CRIT;
        }

        if (t->priority != old_prio) changed++;
    }

    return changed;
}

/* ── IMPLEMENTED: sched_wait_time ─────────────────────────────────── */

uint64_t sched_wait_time(const Scheduler *s, uint64_t task_id) {
    if (s == NULL) return 0;

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        if (s->queue[i].id == task_id) {
            uint64_t current = now_ms();
            uint64_t wait = current - s->queue[i].created_at;
            return wait;
        }
    }
    return 0;
}
