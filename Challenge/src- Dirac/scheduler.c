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
    if (payload == NULL) return 0;

    /* Find a slot: prefer appending, but reuse completed/failed/idle slots if full */
    uint32_t pos;
    bool reused = false;

    if (s->count >= MAX_TASKS) {
        /* Queue is full — scan for a non-active slot to reuse */
        bool found = false;
        for (uint32_t i = 0; i < MAX_TASKS; i++) {
            if (s->queue[i].status != TASK_QUEUED && s->queue[i].status != TASK_RUNNING) {
                pos = i;
                found = true;
                break;
            }
        }
        if (!found) return 0;
        reused = true;
    } else {
        pos = (s->head + s->count) % MAX_TASKS;
    }

    Task *t = &s->queue[pos];

    uint32_t copy_len = len < 256 ? len : 256;
    memcpy(t->payload, payload, copy_len);
    t->payload_len  = copy_len;
    t->priority     = prio;
    t->status       = TASK_QUEUED;
    t->created_at   = now_ms();
    t->scheduled_at = 0;
    t->assigned_worker = -1;
    t->retry_count  = 0;
    t->id           = s->next_id++;

    if (!reused) {
        s->count++;
    }
    return t->id;
}

bool sched_dequeue(Scheduler *s, Task *out) {
    if (s->count == 0) return false;
    if (out == NULL) return false;

    /* Find highest priority queued task */
    int best_idx = -1;
    int best_prio = -1;
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        uint32_t idx = (s->head + i) % MAX_TASKS;
        Task *t = &s->queue[idx];
        if (t->status == TASK_QUEUED) {
            int pv = prio_value(t->priority);
            if (pv > best_prio) { best_prio = pv; best_idx = (int)idx; }
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
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        uint32_t idx = (s->head + i) % MAX_TASKS;
        if (s->queue[idx].id == task_id) {
            s->queue[idx].status = result;
            s->total_processed++;
            return;
        }
    }
}

uint32_t sched_depth(const Scheduler *s) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        uint32_t idx = (s->head + i) % MAX_TASKS;
        if (s->queue[idx].status == TASK_QUEUED) n++;
    }
    return n;
}

/* ── sched_age_priorities ────────────────────────────────────────── */

int sched_age_priorities(Scheduler *s, uint64_t current_time_ms, float aging_factor) {
    if (s == NULL) return -1;
    if (aging_factor < 0.0f) return -1;

    int changed = 0;
    float factor_per_ms = aging_factor / 1000.0f;

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        uint32_t idx = (s->head + i) % MAX_TASKS;
        Task *t = &s->queue[idx];
        if (t->status != TASK_QUEUED) continue;

        uint64_t waited = current_time_ms - t->created_at;
        float boost = (float)waited * factor_per_ms;

        TaskPriority old_prio = t->priority;

        /* Apply aging: LOW -> NORMAL after 5000ms, NORMAL -> HIGH after 10000ms */
        if (t->priority == PRIO_LOW && waited >= 5000) {
            t->priority = PRIO_NORMAL;
            if (old_prio != t->priority) changed++;
        } else if (t->priority == PRIO_NORMAL && waited >= 10000) {
            t->priority = PRIO_HIGH;
            if (old_prio != t->priority) changed++;
        } else if (boost >= 1.0f) {
            /* Boost by at least 1 priority level based on aging_factor */
            int boost_levels = (int)boost;
            int current_val = prio_value(t->priority);
            int new_val = current_val + boost_levels;
            if (new_val > 4) new_val = 4; /* cap at CRIT */
            if (new_val != current_val) {
                switch (new_val) {
                    case 4: t->priority = PRIO_CRIT; break;
                    case 3: t->priority = PRIO_HIGH; break;
                    case 2: t->priority = PRIO_NORMAL; break;
                    default: t->priority = PRIO_LOW; break;
                }
                changed++;
            }
        }
    }
    return changed;
}

/* ── sched_wait_time ─────────────────────────────────────────────── */

uint64_t sched_wait_time(const Scheduler *s, uint64_t task_id) {
    if (s == NULL) return 0;

    uint64_t current = now_ms();

    for (uint32_t i = 0; i < MAX_TASKS; i++) {
        uint32_t idx = (s->head + i) % MAX_TASKS;
        if (s->queue[idx].id == task_id) {
            if (s->queue[idx].created_at > 0) {
                if (current >= s->queue[idx].created_at)
                    return current - s->queue[idx].created_at;
                return 0;
            }
            return 0;
        }
    }
    return 0;
}
