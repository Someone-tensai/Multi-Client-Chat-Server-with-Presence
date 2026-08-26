#ifndef CONN_H
#define CONN_H

#include <pthread.h>
#include <openssl/ssl.h>
#include "registry.h"    // client_t

// ─────────────────────────────────────────────────────────────────────────────
// Per-connection state
//
// With the old blocking model one worker was tied up for the entire lifetime
// of a connection — even when the client was idle. The epoll model instead
// parks idle connections in the kernel and only wakes a worker when there is
// actual data to read.  All state that previously lived on the worker's stack
// now lives here on the heap, shared between epoll and the thread pool.
// ─────────────────────────────────────────────────────────────────────────────

#define CONN_BUF_SIZE 4096   // big enough for several pipelined commands

typedef struct conn_t {
    int              fd;              // client socket (O_NONBLOCK)
    int              epoll_fd;        // epoll instance — needed to re-arm

    // Partial-read buffer.  Data accumulates here until a '\n' is found,
    // at which point the line is processed and the buffer is compacted.
    char             buf[CONN_BUF_SIZE];
    int              buf_len;         // bytes of valid data currently in buf

    // Set to the client_t* after a successful REGISTER or LOGIN command.
    // NULL until then.
    client_t        *me;

    // TLS layer.  NULL when TLS is disabled (plain-text mode).
    SSL             *ssl;

    pthread_mutex_t  lock;            // prevents two workers touching same conn
    int              closing;         // 1 once disconnect cleanup has started
} conn_t;

#endif
