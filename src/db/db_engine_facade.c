#include "db_engine_facade.h"

#include "lock_manager.h"
#include "executor.h"
#include "parser.h"
#include "table_runtime.h"
#include "tokenizer.h"
#include "utils.h"

#include <ctype.h>
#include <stdlib.h>
#include <strings.h>

/*
 * facade 공통 실패 경로에서 DbResult에 에러 메시지를 채운다.
 */
static int db_engine_fail(DbResult *out_result, const char *message) {
    if (out_result != NULL) {
        db_result_set_error(out_result, message);
    }
    return FAILURE;
}

static int db_engine_parse_statement(const char *sql, SqlStatement *out_statement) {
    Token *tokens;
    int token_count;
    char *working_sql;
    int status;

    if (sql == NULL || out_statement == NULL) {
        return FAILURE;
    }

    working_sql = utils_strdup(sql);
    if (working_sql == NULL) {
        return FAILURE;
    }

    utils_trim(working_sql);
    if (working_sql[0] == '\0') {
        free(working_sql);
        return FAILURE;
    }

    tokens = tokenizer_tokenize(working_sql, &token_count);
    if (tokens == NULL || token_count == 0) {
        free(tokens);
        free(working_sql);
        return FAILURE;
    }

    status = parser_parse(tokens, token_count, out_statement);
    free(tokens);
    free(working_sql);
    return status;
}

static QueryLockMode db_engine_choose_initial_lock_mode(const SqlStatement *statement) {
    if (statement != NULL && statement->type == SQL_SELECT) {
        return QUERY_LOCK_READ;
    }

    return QUERY_LOCK_WRITE;
}

int db_engine_init(DbEngine *engine) {
    if (engine == NULL) {
        return FAILURE;
    }

    if (init_lock_manager(LOCK_POLICY_SPLIT_RWLOCK) != SUCCESS) {
        return FAILURE;
    }

    engine->initialized = 1;
    return SUCCESS;
}

int execute_query_with_lock(DbEngine *engine, const char *sql, DbResult *out_result) {
    SqlStatement statement;
    QueryLockMode lock_mode;
    int parsed_for_locking;
    int status;

    if (engine == NULL || sql == NULL || out_result == NULL) {
        return FAILURE;
    }

    parsed_for_locking = db_engine_parse_statement(sql, &statement) == SUCCESS;
    lock_mode = db_engine_choose_initial_lock_mode(parsed_for_locking ? &statement : NULL);
    if (lock_db_for_query(lock_mode) != SUCCESS) {
        return db_engine_fail(out_result, "Failed to acquire DB lock.");
    }

    /*
     * 단일 활성 테이블 구조에서는 아직 적재되지 않은 SELECT가
     * 런타임 상태를 바꾸므로 write lock으로 승격해 직렬화한다.
     */
    if (parsed_for_locking && lock_mode == QUERY_LOCK_READ &&
        !table_runtime_is_loaded_for(statement.select.table_name)) {
        unlock_db_for_query(lock_mode);
        lock_mode = QUERY_LOCK_WRITE;
        if (lock_db_for_query(lock_mode) != SUCCESS) {
            return db_engine_fail(out_result, "Failed to upgrade DB lock.");
        }
    }

    status = db_execute_sql(engine, sql, out_result);
    unlock_db_for_query(lock_mode);
    return status;
}

int db_execute_sql(DbEngine *engine, const char *sql, DbResult *out_result) {
    Token *tokens;
    int token_count;
    SqlStatement statement;
    char *working_sql;
    int status;

    if (engine == NULL || out_result == NULL) {
        return FAILURE;
    }

    db_result_init(out_result);
    if (!engine->initialized) {
        return db_engine_fail(out_result, "DB engine is not initialized.");
    }

    if (sql == NULL) {
        return db_engine_fail(out_result, "SQL statement is missing.");
    }

    working_sql = utils_strdup(sql);
    if (working_sql == NULL) {
        return db_engine_fail(out_result, "Failed to allocate memory for SQL.");
    }

    utils_trim(working_sql);
    if (working_sql[0] == '\0') {
        free(working_sql);
        return db_engine_fail(out_result, "SQL statement is empty.");
    }

    tokens = tokenizer_tokenize(working_sql, &token_count);
    if (tokens == NULL || token_count == 0) {
        free(tokens);
        free(working_sql);
        return db_engine_fail(out_result, "Failed to tokenize SQL statement.");
    }

    status = parser_parse(tokens, token_count, &statement);
    if (status != SUCCESS) {
        free(tokens);
        free(working_sql);
        return db_engine_fail(out_result, "Failed to parse SQL statement.");
    }

    status = executor_execute_into_result(&statement, out_result);
    if (status != SUCCESS && out_result->message[0] == '\0') {
        db_engine_fail(out_result, "Failed to execute SQL statement.");
    }

    free(tokens);
    free(working_sql);
    return status;
}

void db_engine_shutdown(DbEngine *engine) {
    table_runtime_cleanup();
    tokenizer_cleanup_cache();
    destroy_lock_manager();

    if (engine == NULL) {
        return;
    }

    engine->initialized = 0;
}
