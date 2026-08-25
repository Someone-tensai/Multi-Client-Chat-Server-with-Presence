#include "../include/threadpool.h"
#include "../include/server.h"
#include <stdlib.h>
#include <stdio.h>

// ─────────────────────────────────────────────
// Worker thread loop
// Each worker blocks on not_empty, pops a job,
// releases the lock, then handles the client.
// ─────────────────────────────────────────────
static void *worker(void *arg)
{
    threadpool_t *pool = (threadpool_t *)arg;

    while (1)
    {
        pthread_mutex_lock(&pool->lock);

        // Wait until there is work or a shutdown signal
        while (pool->queue_size == 0 && !pool->shutdown)
            pthread_cond_wait(&pool->not_empty, &pool->lock);

        // Exit if shutting down and nothing left to do
        if (pool->shutdown && pool->queue_size == 0)
        {
            pthread_mutex_unlock(&pool->lock);
            return NULL;
        }

        // Pop the front job
        job_t *job  = pool->head;
        pool->head  = job->next;
        if (pool->head == NULL)
            pool->tail = NULL;
        pool->queue_size--;

        pthread_mutex_unlock(&pool->lock);

        // Handle the client — this blocks until the client disconnects
        handle_client(job->client_fd);
        free(job);
    }
}

// ─────────────────────────────────────────────
// Create pool and spawn worker threads
// ─────────────────────────────────────────────
threadpool_t *threadpool_create(int thread_count)
{
    threadpool_t *pool = malloc(sizeof(threadpool_t));
    if (!pool) return NULL;

    pool->threads      = malloc(sizeof(pthread_t) * thread_count);
    if (!pool->threads) { free(pool); return NULL; }

    pool->thread_count = thread_count;
    pool->head         = NULL;
    pool->tail         = NULL;
    pool->queue_size   = 0;
    pool->shutdown     = 0;

    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->not_empty, NULL);

    for (int i = 0; i < thread_count; i++)
    {
        if (pthread_create(&pool->threads[i], NULL, worker, pool) != 0)
        {
            perror("threadpool: pthread_create failed");
            // Shutdown however many threads already started
            pool->thread_count = i;
            threadpool_destroy(pool);
            return NULL;
        }
    }

    printf("Thread pool started (%d workers)\n", thread_count);
    return pool;
}

// ─────────────────────────────────────────────
// Submit a new client fd to the queue
// ─────────────────────────────────────────────
int threadpool_submit(threadpool_t *pool, int client_fd)
{
    if (!pool || pool->shutdown) return -1;

    job_t *job = malloc(sizeof(job_t));
    if (!job) return -1;

    job->client_fd = client_fd;
    job->next      = NULL;

    pthread_mutex_lock(&pool->lock);

    // Append to tail
    if (pool->tail == NULL)
        pool->head = job;
    else
        pool->tail->next = job;
    pool->tail = job;
    pool->queue_size++;

    // Wake one sleeping worker
    pthread_cond_signal(&pool->not_empty);
    pthread_mutex_unlock(&pool->lock);

    return 0;
}

// ─────────────────────────────────────────────
// Signal shutdown, wake all workers, join them
// ─────────────────────────────────────────────
void threadpool_destroy(threadpool_t *pool)
{
    if (!pool) return;

    pthread_mutex_lock(&pool->lock);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->not_empty);  // wake all sleeping workers
    pthread_mutex_unlock(&pool->lock);

    for (int i = 0; i < pool->thread_count; i++)
        pthread_join(pool->threads[i], NULL);

    // Drain any remaining queued jobs (free unprocessed fds)
    job_t *j = pool->head;
    while (j)
    {
        job_t *next = j->next;
        free(j);
        j = next;
    }

    pthread_mutex_destroy(&pool->lock);
    pthread_cond_destroy(&pool->not_empty);
    free(pool->threads);
    free(pool);
}
