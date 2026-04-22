# 미니 DBMS API 서버 설계 문서

이 문서는 이번 과제의 구현 전에 팀이 함께 설계를 검토하기 위한 문서다.
중요한 목적은 다음과 같다.

- 기존에 구현한 SQL 처리기와 B+ Tree 인덱스를 최대한 재사용한다.
- 외부 클라이언트가 사용할 수 있는 API 서버 구조를 설계한다.
- Thread Pool 기반 병렬 처리 구조를 잡는다.
- 멀티스레드 환경에서도 최소한의 정합성을 보장하는 동시성 제어 전략을 정리한다.
- 아직 구현은 하지 않고, 실제 구현 직전에 필요한 plan, 모듈 설명, 의사코드까지 문서화한다.

현재 저장소 기준으로 재사용 대상이 되는 핵심 흐름은 다음과 같다.

- `tokenizer -> parser -> executor -> table_runtime -> bptree`
- 현재 `executor`는 결과를 `stdout`에 출력하는 구조다.
- 현재 `table_runtime`는 내부에 단일 활성 테이블 상태를 유지한다.
- 현재 `tokenizer`는 전역 캐시를 사용한다.

즉, API 서버 구현의 핵심은 완전히 새로운 DB 엔진을 만드는 것이 아니라, 기존 엔진을 서버 환경에 맞게 감싸는 `DB engine facade`를 설계하는 것이다.

## Part 1. 전체 구현 plan

### 1. 이번 과제의 핵심 목표

이번 과제의 핵심 목표는 기존 SQL 처리기와 B+ Tree 인덱스를 재사용하여 내부 DB 엔진을 구성하고, 이를 HTTP 기반 API 서버로 외부에 노출하는 것이다.

이번 서버는 단순히 요청을 하나씩 받는 수준이 아니라, Thread Pool을 통해 여러 요청을 병렬로 처리할 수 있어야 한다.
다만 현재 엔진은 멀티스레드 안전성을 전제로 만들어진 구조가 아니므로, 최소한의 동시성 제어를 먼저 적용해 정합성이 깨지지 않도록 해야 한다.

정리하면 이번 설계의 핵심은 아래 네 가지다.

- 기존 엔진 재사용
- HTTP API 서버 제공
- Thread Pool 기반 병렬 처리
- 최소한의 동시성 제어

### 2. MVP 범위

이번 과제의 MVP는 "동작 가능한 API 서버"를 가장 먼저 만드는 데 초점을 둔다.

MVP 범위는 아래와 같이 제한한다.

- `POST /query` 요청을 받아 SQL 한 문장을 실행한다.
- `GET /health` 요청으로 서버 생존 여부를 확인한다.
- 요청 body는 JSON 형식이며, SQL 문자열 하나만 담는다.
- 응답은 JSON으로 통일한다.
- 기존 SQL 처리기의 `INSERT`, `SELECT`, `WHERE id = ?`, 일반 `WHERE` 경로를 우선 재사용한다.
- Thread Pool 기반으로 요청을 worker thread가 처리한다.
- DB 엔진 진입 구간에는 전역 mutex를 적용해 정합성을 우선 확보한다.

즉, MVP는 "고급 DBMS"가 아니라 "안전하게 동작하는 최소 서버"를 만드는 것이 목적이다.

### 3. 제외 범위

처음부터 범위를 넓히면 구현 난도가 급격히 올라가므로 아래 항목은 MVP에서 제외한다.

- 트랜잭션
- JOIN
- 다중 SQL 문장 배치 실행
- 인증 / 인가
- TLS / HTTPS
- keep-alive 최적화
- 복잡한 HTTP 표준 완전 지원
- 고급 JSON 파서 통합
- 세밀한 lock hierarchy
- 레코드 단위 lock
- 다중 프로세스 동기화
- 분산 환경
- 고급 에러 복구
- DELETE 완전 지원

현재 메모리 런타임 구조상 `DELETE`는 비지원 상태이므로, 서버도 같은 제약을 유지하는 것이 자연스럽다.

### 4. 전체 아키텍처

전체 구조는 다음 흐름으로 잡는다.

1. 메인 스레드는 서버 소켓을 열고 클라이언트 연결을 받는다.
2. 새 요청은 `job queue`에 들어간다.
3. `thread pool`의 worker가 큐에서 요청을 꺼낸다.
4. worker가 HTTP 요청을 읽고 파싱한다.
5. `/query` 요청이면 JSON body에서 SQL을 꺼낸다.
6. lock을 획득한 뒤 `DB engine facade`를 통해 기존 SQL 엔진을 호출한다.
7. 실행 결과를 JSON 응답으로 만든다.
8. HTTP 응답을 클라이언트에게 전송한다.

아키텍처 관점에서 주요 계층은 아래와 같이 나눌 수 있다.

- 네트워크 계층: 소켓 생성, accept, read/write
- 요청 처리 계층: HTTP 파싱, JSON body 해석, 라우팅
- 병렬 처리 계층: thread pool, job queue
- DB 엔진 계층: tokenizer, parser, executor, table_runtime, bptree
- 동시성 제어 계층: lock manager
- 응답 생성 계층: JSON 직렬화, HTTP 응답 생성

핵심은 서버 코드가 기존 `executor`, `table_runtime`, `bptree` 내부를 직접 건드리는 것이 아니라, `DB engine facade`를 통해 진입하는 구조를 만드는 것이다.

### 5. 요청 처리 흐름

클라이언트 요청이 들어와 응답이 나갈 때까지의 흐름은 아래와 같다.

1. 클라이언트가 서버에 연결한다.
2. 메인 스레드가 `accept`로 연결을 받는다.
3. 연결 정보가 `ClientJob` 형태로 큐에 들어간다.
4. worker thread가 job을 꺼낸다.
5. raw HTTP 요청을 읽는다.
6. HTTP method, path, header, body를 파싱한다.
7. `/query` 요청이면 body에서 SQL을 추출한다.
8. 전역 DB lock을 획득한다.
9. `DB engine facade`가 SQL을 실행한다.
10. 실행 결과를 구조화된 `DbResult` 형태로 받는다.
11. lock을 해제한다.
12. `DbResult`를 JSON 응답 문자열로 변환한다.
13. HTTP 응답을 socket에 전송한다.
14. client socket을 닫는다.

이 흐름을 기준으로 함수 설명도 같은 순서로 정리한다.

### 6. 모듈 분리 방식

구현 전 설계 기준으로 모듈은 아래와 같이 나누는 것이 적절하다.

- `api_server`
  - 서버 소켓 생성
  - accept loop
  - 서버 시작 / 종료

- `http_parser`
  - raw HTTP 요청 읽기
  - method / path / header / body 파싱
  - JSON body에서 SQL 추출

- `thread_pool`
  - worker thread 생성
  - job 실행 루프
  - 종료 및 join 처리

- `job_queue`
  - 요청 큐 저장
  - push / pop
  - mutex + condition variable 기반 동기화

- `db_engine_facade`
  - SQL 문자열을 기존 엔진 실행 경로에 연결
  - 결과를 `DbResult` 구조체로 반환

- `lock_manager`
  - 전역 DB mutex 관리
  - 나중에 rwlock으로 바꾸기 쉽게 인터페이스 추상화

- `response_builder`
  - DB 결과를 JSON 응답으로 변환
  - HTTP status와 body 생성

- `server_logger`
  - 요청 로그
  - 에러 로그
  - 처리 시간 로그

### 7. 파일 구조 제안

현재 저장소는 `src/` 아래에 소스 파일을 평면적으로 두고 있으므로, API 서버 관련 파일도 같은 스타일로 추가하는 것이 구현 부담이 가장 낮다.

제안 파일 구조는 아래와 같다.
> TODO: 네트워크, db엔진 이런 계층적 관점으로 맞춰서 분리해줘 즉, src에다가 지금 다 때려박고있는데 이걸 폴더 분리해달라는거지

