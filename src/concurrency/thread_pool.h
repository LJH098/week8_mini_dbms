#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "job_queue.h"

#include <pthread.h>

typedef void (*ThreadPoolJobHandler)(int client_fd, void *context);

typedef struct {
    int worker_count;
    int busy_worker_count;
    int idle_worker_count;
    int queue_length;
    int queue_capacity;
    int available_queue_slots;
} ThreadPoolStats;

typedef struct {
    pthread_t *workers;
    int worker_count;
    int active_worker_count;
    ThreadPoolJobHandler handler;
    void *handler_context;
    JobQueue queue;
} ThreadPool;

/*
 * worker thread와 bounded queue를 포함한 thread pool을 초기화한다.
 */
int thread_pool_init(ThreadPool *pool, int worker_count, int queue_capacity,
                     ThreadPoolJobHandler handler, void *context);

/*
 * client fd를 queue에 제출한다.
 * queue가 가득 차면 즉시 FAILURE를 반환한다.
 */
int thread_pool_submit(ThreadPool *pool, int client_fd);

/*
 * thread pool과 내부 queue의 현재 상태를 스냅샷으로 반환한다.
 */
int thread_pool_get_stats(ThreadPool *pool, ThreadPoolStats *out_stats);

/*
 * thread pool을 종료하고 모든 worker를 join한 뒤 내부 자원을 정리한다.
 */
void thread_pool_shutdown(ThreadPool *pool);

#endif
