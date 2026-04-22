#!/bin/bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)
PORT=$((20000 + RANDOM % 20000))
SERVER_PID=""

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}

trap cleanup EXIT

cd "$ROOT_DIR"
rm -f data/api_users.csv

./api_server "$PORT" >/tmp/week8_api_server.log 2>&1 &
SERVER_PID=$!

for _ in $(seq 1 40); do
    if curl -s "http://127.0.0.1:${PORT}/health" >/tmp/week8_api_health.txt 2>/dev/null; then
        break
    fi
    sleep 0.25
done

health_response=$(curl -s -i "http://127.0.0.1:${PORT}/health")
if ! echo "$health_response" | grep -q 'HTTP/1.1 200 OK'; then
    echo "[FAIL] api health status"
    echo "$health_response"
    exit 1
fi

if ! echo "$health_response" | grep -q '"status":"ok"'; then
    echo "[FAIL] api health body"
    echo "$health_response"
    exit 1
fi

if ! echo "$health_response" | grep -q '"worker_count":4'; then
    echo "[FAIL] api health worker count"
    echo "$health_response"
    exit 1
fi

if ! echo "$health_response" | grep -q '"queue_capacity":16'; then
    echo "[FAIL] api health queue capacity"
    echo "$health_response"
    exit 1
fi

if ! echo "$health_response" | grep -q '"busy_workers":'; then
    echo "[FAIL] api health busy workers"
    echo "$health_response"
    exit 1
fi

if ! echo "$health_response" | grep -q '"busy_workers":0'; then
    echo "[FAIL] api health fast path busy worker snapshot"
    echo "$health_response"
    exit 1
fi

if ! echo "$health_response" | grep -q '"idle_workers":'; then
    echo "[FAIL] api health idle workers"
    echo "$health_response"
    exit 1
fi

if ! echo "$health_response" | grep -q '"queue_length":'; then
    echo "[FAIL] api health queue length"
    echo "$health_response"
    exit 1
fi

insert_response=$(curl -s -i -X POST "http://127.0.0.1:${PORT}/query" \
    -H "Content-Type: application/json" \
    --data '{"sql":"INSERT INTO api_users (name, age) VALUES ('\''Alice'\'', 30);"}')

if ! echo "$insert_response" | grep -q 'HTTP/1.1 200 OK'; then
    echo "[FAIL] api insert status"
    echo "$insert_response"
    exit 1
fi

if ! echo "$insert_response" | grep -q '"rows_affected":1'; then
    echo "[FAIL] api insert body"
    echo "$insert_response"
    exit 1
fi

select_response=$(curl -s -i -X POST "http://127.0.0.1:${PORT}/query" \
    -H "Content-Type: application/json" \
    --data '{"sql":"SELECT name FROM api_users WHERE id = 1;"}')

if ! echo "$select_response" | grep -q 'HTTP/1.1 200 OK'; then
    echo "[FAIL] api select status"
    echo "$select_response"
    exit 1
fi

if ! echo "$select_response" | grep -q '"Alice"'; then
    echo "[FAIL] api select body"
    echo "$select_response"
    exit 1
fi

if ! echo "$select_response" | grep -q '"used_id_index":true'; then
    echo "[FAIL] api select index usage"
    echo "$select_response"
    exit 1
fi

echo "[PASS] api_smoke"
