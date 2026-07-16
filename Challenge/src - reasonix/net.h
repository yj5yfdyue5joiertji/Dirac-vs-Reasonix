#ifndef NET_H
#define NET_H

#include "common.h"

/* Connection pool */
typedef struct {
    Connection  pool[MAX_CONNECTIONS];
    uint32_t    count;
    uint64_t    total_connections;
    uint64_t    total_rejected;
    /* BUG: no mutex */
} ConnectionPool;

/* Initialize the network subsystem */
void net_init(ConnectionPool *cp);

/* Accept a new connection. Returns connection ID or 0 on failure. */
uint32_t net_accept(ConnectionPool *cp, int socket_fd);

/* Close a connection and free its resources */
void     net_close(ConnectionPool *cp, uint32_t conn_id);

/* Receive data on a connection. Returns bytes received, 0 on error. */
uint32_t net_recv(ConnectionPool *cp, uint32_t conn_id, uint8_t *buf, uint32_t buf_len);

/* Send data on a connection. Returns bytes sent, 0 on error. */
uint32_t net_send(ConnectionPool *cp, uint32_t conn_id, const uint8_t *data, uint32_t len);

/* --- NEW: AI must implement --- */

/* Create a new connection pool with pre-warmed connections.
 * 'pool_size' sockets are created and kept ready.
 * Returns number of pre-warmed connections. */
uint32_t net_pool_warmup(ConnectionPool *cp, uint32_t pool_size);

/* Close connections that have been idle for more than 'timeout_ms'.
 * Returns number of connections closed. */
uint32_t net_reap_idle(ConnectionPool *cp, uint64_t current_time_ms, uint64_t timeout_ms);

#endif /* NET_H */
