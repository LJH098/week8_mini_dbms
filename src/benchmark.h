#ifndef BENCHMARK_H
#define BENCHMARK_H

#include "utils.h"

typedef struct {
    int row_count;
    int query_count;
} BenchmarkConfig;

/*
 * 기본 benchmark 설정을 반환한다.
 */
BenchmarkConfig benchmark_default_config(void);

/*
 * 메모리 기준 benchmark를 실행한다.
 * config가 NULL이면 기본 설정을 사용한다.
 */
int benchmark_run(const BenchmarkConfig *config);

#endif
