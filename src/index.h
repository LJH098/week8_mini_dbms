#ifndef INDEX_H
#define INDEX_H

#include "storage.h"

typedef struct {
    long *items;
    int count;
    int capacity;
} OffsetList;

typedef struct HashNode {
    char *key;
    OffsetList offsets;
    struct HashNode *next;
} HashNode;

typedef struct {
    int bucket_count;
    HashNode **buckets;
} EqualityIndex;

typedef struct {
    char key[MAX_VALUE_LEN];
    long offset;
} RangeEntry;

typedef struct {
    RangeEntry *entries;
    int count;
} RangeIndex;

typedef struct {
    int column_index;
    EqualityIndex equality;
    RangeIndex range;
} TableIndex;

/*
 * 테이블의 한 컬럼에 대해 일시적인 메모리 기반 등호/범위 인덱스를 만든다.
 * 이 인덱스는 조회할 때마다 다시 만들어지며 디스크에는 저장되지 않는다.
 */
int index_build(const TableData *table, int column_index, TableIndex *out_index);

/*
 * 등호 인덱스를 조회한다.
 * 반환된 오프셋 배열은 호출자가 해제해야 한다.
 */
int index_query_equals(const TableIndex *index, const char *value,
                       long **offsets, int *count);

/*
 * 범위 인덱스로 `!=`, `>`, `<`, `>=`, `<=` 조건을 조회한다.
 * 반환된 오프셋 배열은 호출자가 해제해야 한다.
 */
int index_query_range(const TableIndex *index, const char *op, const char *value,
                      long **offsets, int *count);

/*
 * 인덱스가 소유한 모든 동적 메모리를 해제한다.
 */
void index_free(TableIndex *index);

#endif
