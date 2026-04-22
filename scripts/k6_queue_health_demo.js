import http from 'k6/http';
import exec from 'k6/execution';
import { check, sleep } from 'k6';
import { Counter } from 'k6/metrics';

const BASE_URL = __ENV.BASE_URL || 'http://127.0.0.1:8080';
const MODE = (__ENV.MODE || 'insert').toLowerCase();
const TABLE_PREFIX = __ENV.TABLE_PREFIX || 'k6_queue_demo';
const HEALTH_INTERVAL = Number(__ENV.HEALTH_INTERVAL || 1);
const START_RPS = Number(__ENV.START_RPS || 2);
const MID_RPS = Number(__ENV.MID_RPS || 8);
const OVERLOAD_RPS = Number(__ENV.OVERLOAD_RPS || 20);
const STAGE1 = __ENV.STAGE1 || '5s';
const STAGE2 = __ENV.STAGE2 || '10s';
const STAGE3 = __ENV.STAGE3 || '10s';
const PROBE_DURATION = __ENV.PROBE_DURATION || '25s';
const PRE_ALLOCATED_VUS = Number(__ENV.PRE_ALLOCATED_VUS || 50);
const MAX_VUS = Number(__ENV.MAX_VUS || 200);
const SEED_ROWS = Number(__ENV.SEED_ROWS || 200);
const QUERY_GRACEFUL_STOP = __ENV.QUERY_GRACEFUL_STOP || '10s';
const HEALTH_GRACEFUL_STOP = __ENV.HEALTH_GRACEFUL_STOP || '1s';
const HTTP_TIMEOUT = __ENV.HTTP_TIMEOUT || '120s';

const JSON_HEADERS = {
    headers: {
        'Content-Type': 'application/json',
    },
};

const queryOk = new Counter('query_ok');
const queryBusy = new Counter('query_busy');
const queryUnexpected = new Counter('query_unexpected');
const healthOk = new Counter('health_ok');
const healthBusy = new Counter('health_busy');
const healthUnexpected = new Counter('health_unexpected');

export const options = {
    scenarios: {
        query_load: {
            executor: 'ramping-arrival-rate',
            exec: 'queryLoad',
            startRate: START_RPS,
            timeUnit: '1s',
            preAllocatedVUs: PRE_ALLOCATED_VUS,
            maxVUs: MAX_VUS,
            stages: [
                { target: MID_RPS, duration: STAGE1 },
                { target: MID_RPS, duration: STAGE2 },
                { target: OVERLOAD_RPS, duration: STAGE3 },
            ],
            gracefulStop: QUERY_GRACEFUL_STOP,
        },
        health_probe: {
            executor: 'constant-vus',
            exec: 'healthProbe',
            vus: 1,
            duration: PROBE_DURATION,
            gracefulStop: HEALTH_GRACEFUL_STOP,
        },
    },
};

function postQuery(sql) {
    return http.post(
        `${BASE_URL}/query`,
        JSON.stringify({ sql }),
        {
            ...JSON_HEADERS,
            timeout: HTTP_TIMEOUT,
        }
    );
}

function buildInsertSql(tableName) {
    const vu = exec.vu.idInTest;
    const iter = exec.scenario.iterationInTest;
    const age = 20 + (iter % 30);
    return `INSERT INTO ${tableName} (name, age) VALUES ('vu${vu}_iter${iter}', ${age});`;
}

function buildSelectSql(tableName) {
    return `SELECT id, name, age FROM ${tableName};`;
}

function seedTable(tableName) {
    let targetRows = 1;
    let i;

    if (MODE === 'select') {
        targetRows = SEED_ROWS;
    } else {
        return;
    }

    for (i = 0; i < targetRows; i++) {
        const sql = `INSERT INTO ${tableName} (name, age) VALUES ('seed_${i}', ${20 + (i % 30)});`;
        const res = postQuery(sql);

        if (res.status !== 200) {
            throw new Error(`Failed to seed table ${tableName}. status=${res.status} body=${res.body}`);
        }
    }
}

export function setup() {
    const tableName = `${TABLE_PREFIX}_${Date.now()}`;
    const healthRes = http.get(`${BASE_URL}/health`, { timeout: HTTP_TIMEOUT });

    if (healthRes.status !== 200) {
        throw new Error(`Server is not ready. GET /health returned ${healthRes.status}`);
    }

    seedTable(tableName);
    console.log(`[setup] mode=${MODE} table=${tableName} base_url=${BASE_URL}`);
    return { tableName };
}

export function queryLoad(data) {
    let sql;
    let res;

    sql = MODE === 'select'
        ? buildSelectSql(data.tableName)
        : buildInsertSql(data.tableName);
    res = postQuery(sql);

    check(res, {
        'query returns 200 or 503': (r) => r.status === 200 || r.status === 503,
    });

    if (res.status === 200) {
        queryOk.add(1);
        return;
    }

    if (res.status === 503) {
        queryBusy.add(1);
        return;
    }

    queryUnexpected.add(1);
    console.log(`[query] unexpected status=${res.status} body=${res.body}`);
}

export function healthProbe() {
    let res;
    let body;

    res = http.get(`${BASE_URL}/health`, { timeout: HTTP_TIMEOUT });
    check(res, {
        'health returns 200 or 503': (r) => r.status === 200 || r.status === 503,
    });

    if (res.status === 200) {
        healthOk.add(1);
        body = res.json();
        console.log(
            `[health] workers=${body.worker_count} busy=${body.busy_workers} ` +
            `idle=${body.idle_workers} queue=${body.queue_length}/${body.queue_capacity} ` +
            `free_slots=${body.available_queue_slots}`
        );
    } else if (res.status === 503) {
        healthBusy.add(1);
        console.log(`[health] status=503 body=${res.body}`);
    } else {
        healthUnexpected.add(1);
        console.log(`[health] unexpected status=${res.status} body=${res.body}`);
    }

    sleep(HEALTH_INTERVAL);
}