```text
week7_index/
├── src/
│   ├── main.c
│   ├── tokenizer.c/h
│   ├── parser.c/h
│   ├── executor.c/h
│   ├── table_runtime.c/h
│   ├── bptree.c/h
│   ├── utils.c/h
│   ├── api_main.c
│   ├── api_server.c/h
│   ├── http_parser.c/h
│   ├── thread_pool.c/h
│   ├── job_queue.c/h
│   ├── db_engine_facade.c/h
│   ├── lock_manager.c/h
│   ├── response_builder.c/h
│   └── server_logger.c/h
├── tests/
│   ├── test_http_parser.c
│   ├── test_thread_pool.c
│   ├── test_db_engine_facade.c
│   ├── test_lock_manager.c
│   └── test_api_smoke.sh
└── docs/
    └── API_SERVER_DESIGN.md
```

파일 이름은 구현 단계에서 팀이 조정할 수 있다.
중요한 것은 "네트워크 / 병렬 처리 / DB 엔진 / lock / 응답 생성"의 책임이 분리되어야 한다는 점이다.

### 8. thread pool / job queue / API server / DB engine facade / lock manager 각각의 역할

#### thread pool

thread pool은 worker thread를 미리 만들어 두고, 들어오는 요청을 재사용 가능한 worker가 처리하게 만드는 구조다.
요청마다 새 thread를 만드는 방식보다 비용이 적고, 구조가 명확하다.

주요 책임은 아래와 같다.

- worker thread 생성
- worker 실행 루프 유지
- 큐에서 job을 받아 처리
- 서버 종료 시 thread 정리

#### job queue

job queue는 메인 accept 스레드와 worker thread 사이의 완충 지점이다.
accept는 요청을 빨리 큐에 넘기고, worker는 자기 속도에 맞춰 처리한다.

주요 책임은 아래와 같다.

- `ClientJob` 저장
- 큐 가득 참 / 비어 있음 관리
- `push` / `pop`
- mutex + condition variable 동기화

#### API server

API server는 네트워크 진입점이다.
클라이언트 연결을 받고, 이를 worker 쪽으로 넘기며, 서버 전체 lifecycle을 제어한다.

주요 책임은 아래와 같다.

- 서버 소켓 생성
- 포트 bind
- listen
- accept loop
- 종료 처리

#### DB engine facade

DB engine facade는 이번 설계의 핵심 연결점이다.
기존 `tokenizer`, `parser`, `executor`, `table_runtime`, `bptree`를 재사용하되, 서버 코드가 직접 내부 구조에 의존하지 않도록 감싸는 역할을 한다.

주요 책임은 아래와 같다.

- SQL 문자열 입력 받기
- tokenizer / parser / executor 연결
- 결과를 `DbResult` 구조체로 정리
- 에러를 API 친화적인 형태로 변환

#### lock manager

lock manager는 DB 엔진 보호용 동시성 제어 계층이다.
처음에는 전역 mutex 하나만 두되, 나중에 `pthread_rwlock_t` 같은 구조로 바꾸기 쉽게 인터페이스를 분리한다.

주요 책임은 아래와 같다.

- lock 초기화
- lock 획득 / 해제
- 정책 변경 지점 제공

### 9. 동시성 제어 전략

#### 1차: 전역 DB mutex

첫 번째 버전에서는 전역 DB mutex 하나로 충분하다.
이유는 현재 엔진 구조가 전역 상태를 포함하기 때문이다.

현재 코드 기준으로 동시성에 민감한 지점은 아래와 같다.

- `table_runtime`가 단일 활성 테이블 상태를 전역으로 유지한다.
- `table_runtime` 내부 row 배열과 `next_id`가 공유 상태다.
- `bptree`는 내부 동기화가 없다.
- `tokenizer`는 전역 캐시를 수정한다.

따라서 가장 안전한 첫 번째 전략은 아래와 같다.

- SQL 요청이 DB 엔진에 진입하기 전에 mutex를 잡는다.
- tokenizer, parser, executor, runtime, bptree 실행을 모두 끝낸다.
- 구조화된 결과를 확보한 뒤 mutex를 푼다.

이 방식은 병렬성은 제한하지만, 정합성을 우선 확보하기 좋다.

#### 2차 개선 가능성: read/write lock

두 번째 단계에서는 read/write lock으로 개선할 수 있다.

예를 들어 아래처럼 나눌 수 있다.

- `SELECT`: read lock
- `INSERT`: write lock

하지만 현재 구조에서는 단순히 SQL 종류만 보고 read/write를 나누면 충분하지 않다.
이유는 read 요청도 `tokenizer` 캐시를 갱신할 수 있기 때문이다.

즉, read/write lock으로 개선하려면 아래 중 하나가 필요하다.
> TODO: tokenizer 캐시에 mutex 락 적용, db단에는 read,write락 적용

- tokenizer 캐시에도 별도 lock을 둔다.
- tokenizer 캐시를 thread-safe 하게 바꾼다.
- API 서버 모드에서는 tokenizer 캐시를 끈다.

따라서 이번 과제에서는 1차로 전역 mutex를 적용하고, rwlock은 "개선 가능성"으로 문서화하는 것이 적절하다.

### 10. 구현 단계 순서

#### Step 1: DB 엔진 facade 정리

가장 먼저 해야 할 일은 API 서버가 쓸 수 있는 엔진 진입점을 만드는 것이다.
현재 `executor`는 결과를 `printf`로 출력하는 구조라서, 서버 응답으로 바로 쓰기 어렵다.

이번 단계에서는 아래를 목표로 한다.

- SQL 문자열을 받아 실행할 수 있는 함수 준비
- 결과를 `DbResult` 구조체로 받을 수 있게 정리
- 기존 엔진의 출력 중심 구조를 실행 중심 구조로 분리

#### Step 2: 싱글 스레드 API 서버

DB facade가 준비되면, 그 다음은 thread pool 없이 가장 단순한 HTTP 서버를 붙인다.

이번 단계에서는 아래를 목표로 한다.

- `GET /health`
- `POST /query`
- JSON 요청 / JSON 응답
- 단일 요청 처리

즉, 이 단계는 "네트워크에서 DB 엔진까지 한 번 연결해 보는 단계"다.

#### Step 3: thread pool 도입

싱글 스레드 서버가 안정적으로 동작하면, 요청 처리를 thread pool로 넘긴다.

이번 단계에서는 아래를 목표로 한다.

- 메인 스레드는 accept만 담당
- worker thread가 요청 처리
- job queue를 통한 분배

즉, 이 단계는 "요청 처리 방식"을 병렬 구조로 바꾸는 단계다.

#### Step 4: lock 적용

병렬 처리 구조가 들어가면, 그 다음은 DB 엔진 정합성을 보호해야 한다.

이번 단계에서는 아래를 목표로 한다.

- 전역 DB mutex 도입
- DB 엔진 진입 구간 보호
- 경쟁 조건 테스트

즉, 이 단계는 "병렬 요청은 받되, 내부 데이터는 안전하게 보호"하는 단계다.

#### Step 5: 테스트 및 데모

마지막 단계에서는 구현이 정말 과제 시연 수준에서 안정적으로 보이는지 검증한다.

이번 단계에서는 아래를 목표로 한다.

- 단일 요청 성공 테스트
- 잘못된 요청 테스트
- 동시 요청 테스트
- `WHERE id = ?` 인덱스 경로 시연
- queue full 혹은 오류 응답 확인
> TODO: 5단계까지 다 끝나면 그 이후에 mutex락에서 read/write 락으로 개선한다.
### 11. 각 단계의 완료 조건

#### Step 1 완료 조건

- SQL 문자열 하나를 받아 구조화된 결과를 반환할 수 있다.
- `stdout` 출력에 의존하지 않고 결과를 응답용 데이터로 넘길 수 있다.
- 기존 tokenizer / parser / executor 경로를 재사용할 수 있다.

#### Step 2 완료 조건

