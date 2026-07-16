#define _POSIX_C_SOURCE 199309L
#include "net.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>

static uint64_t now_ms_net(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

void net_init(ConnectionPool *cp) { memset(cp, 0, sizeof(ConnectionPool)); }

uint32_t net_accept(ConnectionPool *cp, int socket_fd) {
    if (cp == NULL) return 0;

    /* Find a free slot */
    int32_t free_slot = -1;
    for (uint32_t i = 0; i < MAX_CONNECTIONS; i++) {
        if (!cp->pool[i].in_use) {
            free_slot = (int32_t)i;
            break;
        }
    }
    if (free_slot < 0) {
        cp->total_rejected++;
        return 0;
    }

    uint32_t id = (uint32_t)free_slot;
    Connection *c = &cp->pool[id];
    c->id          = id;
    c->socket_fd   = socket_fd;
    c->last_active = now_ms_net();
    c->recv_len    = 0;
    c->in_use      = true;
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
        /* FIXED: close socket */
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
    uint32_t copy_len = buf_len < BUF_SIZE ? buf_len : BUF_SIZE;
    if (copy_len > c->recv_len) copy_len = c->recv_len;
    memcpy(buf, c->recv_buf, copy_len);
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
    /* FIXED: actually send data via socket */
    if (c->socket_fd >= 0 && len <= BUF_SIZE) {
        ssize_t sent = write(c->socket_fd, data, len);
        if (sent < 0) return 0;
        return (uint32_t)sent;
    }
    /* Fallback for non-socket connections: store in recv_buf (for testing) */
    if (len <= BUF_SIZE) {
        memcpy(c->recv_buf, data, len);
        c->recv_len = len;
    }
    return len;
}

/* ── net_pool_warmup ─────────────────────────────────────────────── */

uint32_t net_pool_warmup(ConnectionPool *cp, uint32_t pool_size) {
    if (cp == NULL) return 0;

    uint32_t warmed = 0;
    for (uint32_t i = 0; i < pool_size && cp->count < MAX_CONNECTIONS; i++) {
        /* Create a pre-allocated socket pair for warm connections */
        int fds[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0) {
            uint32_t id = cp->count;
            Connection *c = &cp->pool[id];
            c->id          = id;
            c->socket_fd   = fds[0];
            c->last_active = now_ms_net();
            c->recv_len    = 0;
            c->in_use      = true;
            memset(c->recv_buf, 0, BUF_SIZE);
            cp->count++;
            cp->total_connections++;
            /* Close the other end since we just need a placeholder socket */
            close(fds[1]);
            warmed++;
        } else {
            break;
        }
    }
    return warmed;
}

/* ── net_reap_idle ────────────────────────────────────────────────── */

uint32_t net_reap_idle(ConnectionPool *cp, uint64_t current_time_ms, uint64_t timeout_ms) {
    if (cp == NULL) return 0;

    uint32_t reaped = 0;
    for (uint32_t i = 0; i < MAX_CONNECTIONS; i++) {
        Connection *c = &cp->pool[i];
        if (!c->in_use) continue;

        uint64_t idle_time;
        if (current_time_ms >= c->last_active)
            idle_time = current_time_ms - c->last_active;
        else
            idle_time = 0;

        if (idle_time > timeout_ms) {
            /* Close the connection */
            if (c->socket_fd >= 0) {
                close(c->socket_fd);
                c->socket_fd = -1;
            }
            c->in_use = false;
            if (cp->count > 0) cp->count--;
            reaped++;
        }
    }
    return reaped;
}
