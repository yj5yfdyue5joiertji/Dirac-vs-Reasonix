#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "common.h"

typedef struct {
    Task      queue[MAX_TASKS];
    uint32_t  count;
    uint32_t  head;          /* next dequeue position */
    uint32_t  next_id;       /* auto-increment task ID */
    uint64_t  total_processed;
    /* BUG: no mutex — race condition on enqueue/dequeue */
} Scheduler;

/* Initialize the scheduler */
void sched_init(Scheduler *s);

/* Enqueue a task. Returns task ID, or 0 on failure. */
uint64_t sched_enqueue(Scheduler *s, const uint8_t *payload, uint32_t len, TaskPriority prio);

/* Dequeue the highest-priority task. Returns true if a task was dequeued. */
bool     sched_dequeue(Scheduler *s, Task *out);

/* Mark a task as done/failed */
void     sched_complete(Scheduler *s, uint64_t task_id, TaskStatus result);

/* Get queue depth */
uint32_t sched_depth(const Scheduler *s);

/* --- NEW: AI must implement --- */

/* Recalculate priorities with aging: tasks that have waited longer gain priority.
 * Every 'tick_ms' of waiting increases effective priority by 'aging_factor'/1000.
 * A LOW task waiting > 5000ms should become NORMAL; > 10000ms should become HIGH.
 * Returns number of tasks whose priority changed. */
int      sched_age_priorities(Scheduler *s, uint64_t current_time_ms, float aging_factor);

/* Get estimated wait time for a task (ms since enqueue) */
uint64_t sched_wait_time(const Scheduler *s, uint64_t task_id);

#endif /* SCHEDULER_H */