- 싱글 스레드 서버가 뜬다.
- `/health` 요청에 응답한다.
- `/query` 요청에서 SQL을 실행하고 JSON 응답을 준다.
- 비정상 요청에 대해 적절한 에러 응답을 보낸다.

#### Step 3 완료 조건

- worker thread가 요청을 처리한다.
- 메인 스레드는 accept와 queue 전달에 집중한다.
- 여러 요청이 들어와도 서버가 즉시 죽지 않는다.

#### Step 4 완료 조건

- 동시 `INSERT`, `SELECT` 상황에서도 crash가 없다.
- `next_id`, row 배열, B+Tree 상태가 깨지지 않는다.
- lock 누락으로 인한 race condition이 재현되지 않는다.

#### Step 5 완료 조건

- 단위 테스트와 smoke test가 준비된다.
- 데모 시나리오가 재현 가능하다.
- 팀원이 실행 방법과 요청 예시를 보고 바로 시연할 수 있다.

## Part 2. 핵심 함수/모듈 목록

아래는 실제 구현에 필요할 핵심 함수와 모듈 목록이다.
지금 단계에서는 "구현"이 아니라 "무엇이 필요한가"를 정리하는 목적이다.

### 1. 진입점 및 서버 초기화

- `main`
- `load_server_config`
- `create_server_socket`
- `server_accept_loop`
- `shutdown_server`

### 2. 요청 수신 및 HTTP 파싱

- `read_http_request`
- `parse_http_request`
- `parse_request_line`
- `parse_headers`
- `extract_sql_from_json`
- `validate_query_request`

### 3. thread pool / job queue

- `thread_pool_init`
- `thread_pool_submit`
- `thread_pool_shutdown`
- `worker_main`
- `queue_init`
- `queue_push`
- `queue_pop`
- `queue_shutdown`

### 4. worker의 실제 요청 처리

- `handle_client_job`
- `route_request`
- `handle_health_request`
- `handle_query_request`

### 5. DB 엔진 facade

- `db_engine_init`
- `db_execute_sql`
- `db_engine_shutdown`
- `execute_query_with_lock`
- `executor_execute_into_result`
- `convert_select_result`
- `convert_insert_result`

### 6. 기존 엔진 재사용 대상

- `tokenizer_tokenize`
- `parser_parse`
- `table_get_or_load`
- `table_insert_row`
- `table_linear_scan_by_field`
- `table_get_row_by_slot`
- `bptree_search`
- `bptree_insert`

### 7. lock 관련

- `init_lock_manager`
- `lock_db_for_query`
- `unlock_db_for_query`
- `destroy_lock_manager`

### 8. 응답 생성

- `build_json_response`
- `build_json_error_response`
- `send_http_response`
- `send_error_response`

### 9. 테스트 / 로그

- `log_request_start`
- `log_request_end`
- `log_server_error`
- `test_http_parser`
- `test_thread_pool`
- `test_db_engine_facade`
- `test_api_smoke`

## Part 3. 각 함수/모듈의 한글 설명 + 한글 의사코드

아래 설명은 요청 흐름 순서대로 정리한다.
즉, 클라이언트 요청이 들어와 DB 실행 후 응답이 나갈 때까지의 흐름이 눈에 보이도록 배치한다.

### 1. `main`

#### (1) 이름

`main`

#### (2) 역할

- API 서버 프로세스의 진입점이다.
- 서버 전역 설정과 주요 모듈 초기화를 담당한다.
- 서버 소켓 생성과 thread pool 초기화 이후 accept loop로 진입한다.

즉, 전체 시스템을 켜고 끄는 최상위 제어 함수다.

#### (3) 입력 / 출력

- 입력: 실행 인자, 환경 변수, 기본 설정값
- 출력: 정상 종료 시 성공 코드, 실패 시 실패 코드

#### (4) 호출 위치와 흐름

- 프로그램 시작 시 운영체제가 호출한다.
- `load_server_config`
- `db_engine_init`
- `init_lock_manager`
- `thread_pool_init`
- `create_server_socket`
- `server_accept_loop`

#### (5) 한글 의사코드

- 실행 인자를 읽는다.
- 포트, worker 개수, queue 크기 같은 설정을 정리한다.
- DB 엔진 facade를 초기화한다.
- lock manager를 초기화한다.
- thread pool과 job queue를 초기화한다.
- 서버 소켓을 생성한다.
- accept loop에 진입한다.
- 종료 신호가 오면 자원을 역순으로 정리한다.
- 종료 코드를 반환한다.

#### (6) 구현 시 주의점

- 초기화 도중 실패하면 이미 생성한 자원을 반드시 정리해야 한다.
- 현재 CLI용 `main.c`와 API 서버용 진입점을 어떻게 공존시킬지 빌드 전략이 필요하다.
- 종료 순서를 잘못 잡으면 worker가 아직 쓰는 자원을 먼저 해제할 수 있다.

### 2. 서버 초기화 관련 함수

#### 2-1. `load_server_config`

##### (1) 이름

`load_server_config`

##### (2) 역할

- 서버에 필요한 기본 설정을 모아 구조체로 정리한다.
- 구현 전에 설정값이 흩어지지 않도록 해 주는 준비 함수다.

##### (3) 입력 / 출력

- 입력: 실행 인자, 환경 변수, 기본값
- 출력: `ServerConfig`

##### (4) 호출 위치와 흐름

- `main`이 가장 먼저 호출한다.
- 이후 `create_server_socket`, `thread_pool_init`에 설정이 전달된다.

##### (5) 한글 의사코드

- 기본 포트를 정한다.
- 기본 worker 수를 정한다.
- 기본 queue 크기를 정한다.
- 사용자 입력이 있으면 값을 덮어쓴다.
- 최종 설정 구조체를 반환한다.

##### (6) 구현 시 주의점

- 너무 큰 worker 수나 queue 크기에 대한 상한이 필요하다.
- 과제 데모에서는 설정을 지나치게 복잡하게 만들 필요가 없다.

#### 2-2. `create_server_socket`

##### (1) 이름

`create_server_socket`

##### (2) 역할

- 클라이언트 요청을 받을 listening socket을 만든다.
- API 서버의 네트워크 입구를 준비하는 함수다.

##### (3) 입력 / 출력

- 입력: 포트 번호, backlog 설정
- 출력: 서버 socket fd

##### (4) 호출 위치와 흐름

- `main`에서 호출된다.
- 성공하면 `server_accept_loop`로 이어진다.

##### (5) 한글 의사코드

- TCP 소켓을 생성한다.
- 포트 재사용 옵션을 설정한다.
- 서버 주소 구조체를 채운다.
- 지정한 포트에 bind 한다.
- listen 상태로 전환한다.
- socket fd를 반환한다.

##### (6) 구현 시 주의점

- `bind`, `listen` 실패 시 fd 누수가 없어야 한다.
- backlog가 너무 작으면 짧은 순간에도 연결이 밀릴 수 있다.
- 포트 충돌 시 에러 메시지가 명확해야 한다.

#### 2-3. `shutdown_server`

##### (1) 이름

`shutdown_server`

##### (2) 역할

- 서버 종료 시 필요한 모듈 정리를 담당한다.
- shutdown 로직을 `main`에 흩어놓지 않기 위해 분리하는 것이 좋다.

##### (3) 입력 / 출력

- 입력: 서버 소켓, thread pool, lock manager, DB 엔진 컨텍스트
- 출력: 없음

##### (4) 호출 위치와 흐름

- `main` 종료 직전에 호출된다.
- 내부에서 각 모듈 정리 함수를 순서대로 호출한다.

##### (5) 한글 의사코드

- 새 요청 수신을 멈춘다.
- job queue 종료 플래그를 켠다.
- worker thread가 빠져나오도록 깨운다.
- 모든 worker를 join 한다.
- 서버 소켓을 닫는다.
- lock manager를 정리한다.
- DB 엔진을 정리한다.

##### (6) 구현 시 주의점

- worker가 살아 있는 동안 DB 엔진 자원을 먼저 해제하면 안 된다.
- 종료 중에도 이미 들어온 요청을 어디까지 처리할지 정책이 필요하다.

