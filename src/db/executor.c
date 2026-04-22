#include "executor.h"

#include "bptree.h"
#include "table_runtime.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 활성 런타임 스키마에서 컬럼 이름을 대소문자 무시로 찾는다.
 * 컬럼 인덱스를 반환하고, 없으면 FAILURE를 반환한다.
 */
static int executor_find_column_index(const char columns[][MAX_IDENTIFIER_LEN],
                                      int col_count, const char *target) {
    int i;

    for (i = 0; i < col_count; i++) {
        if (utils_equals_ignore_case(columns[i], target)) {
            return i;
        }
    }

    return FAILURE;
}

/*
 * 결과 셀 문자열 하나를 복제한다.
 * NULL 값은 빈 문자열로 처리하며 반환된 메모리는 호출자가 소유한다.
 */
static char *executor_duplicate_cell(const char *value) {
    return utils_strdup(value == NULL ? "" : value);
}

/*
 * SELECT 결과를 담을 바깥쪽 행 배열을 할당한다.
 * 성공 시 rows에 저장하고 SUCCESS를 반환한다.
 */
static int executor_allocate_result_rows(char ****rows, int row_count) {
    if (rows == NULL) {
        return FAILURE;
    }

    if (row_count <= 0) {
        *rows = NULL;
        return SUCCESS;
    }

    *rows = (char ***)malloc((size_t)row_count * sizeof(char **));
    if (*rows == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    return SUCCESS;
}

/*
 * executor 내부 헬퍼가 만든 조회 결과 테이블을 해제한다.
 */
static void executor_free_result_rows(char ***rows, int row_count, int col_count) {
    int i;
    int j;

    if (rows == NULL) {
        return;
    }

    for (i = 0; i < row_count; i++) {
        if (rows[i] == NULL) {
            continue;
        }
        for (j = 0; j < col_count; j++) {
            free(rows[i][j]);
            rows[i][j] = NULL;
        }
        free(rows[i]);
    }

    free(rows);
}

/*
 * 원본 행에서 선택된 컬럼만 복사해 결과 행으로 만든다.
 * 새 결과 행이 모두 할당되면 SUCCESS를 반환한다.
 */
static int executor_copy_projected_row(char ***result_rows, int result_index,
                                       char **source_row,
                                       const int *selected_indices,
                                       int selected_count) {
    int i;

    result_rows[result_index] = (char **)malloc((size_t)selected_count * sizeof(char *));
    if (result_rows[result_index] == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    for (i = 0; i < selected_count; i++) {
        result_rows[result_index][i] =
            executor_duplicate_cell(source_row[selected_indices[i]]);
        if (result_rows[result_index][i] == NULL) {
            int j;

            for (j = 0; j < i; j++) {
                free(result_rows[result_index][j]);
                result_rows[result_index][j] = NULL;
            }
            free(result_rows[result_index]);
            result_rows[result_index] = NULL;
            return FAILURE;
        }
    }

    return SUCCESS;
}

/*
 * SELECT 표 출력용 가로 경계선을 한 줄 출력한다.
 */
static void executor_print_border(const int *widths, int col_count) {
    int i;
    int j;

    for (i = 0; i < col_count; i++) {
        putchar('+');
        for (j = 0; j < widths[i] + 2; j++) {
            putchar('-');
        }
    }
    puts("+");
}

/*
 * 표시 폭을 고려해 MySQL 스타일 표 형태로 조회 결과를 출력한다.
 */
static void executor_print_table(char headers[][MAX_IDENTIFIER_LEN], int header_count,
                                 char ***rows, int row_count) {
    int widths[MAX_COLUMNS];
    int i;
    int j;
    int cell_width;

    for (i = 0; i < header_count; i++) {
        widths[i] = utils_display_width(headers[i]);
    }

    for (i = 0; i < row_count; i++) {
        for (j = 0; j < header_count; j++) {
            cell_width = utils_display_width(rows[i][j]);
            if (cell_width > widths[j]) {
                widths[j] = cell_width;
            }
        }
    }

    executor_print_border(widths, header_count);
    for (i = 0; i < header_count; i++) {
        printf("| ");
        utils_print_padded(stdout, headers[i], widths[i]);
        putchar(' ');
    }
    puts("|");
    executor_print_border(widths, header_count);

    for (i = 0; i < row_count; i++) {
        for (j = 0; j < header_count; j++) {
            printf("| ");
            utils_print_padded(stdout, rows[i][j], widths[j]);
            putchar(' ');
        }
        puts("|");
    }

    executor_print_border(widths, header_count);
}

/*
 * SELECT 대상 컬럼을 런타임 테이블 인덱스와 출력 헤더로 변환한다.
 * 요청된 컬럼이 모두 존재하면 SUCCESS를 반환한다.
 */
static int executor_prepare_projection(const SelectStatement *stmt,
                                       const TableRuntime *table,
                                       int selected_indices[],
                                       char headers[][MAX_IDENTIFIER_LEN],
                                       int *selected_count) {
    int i;
    int column_index;

    if (stmt == NULL || table == NULL || selected_indices == NULL ||
        headers == NULL || selected_count == NULL) {
        return FAILURE;
    }

    if (stmt->column_count == 0) {
        for (i = 0; i < table->col_count; i++) {
            selected_indices[i] = i;
            if (utils_safe_strcpy(headers[i], sizeof(headers[i]),
                                  table->columns[i]) != SUCCESS) {
                fprintf(stderr, "Error: Column name is too long.\n");
                return FAILURE;
            }
        }
        *selected_count = table->col_count;
        return SUCCESS;
    }

    for (i = 0; i < stmt->column_count; i++) {
        column_index = executor_find_column_index(table->columns, table->col_count,
                                                  stmt->columns[i]);
        if (column_index == FAILURE) {
            fprintf(stderr, "Error: Column '%s' not found.\n", stmt->columns[i]);
            return FAILURE;
        }

        selected_indices[i] = column_index;
        if (utils_safe_strcpy(headers[i], sizeof(headers[i]),
                              table->columns[column_index]) != SUCCESS) {
            fprintf(stderr, "Error: Column name is too long.\n");
            return FAILURE;
        }
    }

    *selected_count = stmt->column_count;
    return SUCCESS;
}

/*
 * row_index 목록을 실제 결과 행 배열로 바꿔 반환한다.
 * 성공 시 out_rows의 소유권은 호출자에게 있다.
 */
static int executor_collect_rows_by_indices(const TableRuntime *table,
                                            const int *selected_indices,
                                            int selected_count,
                                            const int *row_indices,
                                            int row_count,
                                            char ****out_rows,
                                            int *out_row_count) {
    int i;
    char ***result_rows;
    char **source_row;

    if (table == NULL || selected_indices == NULL || out_rows == NULL ||
        out_row_count == NULL) {
        return FAILURE;
    }

    if (executor_allocate_result_rows(&result_rows, row_count) != SUCCESS) {
        return FAILURE;
    }

    for (i = 0; i < row_count; i++) {
        source_row = table_get_row_by_slot(table, row_indices[i]);
        if (source_row == NULL) {
            executor_free_result_rows(result_rows, i, selected_count);
            return FAILURE;
        }

        if (executor_copy_projected_row(result_rows, i, source_row,
                                        selected_indices, selected_count) != SUCCESS) {
            executor_free_result_rows(result_rows, i, selected_count);
            return FAILURE;
        }
    }

    *out_rows = result_rows;
    *out_row_count = row_count;
    return SUCCESS;
}

/*
 * WHERE가 없는 SELECT를 위해 모든 행을 결과 행 배열로 복사한다.
 * 성공 시 out_rows의 소유권은 호출자에게 있다.
 */
static int executor_collect_all_rows(const TableRuntime *table,
                                     const int *selected_indices, int selected_count,
                                     char ****out_rows, int *out_row_count) {
    int *row_indices;
    int row_count;
    int status;

    if (table == NULL || selected_indices == NULL || out_rows == NULL ||
        out_row_count == NULL) {
        return FAILURE;
    }

    row_indices = NULL;
    row_count = 0;
    if (table_linear_scan_by_field(table, NULL, &row_indices, &row_count) != SUCCESS) {
        return FAILURE;
    }

    status = executor_collect_rows_by_indices(table, selected_indices, selected_count,
                                              row_indices, row_count,
                                              out_rows, out_row_count);
    free(row_indices);
    return status;
}

/*
 * WHERE 절이 id 등호 비교면 B+ 트리를 사용할 수 있는지 확인한다.
 * 가능하면 조회 키를 out_key에 저장하고 1을 반환한다.
 */
static int executor_can_use_id_index(const TableRuntime *table,
                                     const WhereClause *where,
                                     int *out_key) {
    long long parsed_key;

    if (table == NULL || where == NULL || out_key == NULL) {
        return 0;
    }

    if (!table->loaded || table->id_index_root == NULL) {
        return 0;
    }

    if (strcmp(where->op, "=") != 0 || !utils_is_integer(where->value)) {
        return 0;
    }

    if (!utils_equals_ignore_case(table->columns[table->id_column_index], where->column)) {
        return 0;
    }

    parsed_key = utils_parse_integer(where->value);
    if (parsed_key < 0 || parsed_key > INT_MAX) {
        return 0;
    }

    *out_key = (int)parsed_key;
    return 1;
}

/*
 * id = 상수 WHERE를 B+ 트리 한 번 조회로 처리한다.
 */
static int executor_collect_rows_by_id(const TableRuntime *table, int search_key,
                                       const int *selected_indices, int selected_count,
                                       char ****out_rows, int *out_row_count) {
    int row_index;

    if (table == NULL || selected_indices == NULL || out_rows == NULL ||
        out_row_count == NULL) {
        return FAILURE;
    }

    row_index = 0;
    if (bptree_search(table->id_index_root, search_key, &row_index) != SUCCESS) {
        *out_rows = NULL;
        *out_row_count = 0;
        return SUCCESS;
    }

    return executor_collect_rows_by_indices(table, selected_indices, selected_count,
                                            &row_index, 1, out_rows, out_row_count);
}

/*
 * id 조건이 아닌 SELECT WHERE를 선형 탐색으로 처리한다.
 */
static int executor_collect_rows_by_scan(const TableRuntime *table,
                                         const WhereClause *where,
                                         const int *selected_indices,
                                         int selected_count,
                                         char ****out_rows, int *out_row_count) {
    int *row_indices;
    int row_count;
    int status;

    if (table == NULL || where == NULL || selected_indices == NULL ||
        out_rows == NULL || out_row_count == NULL) {
        return FAILURE;
    }

    row_indices = NULL;
    row_count = 0;
    if (table_linear_scan_by_field(table, where, &row_indices, &row_count) != SUCCESS) {
        return FAILURE;
    }

    status = executor_collect_rows_by_indices(table, selected_indices, selected_count,
                                              row_indices, row_count,
                                              out_rows, out_row_count);
    free(row_indices);
    return status;
}

/*
 * INSERT 문 하나를 메모리 런타임 계층으로 실행하고 결과 메시지를 출력한다.
 */
static int executor_execute_insert(const InsertStatement *stmt) {
    TableRuntime *table;
    int row_index;

    if (stmt == NULL) {
        return FAILURE;
    }

    table = table_get_or_load(stmt->table_name);
    if (table == NULL) {
        return FAILURE;
    }

    if (table_insert_row(table, stmt, &row_index) != SUCCESS) {
        return FAILURE;
    }

    printf("1 row inserted into %s.\n", stmt->table_name);
    return SUCCESS;
}

/*
 * SELECT 문 하나를 메모리 런타임에서 실행하고 표 형태로 출력한다.
 */
static int executor_execute_select(const SelectStatement *stmt) {
    TableRuntime *table;
    int selected_indices[MAX_COLUMNS];
    char headers[MAX_COLUMNS][MAX_IDENTIFIER_LEN];
    int selected_count;
    char ***result_rows;
    int result_row_count;
    int search_key;
    int status;

    if (stmt == NULL) {
        return FAILURE;
    }

    table = table_get_or_load(stmt->table_name);
    if (table == NULL) {
        return FAILURE;
    }

    if (!table->loaded) {
        fprintf(stderr, "Error: Table '%s' not found in runtime.\n", stmt->table_name);
        return FAILURE;
    }

    status = executor_prepare_projection(stmt, table, selected_indices, headers,
                                         &selected_count);
    if (status != SUCCESS) {
        return FAILURE;
    }

    result_rows = NULL;
    result_row_count = 0;
    if (!stmt->has_where) {
        status = executor_collect_all_rows(table, selected_indices, selected_count,
                                           &result_rows, &result_row_count);
    } else if (executor_can_use_id_index(table, &stmt->where, &search_key)) {
        status = executor_collect_rows_by_id(table, search_key, selected_indices,
                                             selected_count, &result_rows,
                                             &result_row_count);
    } else {
        status = executor_collect_rows_by_scan(table, &stmt->where, selected_indices,
                                               selected_count, &result_rows,
                                               &result_row_count);
    }

    if (status != SUCCESS) {
        executor_free_result_rows(result_rows, result_row_count, selected_count);
        return FAILURE;
    }

    executor_print_table(headers, selected_count, result_rows, result_row_count);
    printf("%d row%s selected.\n", result_row_count,
           result_row_count == 1 ? "" : "s");

    executor_free_result_rows(result_rows, result_row_count, selected_count);
    return SUCCESS;
}

/*
 * DELETE는 이번 메모리 런타임 범위에서 지원하지 않는다.
 */
static int executor_execute_delete(const DeleteStatement *stmt) {
    (void)stmt;
    fprintf(stderr, "Error: DELETE is not supported in memory runtime mode.\n");
    return FAILURE;
}

/*
 * 파싱된 SQL 문을 받아 statement.type에 따라 INSERT, SELECT, DELETE로 분기한다.
 */
int executor_execute(const SqlStatement *statement) {
    if (statement == NULL) {
        return FAILURE;
    }

    switch (statement->type) {
        case SQL_INSERT:
            return executor_execute_insert(&statement->insert);
        case SQL_SELECT:
            return executor_execute_select(&statement->select);
        case SQL_DELETE:
            return executor_execute_delete(&statement->delete_stmt);
        default:
            fprintf(stderr, "Error: Unsupported SQL statement type.\n");
            return FAILURE;
    }
}
