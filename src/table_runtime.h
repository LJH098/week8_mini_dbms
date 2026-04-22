#ifndef TABLE_RUNTIME_H
#define TABLE_RUNTIME_H

#include "parser.h"

struct BPTreeNode;

typedef struct {
    char table_name[MAX_IDENTIFIER_LEN];
    int col_count;
    char columns[MAX_COLUMNS][MAX_IDENTIFIER_LEN];
    char ***rows;
    int row_count;
    int capacity;
    int id_column_index;
    long long next_id;
    struct BPTreeNode *id_index_root;
    int loaded;
} TableRuntime;

/*
 * 런타임 테이블을 빈 상태로 초기화한다.
 */
void table_init(TableRuntime *table);

/*
 * 런타임 테이블이 소유한 행 메모리를 모두 해제한다.
 */
void table_free(TableRuntime *table);

/*
 * 다음 행 삽입을 위해 행 배열 용량을 확보한다.
 */
int table_reserve_if_needed(TableRuntime *table);

/*
 * 활성 테이블 하나를 이름 기준으로 가져오거나 새로 초기화한다.
 * 반환된 포인터는 모듈 내부 정적 저장소를 가리킨다.
 */
TableRuntime *table_get_or_load(const char *table_name);

/*
 * INSERT 문 기준으로 auto id를 붙여 메모리 행을 추가한다.
 * 성공 시 새 row_index를 out_row_index에 저장한다.
 */
int table_insert_row(TableRuntime *table, const InsertStatement *stmt,
                     int *out_row_index);

/*
 * row_index로 행 포인터를 반환한다.
 * 범위를 벗어나면 NULL을 반환한다.
 */
char **table_get_row_by_slot(const TableRuntime *table, int row_index);

/*
 * WHERE 절 조건 또는 전체 스캔 결과의 row_index 목록을 반환한다.
 * 반환된 배열은 호출자가 free()로 해제해야 한다.
 */
int table_linear_scan_by_field(const TableRuntime *table,
                               const WhereClause *where,
                               int **out_row_indices, int *out_count);

/*
 * 활성 런타임 테이블을 종료 시 정리한다.
 */
void table_runtime_cleanup(void);

#endif
