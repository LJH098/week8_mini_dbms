#include "job_queue.h"

#include "utils.h"

#include <stdlib.h>
#include <string.h>

int queue_init(JobQueue *queue, int capacity) {
    if (queue == NULL || capacity <= 0) {
        return FAILURE;
    }

    memset(queue, 0, sizeof(*queue));
    queue->client_fds = (int *)malloc((size_t)capacity * sizeof(int));
    if (queue->client_fds == NULL) {
        return FAILURE;
    }

    queue->capacity = capacity;
    if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
        free(queue->client_fds);
        queue->client_fds = NULL;
        return FAILURE;
    }

    if (pthread_cond_init(&queue->not_empty, NULL) != 0) {
        pthread_mutex_destroy(&queue->mutex);
        free(queue->client_fds);
        queue->client_fds = NULL;
        return FAILURE;
    }

    return SUCCESS;
}

int queue_push(JobQueue *queue, int client_fd) {
    int status;

    if (queue == NULL) {
        return FAILURE;
    }

    status = FAILURE;
    pthread_mutex_lock(&queue->mutex);
    if (!queue->shutting_down && queue->count < queue->capacity) {
        queue->client_fds[queue->tail] = client_fd;
        queue->tail = (queue->tail + 1) % queue->capacity;
        queue->count++;
        status = SUCCESS;
        pthread_cond_signal(&queue->not_empty);
    }
    pthread_mutex_unlock(&queue->mutex);
    return status;
}

int queue_pop(JobQueue *queue, int *out_client_fd) {
    if (queue == NULL || out_client_fd == NULL) {
        return FAILURE;
    }

    pthread_mutex_lock(&queue->mutex);
    while (queue->count == 0 && !queue->shutting_down) {
        pthread_cond_wait(&queue->not_empty, &queue->mutex);
    }

    if (queue->count == 0 && queue->shutting_down) {
        pthread_mutex_unlock(&queue->mutex);
        return FAILURE;
    }

    *out_client_fd = queue->client_fds[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    pthread_mutex_unlock(&queue->mutex);
    return SUCCESS;
}

void queue_shutdown(JobQueue *queue) {
    if (queue == NULL) {
        return;
    }

    pthread_mutex_lock(&queue->mutex);
    queue->shutting_down = 1;
    pthread_cond_broadcast(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
}

void queue_destroy(JobQueue *queue) {
    if (queue == NULL) {
        return;
    }

    pthread_cond_destroy(&queue->not_empty);
    pthread_mutex_destroy(&queue->mutex);
    free(queue->client_fds);
    queue->client_fds = NULL;
    queue->capacity = 0;
    queue->count = 0;
}
