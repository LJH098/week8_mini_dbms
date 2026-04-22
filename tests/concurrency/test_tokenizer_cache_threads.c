#include "tokenizer.h"
#include "lock_manager.h"
#include "utils.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    const char *sql;
    int iterations;
    int failed;
} TokenizerWorkerState;

static int assert_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "[FAIL] %s\n", message);
        return FAILURE;
    }
    return SUCCESS;
}

static void *tokenizer_worker_main(void *arg) {
    TokenizerWorkerState *state;
    int i;

    state = (TokenizerWorkerState *)arg;
    for (i = 0; i < state->iterations; i++) {
        Token *tokens;
        int token_count;

        tokens = tokenizer_tokenize(state->sql, &token_count);
        if (tokens == NULL || token_count == 0) {
            state->failed = 1;
            free(tokens);
            return NULL;
        }
        free(tokens);
    }

    return NULL;
}

int main(void) {
    pthread_t threads[8];
    TokenizerWorkerState workers[8];
    int i;

    tokenizer_cleanup_cache();
    if (init_lock_manager(LOCK_POLICY_SPLIT_RWLOCK) != SUCCESS) {
        return EXIT_FAILURE;
    }

    for (i = 0; i < 8; i++) {
        workers[i].sql = (i % 2 == 0)
                             ? "SELECT name FROM threaded_cache_users WHERE id = 1;"
                             : "INSERT INTO threaded_cache_users (name, age) VALUES ('Alice', 30);";
        workers[i].iterations = 200;
        workers[i].failed = 0;
        if (pthread_create(&threads[i], NULL, tokenizer_worker_main, &workers[i]) != 0) {
            return EXIT_FAILURE;
        }
    }

    for (i = 0; i < 8; i++) {
        pthread_join(threads[i], NULL);
        if (assert_true(workers[i].failed == 0,
                        "tokenizer threads should tokenize without failure") != SUCCESS) {
            tokenizer_cleanup_cache();
            return EXIT_FAILURE;
        }
    }

    if (assert_true(tokenizer_get_cache_entry_count() > 0,
                    "cache entry count should stay readable after threaded access") != SUCCESS ||
        assert_true(tokenizer_get_cache_hit_count() > 0,
                    "threaded access should record cache hits") != SUCCESS) {
        tokenizer_cleanup_cache();
        return EXIT_FAILURE;
    }

    tokenizer_cleanup_cache();
    if (assert_true(tokenizer_get_cache_entry_count() == 0,
                    "cleanup should still reset tokenizer cache after threaded access") != SUCCESS) {
        destroy_lock_manager();
        return EXIT_FAILURE;
    }

    destroy_lock_manager();
    puts("[PASS] tokenizer_cache_threads");
    return EXIT_SUCCESS;
}
