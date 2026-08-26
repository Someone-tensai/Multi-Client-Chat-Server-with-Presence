#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include "conn.h"

// ─────────────────────────────────────────────
// One pending job: a connection that has data
// ready to be processed by a worker thread.
// ─────────────────────────────────────────────
typedef struct job_t {
    conn_t       *conn;
    struct job_t *next;
} job_t;

// ─────────────────────────────────────────────
// Thread pool — supports dynamic resize
// ─────────────────────────────────────────────
typedef struct {
    pthread_t        *threads;
    int               thread_count;
    int               max_threads;
    int               min_threads;

    job_t            *head;
    job_t            *tail;
    int               queue_size;

    int               idle_count;
    int               marked_exits;
    int               shrink_idle_sec;

    pthread_mutex_t   lock;
    pthread_cond_t    not_empty;
    int               shutdown;
} threadpool_t;

// ─────────────────────────────────────────────
// API
// ─────────────────────────────────────────────

// Create a pool with up to `max_threads` workers.
// The pool starts with `max_threads` workers and dynamically
// adjusts: it spawns more when the queue is deep, and shrinks
// when workers have been idle longer than shrink_idle_sec.
threadpool_t *threadpool_create(int max_threads);

// Submit a connection with pending data to the pool.
int threadpool_submit(threadpool_t *pool, conn_t *conn);

// Gracefully stop all workers and free the pool.
void threadpool_destroy(threadpool_t *pool);

// Check whether the pool should shrink.  Call this periodically
// (e.g. from the epoll loop) to reap idle excess workers.
void threadpool_maybe_shrink(threadpool_t *pool);

#endif
