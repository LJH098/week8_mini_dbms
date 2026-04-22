#!/bin/bash

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

for binary in build/tests/test_tokenizer build/tests/test_parser \
              build/tests/test_storage build/tests/test_benchmark build/tests/test_table_runtime \
              build/tests/test_bptree build/tests/test_executor
do
    run_unit_test "$binary"
done

run_sql_test "Basic INSERT" "tests/test_cases/basic_insert.sql" "1 row inserted into users."
run_sql_test "Basic SELECT" "tests/test_cases/basic_select.sql" "Alice"
run_sql_test "WHERE id index" "tests/test_cases/select_where_id.sql" "Bob"
run_sql_test "WHERE equals" "tests/test_cases/select_where.sql" "Bob"
run_sql_test "Edge cases" "tests/test_cases/edge_cases.sql" "Lee, Jr."
run_sql_test "Explicit id rejected" "tests/test_cases/duplicate_primary_key.sql" "Explicit id values are not allowed"
run_sql_test "Delete unsupported" "tests/test_cases/delete_where.sql" "DELETE is not supported in memory runtime mode"

echo ""
echo "Results: $PASS passed, $FAIL failed"

if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