### 3. 요청 수신 관련 함수

#### 3-1. `server_accept_loop`

##### (1) 이름

`server_accept_loop`

##### (2) 역할

- 서버가 클라이언트 연결을 지속적으로 받는 루프다.
- 네트워크 진입점과 thread pool 사이를 연결한다.

##### (3) 입력 / 출력

- 입력: 서버 socket fd, thread pool 또는 queue 핸들
- 출력: 없음

##### (4) 호출 위치와 흐름

- `main`에서 호출된다.
- 새 연결마다 `thread_pool_submit` 또는 `queue_push`로 이어진다.

##### (5) 한글 의사코드

- 무한 루프를 돈다.
- 클라이언트 연결을 `accept` 한다.
- 연결 정보를 `ClientJob` 구조로 묶는다.
- job queue에 넣는다.
- 큐가 가득 찼으면 에러 응답을 보내고 소켓을 닫는다.
- 종료 조건이면 루프를 끝낸다.

##### (6) 구현 시 주의점

- 큐 제출 실패 시 소켓 정리가 빠지면 fd 누수가 생긴다.
- `accept` 오류를 전부 치명적으로 처리하지 않도록 주의해야 한다.
- accept 루프는 가볍게 유지하고, 무거운 처리는 worker에 넘겨야 한다.

#### 3-2. `read_http_request`

##### (1) 이름

`read_http_request`

##### (2) 역할

- 클라이언트 소켓에서 HTTP 요청 전체를 읽어온다.
- header와 body를 모두 수집해 파싱 가능한 raw 문자열을 만든다.

##### (3) 입력 / 출력

- 입력: `client_fd`
- 출력: raw HTTP 요청 버퍼

##### (4) 호출 위치와 흐름

- `handle_client_job` 안에서 가장 먼저 호출된다.
- 완료되면 `parse_http_request`로 넘어간다.

##### (5) 한글 의사코드

- socket에서 데이터를 읽기 시작한다.
- 헤더 종료 구분자까지 먼저 읽는다.
- `Content-Length` 값을 확인한다.
- body 길이만큼 추가로 읽는다.
- 전체 요청 버퍼를 널 종료한다.
- raw 요청 문자열을 반환한다.

##### (6) 구현 시 주의점

- `recv`는 한 번에 다 오지 않을 수 있다.
- 요청 크기 상한을 두지 않으면 메모리 문제가 생길 수 있다.
- timeout이 없으면 느린 요청이 worker를 오래 점유할 수 있다.

### 4. HTTP 파싱 관련 함수

#### 4-1. `parse_http_request`

##### (1) 이름

`parse_http_request`

##### (2) 역할

- raw HTTP 요청 문자열을 서버 내부에서 쓰기 쉬운 `HttpRequest` 구조체로 분해한다.
- method, path, header, body를 구조화한다.

##### (3) 입력 / 출력

- 입력: raw HTTP 요청 문자열
- 출력: `HttpRequest`

##### (4) 호출 위치와 흐름

- `read_http_request` 다음 단계에서 호출된다.
- 이후 `route_request`나 `validate_query_request`로 이어진다.

##### (5) 한글 의사코드

- 첫 줄을 분리한다.
- method, path, protocol을 읽는다.
- 헤더를 한 줄씩 읽는다.
- 필요한 헤더를 저장한다.
- body 위치를 기록한다.
- 구조체로 묶어 반환한다.

##### (6) 구현 시 주의점

- MVP라도 malformed request를 처리해야 한다.
- CRLF 처리 규칙이 어긋나면 파싱이 쉽게 깨진다.
- 모든 헤더를 저장할 필요는 없고 필요한 것만 다루는 편이 단순하다.

#### 4-2. `extract_sql_from_json`

##### (1) 이름

`extract_sql_from_json`

##### (2) 역할

- `/query` 요청 body에서 실제 SQL 문자열을 추출한다.
- HTTP 계층과 DB 엔진 계층을 이어주는 매우 중요한 연결 함수다.

##### (3) 입력 / 출력

- 입력: JSON body 문자열
- 출력: SQL 문자열

##### (4) 호출 위치와 흐름

- `handle_query_request`에서 호출된다.
- 성공하면 `execute_query_with_lock`으로 넘어간다.

##### (5) 한글 의사코드

- body가 비어 있지 않은지 확인한다.
- `"sql"` 키를 찾는다.
- 값이 문자열인지 확인한다.
- escape 문자를 해석한다.
- SQL 문자열을 새 메모리로 복사한다.
- SQL 문자열을 반환한다.

##### (6) 구현 시 주의점

- 문자열 escape 처리 누락 시 잘못된 SQL이 만들어질 수 있다.
- JSON 파서를 직접 만들면 입력 형식을 단순하게 제한하는 것이 안전하다.
- 빈 SQL, 너무 긴 SQL, 여러 문장을 담은 SQL에 대한 정책이 필요하다.

#### 4-3. `validate_query_request`

##### (1) 이름

`validate_query_request`

##### (2) 역할

- `/query` 요청이 서버가 지원하는 최소 조건을 만족하는지 검사한다.
- 잘못된 입력을 DB 엔진까지 넘기지 않도록 막는 방어막 역할을 한다.

##### (3) 입력 / 출력

- 입력: `HttpRequest`
- 출력: 성공 / 실패

##### (4) 호출 위치와 흐름

- `handle_query_request`에서 SQL 추출 전후에 호출할 수 있다.
- 실패 시 곧바로 `send_error_response`로 이어진다.

##### (5) 한글 의사코드

- method가 `POST`인지 확인한다.
- path가 `/query`인지 확인한다.
- body가 존재하는지 확인한다.
- `Content-Type`이 허용 가능한지 확인한다.
- body 크기가 제한 이하인지 확인한다.

##### (6) 구현 시 주의점

- 잘못된 요청을 너무 늦게 거르면 worker 자원을 낭비할 수 있다.
- 에러 코드를 일관되게 정리해야 클라이언트가 다루기 쉽다.

### 5. thread pool / job queue 관련 함수

#### 5-1. `thread_pool_init`

##### (1) 이름

`thread_pool_init`

##### (2) 역할

- worker thread들을 생성하고 실행 준비를 끝낸다.
- 병렬 요청 처리 기반을 만드는 함수다.

##### (3) 입력 / 출력

- 입력: worker 수, queue 핸들, 공용 컨텍스트
- 출력: 초기화된 thread pool 구조체

##### (4) 호출 위치와 흐름

- `main`에서 호출된다.
- worker는 이후 `worker_main`을 실행한다.

##### (5) 한글 의사코드

- thread pool 구조체를 초기화한다.
- worker 개수만큼 thread를 생성한다.
- 각 worker에 queue와 공용 컨텍스트를 전달한다.
- 모든 worker 생성이 끝나면 성공을 반환한다.

##### (6) 구현 시 주의점

- 일부 thread만 생성된 상태에서 실패하면 rollback이 필요하다.
- worker 시작 전에 queue와 lock manager가 초기화되어 있어야 한다.

#### 5-2. `thread_pool_submit`

##### (1) 이름

`thread_pool_submit`

##### (2) 역할

- 새 클라이언트 요청을 worker가 처리할 수 있도록 queue에 넣는다.

##### (3) 입력 / 출력

- 입력: `ClientJob`
- 출력: 성공 / 실패

##### (4) 호출 위치와 흐름

- `server_accept_loop`에서 호출된다.
- 내부적으로 `queue_push`를 사용한다.

##### (5) 한글 의사코드

- 전달받은 job이 유효한지 확인한다.
- queue에 자리가 있는지 확인한다.
- 자리가 있으면 job을 push 한다.
- worker에게 새 일이 생겼음을 알린다.
- 결과를 반환한다.

##### (6) 구현 시 주의점

- queue가 가득 찬 경우 정책이 필요하다.
- 제출 실패 시 job에 포함된 socket 정리를 누가 할지 명확해야 한다.

