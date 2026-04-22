#include "table_runtime.h"

#include "bptree.h"
#include "utils.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TableRuntime table_runtime_active;
static int table_runtime_has_active = 0;

/*
 * 런타임이 소유한 행 하나를 해제한다.
 */
static void table_free_owned_row(char **row, int col_count) {
    int i;

    if (row == NULL) {
        return;
    }

    for (i = 0; i < col_count; i++) {
        free(row[i]);
        row[i] = NULL;
    }
    free(row);
}

/*
 * 문자열 열 이름을 대소문자 무시로 비교해 위치를 찾는다.
 */
static int table_find_column_index(const TableRuntime *table, const char *target) {
    int i;

    if (table == NULL || target == NULL) {
        return FAILURE;
    }

    for (i = 0; i < table->col_count; i++) {
        if (utils_equals_ignore_case(table->columns[i], target)) {
            return i;
        }
    }

    return FAILURE;
}

/*
 * 현재 INSERT 문이 런타임 테이블 스키마와 일치하는지 확인한다.
 */
static int table_validate_insert_schema(const TableRuntime *table,
                                        const InsertStatement *stmt) {
    int i;

    if (table == NULL || stmt == NULL) {
        return FAILURE;
    }

    if (stmt->column_count <= 0 || stmt->column_count != table->col_count - 1) {
        fprintf(stderr, "Error: INSERT columns do not match table schema.\n");
        return FAILURE;
    }

    for (i = 0; i < stmt->column_count; i++) {
        if (!utils_equals_ignore_case(table->columns[i + 1], stmt->columns[i])) {
            fprintf(stderr, "Error: INSERT columns do not match table schema.\n");
            return FAILURE;
        }
    }

    return SUCCESS;
}

/*
 * 첫 INSERT 문을 기준으로 id 포함 스키마를 고정한다.
 */
static int table_initialize_schema(TableRuntime *table,
                                   const InsertStatement *stmt) {
    int i;

    if (table == NULL || stmt == NULL) {
        return FAILURE;
    }

    if (stmt->column_count <= 0 || stmt->column_count + 1 > MAX_COLUMNS) {
        fprintf(stderr, "Error: Invalid INSERT column count.\n");
        return FAILURE;
    }

    table->col_count = stmt->column_count + 1;
    if (utils_safe_strcpy(table->columns[0], sizeof(table->columns[0]), "id") != SUCCESS) {
        return FAILURE;
    }

    for (i = 0; i < stmt->column_count; i++) {
        if (utils_equals_ignore_case(stmt->columns[i], "id")) {
            fprintf(stderr, "Error: Explicit id values are not allowed.\n");
            return FAILURE;
        }

        if (utils_safe_strcpy(table->columns[i + 1], sizeof(table->columns[i + 1]),
                              stmt->columns[i]) != SUCCESS) {
            fprintf(stderr, "Error: Column name is too long.\n");
            return FAILURE;
        }
    }

    table->id_column_index = 0;
    table->loaded = 1;
    return SUCCESS;
}

/*
 * INSERT 값으로 새 행 메모리를 만들고 문자열을 복제한다.
 */
static char **table_build_row(const TableRuntime *table, const InsertStatement *stmt,
                              long long next_id) {
    char **row;
    char id_buffer[MAX_VALUE_LEN];
    int i;

    if (table == NULL || stmt == NULL) {
        return NULL;
    }

    row = (char **)calloc((size_t)table->col_count, sizeof(char *));
    if (row == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return NULL;
    }

    snprintf(id_buffer, sizeof(id_buffer), "%lld", next_id);
    row[0] = utils_strdup(id_buffer);
    if (row[0] == NULL) {
        free(row);
        return NULL;
    }

    for (i = 0; i < stmt->column_count; i++) {
        row[i + 1] = utils_strdup(stmt->values[i]);
        if (row[i + 1] == NULL) {
            int j;
            for (j = 0; j <= i; j++) {
                free(row[j]);
            }
            free(row);
            return NULL;
        }
    }

    return row;
}

/*
 * 비교 결과가 WHERE 연산자를 만족하면 1, 아니면 0을 반환한다.
 */
static int table_where_matches(int comparison, const char *op) {
    if (strcmp(op, "=") == 0) {
        return comparison == 0;
    }
    if (strcmp(op, "!=") == 0) {
        return comparison != 0;
    }
    if (strcmp(op, ">") == 0) {
        return comparison > 0;
    }
    if (strcmp(op, ">=") == 0) {
        return comparison >= 0;
    }
    if (strcmp(op, "<") == 0) {
        return comparison < 0;
    }
    if (strcmp(op, "<=") == 0) {
        return comparison <= 0;
    }

    return 0;
}

void table_init(TableRuntime *table) {
    if (table == NULL) {
        return;
    }

    memset(table, 0, sizeof(*table));
    table->id_column_index = 0;
    table->next_id = 1;
}

void table_free(TableRuntime *table) {
    int i;

    if (table == NULL) {
        return;
    }

    bptree_free(table->id_index_root);
    table->id_index_root = NULL;

    if (table->rows != NULL) {
        for (i = 0; i < table->row_count; i++) {
            table_free_owned_row(table->rows[i], table->col_count);
            table->rows[i] = NULL;
        }
        free(table->rows);
    }

    table_init(table);
}

