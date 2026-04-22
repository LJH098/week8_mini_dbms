#include "executor_result.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*
 * DbResult 내부 행 배열을 해제한다.
 */
static void db_result_free_rows(char ***rows, int row_count, int col_count) {
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

void db_result_init(DbResult *result) {
    if (result == NULL) {
        return;
    }

    memset(result, 0, sizeof(*result));
    result->type = DB_RESULT_NONE;
}

void db_result_free(DbResult *result) {
    if (result == NULL) {
        return;
    }

    db_result_free_rows(result->rows, result->row_count, result->column_count);
    db_result_init(result);
}

int db_result_set_message(DbResult *result, const char *message) {
    if (result == NULL) {
        return FAILURE;
    }

    if (message == NULL) {
        result->message[0] = '\0';
        return SUCCESS;
    }

    snprintf(result->message, sizeof(result->message), "%s", message);
    return SUCCESS;
}

int db_result_set_error(DbResult *result, const char *message) {
    if (result == NULL) {
        return FAILURE;
    }

    db_result_free_rows(result->rows, result->row_count, result->column_count);
    result->rows = NULL;
    result->row_count = 0;
    result->column_count = 0;
    result->rows_affected = 0;
    result->used_id_index = 0;
    result->type = DB_RESULT_ERROR;
    result->success = 0;
    db_result_set_message(result, message);
    return FAILURE;
}