#### 5-3. `worker_main`

##### (1) 이름

`worker_main`

##### (2) 역할

- 각 worker thread가 반복적으로 수행하는 메인 루프다.
- 큐에서 요청을 꺼내 실제 요청 처리 함수로 전달한다.

##### (3) 입력 / 출력

- 입력: worker 컨텍스트
- 출력: thread 종료 시 반환

##### (4) 호출 위치와 흐름

- `thread_pool_init`이 thread 생성 시 시작 함수로 넘긴다.
- 내부에서 `queue_pop`과 `handle_client_job`을 반복 호출한다.

##### (5) 한글 의사코드

- 무한 루프를 시작한다.
- queue에서 job을 하나 꺼낸다.
- 종료 플래그와 queue 상태를 확인한다.
- 정상 job이면 `handle_client_job`을 호출한다.
- 처리 후 다음 job을 기다린다.

##### (6) 구현 시 주의점

- shutdown 시 무한 대기 상태에 빠지지 않게 해야 한다.
- worker 내부 예외 경로에서도 thread가 조용히 죽지 않도록 로그가 필요하다.

#### 5-4. `queue_init`

##### (1) 이름

`queue_init`

##### (2) 역할

- job queue에 필요한 버퍼와 동기화 도구를 준비한다.

##### (3) 입력 / 출력

- 입력: queue 크기
- 출력: 초기화된 queue

##### (4) 호출 위치와 흐름

- `main` 또는 `thread_pool_init` 전에 호출된다.
- 이후 `queue_push`, `queue_pop`이 사용한다.

##### (5) 한글 의사코드

- 큐 버퍼를 할당한다.
- head, tail, count를 초기화한다.
- mutex를 초기화한다.
- condition variable을 초기화한다.

##### (6) 구현 시 주의점

- 큐 크기가 0이 되지 않도록 검증이 필요하다.
- condition variable 둘 다 둘지 하나만 둘지 팀 합의가 필요하다.

#### 5-5. `queue_push`

##### (1) 이름

`queue_push`

##### (2) 역할

- 새 `ClientJob`을 큐에 넣는다.
- 생산자 측 핵심 함수다.

##### (3) 입력 / 출력

- 입력: queue, job
- 출력: 성공 / 실패

##### (4) 호출 위치와 흐름

- `thread_pool_submit`이 호출한다.
- 성공하면 worker가 나중에 `queue_pop`으로 꺼낸다.

##### (5) 한글 의사코드

- queue mutex를 잡는다.
- 큐 종료 여부를 확인한다.
- 큐가 가득 찼는지 확인한다.
- 빈 자리가 있으면 tail 위치에 job을 넣는다.
- count와 tail을 갱신한다.
- waiting 중인 worker를 깨운다.
- mutex를 푼다.

##### (6) 구현 시 주의점

- 큐가 가득 찬 경우 block할지 즉시 실패할지 정책을 정해야 한다.
> TODO: 즉시 실패
- push 성공 시 job 소유권이 queue로 넘어간다는 점이 분명해야 한다.

#### 5-6. `queue_pop`

##### (1) 이름

`queue_pop`

##### (2) 역할

- worker가 처리할 다음 요청을 큐에서 꺼낸다.
- 소비자 측 핵심 함수다.

##### (3) 입력 / 출력

- 입력: queue
- 출력: `ClientJob`

##### (4) 호출 위치와 흐름

- `worker_main`에서 반복 호출된다.
- 성공하면 `handle_client_job`으로 이어진다.

##### (5) 한글 의사코드

- queue mutex를 잡는다.
- 큐가 비었으면 condition variable을 기다린다.
- shutdown 상태인지 확인한다.
- job 하나를 꺼낸다.
- head와 count를 갱신한다.
- mutex를 푼다.
- job을 반환한다.

##### (6) 구현 시 주의점

- condition wait는 반드시 while 루프와 함께 써야 한다.
- shutdown과 빈 큐 상태를 구분해야 worker 종료가 자연스럽다.

### 6. worker가 실제 요청 처리하는 함수

#### 6-1. `handle_client_job`

##### (1) 이름

`handle_client_job`

##### (2) 역할

- worker thread가 요청 하나를 실제로 끝까지 처리하는 핵심 함수다.
- raw HTTP 요청 수신, 라우팅, SQL 실행, 응답 전송을 모두 연결한다.

이 함수는 네트워크 계층과 DB 계층을 이어주는 중심 연결 지점이다.

##### (3) 입력 / 출력

- 입력: `ClientJob`
- 출력: 직접 HTTP 응답을 전송하고 소켓을 닫는다

##### (4) 호출 위치와 흐름

- `worker_main`이 큐에서 job을 꺼낸 후 호출한다.
- 내부 흐름은 아래와 같다.
  - `read_http_request`
  - `parse_http_request`
  - `route_request`
  - `handle_query_request` 또는 `handle_health_request`
  - `send_http_response`

##### (5) 한글 의사코드

- 클라이언트 소켓에서 raw HTTP를 읽는다.
- 요청을 파싱한다.
- method와 path를 확인한다.
- `/health`면 즉시 성공 응답을 만든다.
- `/query`면 body에서 SQL을 꺼낸다.
- DB 실행 함수를 호출한다.
- 실행 결과를 JSON으로 변환한다.
- 응답을 클라이언트에게 보낸다.
- 소켓을 닫는다.
- 임시 메모리를 해제한다.

##### (6) 구현 시 주의점

- 실패 경로에서도 socket이 반드시 닫혀야 한다.
- DB lock을 네트워크 I/O 전체에 걸지 않도록 주의해야 한다.
- 요청 파싱 실패와 DB 실행 실패를 구분해 로그를 남기면 디버깅이 쉬워진다.

#### 6-2. `route_request`

##### (1) 이름

`route_request`

##### (2) 역할

- path와 method를 보고 어떤 요청 처리 함수로 넘길지 결정한다.

##### (3) 입력 / 출력

- 입력: `HttpRequest`
- 출력: 라우팅 결과 또는 직접 응답

##### (4) 호출 위치와 흐름

- `handle_client_job`이 호출한다.
- `handle_health_request`, `handle_query_request`, 혹은 에러 응답으로 이어진다.

##### (5) 한글 의사코드

- path가 `/health`인지 확인한다.
- 맞으면 health 처리 함수로 넘긴다.
- path가 `/query`인지 확인한다.
- 맞으면 query 처리 함수로 넘긴다.
- 아니면 404 응답을 보낸다.

##### (6) 구현 시 주의점

- method까지 함께 검사해야 한다.
- 라우팅 규칙을 초기에 단순하게 유지하는 것이 MVP에 유리하다.

#### 6-3. `handle_health_request`

##### (1) 이름

`handle_health_request`

##### (2) 역할

- 서버 생존 여부 확인용 요청을 처리한다.
- DB 엔진까지 가지 않는 가장 단순한 성공 경로다.

##### (3) 입력 / 출력

- 입력: 요청 정보
- 출력: 상태 확인용 JSON 응답

##### (4) 호출 위치와 흐름

- `route_request`에서 `/health` 요청으로 분기된다.
- 이후 `send_http_response`로 이어진다.

##### (5) 한글 의사코드

- 상태 확인 응답 구조를 만든다.
- JSON body를 생성한다.
- HTTP 200 응답으로 전송한다.

##### (6) 구현 시 주의점

- 불필요하게 DB lock을 잡지 않아야 한다.
- health check는 최대한 가볍고 안정적이어야 한다.

#### 6-4. `handle_query_request`

##### (1) 이름

`handle_query_request`

##### (2) 역할

- `/query` 요청의 실제 업무 처리를 담당한다.
- 입력 검증, SQL 추출, DB 실행, 응답 생성까지 연결한다.

##### (3) 입력 / 출력

- 입력: `HttpRequest`
- 출력: SQL 실행 결과에 따른 HTTP 응답

##### (4) 호출 위치와 흐름

