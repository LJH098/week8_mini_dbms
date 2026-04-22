#include "db_engine_facade.h"
#include "executor.h"
#include "tokenizer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int assert_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "[FAIL] %s\n", message);
        return FAILURE;
    }
    return SUCCESS;
}

int main(void) {
    DbEngine engine;
    DbResult result;

    if (assert_true(db_engine_init(&engine) == SUCCESS,
                    "db_engine_init should succeed") != SUCCESS) {
        return EXIT_FAILURE;
    }

    db_result_init(&result);
    if (assert_true(db_execute_sql(&engine,
                                   "INSERT INTO facade_users (name, age) VALUES ('Alice', 30);",
                                   &result) == SUCCESS,
                    "db_execute_sql should execute INSERT") != SUCCESS ||
        assert_true(result.success == 1, "INSERT result should be marked successful") != SUCCESS ||
        assert_true(result.type == DB_RESULT_INSERT, "INSERT result type should match") != SUCCESS ||
        assert_true(result.rows_affected == 1, "INSERT should report one affected row") != SUCCESS ||
        assert_true(strcmp(result.message, "1 row inserted into facade_users.") == 0,
                    "INSERT message should be structured") != SUCCESS) {
        db_result_free(&result);
        db_engine_shutdown(&engine);
        return EXIT_FAILURE;
    }
    db_result_free(&result);

    db_result_init(&result);
    if (assert_true(db_execute_sql(&engine,
                                   "INSERT INTO facade_users (name, age) VALUES ('Bob', 25);",
                                   &result) == SUCCESS,
                    "second INSERT should succeed") != SUCCESS ||
        assert_true(result.type == DB_RESULT_INSERT, "second INSERT should still use INSERT result") != SUCCESS) {
        db_result_free(&result);
        db_engine_shutdown(&engine);
        return EXIT_FAILURE;
    }
    db_result_free(&result);

    db_result_init(&result);
    if (assert_true(db_execute_sql(&engine,
                                   "SELECT name FROM facade_users WHERE id = 2;",
                                   &result) == SUCCESS,
                    "SELECT by id should succeed") != SUCCESS ||
        assert_true(result.success == 1, "SELECT result should be successful") != SUCCESS ||
        assert_true(result.type == DB_RESULT_SELECT, "SELECT result type should match") != SUCCESS ||
        assert_true(result.used_id_index == 1, "WHERE id should mark index usage") != SUCCESS ||
        assert_true(result.column_count == 1, "projection should contain one column") != SUCCESS ||
        assert_true(strcmp(result.columns[0], "name") == 0,
                    "projected column name should be preserved") != SUCCESS ||
        assert_true(result.row_count == 1, "id lookup should return one row") != SUCCESS ||
        assert_true(strcmp(result.rows[0][0], "Bob") == 0,
                    "id lookup should return Bob") != SUCCESS) {
        db_result_free(&result);
        db_engine_shutdown(&engine);
        return EXIT_FAILURE;
    }
    executor_render_result_for_cli(&result);
    db_result_free(&result);

    db_result_init(&result);
    if (assert_true(db_execute_sql(&engine,
                                   "SELECT name, age FROM facade_users WHERE age >= 25;",
                                   &result) == SUCCESS,
                    "linear scan SELECT should succeed") != SUCCESS ||
        assert_true(result.used_id_index == 0, "non-id WHERE should avoid index flag") != SUCCESS ||
        assert_true(result.column_count == 2, "second SELECT should return two columns") != SUCCESS ||
        assert_true(result.row_count == 2, "age scan should return two rows") != SUCCESS ||
        assert_true(strcmp(result.rows[0][0], "Alice") == 0,
                    "first scanned row should be Alice") != SUCCESS ||
        assert_true(strcmp(result.rows[1][0], "Bob") == 0,
                    "second scanned row should be Bob") != SUCCESS) {
        db_result_free(&result);
        db_engine_shutdown(&engine);
        return EXIT_FAILURE;
    }
    db_result_free(&result);

    if (assert_true(tokenizer_get_cache_entry_count() > 0,
                    "tokenizer cache should be populated after facade execution") != SUCCESS) {
        db_engine_shutdown(&engine);
        return EXIT_FAILURE;
    }

    db_engine_shutdown(&engine);
    if (assert_true(tokenizer_get_cache_entry_count() == 0,
                    "db_engine_shutdown should clear tokenizer cache") != SUCCESS) {
        return EXIT_FAILURE;
    }

    puts("[PASS] db_engine_facade");
    return EXIT_SUCCESS;
}
