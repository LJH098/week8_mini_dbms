#include "benchmark.h"

#include <stdio.h>
#include <stdlib.h>

static int assert_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "[FAIL] %s\n", message);
        return FAILURE;
    }
    return SUCCESS;
}

int main(void) {
    BenchmarkConfig config;

    config.row_count = 128;
    config.query_count = 32;

    if (assert_true(benchmark_run(&config) == SUCCESS,
                    "benchmark smoke run should succeed") != SUCCESS) {
        return EXIT_FAILURE;
    }

    puts("[PASS] benchmark");
    return EXIT_SUCCESS;
}
