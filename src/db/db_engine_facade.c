#include "db_engine_facade.h"

#include "executor.h"
#include "parser.h"
#include "table_runtime.h"
#include "tokenizer.h"
#include "utils.h"

#include <stdlib.h>

/*
 * facade 공통 실패 경로에서 DbResult에 에러 메시지를 채운다.
 */
static int db_engine_fail(DbResult *out_result, const char *message) {
    if (out_result != NULL) {
        db_result_set_error(out_result, message);
    }
    return FAILURE;
}

int db_engine_init(DbEngine *engine) {
    if (engine == NULL) {
        return FAILURE;
    }

    engine->initialized = 1;
    return SUCCESS;
}

int execute_query_with_lock(DbEngine *engine, const char *sql, DbResult *out_result) {
    return db_execute_sql(engine, sql, out_result);
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

    if (engine == NULL) {
        return;
    }

    engine->initialized = 0;
}
