#include "storage.h"

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
                           int include_id, const char *id, const char *name,
                           const char *age) {
    memset(stmt, 0, sizeof(*stmt));
    snprintf(stmt->table_name, sizeof(stmt->table_name), "%s", table_name);
    if (include_id) {
        stmt->column_count = 3;
        snprintf(stmt->columns[0], sizeof(stmt->columns[0]), "id");
        snprintf(stmt->columns[1], sizeof(stmt->columns[1]), "name");
        snprintf(stmt->columns[2], sizeof(stmt->columns[2]), "age");
        snprintf(stmt->values[0], sizeof(stmt->values[0]), "%s", id);
        snprintf(stmt->values[1], sizeof(stmt->values[1]), "%s", name);
        snprintf(stmt->values[2], sizeof(stmt->values[2]), "%s", age);
    } else {
        stmt->column_count = 2;
        snprintf(stmt->columns[0], sizeof(stmt->columns[0]), "name");
        snprintf(stmt->columns[1], sizeof(stmt->columns[1]), "age");
        snprintf(stmt->values[0], sizeof(stmt->values[0]), "%s", name);
        snprintf(stmt->values[1], sizeof(stmt->values[1]), "%s", age);
    }
}

static void prepare_delete(DeleteStatement *stmt, const char *table_name,
                           int has_where, const char *column, const char *op,
                           const char *value) {
    memset(stmt, 0, sizeof(*stmt));
    snprintf(stmt->table_name, sizeof(stmt->table_name), "%s", table_name);
    stmt->has_where = has_where;
    if (has_where) {
        snprintf(stmt->where.column, sizeof(stmt->where.column), "%s", column);
        snprintf(stmt->where.op, sizeof(stmt->where.op), "%s", op);
        snprintf(stmt->where.value, sizeof(stmt->where.value), "%s", value);
    }
}

int main(void) {
    InsertStatement stmt;
    DeleteStatement delete_stmt;
    char columns[MAX_COLUMNS][MAX_IDENTIFIER_LEN];
    int col_count;
    int row_count;
    int deleted_count;
    char ***rows;
    TableData table;
    char **row;

    remove("data/storage_users.csv");

    prepare_insert(&stmt, "storage_users", 0, "1", "Alice", "30");
    if (assert_true(storage_insert("storage_users", &stmt) == SUCCESS,
                    "storage_insert should create table with auto id") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_insert(&stmt, "storage_users", 0, "2", "Lee, Jr.", "28");
    if (assert_true(storage_insert("storage_users", &stmt) == SUCCESS,
                    "storage_insert should append row with next auto id") != SUCCESS) {
        return EXIT_FAILURE;
    }

    prepare_insert(&stmt, "storage_users", 1, "2", "Duplicate", "40");
    if (assert_true(storage_insert("storage_users", &stmt) == FAILURE,
                    "storage_insert should reject duplicate id values") != SUCCESS) {
        return EXIT_FAILURE;
    }

    if (assert_true(storage_get_columns("storage_users", columns, &col_count) == SUCCESS,
                    "storage_get_columns should read header") != SUCCESS ||
        assert_true(col_count == 3, "header column count should be 3") != SUCCESS ||
        assert_true(strcmp(columns[1], "name") == 0, "second column should be name") != SUCCESS) {
        return EXIT_FAILURE;
    }

    rows = storage_select("storage_users", &row_count, &col_count);
    if (assert_true(rows != NULL, "storage_select should read rows") != SUCCESS ||
        assert_true(row_count == 2, "row count should stay 2 after duplicate reject") != SUCCESS ||
        assert_true(strcmp(rows[0][0], "1") == 0, "first row should receive id 1") != SUCCESS ||
        assert_true(strcmp(rows[1][0], "2") == 0, "second row should receive id 2") != SUCCESS ||
        assert_true(strcmp(rows[1][1], "Lee, Jr.") == 0,
                    "CSV parser should preserve commas in strings") != SUCCESS) {
        storage_free_rows(rows, row_count, col_count);
        return EXIT_FAILURE;
    }
    storage_free_rows(rows, row_count, col_count);

    if (assert_true(storage_load_table("storage_users", &table) == SUCCESS,
                    "storage_load_table should load offsets") != SUCCESS ||
        assert_true(table.offsets != NULL, "offset array should exist") != SUCCESS ||
        assert_true(table.row_count == 2, "loaded table row count should be 2") != SUCCESS) {
        storage_free_table(&table);
        return EXIT_FAILURE;
    }

    if (assert_true(storage_read_row_at_offset("storage_users", table.offsets[1],
                                               table.col_count, &row) == SUCCESS,
                    "storage_read_row_at_offset should read indexed row") != SUCCESS ||
        assert_true(strcmp(row[1], "Lee, Jr.") == 0,
                    "offset read should return second row") != SUCCESS) {
        storage_free_row(row, table.col_count);
        storage_free_table(&table);
        return EXIT_FAILURE;
    }

    storage_free_row(row, table.col_count);
    storage_free_table(&table);

    prepare_delete(&delete_stmt, "storage_users", 1, "name", "=", "Alice");
    if (assert_true(storage_delete("storage_users", &delete_stmt, &deleted_count) == SUCCESS,
                    "storage_delete should delete matching rows") != SUCCESS ||
        assert_true(deleted_count == 1, "storage_delete should report one deleted row") != SUCCESS) {
        return EXIT_FAILURE;
    }

    rows = storage_select("storage_users", &row_count, &col_count);
    if (assert_true(rows != NULL, "storage_select should still work after delete") != SUCCESS ||
        assert_true(row_count == 1, "one row should remain after DELETE WHERE") != SUCCESS ||
        assert_true(strcmp(rows[0][1], "Lee, Jr.") == 0,
                    "remaining row should be Lee, Jr.") != SUCCESS) {
        storage_free_rows(rows, row_count, col_count);
        return EXIT_FAILURE;
    }
    storage_free_rows(rows, row_count, col_count);

    prepare_delete(&delete_stmt, "storage_users", 0, "", "", "");
    if (assert_true(storage_delete("storage_users", &delete_stmt, &deleted_count) == SUCCESS,
                    "storage_delete should support full table delete") != SUCCESS ||
        assert_true(deleted_count == 1, "full delete should remove remaining row") != SUCCESS) {
        return EXIT_FAILURE;
    }

    rows = storage_select("storage_users", &row_count, &col_count);
    if (assert_true(rows != NULL, "storage_select should read empty table") != SUCCESS ||
        assert_true(row_count == 0, "no rows should remain after full delete") != SUCCESS) {
        storage_free_rows(rows, row_count, col_count);
        return EXIT_FAILURE;
    }
    storage_free_rows(rows, row_count, col_count);

    puts("[PASS] storage");
    return EXIT_SUCCESS;
}
