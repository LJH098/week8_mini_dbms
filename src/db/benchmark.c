#include "benchmark.h"

#include "bptree.h"
#include "table_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct
{
    char ***rows;
    int row_count;
    int capacity;
} BenchmarkRowStore;

typedef enum
{
    BENCHMARK_LOOKUP_AVERAGE,
    BENCHMARK_LOOKUP_WORST,
    BENCHMARK_LOOKUP_RANDOM
} BenchmarkLookupCase;

typedef struct
{
    const char *label;
    int query_count;
    double bptree_ms;
    double linear_scan_ms;
} BenchmarkLookupResult;

/*
 * 벤치마크용 단순 행 저장소의 메모리를 모두 해제한다.
 */
static void benchmark_plain_store_free(BenchmarkRowStore *store)
{
    int i;
    int j;

    if (store == NULL || store->rows == NULL)
    {
        return;
    }

    for (i = 0; i < store->row_count; i++)
    {
        if (store->rows[i] == NULL)
        {
            continue;
        }
        for (j = 0; j < 3; j++)
        {
            free(store->rows[i][j]);
            store->rows[i][j] = NULL;
        }
        free(store->rows[i]);
    }

    free(store->rows);
    store->rows = NULL;
    store->row_count = 0;
    store->capacity = 0;
}

/*
 * 벤치마크용 단순 행 저장소에 용량을 확보한다.
 */