- `route_request`에서 `/query` 요청으로 분기된다.
- 내부에서 `extract_sql_from_json`, `execute_query_with_lock`, `build_json_response`가 이어진다.

##### (5) 한글 의사코드

- 요청 형식이 올바른지 확인한다.
- JSON body에서 SQL 문자열을 추출한다.
- SQL 실행 함수를 호출한다.
- 실행 결과를 응답용 JSON으로 바꾼다.
- HTTP 응답을 보낸다.

##### (6) 구현 시 주의점

- SQL 추출 실패와 SQL 실행 실패를 구분해야 한다.
- 너무 긴 요청이나 지원하지 않는 SQL에 대한 정책이 필요하다.

### 7. DB 엔진 facade 관련 함수

#### 7-1. `db_engine_init`

##### (1) 이름

`db_engine_init`

##### (2) 역할

- DB 엔진 facade가 사용할 공용 상태를 초기화한다.
- 현재 구조에서는 복잡한 초기화가 많지 않더라도, 진입점을 명확히 두는 것이 좋다.

##### (3) 입력 / 출력

- 입력: 없음 또는 기본 설정
- 출력: 성공 / 실패

##### (4) 호출 위치와 흐름

- `main`에서 서버 시작 시 호출된다.
- 이후 `db_execute_sql`이 이 컨텍스트를 사용한다.

##### (5) 한글 의사코드

- 엔진 상태 구조체를 준비한다.
- 필요한 기본값을 채운다.
- 초기화 성공 상태를 기록한다.

##### (6) 구현 시 주의점

- 현재는 단순해 보여도 나중에 엔진 설정이나 캐시 정책이 들어올 수 있으므로 초기화 지점을 따로 두는 편이 좋다.

#### 7-2. `execute_query_with_lock`

##### (1) 이름

`execute_query_with_lock`

##### (2) 역할

- SQL 실행 전후에 lock을 감싸는 함수다.
- 멀티스레드 환경에서 정합성을 지키기 위한 핵심 보호 함수다.

##### (3) 입력 / 출력

- 입력: SQL 문자열
- 출력: `DbResult`

##### (4) 호출 위치와 흐름

- `handle_query_request`가 호출한다.
- 내부에서 `lock_db_for_query`, `db_execute_sql`, `unlock_db_for_query`를 호출한다.

##### (5) 한글 의사코드

- SQL 종류를 대략 확인한다.
- 현재 lock 정책에 따라 lock을 획득한다.
- DB facade 실행 함수를 호출한다.
- 실행 결과를 저장한다.
- lock을 해제한다.
- 결과를 반환한다.

##### (6) 구현 시 주의점

- 모든 반환 경로에서 unlock이 반드시 호출되어야 한다.
- 응답 생성이나 socket write까지 lock을 들고 가지 않는 것이 중요하다.

#### 7-3. `db_execute_sql`

##### (1) 이름

`db_execute_sql`

##### (2) 역할

- SQL 문자열을 받아 기존 SQL 엔진 경로를 실행하고 구조화된 결과를 만든다.
- API 서버가 기존 엔진을 사용하는 공식 진입점이다.

##### (3) 입력 / 출력

- 입력: SQL 문자열
- 출력: `DbResult`

##### (4) 호출 위치와 흐름

- `execute_query_with_lock`이 호출한다.
- 내부에서 `tokenizer_tokenize`, `parser_parse`, `executor_execute_into_result`가 이어진다.

##### (5) 한글 의사코드

- SQL 문자열을 정리한다.
- tokenizer로 토큰 배열을 만든다.
- parser로 `SqlStatement`를 만든다.
- executor 계층으로 실행을 넘긴다.
- 실행 결과를 `DbResult`로 채운다.
- 임시 토큰 메모리를 정리한다.
- 결과를 반환한다.

##### (6) 구현 시 주의점

- 현재 parser와 tokenizer는 오류를 `stderr`로 출력하는 경향이 있어, API 친화적인 에러 메시지 수집 전략이 필요하다.
- SQL 한 문장만 처리한다는 제약을 분명히 해야 한다.

#### 7-4. `executor_execute_into_result`

##### (1) 이름

`executor_execute_into_result`

##### (2) 역할

- 현재 `executor_execute`의 출력 중심 구조를 API 응답 중심 구조로 바꿔 주는 핵심 함수다.
- 사실상 이번 설계에서 가장 먼저 정리되어야 할 함수다.

##### (3) 입력 / 출력

- 입력: `SqlStatement`
- 출력: `DbResult`

##### (4) 호출 위치와 흐름

- `db_execute_sql` 내부에서 호출된다.
- `INSERT`는 `table_insert_row`
- `SELECT`는 `table_linear_scan_by_field`, `bptree_search`, `table_get_row_by_slot`

##### (5) 한글 의사코드

- statement 타입을 확인한다.
- `INSERT`면 대상 테이블을 가져온다.
- row를 삽입하고 영향을 받은 행 수를 기록한다.
- `SELECT`면 projection 정보를 준비한다.
- `WHERE id = 값`이면 B+Tree 인덱스 경로를 사용한다.
- 그 외 조건이면 선형 탐색 경로를 사용한다.
- 결과 컬럼과 결과 행을 `DbResult`에 저장한다.
- 결과를 반환한다.

##### (6) 구현 시 주의점

- 현재 `executor`는 표를 출력하는 로직과 실행 로직이 섞여 있다.
- 이번 단계에서 실행 결과 수집 로직과 출력 로직을 분리해야 서버에서 재사용하기 좋다.
- stdout 캡처 방식은 멀티스레드 서버와 잘 맞지 않는다.

#### 7-5. `convert_select_result`

##### (1) 이름

`convert_select_result`

##### (2) 역할

- SELECT 실행 결과를 응답 구조체에 채운다.
- 컬럼명, row 수, row 데이터 배열을 일관된 형식으로 관리하게 해 준다.

##### (3) 입력 / 출력

- 입력: 컬럼 목록, row index 목록, 실제 row 데이터
- 출력: `DbResult`

##### (4) 호출 위치와 흐름

- `executor_execute_into_result` 내부에서 호출된다.

##### (5) 한글 의사코드

- 결과 컬럼 배열을 준비한다.
- 결과 행 수를 기록한다.
- 각 row를 순회하며 응답용 셀 문자열을 복사한다.
- 응답 구조체에 저장한다.

##### (6) 구현 시 주의점

- 응답 구조체가 소유하는 메모리 범위를 분명히 해야 한다.
- 메모리 할당 실패 시 부분 해제가 중요하다.

#### 7-6. `convert_insert_result`

##### (1) 이름

`convert_insert_result`

##### (2) 역할

- INSERT 실행 결과를 응답 구조체에 채운다.
- affected rows와 메시지를 일관된 형식으로 만들기 위한 함수다.

##### (3) 입력 / 출력

- 입력: 테이블 이름, 삽입 결과 정보
- 출력: `DbResult`

##### (4) 호출 위치와 흐름

- `executor_execute_into_result`의 INSERT 분기에서 호출된다.

##### (5) 한글 의사코드

- 삽입 성공 여부를 확인한다.
- affected rows를 1로 기록한다.
- 사용자에게 보여줄 메시지를 만든다.
- 응답 구조체에 저장한다.

##### (6) 구현 시 주의점

- INSERT 응답 형식을 SELECT와 너무 다르게 만들면 클라이언트가 복잡해질 수 있다.

### 8. lock 관련 함수

#### 8-1. `init_lock_manager`

##### (1) 이름

`init_lock_manager`

##### (2) 역할

- DB 보호용 lock 구조를 초기화한다.
- 1차는 mutex, 2차는 rwlock으로 확장 가능하도록 설계한다.

##### (3) 입력 / 출력

- 입력: lock 정책 설정
- 출력: 성공 / 실패

##### (4) 호출 위치와 흐름

- `main`에서 서버 시작 시 호출된다.

##### (5) 한글 의사코드

