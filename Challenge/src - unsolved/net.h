#ifndef NET_H
#define NET_H

#include "common.h"

typedef struct {
    Connection  pool[MAX_CONNECTIONS];
    uint32_t    count;
    uint64_t    total_connections;
    uint64_t    total_rejected;
    /* BUG: no mutex */
} ConnectionPool;

void     net_init(ConnectionPool *cp);
uint32_t net_accept(ConnectionPool *cp, int socket_fd);
void     net_close(ConnectionPool *cp, uint32_t conn_id);
uint32_t net_recv(ConnectionPool *cp, uint32_t conn_id, uint8_t *buf, uint32_t buf_len);
uint32_t net_send(ConnectionPool *cp, uint32_t conn_id, const uint8_t *data, uint32_t len);

/* --- NEW: AI must implement --- */
uint32_t net_pool_warmup(ConnectionPool *cp, uint32_t pool_size);
uint32_t net_reap_idle(ConnectionPool *cp, uint64_t current_time_ms, uint64_t timeout_ms);

#endif /* NET_H */
