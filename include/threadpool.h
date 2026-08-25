#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>

// ─────────────────────────────────────────────
// One pending job in the queue: a client fd
// to be handled by the next free worker thread.
// ─────────────────────────────────────────────
typedef struct job_t {
    int         client_fd;
    struct job_t *next;
} job_t;

// ─────────────────────────────────────────────
// Thread pool
// ─────────────────────────────────────────────
typedef struct {
    pthread_t        *threads;      // worker thread array
    int               thread_count; // number of workers

    job_t            *head;         // front of job queue
    job_t            *tail;         // back of job queue
    int               queue_size;   // current number of pending jobs

    pthread_mutex_t   lock;         // protects queue
    pthread_cond_t    not_empty;    // signalled when a job is added
    int               shutdown;     // set to 1 to stop workers
} threadpool_t;

// ─────────────────────────────────────────────
// API
// ─────────────────────────────────────────────

// Create a pool with `thread_count` worker threads.
// Returns NULL on failure.
threadpool_t *threadpool_create(int thread_count);

// Submit a client fd to be handled by a worker.
// Returns 0 on success, -1 on failure.
int threadpool_submit(threadpool_t *pool, int client_fd);

// Gracefully stop all workers and free the pool.
void threadpool_destroy(threadpool_t *pool);

#define THREADPOOL_SIZE 16   // default number of worker threads

#endif
