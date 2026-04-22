#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)
PORT=$((25000 + RANDOM % 15000))
SERVER_PID=""
INSERT_PIDS=()
TMP_DIR=$(mktemp -d)

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$TMP_DIR"
}

trap cleanup EXIT

cd "$ROOT_DIR"
rm -f data/api_concurrent_users.csv

./api_server "$PORT" 4 16 >/tmp/week8_api_concurrency_server.log 2>&1 &
SERVER_PID=$!

for _ in $(seq 1 40); do
    if curl -s "http://127.0.0.1:${PORT}/health" >/dev/null 2>&1; then
        break
    fi
    sleep 0.25
done

for i in $(seq 1 10); do
    curl -s -i -X POST "http://127.0.0.1:${PORT}/query" \
        -H "Content-Type: application/json" \
        --data "{\"sql\":\"INSERT INTO api_concurrent_users (name, age) VALUES ('user${i}', ${i});\"}" \
        >"${TMP_DIR}/insert_${i}.txt" &
    INSERT_PIDS+=($!)
done

for pid in "${INSERT_PIDS[@]}"; do
    wait "$pid"
done

for i in $(seq 1 10); do
    if ! grep -q 'HTTP/1.1 200 OK' "${TMP_DIR}/insert_${i}.txt"; then
        echo "[FAIL] api concurrent insert ${i}"
        cat "${TMP_DIR}/insert_${i}.txt"
        exit 1
    fi
done

select_response=$(curl -s -i -X POST "http://127.0.0.1:${PORT}/query" \
    -H "Content-Type: application/json" \
    --data '{"sql":"SELECT id, name FROM api_concurrent_users;"}')

if ! echo "$select_response" | grep -q 'HTTP/1.1 200 OK'; then
    echo "[FAIL] api concurrency select status"
    echo "$select_response"
    exit 1
fi

if ! echo "$select_response" | grep -q '"row_count":10'; then
    echo "[FAIL] api concurrency row count"
    echo "$select_response"
    exit 1
fi

echo "[PASS] api_concurrency_smoke"
