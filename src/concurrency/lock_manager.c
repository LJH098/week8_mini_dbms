#include "lock_manager.h"

#include "utils.h"

#include <pthread.h>
#include <string.h>

typedef struct {
    int initialized;
    LockPolicy policy;
    int tokenizer_cache_lock_enabled;
    pthread_mutex_t db_mutex;
    pthread_rwlock_t db_rwlock;
    pthread_mutex_t tokenizer_cache_mutex;
} LockManagerState;

static LockManagerState lock_manager_state;

int init_lock_manager(LockPolicy policy) {
    destroy_lock_manager();
    memset(&lock_manager_state, 0, sizeof(lock_manager_state));

    if (pthread_mutex_init(&lock_manager_state.db_mutex, NULL) != 0) {
        return FAILURE;
    }

    if (pthread_rwlock_init(&lock_manager_state.db_rwlock, NULL) != 0) {
        pthread_mutex_destroy(&lock_manager_state.db_mutex);
        return FAILURE;
    }

    if (pthread_mutex_init(&lock_manager_state.tokenizer_cache_mutex, NULL) != 0) {
        pthread_rwlock_destroy(&lock_manager_state.db_rwlock);
        pthread_mutex_destroy(&lock_manager_state.db_mutex);
        return FAILURE;
    }

    lock_manager_state.initialized = 1;
    lock_manager_state.policy = policy;
    lock_manager_state.tokenizer_cache_lock_enabled =
        (policy == LOCK_POLICY_SPLIT_RWLOCK);
    return SUCCESS;
}

int lock_db_for_query(QueryLockMode mode) {
    if (!lock_manager_state.initialized) {
        return FAILURE;
    }

    if (lock_manager_state.policy == LOCK_POLICY_GLOBAL_MUTEX) {
        return pthread_mutex_lock(&lock_manager_state.db_mutex) == 0 ? SUCCESS : FAILURE;
    }

    if (mode == QUERY_LOCK_READ) {
        return pthread_rwlock_rdlock(&lock_manager_state.db_rwlock) == 0 ?
               SUCCESS : FAILURE;
    }

    return pthread_rwlock_wrlock(&lock_manager_state.db_rwlock) == 0 ?
           SUCCESS : FAILURE;
}

void unlock_db_for_query(QueryLockMode mode) {
    (void)mode;

    if (!lock_manager_state.initialized) {
        return;
    }

    if (lock_manager_state.policy == LOCK_POLICY_GLOBAL_MUTEX) {
        pthread_mutex_unlock(&lock_manager_state.db_mutex);
        return;
    }

    pthread_rwlock_unlock(&lock_manager_state.db_rwlock);
}

void lock_tokenizer_cache(void) {
    if (!lock_manager_state.initialized || !lock_manager_state.tokenizer_cache_lock_enabled) {
        return;
    }

    pthread_mutex_lock(&lock_manager_state.tokenizer_cache_mutex);
}

void unlock_tokenizer_cache(void) {
    if (!lock_manager_state.initialized || !lock_manager_state.tokenizer_cache_lock_enabled) {
        return;
    }

    pthread_mutex_unlock(&lock_manager_state.tokenizer_cache_mutex);
}

void destroy_lock_manager(void) {
    if (!lock_manager_state.initialized) {
        return;
    }

    pthread_mutex_destroy(&lock_manager_state.tokenizer_cache_mutex);
    pthread_rwlock_destroy(&lock_manager_state.db_rwlock);
    pthread_mutex_destroy(&lock_manager_state.db_mutex);
    memset(&lock_manager_state, 0, sizeof(lock_manager_state));
}