- lock manager 구조체를 초기화한다.
- 현재 정책이 mutex면 mutex를 생성한다.
- 확장용 필드도 기본값으로 채운다.

##### (6) 구현 시 주의점

- 지금은 mutex만 써도 인터페이스는 일반화해 두는 편이 좋다.

#### 8-2. `lock_db_for_query`

##### (1) 이름

`lock_db_for_query`

##### (2) 역할

- SQL 실행 직전에 적절한 lock을 획득한다.

##### (3) 입력 / 출력

- 입력: SQL 종류 또는 lock mode
- 출력: 성공 / 실패

##### (4) 호출 위치와 흐름

- `execute_query_with_lock`이 호출한다.
- 완료 후 `db_execute_sql`이 실행된다.

##### (5) 한글 의사코드

- 현재 SQL이 읽기인지 쓰기인지 확인한다.
- 현재 정책이 mutex면 전역 mutex를 잡는다.
- 현재 정책이 rwlock이면 read 또는 write lock을 잡는다.
- lock 획득 결과를 반환한다.

##### (6) 구현 시 주의점

- 현재 구조에서는 tokenizer 캐시 때문에 단순 read 분류가 안전하지 않을 수 있다.
- MVP는 전역 mutex 한 개로 고정하는 편이 안전하다.

#### 8-3. `unlock_db_for_query`

##### (1) 이름

`unlock_db_for_query`

##### (2) 역할

- SQL 실행이 끝난 뒤 lock을 해제한다.

##### (3) 입력 / 출력

- 입력: lock mode
- 출력: 없음

##### (4) 호출 위치와 흐름

- `execute_query_with_lock`이 `db_execute_sql` 뒤에 호출한다.

##### (5) 한글 의사코드

- 현재 lock 정책을 확인한다.
- 대응되는 lock을 해제한다.

##### (6) 구현 시 주의점

- early return 경로에서 unlock이 빠지지 않게 설계해야 한다.

### 9. 응답 생성 관련 함수

#### 9-1. `build_json_response`

##### (1) 이름

`build_json_response`

##### (2) 역할

- DB 실행 성공 결과를 JSON body로 바꾼다.
- API 계약을 실제 응답 문자열로 만드는 함수다.

##### (3) 입력 / 출력

- 입력: `DbResult`
- 출력: JSON 문자열

##### (4) 호출 위치와 흐름

- `handle_query_request`에서 SQL 실행 후 호출된다.
- 이후 `send_http_response`로 이어진다.

##### (5) 한글 의사코드

- 결과 타입이 SELECT인지 INSERT인지 확인한다.
- 공통 필드인 success 값을 넣는다.
- SELECT면 컬럼과 rows를 JSON 배열로 넣는다.
- INSERT면 affected rows와 message를 넣는다.
- 최종 JSON 문자열을 반환한다.

##### (6) 구현 시 주의점

- 문자열 escape 처리가 정확해야 한다.
- 결과셋이 커질 경우 메모리 사용량이 커질 수 있다.

#### 9-2. `build_json_error_response`

##### (1) 이름

`build_json_error_response`

##### (2) 역할

- 에러 상황을 통일된 JSON 형식으로 만든다.

##### (3) 입력 / 출력

- 입력: 에러 코드, 메시지
- 출력: JSON 문자열

##### (4) 호출 위치와 흐름

- 요청 파싱 실패, SQL 실행 실패, queue overflow 등에서 호출된다.
- 이후 `send_http_response`로 이어진다.

##### (5) 한글 의사코드

- `success: false` 값을 넣는다.
- 에러 코드 문자열을 넣는다.
- 에러 메시지를 넣는다.
- 최종 JSON 문자열을 반환한다.

##### (6) 구현 시 주의점

- 내부 구현 세부 메시지를 과도하게 노출하지 않도록 주의해야 한다.

#### 9-3. `send_http_response`

##### (1) 이름

`send_http_response`

##### (2) 역할

- 상태 코드, 헤더, body를 조합해 HTTP 응답 전체를 socket으로 보낸다.

##### (3) 입력 / 출력

- 입력: `client_fd`, status code, content type, body
- 출력: 성공 / 실패

##### (4) 호출 위치와 흐름

- `handle_health_request`
- `handle_query_request`
- `send_error_response`

##### (5) 한글 의사코드

- status line을 만든다.
- `Content-Type`을 쓴다.
- `Content-Length`를 계산한다.
- 헤더를 보낸다.
- body를 끝까지 보낸다.

##### (6) 구현 시 주의점

- 부분 write 가능성을 고려해야 한다.
- 길이 계산이 틀리면 클라이언트가 응답을 잘못 해석할 수 있다.

#### 9-4. `send_error_response`

##### (1) 이름

`send_error_response`

##### (2) 역할

- 공통 에러 응답 전송 함수다.
- 에러 응답 형식을 통일하고 코드 중복을 줄인다.

##### (3) 입력 / 출력

- 입력: 상태 코드, 에러 코드, 메시지
- 출력: 직접 HTTP 응답 전송

##### (4) 호출 위치와 흐름

- 요청 파싱 오류
- 라우팅 오류
- DB 실행 오류
- queue 가득 참

##### (5) 한글 의사코드

- 에러 JSON body를 만든다.
- 상태 코드에 맞는 HTTP 응답을 만든다.
- socket으로 전송한다.

##### (6) 구현 시 주의점

- 400 / 404 / 405 / 500 / 503 구분 기준을 미리 정하면 좋다.

### 10. 테스트 / 로그 관련 모듈

#### 10-1. `server_logger`

##### (1) 이름

`server_logger`

##### (2) 역할

- 서버 요청 흐름과 오류를 추적하기 위한 로그 모듈이다.
- 과제 데모와 디버깅에서 매우 중요하다.

##### (3) 입력 / 출력

- 입력: 로그 레벨, 요청 정보, 에러 정보
- 출력: 콘솔 또는 로그 파일 출력

##### (4) 호출 위치와 흐름

- accept 시작
- 요청 처리 시작
- 요청 종료
- 에러 발생

##### (5) 한글 의사코드

- 로그 메시지 포맷을 만든다.
- 스레드 정보와 요청 정보를 붙인다.
- 표준 출력 또는 표준 에러로 기록한다.

##### (6) 구현 시 주의점

- 여러 스레드 로그가 뒤섞일 수 있다.
- 로그를 과도하게 많이 찍으면 오히려 분석이 어려워질 수 있다.

#### 10-2. `test_http_parser`

##### (1) 이름

`test_http_parser`

##### (2) 역할

- HTTP 파싱 로직이 기본 케이스와 실패 케이스를 안정적으로 처리하는지 검증한다.

##### (3) 입력 / 출력

- 입력: 샘플 raw HTTP 요청
- 출력: 테스트 성공 / 실패

##### (4) 호출 위치와 흐름

- 테스트 실행 시 독립적으로 호출된다.

##### (5) 한글 의사코드

- 정상 요청 문자열을 준비한다.
- 파싱 결과가 기대값과 같은지 확인한다.
- 잘못된 요청 문자열도 준비한다.
- 실패 응답이 적절한지 확인한다.

##### (6) 구현 시 주의점

- 헤더 누락, body 길이 불일치, 잘못된 method 같은 케이스를 꼭 넣어야 한다.

#### 10-3. `test_thread_pool`

##### (1) 이름

`test_thread_pool`

##### (2) 역할

- 큐와 worker가 요청을 안정적으로 주고받는지 검증한다.

##### (3) 입력 / 출력

- 입력: 테스트용 job
- 출력: 테스트 성공 / 실패

##### (4) 호출 위치와 흐름

- 테스트 실행 시 독립적으로 호출된다.

##### (5) 한글 의사코드

- queue를 초기화한다.
- thread pool을 만든다.
- 여러 job을 제출한다.
- worker가 모든 job을 처리했는지 확인한다.
- 종료가 정상적으로 되는지 확인한다.

##### (6) 구현 시 주의점

- 테스트 자체도 timing 민감할 수 있으므로 deterministic하게 짜는 것이 좋다.

