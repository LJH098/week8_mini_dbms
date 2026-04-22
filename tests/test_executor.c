#include "bptree.h"
#include "executor.h"
#include "table_runtime.h"

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

static void prepare_insert(SqlStatement *statement, const char *table_name,
                           int include_id, const char *id,
                           const char *name, const char *age) {
    memset(statement, 0, sizeof(*statement));
    statement->type = SQL_INSERT;
    snprintf(statement->insert.table_name, sizeof(statement->insert.table_name),
             "%s", table_name);

    if (include_id) {
        statement->insert.column_count = 3;
        snprintf(statement->insert.columns[0], sizeof(statement->insert.columns[0]), "id");
        snprintf(statement->insert.columns[1], sizeof(statement->insert.columns[1]), "name");
        snprintf(statement->insert.columns[2], sizeof(statement->insert.columns[2]), "age");
        snprintf(statement->insert.values[0], sizeof(statement->insert.values[0]), "%s", id);
        snprintf(statement->insert.values[1], sizeof(statement->insert.values[1]), "%s", name);
        snprintf(statement->insert.values[2], sizeof(statement->insert.values[2]), "%s", age);
        return;
    }

    statement->insert.column_count = 2;
    snprintf(statement->insert.columns[0], sizeof(statement->insert.columns[0]), "name");
    snprintf(statement->insert.columns[1], sizeof(statement->insert.columns[1]), "age");
    snprintf(statement->insert.values[0], sizeof(statement->insert.values[0]), "%s", name);
    snprintf(statement->insert.values[1], sizeof(statement->insert.values[1]), "%s", age);
}

static void prepare_select(SqlStatement *statement, const char *table_name,
                           const char *select_column, const char *where_column,
                           const char *op, const char *value) {
    memset(statement, 0, sizeof(*statement));
    statement->type = SQL_SELECT;
    snprintf(statement->select.table_name, sizeof(statement->select.table_name),
             "%s", table_name);
    statement->select.column_count = 1;
    snprintf(statement->select.columns[0], sizeof(statement->select.columns[0]),
             "%s", select_column);
    statement->select.has_where = 1;
    snprintf(statement->select.where.column, sizeof(statement->select.where.column),
             "%s", where_column);
    snprintf(statement->select.where.op, sizeof(statement->select.where.op), "%s", op);
    snprintf(statement->select.where.value, sizeof(statement->select.where.value),
             "%s", value);
}

static void prepare_delete(SqlStatement *statement, const char *table_name,
                           const char *name) {
    memset(statement, 0, sizeof(*statement));
    statement->type = SQL_DELETE;
    snprintf(statement->delete_stmt.table_name,
             sizeof(statement->delete_stmt.table_name), "%s", table_name);
    statement->delete_stmt.has_where = 1;
    snprintf(statement->delete_stmt.where.column,
             sizeof(statement->delete_stmt.where.column), "name");
    snprintf(statement->delete_stmt.where.op,
             sizeof(statement->delete_stmt.where.op), "=");
    snprintf(statement->delete_stmt.where.value,
             sizeof(statement->delete_stmt.where.value), "%s", name);
}

int main(void) {
    SqlStatement statement;
    TableRuntime *table;
    char **row;
    int row_index;

    table_runtime_cleanup();

    prepare_insert(&statement, "executor_users", 0, "", "Alice", "30");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should insert first row into runtime") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_insert(&statement, "executor_users", 0, "", "Bob", "25");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should insert second row into runtime") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    table = table_get_or_load("executor_users");
    if (assert_true(table != NULL, "runtime table should be available after insert") != SUCCESS ||
        assert_true(table->row_count == 2, "runtime should contain two rows") != SUCCESS ||
        assert_true(table->id_index_root != NULL, "id index should be built on insert") != SUCCESS ||
        assert_true(bptree_search(table->id_index_root, 2, &row_index) == SUCCESS,
                    "id index should find the second row") != SUCCESS ||
        assert_true(row_index == 1, "id 2 should map to row_index 1") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    prepare_select(&statement, "executor_users", "name", "id", "=", "2");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should use id index for id equality select") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    prepare_select(&statement, "executor_users", "name", "age", ">=", "27");
    if (assert_true(executor_execute(&statement) == SUCCESS,
                    "executor should use linear scan for non-id select") != SUCCESS ||
        assert_true(executor_execute(&statement) == SUCCESS,
                    "repeated select should stay stable") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    prepare_insert(&statement, "executor_users", 1, "7", "Charlie", "35");
    if (assert_true(executor_execute(&statement) == FAILURE,
                    "executor should reject explicit id inserts") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    prepare_delete(&statement, "executor_users", "Bob");
    if (assert_true(executor_execute(&statement) == FAILURE,
                    "executor should reject delete in memory runtime mode") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    table = table_get_or_load("executor_users");
    row = table_get_row_by_slot(table, 1);
    if (assert_true(row != NULL, "runtime row lookup should still succeed") != SUCCESS ||
        assert_true(strcmp(row[0], "2") == 0, "second row id should remain 2") != SUCCESS ||
        assert_true(strcmp(row[1], "Bob") == 0, "Bob should remain after delete failure") != SUCCESS ||
        assert_true(table->row_count == 2, "failed operations should not change row count") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    table_runtime_cleanup();
    puts("[PASS] executor");
    return EXIT_SUCCESS;
}
