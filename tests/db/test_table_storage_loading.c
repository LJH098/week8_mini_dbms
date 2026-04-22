#include "storage.h"
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

static void prepare_insert(InsertStatement *stmt, const char *table_name,
                           const char *name, const char *age) {
    memset(stmt, 0, sizeof(*stmt));
    snprintf(stmt->table_name, sizeof(stmt->table_name), "%s", table_name);
    stmt->column_count = 2;
    snprintf(stmt->columns[0], sizeof(stmt->columns[0]), "name");
    snprintf(stmt->columns[1], sizeof(stmt->columns[1]), "age");
    snprintf(stmt->values[0], sizeof(stmt->values[0]), "%s", name);
    snprintf(stmt->values[1], sizeof(stmt->values[1]), "%s", age);
}

int main(void) {
    InsertStatement stmt;
    TableRuntime *table;
    char **row;
    int row_index;
    int row_count;
    int col_count;
    char ***rows;

    remove("data/runtime_orders.csv");
    table_runtime_cleanup();

    prepare_insert(&stmt, "runtime_orders", "Alice", "31");
    if (assert_true(storage_insert("runtime_orders", &stmt) == SUCCESS,
                    "storage_insert should create persisted test table") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_insert(&stmt, "runtime_orders", "Bob", "27");
    if (assert_true(storage_insert("runtime_orders", &stmt) == SUCCESS,
                    "storage_insert should append persisted test row") != SUCCESS) {
        return EXIT_FAILURE;
    }

    table = table_get_or_load("runtime_orders");
    if (assert_true(table != NULL, "table_get_or_load should return runtime table") != SUCCESS ||
        assert_true(table->loaded == 0, "runtime should start unloaded before storage load") != SUCCESS ||
        assert_true(table_load_from_storage_if_needed(table, "runtime_orders") == SUCCESS,
                    "table should load from storage on demand") != SUCCESS ||
        assert_true(table->loaded == 1, "runtime should be marked loaded after storage load") != SUCCESS ||
        assert_true(table->row_count == 2, "two rows should be loaded from storage") != SUCCESS ||
        assert_true(table->next_id == 3, "next_id should advance from persisted ids") != SUCCESS ||
        assert_true(table->id_index_root != NULL, "id index should be rebuilt after load") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    row = table_get_row_by_slot(table, 1);
    if (assert_true(row != NULL, "loaded row should be accessible by slot") != SUCCESS ||
        assert_true(strcmp(row[0], "2") == 0, "second loaded row should keep persisted id") != SUCCESS ||
        assert_true(strcmp(row[1], "Bob") == 0, "second loaded row should keep persisted name") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    if (assert_true(table_load_from_storage_if_needed(table, "runtime_orders") == SUCCESS,
                    "reloading an already loaded table should be a no-op") != SUCCESS ||
        assert_true(table->row_count == 2, "memory hit should keep row count stable") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    prepare_insert(&stmt, "runtime_orders", "Charlie", "29");
    if (assert_true(table_insert_row(table, &stmt, &row_index) == SUCCESS,
                    "table_insert_row should keep memory and storage in sync") != SUCCESS ||
        assert_true(row_index == 2, "new row should be appended at slot 2") != SUCCESS ||
        assert_true(table->row_count == 3, "runtime row count should grow after write-through insert") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    rows = storage_select("runtime_orders", &row_count, &col_count);
    if (assert_true(rows != NULL, "storage_select should read persisted rows after write-through") != SUCCESS ||
        assert_true(row_count == 3, "storage should contain the newly inserted row") != SUCCESS ||
        assert_true(strcmp(rows[2][1], "Charlie") == 0,
                    "persisted third row should be Charlie") != SUCCESS) {
        storage_free_rows(rows, row_count, col_count);
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }
    storage_free_rows(rows, row_count, col_count);

    table = table_get_or_load("missing_runtime_orders");
    if (assert_true(table != NULL, "switching to missing table should still return runtime object") != SUCCESS ||
        assert_true(table_load_from_storage_if_needed(table, "missing_runtime_orders") == SUCCESS,
                    "missing table should not hard-fail storage load helper") != SUCCESS ||
        assert_true(table->loaded == 0, "missing table should remain unloaded") != SUCCESS ||
        assert_true(table->row_count == 0, "missing table should not load rows") != SUCCESS) {
        table_runtime_cleanup();
        return EXIT_FAILURE;
    }

    table_runtime_cleanup();
    puts("[PASS] table_storage_loading");
    return EXIT_SUCCESS;
}