int table_reserve_if_needed(TableRuntime *table) {
    char ****rows_ptr;
    char ***new_rows;
    int new_capacity;

    if (table == NULL) {
        return FAILURE;
    }

    if (table->row_count < table->capacity) {
        return SUCCESS;
    }

    new_capacity = table->capacity == 0 ? INITIAL_ROW_CAPACITY : table->capacity * 2;
    rows_ptr = &table->rows;
    new_rows = (char ***)realloc(*rows_ptr, (size_t)new_capacity * sizeof(char **));
    if (new_rows == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    table->rows = new_rows;
    table->capacity = new_capacity;
    return SUCCESS;
}

TableRuntime *table_get_or_load(const char *table_name) {
    if (table_name == NULL) {
        return NULL;
    }

    if (!table_runtime_has_active) {
        table_init(&table_runtime_active);
        table_runtime_has_active = 1;
    }

    if (table_runtime_active.table_name[0] != '\0' &&
        utils_equals_ignore_case(table_runtime_active.table_name, table_name)) {
        return &table_runtime_active;
    }

    table_free(&table_runtime_active);
    if (utils_safe_strcpy(table_runtime_active.table_name,
                          sizeof(table_runtime_active.table_name),
                          table_name) != SUCCESS) {
        fprintf(stderr, "Error: Table name is too long.\n");
        return NULL;
    }
    return &table_runtime_active;
}

int table_insert_row(TableRuntime *table, const InsertStatement *stmt,
                     int *out_row_index) {
    char **row;
    int row_index;

    if (table == NULL || stmt == NULL || out_row_index == NULL) {
        return FAILURE;
    }

    if (table->table_name[0] == '\0') {
        if (utils_safe_strcpy(table->table_name, sizeof(table->table_name),
                              stmt->table_name) != SUCCESS) {
            fprintf(stderr, "Error: Table name is too long.\n");
            return FAILURE;
        }
    }

    if (!utils_equals_ignore_case(table->table_name, stmt->table_name)) {
        fprintf(stderr, "Error: Active runtime table does not match INSERT target.\n");
        return FAILURE;
    }

    if (!table->loaded) {
        if (table_initialize_schema(table, stmt) != SUCCESS) {
            return FAILURE;
        }
    } else if (table_validate_insert_schema(table, stmt) != SUCCESS) {
        return FAILURE;
    }

    if (table_reserve_if_needed(table) != SUCCESS) {
        return FAILURE;
    }

    row = table_build_row(table, stmt, table->next_id);
    if (row == NULL) {
        return FAILURE;
    }

    if (table->next_id > INT_MAX) {
        fprintf(stderr, "Error: Runtime id exceeds B+ tree key range.\n");
        table_free_owned_row(row, table->col_count);
        return FAILURE;
    }

    row_index = table->row_count;
    table->rows[row_index] = row;
    if (bptree_insert(&table->id_index_root, (int)table->next_id, row_index) != SUCCESS) {
        table_free_owned_row(table->rows[row_index], table->col_count);
        table->rows[row_index] = NULL;
        return FAILURE;
    }

    *out_row_index = row_index;
    table->row_count++;
    table->next_id++;
    return SUCCESS;
}

char **table_get_row_by_slot(const TableRuntime *table, int row_index) {
    if (table == NULL || row_index < 0 || row_index >= table->row_count) {
        return NULL;
    }

    return table->rows[row_index];
}

int table_linear_scan_by_field(const TableRuntime *table,
                               const WhereClause *where,
                               int **out_row_indices, int *out_count) {
    int *matches;
    int match_count;
    int i;
    int where_column_index;

    if (table == NULL || out_row_indices == NULL || out_count == NULL) {
        return FAILURE;
    }

    *out_row_indices = NULL;
    *out_count = 0;

    if (!table->loaded || table->row_count == 0) {
        return SUCCESS;
    }

    matches = (int *)malloc((size_t)table->row_count * sizeof(int));
    if (matches == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    if (where == NULL) {
        for (i = 0; i < table->row_count; i++) {
            matches[i] = i;
        }
        *out_row_indices = matches;
        *out_count = table->row_count;
        return SUCCESS;
    }

    where_column_index = table_find_column_index(table, where->column);
    if (where_column_index == FAILURE) {
        fprintf(stderr, "Error: Column '%s' not found.\n", where->column);
        free(matches);
        return FAILURE;
    }

    match_count = 0;
    for (i = 0; i < table->row_count; i++) {
        int comparison = utils_compare_values(table->rows[i][where_column_index],
                                              where->value);
        if (table_where_matches(comparison, where->op)) {
            matches[match_count++] = i;
        }
    }

    if (match_count == 0) {
        free(matches);
        return SUCCESS;
    }

    *out_row_indices = matches;
    *out_count = match_count;
    return SUCCESS;
}

void table_runtime_cleanup(void) {
    if (!table_runtime_has_active) {
        return;
    }

    table_free(&table_runtime_active);
    table_runtime_has_active = 0;
}
