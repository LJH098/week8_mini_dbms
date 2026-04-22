#ifndef DB_ENGINE_FACADE_H
#define DB_ENGINE_FACADE_H

#include "executor_result.h"

typedef struct {
    int initialized;
} DbEngine;

/*
 * DB 엔진 facade 상태를 초기화한다.
 */
int db_engine_init(DbEngine *engine);

/*
 * 현재 단계에서는 락 없이 SQL을 실행하고 결과를 반환한다.
 * 이후 단계에서 lock manager가 같은 진입점을 감싸게 된다.
 */
int execute_query_with_lock(DbEngine *engine, const char *sql, DbResult *out_result);

/*
 * SQL 문자열을 tokenizer -> parser -> executor 경로로 실행해 DbResult를 반환한다.
 */
int db_execute_sql(DbEngine *engine, const char *sql, DbResult *out_result);

/*
 * DB 엔진 facade와 관련 캐시/런타임 자원을 정리한다.
 */
void db_engine_shutdown(DbEngine *engine);

#endif
