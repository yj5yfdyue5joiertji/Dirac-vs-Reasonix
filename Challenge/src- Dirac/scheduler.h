#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "common.h"

typedef struct {
    Task      queue[MAX_TASKS];
    uint32_t  count;
    uint32_t  head;
    uint32_t  next_id;
    uint64_t  total_processed;
    /* BUG: no mutex */
} Scheduler;

void     sched_init(Scheduler *s);
uint64_t sched_enqueue(Scheduler *s, const uint8_t *payload, uint32_t len, TaskPriority prio);
bool     sched_dequeue(Scheduler *s, Task *out);
void     sched_complete(Scheduler *s, uint64_t task_id, TaskStatus result);
uint32_t sched_depth(const Scheduler *s);

/* --- NEW: AI must implement --- */
int      sched_age_priorities(Scheduler *s, uint64_t current_time_ms, float aging_factor);
uint64_t sched_wait_time(const Scheduler *s, uint64_t task_id);

#endif /* SCHEDULER_H */
