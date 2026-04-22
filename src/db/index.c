#include "index.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 문자열 키 하나를 해시 값으로 변환해 등호 인덱스 버킷 위치를 정한다.
 */
static unsigned long index_hash_string(const char *text) {
    unsigned long hash;
    size_t i;

    hash = 5381UL;
    for (i = 0; text[i] != '\0'; i++) {
        hash = ((hash << 5U) + hash) + (unsigned char)text[i];
    }

    return hash;
}

/*
 * 동적 오프셋 목록에 파일 오프셋 하나를 추가한다.
 * list에 저장되면 SUCCESS를 반환한다.
 */
static int index_offset_list_append(OffsetList *list, long offset) {
    long *new_items;

    if (list == NULL) {
        return FAILURE;
    }

    if (list->items == NULL) {
        list->capacity = 4;
        list->items = (long *)malloc((size_t)list->capacity * sizeof(long));
        if (list->items == NULL) {
            fprintf(stderr, "Error: Failed to allocate memory.\n");
            return FAILURE;
        }
    } else if (list->count >= list->capacity) {
        list->capacity *= 2;
        new_items = (long *)realloc(list->items,
                                    (size_t)list->capacity * sizeof(long));
        if (new_items == NULL) {
            fprintf(stderr, "Error: Failed to allocate memory.\n");
            return FAILURE;
        }
        list->items = new_items;
    }

    list->items[list->count++] = offset;
    return SUCCESS;
}

/*
 * 키와 오프셋 한 쌍을 등호 해시 인덱스에 넣는다.
 */
