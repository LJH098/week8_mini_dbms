#ifndef LOCK_MANAGER_H
#define LOCK_MANAGER_H

typedef enum {
    LOCK_POLICY_GLOBAL_MUTEX,
    LOCK_POLICY_SPLIT_RWLOCK
} LockPolicy;

typedef enum {
    QUERY_LOCK_READ,
    QUERY_LOCK_WRITE
} QueryLockMode;

/*
 * 현재 단계의 락 정책으로 lock manager를 초기화한다.
 */
int init_lock_manager(LockPolicy policy);

/*
 * SQL 실행 전에 현재 정책에 맞는 DB 락을 획득한다.
 */
int lock_db_for_query(QueryLockMode mode);

/*
 * SQL 실행 후 현재 정책에 맞는 DB 락을 해제한다.
 */
void unlock_db_for_query(QueryLockMode mode);

/*
 * tokenizer 전역 캐시 접근 전용 락이다.
 * Step 5에서는 no-op이고 Step 6에서 활성화된다.
 */
void lock_tokenizer_cache(void);

/*
 * tokenizer 전역 캐시 락을 해제한다.
 */
void unlock_tokenizer_cache(void);

/*
 * lock manager 내부 자원을 정리한다.
 */
void destroy_lock_manager(void);

#endif
