#!/bin/bash

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)

cd "$ROOT_DIR" || exit 1

echo "=== SQL Processor Test Suite ==="
PASS=0
FAIL=0

run_unit_test() {
    local binary=$1
    if "$binary" >/tmp/sql_processor_test.log 2>&1; then
        echo "[PASS] $(basename "$binary")"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $(basename "$binary")"
        cat /tmp/sql_processor_test.log
        FAIL=$((FAIL + 1))
    fi
}

run_sql_test() {
    local test_name=$1
    local sql_file=$2
    local expected=$3
    local output

    rm -rf data
    mkdir -p data

    output=$(./sql_processor "$sql_file" 2>&1)
    if echo "$output" | grep -q "$expected"; then
        echo "[PASS] $test_name"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $test_name"
        echo "Expected to find: $expected"
        echo "Actual output:"
        echo "$output"
        FAIL=$((FAIL + 1))
    fi
}

for binary in build/tests/db/test_tokenizer build/tests/db/test_parser \
              build/tests/db/test_storage build/tests/db/test_benchmark build/tests/db/test_table_runtime \
              build/tests/db/test_bptree build/tests/db/test_executor build/tests/db/test_db_engine_facade \
              build/tests/db/test_table_storage_loading \
              build/tests/concurrency/test_thread_pool build/tests/concurrency/test_tokenizer_cache_threads
do
    run_unit_test "$binary"
done

run_sql_test "Basic INSERT" "tests/integration/test_cases/basic_insert.sql" "1 row inserted into users."
run_sql_test "Basic SELECT" "tests/integration/test_cases/basic_select.sql" "Alice"
run_sql_test "WHERE id index" "tests/integration/test_cases/select_where_id.sql" "Bob"
run_sql_test "WHERE equals" "tests/integration/test_cases/select_where.sql" "Bob"
run_sql_test "Edge cases" "tests/integration/test_cases/edge_cases.sql" "Lee, Jr."
run_sql_test "Explicit id rejected" "tests/integration/test_cases/duplicate_primary_key.sql" "Explicit id values are not allowed"
run_sql_test "Delete unsupported" "tests/integration/test_cases/delete_where.sql" "DELETE is not supported in memory runtime mode"

if bash tests/api/test_api_smoke.sh >/tmp/week8_api_test.log 2>&1; then
    echo "[PASS] api_smoke"
    PASS=$((PASS + 1))
else
    echo "[FAIL] api_smoke"
    cat /tmp/week8_api_test.log
    FAIL=$((FAIL + 1))
fi

if bash tests/api/test_api_concurrency_smoke.sh >/tmp/week8_api_concurrency_test.log 2>&1; then
    echo "[PASS] api_concurrency_smoke"
    PASS=$((PASS + 1))
else
    echo "[FAIL] api_concurrency_smoke"
    cat /tmp/week8_api_concurrency_test.log
    FAIL=$((FAIL + 1))
fi

if bash tests/api/test_api_parallel_select_smoke.sh >/tmp/week8_api_parallel_test.log 2>&1; then
    echo "[PASS] api_parallel_select_smoke"
    PASS=$((PASS + 1))
else
    echo "[FAIL] api_parallel_select_smoke"
    cat /tmp/week8_api_parallel_test.log
    FAIL=$((FAIL + 1))
fi

echo ""
echo "Results: $PASS passed, $FAIL failed"

if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