#### 10-4. `test_db_engine_facade`

##### (1) 이름

`test_db_engine_facade`

##### (2) 역할

- SQL 문자열부터 `DbResult`까지 이어지는 핵심 경로를 검증한다.

##### (3) 입력 / 출력

- 입력: 샘플 SQL
- 출력: 테스트 성공 / 실패

##### (4) 호출 위치와 흐름

- 테스트 실행 시 독립적으로 호출된다.

##### (5) 한글 의사코드

- INSERT SQL을 실행한다.
- 성공 결과와 affected rows를 확인한다.
- SELECT SQL을 실행한다.
- 결과 컬럼과 row 수를 확인한다.
- `WHERE id = ?` 경로도 확인한다.

##### (6) 구현 시 주의점

- 현재 엔진 제약과 동일한 테스트 기준을 유지해야 한다.
- `DELETE` 비지원 여부도 명확히 검증하면 좋다.

#### 10-5. `test_api_smoke`

##### (1) 이름

`test_api_smoke`

##### (2) 역할

- 실제 서버 프로세스를 띄운 뒤 HTTP 요청을 보내는 end-to-end 검증이다.

##### (3) 입력 / 출력

- 입력: curl 또는 간단한 클라이언트 요청
- 출력: 테스트 성공 / 실패

##### (4) 호출 위치와 흐름

- 서버 실행 후 테스트 스크립트에서 호출된다.

##### (5) 한글 의사코드

- 서버를 백그라운드로 실행한다.
- `/health` 요청을 보낸다.
- `INSERT` 요청을 보낸다.
- `SELECT` 요청을 보낸다.
- 동시 요청을 여러 개 보낸다.
- 응답이 기대값과 같은지 확인한다.
- 서버를 종료한다.

##### (6) 구현 시 주의점

- 테스트 시작 전 DB 상태를 초기화해야 한다.
- 서버 시작 타이밍을 기다리는 로직이 필요할 수 있다.

## Part 4. 팀이 검토해야 할 설계 포인트

아래 항목은 구현 전에 반드시 팀이 함께 검토해야 한다.

### 1. `executor`를 어떻게 API 친화적으로 바꿀 것인가

현재 `executor_execute`는 결과를 표 형태로 `stdout`에 출력한다.
API 서버에서는 이 출력 대신 구조화된 `DbResult`가 필요하다.

검토 포인트는 아래와 같다.

- 출력 로직과 실행 로직을 분리할지
- 새로운 `executor_execute_into_result`를 추가할지
- 기존 `executor_execute`는 CLI용으로 유지할지
> TODO: 기존처럼 터미널에 표 형태로 출력하게 하고 그리고 DbResult로도 반환하게 한다.

이 부분이 정리되어야 Step 2 이후 구현이 자연스럽다.

### 2. `table_runtime`의 단일 활성 테이블 제약을 그대로 둘 것인가

현재 `table_runtime`는 내부에 단일 활성 테이블 하나만 유지한다.
즉, 서로 다른 테이블 요청이 번갈아 들어오면 기존 메모리 상태를 해제하고 갈아끼우는 구조다.

검토 포인트는 아래와 같다.

- MVP를 단일 테이블 데모로 제한할지
- 여러 테이블을 지원하려면 registry 구조로 바꿀지
- 지금 과제 범위에서 어느 정도까지 일반화할지
> 지금은 테이블 전체가 메모리에 올라가는데, select를 할 때 메모리를 확인하고 있다면 메모리에서 가져오고 없다면 메모리에 올리고 디스크에서 가져오게끔 구현해줘 
### 3. tokenizer 전역 캐시를 어떻게 볼 것인가

현재 tokenizer는 전역 캐시를 갖고 있다.
이 때문에 SELECT조차 완전히 읽기 전용 경로로 보기 어렵다.

검토 포인트는 아래와 같다.

- API 서버 모드에서는 tokenizer 캐시를 그대로 쓸지
- 캐시에 별도 lock을 둘지
- 일단 전역 mutex로 전체 엔진 경로를 보호할지
> TODO: tokenizer 캐시에 mutex락을 걸고 나중에 db엔진단에는 read/write 락을 건다

MVP 관점에서는 전역 mutex가 가장 안전하다.

### 4. 요청 포맷과 응답 스키마를 어디까지 단순화할 것인가

검토 포인트는 아래와 같다.

- 요청 body를 `{"sql":"..."}` 하나로 고정할지
- 응답에 `success`, `message`, `rows`, `columns`, `row_count`를 어떤 형식으로 넣을지
- 에러 응답 스키마를 어떻게 통일할지

이 부분이 먼저 정해져야 테스트도 고정하기 쉽다.

### 5. queue full 정책

thread pool을 쓰면 queue가 꽉 찰 수 있다.

검토 포인트는 아래와 같다.

- 가득 찼을 때 block할지
- 즉시 `503 Service Unavailable`로 응답할지

> TODO: 즉시 503 Service Unavailable 응답

과제 시연 관점에서는 즉시 실패가 더 단순하고 설명하기 쉽다.

### 6. lock 범위를 어디까지 잡을 것인가

검토 포인트는 아래와 같다.

- tokenizer부터 executor까지 모두 lock 안에 넣을지
- 결과 구조체 변환까지 lock 안에 둘지
- JSON 응답 생성은 lock 밖으로 뺄지

MVP에서는 "DB 내부 상태를 읽고 쓰는 동안만 lock 유지"가 가장 균형이 좋다.

## Part 5. 구현 전에 확정해야 할 결정사항

아래 항목은 구현 시작 전에 팀이 합의해야 한다.

### 1. 팀이 지금 결정해야 할 것

- API 엔드포인트를 `GET /health`, `POST /query` 두 개로 확정할지
- 요청 포맷을 `{"sql":"..."}`로 고정할지
- MVP SQL 범위를 `INSERT`, `SELECT` 중심으로 제한할지
- queue full 시 즉시 실패할지
- 전역 DB mutex를 1차 정책으로 확정할지

> TODO: 1. API 엔드포인트 두개로 확정 2. 요청 포맷을 {"sql":"..."}` 고정 3. INSERT, SELECT 4. queue full시 즉시 실패 5. 1차 정책으로 mutex 후 read/write 락으로 개선

### 2. 아직 열려 있는 설계 선택지

- `table_runtime`를 단일 활성 테이블 구조로 유지할지, 다중 테이블 registry로 확장할지
- tokenizer 캐시를 유지할지, API 서버 모드에서는 비활성화할지
- API 서버용 바이너리를 별도로 만들지, 기존 프로그램에 모드를 추가할지
- JSON 파서를 직접 간단히 구현할지, 작은 외부 의존성을 허용할지
- 향후 rwlock 확장을 어느 정도까지 열어둘지

> TODO: 1번 단일 활성 테이블, 2번 tokenizer 캐시 유지, 3번 별도로 만든다, 4번 작은 의존성 허용, 5번 rwlock으로 무조건 개선할거야 
### 3. 구현 전에 합의해야 할 사항

- `DbResult` 구조
- 에러 응답 구조
- HTTP status 매핑 기준
- lock 범위
- 멀티테이블 지원 여부
- 테스트 기준 시나리오
- 데모 시나리오
- 로그 포맷

## 결론

이번 과제의 핵심은 "새 DBMS를 만드는 것"이 아니라, 이미 구현한 SQL 처리기와 B+ Tree 기반 런타임 엔진을 API 서버 구조에 안전하게 연결하는 것이다.

가장 먼저 정리해야 할 것은 아래 두 가지다.

1. `executor`의 출력 중심 구조를 `DbResult` 반환 구조로 어떻게 바꿀 것인가
2. 현재 `table_runtime`의 단일 활성 테이블 제약을 MVP에서 어떻게 다룰 것인가

이 두 가지가 정리되면, 이후의 API 서버, thread pool, lock 적용은 비교적 자연스럽게 구현 단계로 이어질 수 있다.
