#include "../include/threadpool.h"
#include "../include/server.h"
#include "../include/config.h"
#include "../include/log.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

// ─────────────────────────────────────────────
// Worker thread loop  (dynamic resize aware)
//
// IMPORTANT: We always dequeue and unlock BEFORE checking shrink/idle
// conditions.  Previously these checks were done before the dequeue,
// which meant a worker woken by a job signal could exit instead of
// processing the job — leaving it stranded forever because EPOLLONESHOT
// disables the fd until it is rearmed.
// ─────────────────────────────────────────────
static void *worker(void *arg)
{
    threadpool_t *pool = (threadpool_t *)arg;

    while (1)
    {
        // ── Mark this thread as idle and wait for work ──────────────────────
        pthread_mutex_lock(&pool->lock);
        pool->idle_count++;

        while (pool->queue_size == 0 && !pool->shutdown && pool->marked_exits == 0)
            pthread_cond_wait(&pool->not_empty, &pool->lock);

        pool->idle_count--;

        // ── Exit: pool shutting down and queue drained ───────────────────
        if (pool->shutdown && pool->queue_size == 0)
        {
            pthread_mutex_unlock(&pool->lock);
            return NULL;
        }

        // ── Exit: marked for shrink (no work waiting) ────────────────────
        if (pool->marked_exits > 0 && pool->queue_size == 0 &&
            pool->thread_count > pool->min_threads)
        {
            pool->marked_exits--;
            pool->thread_count--;
            LOG_INFO("Thread pool: worker exited (shrink, %d remain)", pool->thread_count);
            pthread_mutex_unlock(&pool->lock);
            return NULL;
        }

        // ── Dequeue a job FIRST (before any shrink checks) ──────────────
        job_t *job   = pool->head;
        pool->head   = job->next;
        if (pool->head == NULL)
            pool->tail = NULL;
        pool->queue_size--;

        pthread_mutex_unlock(&pool->lock);

        // ── Process the job ─────────────────────────────────────────────
        handle_client(job->conn);
        free(job);

        // ── After processing, check if this worker should exit ──────────
        // (safe to do outside the lock — we only decrement thread_count)
        if (pool->marked_exits > 0 && pool->thread_count > pool->min_threads)
        {
            pthread_mutex_lock(&pool->lock);
            if (pool->marked_exits > 0 && pool->thread_count > pool->min_threads)
            {
                pool->marked_exits--;
                pool->thread_count--;
                LOG_INFO("Thread pool: worker exited (shrink, %d remain)", pool->thread_count);
                pthread_mutex_unlock(&pool->lock);
                return NULL;
            }
            pthread_mutex_unlock(&pool->lock);
        }
    }
}

// ─────────────────────────────────────────────
// Create pool and spawn workers
// ─────────────────────────────────────────────
threadpool_t *threadpool_create(int max_threads)
{
    threadpool_t *pool = malloc(sizeof(threadpool_t));
    if (!pool) return NULL;

    pool->threads = malloc(sizeof(pthread_t) * max_threads);
    if (!pool->threads) { free(pool); return NULL; }

    pool->thread_count    = max_threads;
    pool->max_threads     = max_threads;
    pool->min_threads     = CFG_DEFAULT_POOL_MIN_THREADS;
    pool->head            = NULL;
    pool->tail            = NULL;
    pool->queue_size      = 0;
    pool->idle_count      = 0;
    pool->marked_exits    = 0;
    pool->shrink_idle_sec = CFG_DEFAULT_POOL_SHRINK_IDLE;
    pool->shutdown        = 0;

    pthread_mutex_init(&pool->lock, NULL);
    pthread_cond_init(&pool->not_empty, NULL);

    for (int i = 0; i < max_threads; i++)
    {
        if (pthread_create(&pool->threads[i], NULL, worker, pool) != 0)
        {
            LOG_ERROR_ERRNO("threadpool: pthread_create failed");
            pool->thread_count = i;
            threadpool_destroy(pool);
            return NULL;
        }
    }

    LOG_INFO("Thread pool started (%d workers)", max_threads);
    return pool;
}

// ─────────────────────────────────────────────
// Submit a ready connection to the queue
// ─────────────────────────────────────────────
int threadpool_submit(threadpool_t *pool, conn_t *conn)
{
    if (!pool || pool->shutdown) return -1;

    job_t *job = malloc(sizeof(job_t));
    if (!job) return -1;

    job->conn = conn;
    job->next = NULL;

    pthread_mutex_lock(&pool->lock);

    if (pool->tail == NULL)
        pool->head = job;
    else
        pool->tail->next = job;
    pool->tail = job;
    pool->queue_size++;

    pthread_cond_signal(&pool->not_empty);

    // ── Dynamic scale-up: spawn extra workers when queue is deep ─────
    if (pool->queue_size > pool->thread_count &&
        pool->thread_count < pool->max_threads)
    {
        int need = pool->queue_size - pool->thread_count;
        int can  = pool->max_threads - pool->thread_count;
        int spawn = need < can ? need : can;

        for (int i = 0; i < spawn; i++)
        {
            if (pool->thread_count >= pool->max_threads) break;

            int idx = pool->thread_count;
            pthread_t tid;
            if (pthread_create(&tid, NULL, worker, pool) == 0)
            {
                pool->threads[idx] = tid;
                pool->thread_count++;
            }
            else
            {
                LOG_ERROR_ERRNO("threadpool: scale-up pthread_create failed");
            }
        }

        LOG_INFO("Thread pool: scaled up to %d workers (queue=%d)", pool->thread_count, pool->queue_size);
    }

    pthread_mutex_unlock(&pool->lock);
    return 0;
}

// ─────────────────────────────────────────────
// Signal idle excess workers to exit
// ─────────────────────────────────────────────
void threadpool_maybe_shrink(threadpool_t *pool)
{
    if (!pool) return;

    pthread_mutex_lock(&pool->lock);

    if (pool->thread_count <= pool->min_threads || pool->shutdown)
    {
        pthread_mutex_unlock(&pool->lock);
        return;
    }

    // Only shrink when the queue is nearly empty — don't shed load
    // if there is work waiting.
    if (pool->queue_size > 0)
    {
        pthread_mutex_unlock(&pool->lock);
        return;
    }

    int excess = pool->thread_count - pool->min_threads;
    int to_mark = excess < pool->idle_count ? excess : pool->idle_count;

    if (to_mark > 0)
    {
        pool->marked_exits += to_mark;
        pthread_cond_broadcast(&pool->not_empty);
        LOG_INFO("Thread pool: requesting %d idle workers to exit (%d remain)", to_mark, pool->thread_count);
    }

    pthread_mutex_unlock(&pool->lock);
}

// ─────────────────────────────────────────────
// Signal shutdown, wait for workers, free pool
// ─────────────────────────────────────────────
void threadpool_destroy(threadpool_t *pool)
{
    if (!pool) return;

    pthread_mutex_lock(&pool->lock);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->not_empty);
    pthread_mutex_unlock(&pool->lock);

    // Snapshot thread_count — workers may decrement it while exiting.
    int n = pool->thread_count;

    for (int i = 0; i < n; i++)
        pthread_join(pool->threads[i], NULL);

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
