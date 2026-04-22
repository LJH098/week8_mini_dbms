#include "thread_pool.h"

#include "utils.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    ThreadPool *pool;
} ThreadPoolWorkerArgs;

static void *thread_pool_worker_main(void *arg) {
    ThreadPool *pool;
    int client_fd;

    pool = ((ThreadPoolWorkerArgs *)arg)->pool;
    free(arg);

    while (queue_pop(&pool->queue, &client_fd) == SUCCESS) {
        pthread_mutex_lock(&pool->queue.mutex);
        pool->active_worker_count++;
        pthread_mutex_unlock(&pool->queue.mutex);

        pool->handler(client_fd, pool->handler_context);

        pthread_mutex_lock(&pool->queue.mutex);
        pool->active_worker_count--;
        pthread_mutex_unlock(&pool->queue.mutex);
    }

    return NULL;
}

int thread_pool_init(ThreadPool *pool, int worker_count, int queue_capacity,
                     ThreadPoolJobHandler handler, void *context) {
    int i;

    if (pool == NULL || worker_count <= 0 || queue_capacity <= 0 ||
        handler == NULL) {
        return FAILURE;
    }

    memset(pool, 0, sizeof(*pool));
    if (queue_init(&pool->queue, queue_capacity) != SUCCESS) {
        return FAILURE;
    }

    pool->workers = (pthread_t *)calloc((size_t)worker_count, sizeof(pthread_t));
    if (pool->workers == NULL) {
        queue_destroy(&pool->queue);
        return FAILURE;
    }

    pool->worker_count = worker_count;
    pool->handler = handler;
    pool->handler_context = context;

    for (i = 0; i < worker_count; i++) {
        ThreadPoolWorkerArgs *args;

        args = (ThreadPoolWorkerArgs *)malloc(sizeof(ThreadPoolWorkerArgs));
        if (args == NULL) {
            thread_pool_shutdown(pool);
            return FAILURE;
        }
        args->pool = pool;

        if (pthread_create(&pool->workers[i], NULL,
                           thread_pool_worker_main, args) != 0) {
            free(args);
            pool->worker_count = i;
            thread_pool_shutdown(pool);
            return FAILURE;
        }
    }

    return SUCCESS;
}

int thread_pool_submit(ThreadPool *pool, int client_fd) {
    if (pool == NULL) {
        return FAILURE;
    }

    return queue_push(&pool->queue, client_fd);
}

int thread_pool_get_stats(ThreadPool *pool, ThreadPoolStats *out_stats) {
    if (pool == NULL || out_stats == NULL) {
        return FAILURE;
    }

    pthread_mutex_lock(&pool->queue.mutex);
    out_stats->worker_count = pool->worker_count;
    out_stats->busy_worker_count = pool->active_worker_count;
    out_stats->idle_worker_count = pool->worker_count - pool->active_worker_count;
    out_stats->queue_length = pool->queue.count;
    out_stats->queue_capacity = pool->queue.capacity;
    out_stats->available_queue_slots = pool->queue.capacity - pool->queue.count;
    pthread_mutex_unlock(&pool->queue.mutex);
    return SUCCESS;
}

void thread_pool_shutdown(ThreadPool *pool) {
    int i;

    if (pool == NULL) {
        return;
    }

    queue_shutdown(&pool->queue);
    for (i = 0; i < pool->worker_count; i++) {
        if (pool->workers[i] != 0) {
            pthread_join(pool->workers[i], NULL);
        }
    }

    free(pool->workers);
    pool->workers = NULL;
    pool->worker_count = 0;
    pool->active_worker_count = 0;
    queue_destroy(&pool->queue);
}
