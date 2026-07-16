#define _POSIX_C_SOURCE 199309L
#include "net.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

static uint64_t now_ms_net(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

void net_init(ConnectionPool *cp) { memset(cp, 0, sizeof(ConnectionPool)); }

uint32_t net_accept(ConnectionPool *cp, int socket_fd) {
    /* BUG: no MAX_CONNECTIONS check */
    uint32_t id = cp->count;
    Connection *c = &cp->pool[id];  /* BUG: out of bounds if id >= MAX_CONNECTIONS */
    c->id          = id;
    c->socket_fd   = socket_fd;
    c->last_active = now_ms_net();
    c->recv_len    = 0;
    c->in_use      = true;
    /* BUG: recv_buf not zeroed */
    cp->count++;
    cp->total_connections++;
    return id;
}

void net_close(ConnectionPool *cp, uint32_t conn_id) {
    /* BUG: no bounds check */
    Connection *c = &cp->pool[conn_id];
    if (c->in_use) {
        c->in_use = false;
        /* BUG: doesn't close socket or decrement count */
    }
}

uint32_t net_recv(ConnectionPool *cp, uint32_t conn_id, uint8_t *buf, uint32_t buf_len) {
    /* BUG: no bounds check */
    Connection *c = &cp->pool[conn_id];
    if (!c->in_use) return 0;
    uint32_t copy_len = buf_len < BUF_SIZE ? buf_len : BUF_SIZE;
    memcpy(buf, c->recv_buf, copy_len);
    c->last_active = now_ms_net();
    return copy_len;
}

uint32_t net_send(ConnectionPool *cp, uint32_t conn_id, const uint8_t *data, uint32_t len) {
    Connection *c = &cp->pool[conn_id];  /* BUG: no bounds check */
    if (!c->in_use) return 0;
    c->last_active = now_ms_net();
    if (len <= BUF_SIZE) {
        memcpy(c->recv_buf, data, len);  /* BUG: writes to recv_buf, doesn't send */
        c->recv_len = len;
    }
    return len;
}

/* ── STUB: AI must implement ──────────────────────────────────────── */

uint32_t net_pool_warmup(ConnectionPool *cp, uint32_t pool_size) {
    (void)cp; (void)pool_size;
    fprintf(stderr, "net_pool_warmup: NOT IMPLEMENTED\n");
    return 0;
}

uint32_t net_reap_idle(ConnectionPool *cp, uint64_t current_time_ms, uint64_t timeout_ms) {
    (void)cp; (void)current_time_ms; (void)timeout_ms;
    fprintf(stderr, "net_reap_idle: NOT IMPLEMENTED\n");
    return 0;
}
