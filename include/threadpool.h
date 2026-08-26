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
// Thread pool
// ─────────────────────────────────────────────
typedef struct {
    pthread_t        *threads;
    int               thread_count;

    job_t            *head;
    job_t            *tail;
    int               queue_size;

    pthread_mutex_t   lock;
    pthread_cond_t    not_empty;
    int               shutdown;
} threadpool_t;

// ─────────────────────────────────────────────
// API
// ─────────────────────────────────────────────

// Create a pool with `thread_count` worker threads.
threadpool_t *threadpool_create(int thread_count);

// Submit a connection with pending data to the pool.
int threadpool_submit(threadpool_t *pool, conn_t *conn);

// Gracefully stop all workers and free the pool.
void threadpool_destroy(threadpool_t *pool);

#define THREADPOOL_SIZE 16

#endif
