#define _POSIX_C_SOURCE 199309L
#include "net.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>

static uint64_t now_ms_net(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

void net_init(ConnectionPool *cp) {
    if (cp == NULL) return;
    memset(cp, 0, sizeof(ConnectionPool));
}

uint32_t net_accept(ConnectionPool *cp, int socket_fd) {
    if (cp == NULL) return 0;

    /* FIXED: check for MAX_CONNECTIONS */
    if (cp->count >= MAX_CONNECTIONS) {
        cp->total_rejected++;
        return 0;
    }

    /* FIXED: find a free slot instead of using count as id */
    uint32_t id = 0;
    for (uint32_t i = 0; i < MAX_CONNECTIONS; i++) {
        if (!cp->pool[i].in_use) {
            id = i;
            break;
        }
    }

    /* Double-check bounds */
    if (id >= MAX_CONNECTIONS) {
        cp->total_rejected++;
        return 0;
    }

    Connection *c = &cp->pool[id];

    c->id           = id;
    c->socket_fd    = socket_fd;
    c->last_active  = now_ms_net();
    c->recv_len     = 0;
    c->in_use       = true;

    /* FIXED: zero the receive buffer */
    memset(c->recv_buf, 0, BUF_SIZE);

    cp->count++;
    cp->total_connections++;

    return id;
}

void net_close(ConnectionPool *cp, uint32_t conn_id) {
    if (cp == NULL) return;

    /* FIXED: bounds check */
    if (conn_id >= MAX_CONNECTIONS) return;

    Connection *c = &cp->pool[conn_id];

    if (c->in_use) {
        c->in_use = false;

        /* FIXED: actually close the socket */
        if (c->socket_fd >= 0) {
            close(c->socket_fd);
            c->socket_fd = -1;
        }

        /* FIXED: decrement count */
        if (cp->count > 0) cp->count--;
    }
}

uint32_t net_recv(ConnectionPool *cp, uint32_t conn_id, uint8_t *buf, uint32_t buf_len) {
    if (cp == NULL || buf == NULL) return 0;

    /* FIXED: bounds check */
    if (conn_id >= MAX_CONNECTIONS) return 0;

    Connection *c = &cp->pool[conn_id];
    if (!c->in_use) return 0;

    /* FIXED: clamp copy length */
    uint32_t copy_len = (buf_len < BUF_SIZE) ? buf_len : BUF_SIZE;
    if (copy_len > c->recv_len) copy_len = c->recv_len;

    if (copy_len > 0) {
        memcpy(buf, c->recv_buf, copy_len);
    }

    c->last_active = now_ms_net();
    return copy_len;
}

uint32_t net_send(ConnectionPool *cp, uint32_t conn_id, const uint8_t *data, uint32_t len) {
    if (cp == NULL || data == NULL) return 0;

    /* FIXED: bounds check */
    if (conn_id >= MAX_CONNECTIONS) return 0;

    Connection *c = &cp->pool[conn_id];
    if (!c->in_use) return 0;

    c->last_active = now_ms_net();

    /* FIXED: only copy what fits */
    uint32_t copy_len = (len < BUF_SIZE) ? len : BUF_SIZE;
    if (copy_len > 0) {
        memcpy(c->recv_buf, data, copy_len);
    }
    c->recv_len = copy_len;

    return len;
}

/* ── IMPLEMENTED: net_pool_warmup ─────────────────────────────────── */

uint32_t net_pool_warmup(ConnectionPool *cp, uint32_t pool_size) {
    if (cp == NULL) return 0;

    if (pool_size > MAX_CONNECTIONS) pool_size = MAX_CONNECTIONS;

    uint32_t warmed = 0;
    for (uint32_t i = 0; i < pool_size; i++) {
        /* Create a dummy socket for pre-warming (socketpair or just a placeholder) */
        int fd = -1;
        /* Use socketpair to create a connected pair */
        int sv[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
            fd = sv[0];
            close(sv[1]);  /* Close the other end */
        }

        uint32_t id = net_accept(cp, fd);
        if (id != 0 || fd >= 0) {
            warmed++;
        } else if (fd >= 0) {
            close(fd);
        }
    }

    return warmed;
}

/* ── IMPLEMENTED: net_reap_idle ───────────────────────────────────── */

uint32_t net_reap_idle(ConnectionPool *cp, uint64_t current_time_ms, uint64_t timeout_ms) {
    if (cp == NULL) return 0;

    uint32_t reaped = 0;

    for (uint32_t i = 0; i < MAX_CONNECTIONS; i++) {
        Connection *c = &cp->pool[i];
        if (!c->in_use) continue;

        uint64_t idle_time = current_time_ms - c->last_active;
        if (idle_time > timeout_ms) {
            net_close(cp, i);
            reaped++;
        }
    }

    return reaped;
}
