#ifndef JOB_QUEUE_H
#define JOB_QUEUE_H

#include <pthread.h>

typedef struct {
    int *client_fds;
    int capacity;
    int head;
    int tail;
    int count;
    int shutting_down;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
} JobQueue;

/*
 * bounded job queue를 초기화한다.
 */
int queue_init(JobQueue *queue, int capacity);

/*
 * 큐가 가득 차지 않았으면 client fd를 즉시 push한다.
 */
int queue_push(JobQueue *queue, int client_fd);

/*
 * 다음 client fd를 pop한다.
 * shutdown 이후 큐가 비어 있으면 FAILURE를 반환한다.
 */
int queue_pop(JobQueue *queue, int *out_client_fd);

/*
 * queue shutdown 상태를 켜고 대기 중인 worker를 깨운다.
 */
void queue_shutdown(JobQueue *queue);

/*
 * queue 내부 자원을 정리한다.
 */
void queue_destroy(JobQueue *queue);

#endif
