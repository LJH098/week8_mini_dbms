#ifndef EXECUTOR_RESULT_H
#define EXECUTOR_RESULT_H

#include "utils.h"

#define MAX_DB_RESULT_MESSAGE_LEN 512

typedef enum {
    DB_RESULT_NONE,
    DB_RESULT_INSERT,
    DB_RESULT_SELECT,
    DB_RESULT_DELETE,
    DB_RESULT_ERROR
} DbResultType;

typedef struct {
    DbResultType type;
    int success;
    int column_count;
    char columns[MAX_COLUMNS][MAX_IDENTIFIER_LEN];
    char ***rows;
    int row_count;
    int rows_affected;
    int used_id_index;
    char message[MAX_DB_RESULT_MESSAGE_LEN];
} DbResult;

/*
 * DbResult를 빈 상태로 초기화한다.
 */
void db_result_init(DbResult *result);

/*
 * DbResult가 소유한 행 메모리를 모두 해제하고 초기 상태로 되돌린다.
 */
void db_result_free(DbResult *result);

/*
 * DbResult 메시지를 덮어쓴다.
 */
int db_result_set_message(DbResult *result, const char *message);

/*
 * DbResult를 실패 상태로 전환하고 에러 메시지를 저장한다.
 */
int db_result_set_error(DbResult *result, const char *message);

#endif
