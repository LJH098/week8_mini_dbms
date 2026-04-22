#include "thread_pool.h"
#include "utils.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct {
    pthread_mutex_t mutex;
    int handled_count;
    int handled_sum;
} HandlerState;

static int assert_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "[FAIL] %s\n", message);
        return FAILURE;
    }
    return SUCCESS;
}

static void test_handler(int client_fd, void *context) {
    HandlerState *state;

    state = (HandlerState *)context;
    usleep(100000);

    pthread_mutex_lock(&state->mutex);
    state->handled_count++;
    state->handled_sum += client_fd;
    pthread_mutex_unlock(&state->mutex);
}

static int wait_for_count(HandlerState *state, int expected_count) {
    int retries;

    for (retries = 0; retries < 100; retries++) {
        int count;

        pthread_mutex_lock(&state->mutex);
        count = state->handled_count;
        pthread_mutex_unlock(&state->mutex);

        if (count >= expected_count) {
            return SUCCESS;
        }
        usleep(20000);
    }

    return FAILURE;
}

int main(void) {
    HandlerState state;
    ThreadPool pool;
    ThreadPoolStats stats;

    pthread_mutex_init(&state.mutex, NULL);
    state.handled_count = 0;
    state.handled_sum = 0;

    if (assert_true(thread_pool_init(&pool, 2, 8, test_handler, &state) == SUCCESS,
                    "thread_pool_init should create workers") != SUCCESS) {
        pthread_mutex_destroy(&state.mutex);
        return EXIT_FAILURE;
    }

    if (assert_true(thread_pool_get_stats(&pool, &stats) == SUCCESS,
                    "thread_pool_get_stats should succeed after initialization") != SUCCESS ||
        assert_true(stats.worker_count == 2,
                    "thread pool should report configured worker count") != SUCCESS ||
        assert_true(stats.busy_worker_count == 0,
                    "new thread pool should start with zero busy workers") != SUCCESS ||
        assert_true(stats.idle_worker_count == 2,
                    "new thread pool should report all workers as idle") != SUCCESS ||
        assert_true(stats.queue_length == 0,
                    "new thread pool should start with empty queue") != SUCCESS ||
        assert_true(stats.queue_capacity == 8,
                    "thread pool should report queue capacity") != SUCCESS) {
        thread_pool_shutdown(&pool);
        pthread_mutex_destroy(&state.mutex);
        return EXIT_FAILURE;
    }

    if (assert_true(thread_pool_submit(&pool, 1) == SUCCESS, "submit 1 should succeed") != SUCCESS ||
        assert_true(thread_pool_submit(&pool, 2) == SUCCESS, "submit 2 should succeed") != SUCCESS ||
        assert_true(thread_pool_submit(&pool, 3) == SUCCESS, "submit 3 should succeed") != SUCCESS ||
        assert_true(thread_pool_submit(&pool, 4) == SUCCESS, "submit 4 should succeed") != SUCCESS ||
        assert_true(thread_pool_submit(&pool, 5) == SUCCESS, "submit 5 should succeed") != SUCCESS ||
        assert_true(wait_for_count(&state, 5) == SUCCESS,
                    "workers should process submitted jobs") != SUCCESS) {
        thread_pool_shutdown(&pool);
        pthread_mutex_destroy(&state.mutex);
        return EXIT_FAILURE;
    }

    if (assert_true(state.handled_sum == 15,
                    "all submitted jobs should reach the handler") != SUCCESS) {
        thread_pool_shutdown(&pool);
        pthread_mutex_destroy(&state.mutex);
        return EXIT_FAILURE;
    }

    thread_pool_shutdown(&pool);

    state.handled_count = 0;
    state.handled_sum = 0;
    if (assert_true(thread_pool_init(&pool, 1, 1, test_handler, &state) == SUCCESS,
                    "small thread pool should initialize") != SUCCESS) {
        pthread_mutex_destroy(&state.mutex);
        return EXIT_FAILURE;
    }

    if (assert_true(thread_pool_submit(&pool, 10) == SUCCESS,
                    "first job should enter the pool") != SUCCESS) {
        thread_pool_shutdown(&pool);
        pthread_mutex_destroy(&state.mutex);
        return EXIT_FAILURE;
    }

    usleep(50000);
    if (assert_true(thread_pool_get_stats(&pool, &stats) == SUCCESS,
                    "thread_pool_get_stats should succeed while jobs are active") != SUCCESS ||
        assert_true(stats.worker_count == 1,
                    "small thread pool should report one worker") != SUCCESS ||
        assert_true(stats.busy_worker_count == 1,
                    "active worker should be counted as busy") != SUCCESS ||
        assert_true(stats.idle_worker_count == 0,
                    "busy single worker leaves no idle workers") != SUCCESS) {
        thread_pool_shutdown(&pool);
        pthread_mutex_destroy(&state.mutex);
        return EXIT_FAILURE;
    }

    if (assert_true(thread_pool_submit(&pool, 20) == SUCCESS,
                    "second job should occupy the bounded queue") != SUCCESS ||
        assert_true(thread_pool_submit(&pool, 30) == FAILURE,
                    "third job should fail immediately when queue is full") != SUCCESS) {
        thread_pool_shutdown(&pool);
        pthread_mutex_destroy(&state.mutex);
        return EXIT_FAILURE;
    }

    if (assert_true(thread_pool_get_stats(&pool, &stats) == SUCCESS,
                    "thread_pool_get_stats should include queue pressure") != SUCCESS ||
        assert_true(stats.queue_length == 1,
                    "bounded queue should report one waiting job") != SUCCESS ||
        assert_true(stats.queue_capacity == 1,
                    "small thread pool queue capacity should be one") != SUCCESS ||
        assert_true(stats.available_queue_slots == 0,
                    "full queue should report zero available slots") != SUCCESS) {
        thread_pool_shutdown(&pool);
        pthread_mutex_destroy(&state.mutex);
        return EXIT_FAILURE;
    }

    thread_pool_shutdown(&pool);
    pthread_mutex_destroy(&state.mutex);

    puts("[PASS] thread_pool");
    return EXIT_SUCCESS;
}