static int index_add_hash_entry(EqualityIndex *equality, const char *key,
                                long offset) {
    unsigned long bucket_index;
    HashNode *node;

    if (equality == NULL || key == NULL) {
        return FAILURE;
    }

    bucket_index = index_hash_string(key) % (unsigned long)equality->bucket_count;
    node = equality->buckets[bucket_index];
    while (node != NULL) {
        if (strcmp(node->key, key) == 0) {
            return index_offset_list_append(&node->offsets, offset);
        }
        node = node->next;
    }

    node = (HashNode *)calloc(1, sizeof(HashNode));
    if (node == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    node->key = utils_strdup(key);
    if (node->key == NULL) {
        free(node);
        return FAILURE;
    }

    if (index_offset_list_append(&node->offsets, offset) != SUCCESS) {
        free(node->key);
        free(node);
        return FAILURE;
    }

    node->next = equality->buckets[bucket_index];
    equality->buckets[bucket_index] = node;
    return SUCCESS;
}

/*
 * 범위 인덱스 엔트리를 값 기준으로 정렬하고, 같으면 원본 오프셋 기준으로 정렬한다.
 */
static int index_compare_range_entries(const void *lhs, const void *rhs) {
    const RangeEntry *left = (const RangeEntry *)lhs;
    const RangeEntry *right = (const RangeEntry *)rhs;
    int comparison = utils_compare_values(left->key, right->key);

    if (comparison != 0) {
        return comparison;
    }

    if (left->offset < right->offset) {
        return -1;
    }
    if (left->offset > right->offset) {
        return 1;
    }
    return 0;
}

/*
 * 정렬된 범위 엔트리 하나와 조회 값을 비교한다.
 */
static int index_compare_entry_to_value(const RangeEntry *entry, const char *value) {
    return utils_compare_values(entry->key, value);
}

/*
 * key가 value보다 작지 않은 첫 위치를 반환한다.
 */
static int index_lower_bound(const RangeEntry *entries, int count,
                             const char *value) {
    int left;
    int right;
    int middle;

    left = 0;
    right = count;
    while (left < right) {
        middle = left + (right - left) / 2;
        if (index_compare_entry_to_value(&entries[middle], value) < 0) {
            left = middle + 1;
        } else {
            right = middle;
        }
    }

    return left;
}

/*
 * key가 value보다 큰 첫 위치를 반환한다.
 */
static int index_upper_bound(const RangeEntry *entries, int count,
                             const char *value) {
    int left;
    int right;
    int middle;

    left = 0;
    right = count;
    while (left < right) {
        middle = left + (right - left) / 2;
        if (index_compare_entry_to_value(&entries[middle], value) <= 0) {
            left = middle + 1;
        } else {
            right = middle;
        }
    }

    return left;
}

/*
 * 조회 결과용 오프셋 배열을 새 메모리에 복사한다.
 * 반환된 배열은 호출자가 소유한다.
 */
static int index_copy_offsets(const long *source, int count, long **offsets) {
    if (count <= 0) {
        *offsets = NULL;
        return SUCCESS;
    }

    *offsets = (long *)malloc((size_t)count * sizeof(long));
    if (*offsets == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    memcpy(*offsets, source, (size_t)count * sizeof(long));
    return SUCCESS;
}

/*
 * 메모리에 올라온 테이블의 한 컬럼에 대해 등호/범위 인덱스를 만든다.
 * 생성된 out_index는 호출자가 index_free()로 해제해야 한다.
 */
int index_build(const TableData *table, int column_index, TableIndex *out_index) {
    int i;

    if (table == NULL || out_index == NULL || column_index < 0 ||
        column_index >= table->col_count) {
        return FAILURE;
    }

    memset(out_index, 0, sizeof(*out_index));
    out_index->column_index = column_index;
    out_index->equality.bucket_count = HASH_BUCKET_COUNT;
    out_index->equality.buckets = (HashNode **)calloc(
        (size_t)out_index->equality.bucket_count, sizeof(HashNode *));
    if (out_index->equality.buckets == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    if (table->row_count > 0) {
        out_index->range.entries = (RangeEntry *)malloc(
            (size_t)table->row_count * sizeof(RangeEntry));
        if (out_index->range.entries == NULL) {
            fprintf(stderr, "Error: Failed to allocate memory.\n");
            index_free(out_index);
            return FAILURE;
        }
    }

    out_index->range.count = table->row_count;
    for (i = 0; i < table->row_count; i++) {
        if (index_add_hash_entry(&out_index->equality,
                                 table->rows[i][column_index],
                                 table->offsets[i]) != SUCCESS) {
            index_free(out_index);
            return FAILURE;
        }

        if (utils_safe_strcpy(out_index->range.entries[i].key,
                              sizeof(out_index->range.entries[i].key),
                              table->rows[i][column_index]) != SUCCESS) {
            fprintf(stderr, "Error: Indexed value is too long.\n");
            index_free(out_index);
            return FAILURE;
        }
        out_index->range.entries[i].offset = table->offsets[i];
    }

    qsort(out_index->range.entries, (size_t)out_index->range.count,
          sizeof(RangeEntry), index_compare_range_entries);
    return SUCCESS;
}

/*
 * 등호 해시 인덱스에서 값 하나를 조회한다.
 * 반환된 오프셋 배열은 호출자가 해제해야 한다.
 */
int index_query_equals(const TableIndex *index, const char *value,
                       long **offsets, int *count) {
    unsigned long bucket_index;
    HashNode *node;

    if (index == NULL || value == NULL || offsets == NULL || count == NULL ||
        index->equality.buckets == NULL) {
        return FAILURE;
    }

    bucket_index = index_hash_string(value) %
                   (unsigned long)index->equality.bucket_count;
    node = index->equality.buckets[bucket_index];
    while (node != NULL) {
        if (strcmp(node->key, value) == 0) {
            *count = node->offsets.count;
            return index_copy_offsets(node->offsets.items, node->offsets.count,
                                      offsets);
        }
        node = node->next;
    }

    *count = 0;
    *offsets = NULL;
    return SUCCESS;
}

/*
 * 범위 인덱스로 `!=`, `>`, `>=`, `<`, `<=` 조건을 조회한다.
 * 반환된 오프셋 배열은 호출자가 해제해야 한다.
 */
int index_query_range(const TableIndex *index, const char *op, const char *value,
                      long **offsets, int *count) {
    int start;
    int end;
    int i;
    int result_count;
    long *result_offsets;

    if (index == NULL || op == NULL || value == NULL || offsets == NULL ||
        count == NULL) {
        return FAILURE;
    }

    *offsets = NULL;
    *count = 0;

    if (strcmp(op, "!=") == 0) {
        if (index->range.count == 0) {
            return SUCCESS;
        }

        result_offsets = (long *)malloc((size_t)index->range.count * sizeof(long));
        if (result_offsets == NULL) {
            fprintf(stderr, "Error: Failed to allocate memory.\n");
            return FAILURE;
        }

        result_count = 0;
        for (i = 0; i < index->range.count; i++) {
            if (utils_compare_values(index->range.entries[i].key, value) != 0) {
                result_offsets[result_count++] = index->range.entries[i].offset;
            }
        }

        if (result_count == 0) {
            free(result_offsets);
            return SUCCESS;
        }

        *offsets = result_offsets;
        *count = result_count;
        return SUCCESS;
    }

    start = 0;
    end = index->range.count;
    if (strcmp(op, ">") == 0) {
        start = index_upper_bound(index->range.entries, index->range.count, value);
    } else if (strcmp(op, ">=") == 0) {
        start = index_lower_bound(index->range.entries, index->range.count, value);
    } else if (strcmp(op, "<") == 0) {
        end = index_lower_bound(index->range.entries, index->range.count, value);
    } else if (strcmp(op, "<=") == 0) {
        end = index_upper_bound(index->range.entries, index->range.count, value);
    } else {
        fprintf(stderr, "Error: Unsupported WHERE operator '%s'.\n", op);
        return FAILURE;
    }

    result_count = end - start;
    if (result_count <= 0) {
        return SUCCESS;
    }

    result_offsets = (long *)malloc((size_t)result_count * sizeof(long));
    if (result_offsets == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    for (i = 0; i < result_count; i++) {
        result_offsets[i] = index->range.entries[start + i].offset;
    }

    *offsets = result_offsets;
    *count = result_count;
    return SUCCESS;
}

/*
 * 생성된 인덱스가 소유한 동적 메모리를 모두 해제한다.
 */
void index_free(TableIndex *index) {
    int i;
    HashNode *node;
    HashNode *next;

    if (index == NULL) {
        return;
    }

    if (index->equality.buckets != NULL) {
        for (i = 0; i < index->equality.bucket_count; i++) {
            node = index->equality.buckets[i];
            while (node != NULL) {
                next = node->next;
                free(node->key);
                node->key = NULL;
                free(node->offsets.items);
                node->offsets.items = NULL;
                free(node);
                node = next;
            }
        }
        free(index->equality.buckets);
        index->equality.buckets = NULL;
    }

    free(index->range.entries);
    index->range.entries = NULL;
    index->range.count = 0;
}
