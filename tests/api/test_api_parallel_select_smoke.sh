#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)
PORT=$((26000 + RANDOM % 12000))
SERVER_PID=""
TMP_DIR=$(mktemp -d)
SELECT_PIDS=()

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$TMP_DIR"
}

trap cleanup EXIT

cd "$ROOT_DIR"
rm -f data/api_parallel_users.csv

./api_server "$PORT" 4 16 >/tmp/week8_api_parallel_server.log 2>&1 &
SERVER_PID=$!

for _ in $(seq 1 40); do
    if curl -s "http://127.0.0.1:${PORT}/health" >/dev/null 2>&1; then
        break
    fi
    sleep 0.25
done

for i in $(seq 1 3); do
    curl -s -i -X POST "http://127.0.0.1:${PORT}/query" \
        -H "Content-Type: application/json" \
        --data "{\"sql\":\"INSERT INTO api_parallel_users (name, age) VALUES ('reader${i}', ${i});\"}" \
        >/tmp/week8_api_parallel_insert.txt
done

for i in $(seq 1 8); do
    curl -s -i -X POST "http://127.0.0.1:${PORT}/query" \
        -H "Content-Type: application/json" \
        --data '{"sql":"SELECT id, name FROM api_parallel_users;"}' \
        >"${TMP_DIR}/select_${i}.txt" &
    SELECT_PIDS+=($!)
done

for pid in "${SELECT_PIDS[@]}"; do
    wait "$pid"
done

for i in $(seq 1 8); do
    if ! grep -q 'HTTP/1.1 200 OK' "${TMP_DIR}/select_${i}.txt"; then
        echo "[FAIL] api parallel select status ${i}"
        cat "${TMP_DIR}/select_${i}.txt"
        exit 1
    fi
    if ! grep -q '"row_count":3' "${TMP_DIR}/select_${i}.txt"; then
        echo "[FAIL] api parallel select row count ${i}"
        cat "${TMP_DIR}/select_${i}.txt"
        exit 1
    fi
done

echo "[PASS] api_parallel_select_smoke"
