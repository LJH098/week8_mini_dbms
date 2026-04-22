# Load Test Comparison

## 목적

발표에서 단순히 "큐가 찼다"를 보여주는 것을 넘어서,  
왜 `worker_count`와 `queue_capacity`를 늘렸는데도 `INSERT` 부하 처리량이 크게 좋아지지 않았는지 설명하기 위한 비교 자료입니다.

이번 비교는 `GET /health` fast-path가 적용된 상태에서, `POST /query`에 `INSERT` 부하를 가한 실험입니다.

---

## 실험 조건

공통 k6 설정:

```bash
k6 run \
  -e BASE_URL=http://127.0.0.1:8080 \
  -e MODE=insert \
  -e START_RPS=50 \
  -e MID_RPS=600 \
  -e OVERLOAD_RPS=500 \
  -e STAGE1=3s \
  -e STAGE2=5s \
  -e STAGE3=25s \
  -e PROBE_DURATION=33s \
  -e HEALTH_INTERVAL=0.2 \
  -e PRE_ALLOCATED_VUS=500 \
  -e MAX_VUS=2000 \
  scripts/k6_queue_health_demo.js
```

비교 대상:

- 실험 A: `worker_count = 4`, `queue_capacity = 16`
- 실험 B: `worker_count = 8`, `queue_capacity = 20`

---

## 결과 요약

| 항목 | 4 workers / 16 queue | 8 workers / 20 queue |
|---|---:|---:|
| `query_ok` | 11,676 | 11,646 |
| `query_busy` | 6,046 | 6,076 |

한눈에 보면:

- `8 / 20`으로 늘렸는데도 `query_ok`가 크게 늘지 않았습니다.
- 오히려 `query_busy`도 거의 비슷하게 나왔습니다.
- 즉, 병목이 단순히 워커 수나 큐 크기에만 있지 않다는 뜻입니다.

---

## 왜 이런 결과가 나왔는가

핵심 원인은 `INSERT` 요청이 내부 DB 엔진에서 write lock 경로를 타기 때문입니다.

코드 기준으로 보면:

- [db_engine_facade.c](/Users/jinhyuk/krafton/project/week8_mini_dbms/src/db/db_engine_facade.c:49)
  - `SELECT`가 아니면 `QUERY_LOCK_WRITE`를 선택합니다.
- [lock_manager.c](/Users/jinhyuk/krafton/project/week8_mini_dbms/src/concurrency/lock_manager.c:43)
  - write 경로는 `pthread_rwlock_wrlock()`으로 들어갑니다.

즉 `INSERT`는 여러 워커가 동시에 들어와도, 실제 DB 수정 구간에서는 병렬성이 크게 제한됩니다.

정리하면 처리 흐름은 아래와 같습니다.

```mermaid
flowchart LR
    A["Client Requests"] --> B["API Server"]
    B --> C["Thread Pool"]
    C --> D["Worker Threads"]
    D --> E["DB Engine"]
    E --> F["Write Lock"]
    F --> G["CSV Storage"]
```

여기서 중요한 점은:

- 앞단의 `Thread Pool`과 `Queue`는 요청을 받아두는 버퍼 역할을 합니다.
- 하지만 `INSERT`는 뒤쪽 `Write Lock`에서 사실상 직렬화됩니다.
- 따라서 `worker_count`를 `4 -> 8`로 늘려도, DB write 처리량이 거의 그대로면 전체 성능 향상도 거의 없습니다.
- `queue_capacity`를 `16 -> 20`으로 늘리는 것도 "더 오래 기다릴 자리"를 조금 늘리는 효과일 뿐, 처리 속도를 높이진 못합니다.

즉 이번 실험은:

- "워커가 부족해서 느린 상황"이라기보다
- "DB write lock이 실제 병목인 상황"

을 보여줍니다.

---

## health 로그와 함께 해석하기

부하가 걸릴 때 `GET /health`에서는 이런 그림이 보입니다.

```json
{
  "status": "ok",
  "worker_count": 4,
  "busy_workers": 4,
  "idle_workers": 0,
  "queue_length": 16,
  "queue_capacity": 16,
  "available_queue_slots": 0
}
```

이 의미는:

- 워커 4개가 모두 바쁘고
- 큐도 꽉 차 있으며
- 이후 들어오는 요청은 `503 Server is busy`로 거절될 수 있다는 뜻입니다.

하지만 worker 수를 늘려도 결과가 거의 그대로라는 것은,  
앞단 큐가 아니라 뒤쪽 DB write path가 이미 처리량 상한을 만들고 있다는 해석과 연결됩니다.

---

## 발표 때 이렇게 설명하면 좋다

### 1. 먼저 관찰 결과를 말한다

"처음에는 worker 4개, queue 16개로 테스트했고, 이후 worker 8개, queue 20개로 늘려서 다시 테스트했습니다. 그런데 기대와 달리 `query_ok`와 `query_busy`가 거의 비슷하게 나왔습니다."

### 2. 그 다음 왜 그런지 설명한다

"이유는 `INSERT` 요청이 내부 DB 엔진의 write lock을 잡기 때문입니다. 즉 API 서버 앞단의 worker를 늘려도, 실제 DB 수정 구간은 병렬로 많이 처리되지 못합니다."

### 3. 마지막에 학습 포인트로 연결한다

"그래서 이 실험을 통해 단순히 thread 수를 늘리는 것만으로는 성능이 비례해서 올라가지 않고, 실제 병목 지점을 찾아야 한다는 점을 확인할 수 있었습니다."

---

## 한 줄 결론

`4 / 16`과 `8 / 20`의 결과가 거의 비슷했던 이유는,  
현재 `INSERT` 부하의 병목이 thread pool이 아니라 DB 엔진의 write lock 경로에 더 가깝기 때문입니다.