static int benchmark_plain_store_reserve(BenchmarkRowStore *store)
{
    char ***new_rows;
    int new_capacity;

    if (store == NULL)
    {
        return FAILURE;
    }

    if (store->row_count < store->capacity)
    {
        return SUCCESS;
    }

    new_capacity = store->capacity == 0 ? INITIAL_ROW_CAPACITY : store->capacity * 2;
    new_rows = (char ***)realloc(store->rows, (size_t)new_capacity * sizeof(char **));
    if (new_rows == NULL)
    {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    store->rows = new_rows;
    store->capacity = new_capacity;
    return SUCCESS;
}

/*
 * 벤치마크용 no-index 저장소에 행 하나를 추가한다.
 */
static int benchmark_plain_store_append(BenchmarkRowStore *store, int id,
                                        const char *name, const char *age)
{
    char **row;
    char id_buffer[MAX_VALUE_LEN];
    int i;

    if (store == NULL || name == NULL || age == NULL)
    {
        return FAILURE;
    }

    if (benchmark_plain_store_reserve(store) != SUCCESS)
    {
        return FAILURE;
    }

    row = (char **)calloc(3, sizeof(char *));
    if (row == NULL)
    {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    snprintf(id_buffer, sizeof(id_buffer), "%d", id);
    row[0] = utils_strdup(id_buffer);
    row[1] = utils_strdup(name);
    row[2] = utils_strdup(age);
    if (row[0] == NULL || row[1] == NULL || row[2] == NULL)
    {
        for (i = 0; i < 3; i++)
        {
            free(row[i]);
            row[i] = NULL;
        }
        free(row);
        return FAILURE;
    }

    store->rows[store->row_count++] = row;
    return SUCCESS;
}

/*
 * 각 synthetic row의 name/age 문자열을 채운다.
 */
static void benchmark_build_values(int index, char *name, size_t name_size,
                                   char *age, size_t age_size)
{
    snprintf(name, name_size, "user_%d", index + 1);
    snprintf(age, age_size, "%d", 20 + (index % 50));
}

/*
 * runtime insert에 재사용할 INSERT 문 구조를 준비한다.
 */
static void benchmark_prepare_insert_stmt(InsertStatement *stmt)
{
    memset(stmt, 0, sizeof(*stmt));
    snprintf(stmt->table_name, sizeof(stmt->table_name), "benchmark_users");
    stmt->column_count = 2;
    snprintf(stmt->columns[0], sizeof(stmt->columns[0]), "name");
    snprintf(stmt->columns[1], sizeof(stmt->columns[1]), "age");
}

/*
 * 인덱스가 있는 메모리 런타임 삽입 시간을 측정한다.
 */
static int benchmark_measure_indexed_insert(const BenchmarkConfig *config,
                                            TableRuntime *table,
                                            int **out_inserted_ids,
                                            double *elapsed_ms)
{
    InsertStatement stmt;
    char name[MAX_VALUE_LEN];
    char age[MAX_VALUE_LEN];
    char **row;
    int *inserted_ids;
    int row_index;
    int id_key;
    int i;
    clock_t start;
    clock_t end;

    if (config == NULL || table == NULL || out_inserted_ids == NULL ||
        elapsed_ms == NULL)
    {
        return FAILURE;
    }

    benchmark_prepare_insert_stmt(&stmt);
    table_init(table);
    *out_inserted_ids = NULL;
    inserted_ids = (int *)malloc((size_t)config->row_count * sizeof(int));
    if (inserted_ids == NULL)
    {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        return FAILURE;
    }

    start = clock();
    for (i = 0; i < config->row_count; i++)
    {
        benchmark_build_values(i, name, sizeof(name), age, sizeof(age));
        snprintf(stmt.values[0], sizeof(stmt.values[0]), "%s", name);
        snprintf(stmt.values[1], sizeof(stmt.values[1]), "%s", age);
        if (table_insert_row(table, &stmt, &row_index) != SUCCESS)
        {
            free(inserted_ids);
            table_free(table);
            return FAILURE;
        }

        row = table_get_row_by_slot(table, row_index);
        if (row == NULL || !utils_is_integer(row[0]))
        {
            free(inserted_ids);
            table_free(table);
            return FAILURE;
        }

        id_key = (int)utils_parse_integer(row[0]);
        inserted_ids[i] = id_key;
    }
    end = clock();

    *out_inserted_ids = inserted_ids;
    *elapsed_ms = 1000.0 * (double)(end - start) / (double)CLOCKS_PER_SEC;
    return SUCCESS;
}

/*
 * 인덱스가 없는 단순 삽입 시간을 측정한다.
 */
static int benchmark_measure_plain_insert(const BenchmarkConfig *config,
                                          double *elapsed_ms)
{
    BenchmarkRowStore store;
    char name[MAX_VALUE_LEN];
    char age[MAX_VALUE_LEN];
    int i;
    clock_t start;
    clock_t end;

    if (config == NULL || elapsed_ms == NULL)
    {
        return FAILURE;
    }

    memset(&store, 0, sizeof(store));
    start = clock();
    for (i = 0; i < config->row_count; i++)
    {
        benchmark_build_values(i, name, sizeof(name), age, sizeof(age));
        if (benchmark_plain_store_append(&store, i + 1, name, age) != SUCCESS)
        {
            benchmark_plain_store_free(&store);
            return FAILURE;
        }
    }
    end = clock();

    *elapsed_ms = 1000.0 * (double)(end - start) / (double)CLOCKS_PER_SEC;
    benchmark_plain_store_free(&store);
    return SUCCESS;
}

/*
 * 조회 쿼리별로 테이블 전반에 고르게 퍼진 sample index를 반환한다.
 */
static int benchmark_pick_lookup_index(const BenchmarkConfig *config,
                                       int query_index)
{
    long long scaled_index;

    if (config == NULL || config->row_count <= 0 || config->query_count <= 0)
    {
        return 0;
    }

    scaled_index =
        ((long long)query_index * (long long)config->row_count) /
        (long long)config->query_count;
    if (scaled_index >= config->row_count)
    {
        scaled_index = config->row_count - 1;
    }

    return (int)scaled_index;
}

/*
 * lookup case에 맞는 조회용 id 배열을 채운다.
 */
static int benchmark_build_lookup_keys(const BenchmarkConfig *config,
                                       const int *inserted_ids,
                                       BenchmarkLookupCase lookup_case,
                                       int query_count,
                                       int *lookup_keys)
{
    unsigned int random_state;
    int i;
    int random_index;

    if (config == NULL || inserted_ids == NULL || lookup_keys == NULL ||
        config->row_count <= 0 || query_count <= 0)
    {
        return FAILURE;
    }

    random_state = 0x00C0FFEEu;
    for (i = 0; i < query_count; i++)
    {
        switch (lookup_case)
        {
            case BENCHMARK_LOOKUP_AVERAGE:
                lookup_keys[i] =
                    inserted_ids[benchmark_pick_lookup_index(config, i)];
                break;
            case BENCHMARK_LOOKUP_WORST:
                lookup_keys[i] = inserted_ids[config->row_count - 1];
                break;
            case BENCHMARK_LOOKUP_RANDOM:
                random_state = random_state * 1103515245u + 12345u;
                random_index = (int)(random_state % (unsigned int)config->row_count);
                lookup_keys[i] = inserted_ids[random_index];
                break;
            default:
                return FAILURE;
        }
    }

    return SUCCESS;
}

/*
 * lookup case별 실제 측정 query 수를 반환한다.
 */
static int benchmark_query_count_for_case(const BenchmarkConfig *config,
                                          BenchmarkLookupCase lookup_case)
{
    if (config == NULL || config->query_count <= 0)
    {
        return 0;
    }

    if (lookup_case == BENCHMARK_LOOKUP_WORST)
    {
        return 1;
    }

    return config->query_count;
}

/*
 * row_index가 기대한 id를 가진 유효한 행인지 확인한다.
 */
static int benchmark_validate_lookup_row(const TableRuntime *table,
                                         int row_index, int expected_id)
{
    char **row;

    if (table == NULL)
    {
        return FAILURE;
    }

    row = table_get_row_by_slot(table, row_index);
    if (row == NULL || !utils_is_integer(row[0]))
    {
        return FAILURE;
    }

    return utils_parse_integer(row[0]) == expected_id ? SUCCESS : FAILURE;
}

/*
 * 인덱스 없이 id를 순차 탐색해 row_index를 찾는다.
 */
static int benchmark_find_row_index_by_id_linear(const TableRuntime *table,
                                                 int target_id,
                                                 int *out_row_index)
{
    int i;
    char **row;

    if (table == NULL || out_row_index == NULL)
    {
        return FAILURE;
    }

    for (i = 0; i < table->row_count; i++)
    {
        row = table->rows[i];
        if (row == NULL || !utils_is_integer(row[0]))
        {
            continue;
        }

        if (utils_parse_integer(row[0]) == target_id)
        {
            *out_row_index = i;
            return SUCCESS;
        }
    }

    return FAILURE;
}

/*
 * B+ 트리 id 조회 시간을 측정한다.
 */
static int benchmark_measure_id_lookup(const BenchmarkConfig *config,
                                       const TableRuntime *table,
                                       BPTreeNode *id_index_root,
                                       const int *lookup_keys,
                                       int query_count,
                                       double *elapsed_ms)
{
    int row_index;
    int i;
    int key;
    clock_t start;
    clock_t end;

    if (config == NULL || table == NULL || elapsed_ms == NULL ||
        id_index_root == NULL || lookup_keys == NULL)
    {
        return FAILURE;
    }

    start = clock();
    for (i = 0; i < query_count; i++)
    {
        key = lookup_keys[i];
        if (bptree_search(id_index_root, key, &row_index) != SUCCESS)
        {
            return FAILURE;
        }
        if (benchmark_validate_lookup_row(table, row_index, key) != SUCCESS)
        {
            return FAILURE;
        }
    }
    end = clock();

    *elapsed_ms = 1000.0 * (double)(end - start) / (double)CLOCKS_PER_SEC;
    return SUCCESS;
}

/*
 * 인덱스 없이 id 선형 탐색 시간을 측정한다.
 */
static int benchmark_measure_id_linear_scan(const BenchmarkConfig *config,
                                            const TableRuntime *table,
                                            const int *lookup_keys,
                                            int query_count,
                                            double *elapsed_ms)
{
    int i;
    int key;
    int row_index;
    clock_t start;
    clock_t end;

    if (config == NULL || table == NULL || lookup_keys == NULL ||
        elapsed_ms == NULL)
    {
        return FAILURE;
    }

    start = clock();
    for (i = 0; i < query_count; i++)
    {
        key = lookup_keys[i];
        if (benchmark_find_row_index_by_id_linear(table, key, &row_index) != SUCCESS)
        {
            return FAILURE;
        }
        if (benchmark_validate_lookup_row(table, row_index, key) != SUCCESS)
        {
            return FAILURE;
        }
    }
    end = clock();

    *elapsed_ms = 1000.0 * (double)(end - start) / (double)CLOCKS_PER_SEC;
    return SUCCESS;
}

BenchmarkConfig benchmark_default_config(void)
{
    BenchmarkConfig config;

    config.row_count = 1000000;
    config.query_count = 1000;
    return config;
}

int benchmark_run(const BenchmarkConfig *config)
{
    BenchmarkConfig active_config;
    TableRuntime indexed_table;
    int *inserted_ids;
    int *lookup_keys;
    double indexed_insert_ms;
    double plain_insert_ms;
    BenchmarkLookupResult lookup_results[3];
    BenchmarkLookupCase lookup_cases[3];
    int lookup_case_count;
    int lookup_query_count;
    int i;

    active_config = config == NULL ? benchmark_default_config() : *config;
    if (active_config.row_count <= 0 || active_config.query_count <= 0)
    {
        fprintf(stderr, "Error: Benchmark config must be positive.\n");
        return FAILURE;
    }

    indexed_insert_ms = 0.0;
    plain_insert_ms = 0.0;
    inserted_ids = NULL;
    lookup_keys = NULL;
    lookup_case_count = 3;
    lookup_cases[0] = BENCHMARK_LOOKUP_AVERAGE;
    lookup_cases[1] = BENCHMARK_LOOKUP_WORST;
    lookup_cases[2] = BENCHMARK_LOOKUP_RANDOM;
    lookup_results[0].label = "average-case";
    lookup_results[1].label = "worst-case";
    lookup_results[2].label = "random-case";
    for (i = 0; i < lookup_case_count; i++)
    {
        lookup_results[i].query_count = 0;
        lookup_results[i].bptree_ms = 0.0;
        lookup_results[i].linear_scan_ms = 0.0;
    }
    memset(&indexed_table, 0, sizeof(indexed_table));

    if (benchmark_measure_indexed_insert(&active_config, &indexed_table, &inserted_ids,
                                         &indexed_insert_ms) != SUCCESS)
    {
        return FAILURE;
    }

    lookup_keys = (int *)malloc((size_t)active_config.query_count * sizeof(int));
    if (lookup_keys == NULL)
    {
        fprintf(stderr, "Error: Failed to allocate memory.\n");
        free(inserted_ids);
        table_free(&indexed_table);
        return FAILURE;
    }

    if (benchmark_measure_plain_insert(&active_config, &plain_insert_ms) != SUCCESS)
    {
        free(lookup_keys);
        free(inserted_ids);
        table_free(&indexed_table);
        return FAILURE;
    }

    for (i = 0; i < lookup_case_count; i++)
    {
        lookup_query_count =
            benchmark_query_count_for_case(&active_config, lookup_cases[i]);
        lookup_results[i].query_count = lookup_query_count;
        if (benchmark_build_lookup_keys(&active_config, inserted_ids,
                                        lookup_cases[i], lookup_query_count,
                                        lookup_keys) != SUCCESS)
        {
            free(lookup_keys);
            free(inserted_ids);
            table_free(&indexed_table);
            return FAILURE;
        }

        if (benchmark_measure_id_lookup(&active_config, &indexed_table,
                                        indexed_table.id_index_root, lookup_keys,
                                        lookup_query_count,
                                        &lookup_results[i].bptree_ms) != SUCCESS)
        {
            free(lookup_keys);
            free(inserted_ids);
            table_free(&indexed_table);
            return FAILURE;
        }
        if (benchmark_measure_id_linear_scan(&active_config, &indexed_table,
                                             lookup_keys, lookup_query_count,
                                             &lookup_results[i].linear_scan_ms) != SUCCESS)
        {
            free(lookup_keys);
            free(inserted_ids);
            table_free(&indexed_table);
            return FAILURE;
        }
    }

    printf("[Benchmark]\n");
    printf("Rows: %d\n", active_config.row_count);
    printf("Queries: %d\n", active_config.query_count);
    printf("Insert with id index: %.3f ms\n", indexed_insert_ms);
    printf("Insert without id index: %.3f ms\n", plain_insert_ms);
    for (i = 0; i < lookup_case_count; i++)
    {
        printf("%s queries: %d\n",
               lookup_results[i].label, lookup_results[i].query_count);
        printf("%s b+ tree lookup: %.3f ms\n",
               lookup_results[i].label, lookup_results[i].bptree_ms);
        printf("%s linear scan lookup: %.3f ms\n",
               lookup_results[i].label, lookup_results[i].linear_scan_ms);
        if (lookup_results[i].bptree_ms > 0.0)
        {
            printf("%s lookup speedup (linear scan / B+ tree): %.2fx\n",
                   lookup_results[i].label,
                   lookup_results[i].linear_scan_ms / lookup_results[i].bptree_ms);
        }
    }

    free(lookup_keys);
    free(inserted_ids);
    table_free(&indexed_table);
    return SUCCESS;
}
