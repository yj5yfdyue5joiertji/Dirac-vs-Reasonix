#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_TASKS       256
#define MAX_WORKERS     16
#define MAX_CONNECTIONS 64
#define BUF_SIZE        4096
#define POOL_SIZE       (1024 * 1024)  /* 1 MB */
#define BLOCK_SIZE      64
#define MAX_BLOCKS      (POOL_SIZE / BLOCK_SIZE)

typedef enum {
    TASK_IDLE = 0,
    TASK_QUEUED,
    TASK_RUNNING,
    TASK_DONE,
    TASK_FAILED,
    TASK_CANCELLED = 99
} TaskStatus;

typedef enum {
    PRIO_LOW    = 0,
    PRIO_NORMAL = 1,
    PRIO_HIGH   = 2,
    PRIO_CRIT   = 3
} TaskPriority;

typedef struct {
    uint64_t id;
    uint8_t  payload[256];
    uint32_t payload_len;
    TaskPriority priority;
    TaskStatus  status;
    uint64_t    created_at;   /* timestamp */
    uint64_t    scheduled_at;
    int32_t     assigned_worker;
    uint32_t    retry_count;
} Task;

typedef struct {
    uint8_t data[BUF_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    /* WARNING: no mutex here — AI must add synchronization */
} CircularBuffer;

typedef struct {
    uint32_t id;
    int      socket_fd;
    uint64_t last_active;
    uint8_t  recv_buf[BUF_SIZE];
    uint32_t recv_len;
    bool     in_use;
} Connection;

/* Global error codes */
#define ERR_NONE            0
#define ERR_MEMORY          1
#define ERR_QUEUE_FULL      2
#define ERR_INVALID_TASK    3
#define ERR_WORKER_BUSY     4
#define ERR_PARSE_FAILED    5
#define ERR_CONN_REFUSED    6
#define ERR_TIMEOUT         7
#define ERR_INTERNAL        99

#endif /* COMMON_H */
