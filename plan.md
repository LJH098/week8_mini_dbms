# 미니 DBMS API 서버 최종 구현 계획서

이 문서는 [API_SERVER_DESIGN.md](/Users/jinhyuk/krafton/week7_index/docs/API_SERVER_DESIGN.md:1)를 바탕으로, 그 안에 달아둔 `TODO`를 실제 구현 정책으로 반영해 다시 정리한 **최종 구현 기준 문서**다.

즉, 이 문서는 단순 요약본이 아니다.
팀원이 이 문서만 읽어도 아래 내용을 이해하고 구현을 시작할 수 있어야 한다.

- 이번 과제의 핵심 목표
- 구현 범위와 제외 범위
- 전체 아키텍처
- 파일 / 폴더 구조
- 요청 처리 흐름
- 핵심 모듈과 함수
- 각 함수의 역할, 입력/출력, 호출 위치, 한글 의사코드
- 동시성 제어 전략
- 테스트 및 데모 기준
- 구현 단계 순서와 완료 조건

## 문서 사용 원칙

- 이 문서는 **구현 기준 문서**다.
- [API_SERVER_DESIGN.md](/Users/jinhyuk/krafton/week7_index/docs/API_SERVER_DESIGN.md:1)는 설계 배경과 설명이 담긴 상위 참고 문서다.
- 두 문서 사이에 표현 차이가 있으면, **TODO가 반영된 최종 정책은 이 `plan.md`를 우선**한다.
- 구현자는 "설계 의도 이해"가 필요할 때 `API_SERVER_DESIGN.md`를 참고하고, 실제 구현 방향과 정책은 `plan.md`를 따른다.

---

## Part 1. 최종 구현 목표와 범위

### 1. 핵심 목표

이번 과제의 핵심 목표는 기존에 구현한 SQL 처리기와 B+ Tree 인덱스를 재사용하여 내부 DB 엔진을 만들고, 그 위에 외부 클라이언트가 요청할 수 있는 API 서버를 구성하는 것이다.

핵심은 세 가지다.

1. 기존 SQL 처리 흐름을 최대한 유지한다.
2. API 서버 요청을 Thread Pool 기반으로 병렬 처리한다.
3. 멀티스레드 환경에서도 DB 엔진 정합성이 깨지지 않도록 최소한의 락 정책을 먼저 적용한다.

현재 재사용 대상 엔진의 중심 흐름은 아래와 같다.

- `tokenizer -> parser -> executor -> table_runtime -> bptree`

또한 현재 코드 구조에서 반드시 기억해야 할 제약은 아래와 같다.

- `executor`는 결과를 `stdout`에 표 형태로 출력하는 구조다.
- `table_runtime`는 단일 활성 테이블 중심 구조다.
- `tokenizer`는 전역 캐시를 유지한다.
- `bptree`, `table_runtime`는 자체적인 멀티스레드 보호가 없다.

따라서 이번 구현의 핵심은 "새 DBMS 작성"이 아니라, **기존 엔진을 API 서버용으로 감싸고 안전하게 병렬 처리 가능한 구조로 재배치하는 것**이다.

### 2. MVP 범위

이번 과제의 MVP 범위는 아래로 고정한다.

- API 엔드포인트는 `GET /health`, `POST /query` 두 개만 지원한다.
- `/query` 요청 body 형식은 `{"sql":"..."}`로 고정한다.
- 요청 하나당 SQL 문장 하나만 처리한다.
- MVP에서 지원하는 SQL은 `INSERT`, `SELECT`로 제한한다.
- `SELECT ... WHERE id = ?`는 기존 B+ Tree 인덱스 경로를 활용한다.
- 일반 `WHERE`는 기존 선형 탐색 경로를 활용한다.
- Thread Pool 기반으로 병렬 처리한다.
- queue가 가득 차면 즉시 실패하고 `503 Service Unavailable`를 반환한다.
- 1차 동시성 제어는 전역 DB mutex를 사용한다.
- 이후 개선 단계에서 tokenizer cache mutex + DB read/write lock으로 확장한다.

### 3. 제외 범위

이번 구현에서는 아래 항목을 제외한다.

- `DELETE` 정식 지원
- 다중 SQL 문장 배치 실행
- JOIN
- 트랜잭션
- 인증 / 인가
- TLS / HTTPS
- keep-alive 최적화
- 완전한 HTTP 표준 지원
- 레코드 단위 lock
- 복잡한 lock hierarchy
- 다중 프로세스 동기화
- 분산 처리
- 완전한 다중 테이블 메모리 cache

이 과제의 목적은 "동작 가능한 API 서버 + 안전한 병렬 요청 처리"이지, 완전한 DBMS 기능 구현이 아니다.

---

## Part 2. TODO를 반영한 최종 정책

이 파트는 `API_SERVER_DESIGN.md` 안에 적어 둔 TODO를 실제 구현 정책으로 확정한 내용이다.

### 1. 폴더 구조 정책

기존처럼 `src/` 아래에 모든 `.c`, `.h` 파일을 평면적으로 두지 않는다.
네트워크, 동시성, DB 엔진, 공통 유틸, CLI 진입점을 기준으로 폴더를 분리한다.

즉, 이번 구현은 단순 파일 추가가 아니라 **계층 기반 폴더 구조 재정리**를 전제로 한다.

### 2. 실행 결과 정책

`executor`는 기존처럼 터미널에 표 형태로 결과를 출력할 수 있어야 한다.
동시에 API 서버를 위해서는 구조화된 `DbResult`도 반환할 수 있어야 한다.

즉, 정책은 아래와 같다.

- 기존 CLI 출력 유지
- API용 `DbResult` 반환 추가
- 출력 로직과 실행 로직 분리

### 3. queue full 정책

queue가 가득 찼을 때 block하지 않는다.
즉시 실패한다.
응답 코드는 `503 Service Unavailable`로 고정한다.

정책 요약:

- queue full -> 즉시 실패
- 즉시 HTTP 503 반환

### 4. API 정책

이번 과제에서 API 정책은 아래로 확정한다.

- 엔드포인트는 `GET /health`, `POST /query`
- 요청 포맷은 `{"sql":"..."}`
- 지원 SQL은 `INSERT`, `SELECT`

### 5. 동시성 정책

1차 구현 정책:

- 전역 DB mutex 사용

2차 개선 정책:

- tokenizer 캐시에 별도 mutex 적용
- DB 엔진에는 read/write lock 적용
- `SELECT`는 read lock
- `INSERT`는 write lock

즉, 이번 과제는 "mutex로 시작해서 rwlock으로 개선하는 로드맵"을 전제로 한다.

### 6. 런타임 테이블 정책

현재 구조는 단일 활성 테이블 중심으로 유지한다.
다만 `SELECT` 요청이 들어왔을 때,

- 요청한 테이블이 이미 메모리에 올라와 있으면 메모리에서 사용한다.
- 메모리에 없으면 디스크에서 읽어 메모리에 올리고 사용한다.

즉, 이번 과제는 "단일 활성 테이블 + 필요 시 디스크 로딩" 정책으로 간다.

### 7. 빌드 정책

API 서버는 기존 CLI와 별도 바이너리로 만든다.
즉, 기존 SQL REPL / 파일 실행 프로그램과 API 서버 실행 파일은 구분한다.

### 8. 의존성 정책

작은 외부 의존성은 허용한다.
다만 과제 범위를 벗어나는 수준으로 무거운 프레임워크를 도입하지는 않는다.

---

## Part 3. 전체 아키텍처

### 1. 계층 구조

이번 시스템은 아래 계층으로 나눈다.

- API 계층
  - 소켓 생성
  - HTTP 요청 읽기
  - HTTP 파싱
  - 라우팅
  - 응답 생성

- 동시성 계층
  - thread pool
  - job queue
  - mutex / rwlock 관리

- DB 엔진 계층
  - tokenizer
  - parser
  - executor
  - table_runtime
  - bptree
  - storage
  - facade

- 공통 계층
  - 문자열 / 버퍼 / 비교 / 파일 유틸

- CLI 계층
  - 기존 REPL / 파일 모드 실행 진입점

### 2. 최종 폴더 구조 제안

```text
week7_index/
├── src/
│   ├── api/
│   │   ├── api_main.c
│   │   ├── api_server.c
│   │   ├── api_server.h
│   │   ├── http_parser.c
│   │   ├── http_parser.h
│   │   ├── request_router.c
│   │   ├── request_router.h
│   │   ├── response_builder.c
│   │   ├── response_builder.h
│   │   ├── server_logger.c
│   │   └── server_logger.h
│   ├── concurrency/
│   │   ├── thread_pool.c
│   │   ├── thread_pool.h
│   │   ├── job_queue.c
│   │   ├── job_queue.h
│   │   ├── lock_manager.c
│   │   └── lock_manager.h
│   ├── db/
│   │   ├── db_engine_facade.c
│   │   ├── db_engine_facade.h
│   │   ├── executor_result.c
│   │   ├── executor_result.h
│   │   ├── tokenizer.c
│   │   ├── tokenizer.h
│   │   ├── parser.c
│   │   ├── parser.h
│   │   ├── executor.c
│   │   ├── executor.h
│   │   ├── table_runtime.c
│   │   ├── table_runtime.h
│   │   ├── bptree.c
│   │   ├── bptree.h
│   │   ├── storage.c
│   │   └── storage.h
│   ├── common/
│   │   ├── utils.c
│   │   └── utils.h
│   └── cli/
│       └── main.c
├── tests/
│   ├── api/
│   ├── concurrency/
│   ├── db/
│   └── integration/
├── docs/
│   └── API_SERVER_DESIGN.md
└── plan.md
```

### 3. 전체 요청 처리 흐름

클라이언트 요청이 들어와 응답이 나갈 때까지의 흐름은 아래와 같다.

1. 클라이언트가 서버에 TCP 연결을 시도한다.
2. 메인 스레드가 `accept`로 연결을 받는다.
3. 연결 정보를 `ClientJob`으로 만들어 queue에 넣는다.
4. worker thread가 queue에서 job을 꺼낸다.
5. worker가 HTTP 요청을 socket에서 읽는다.
6. raw HTTP를 `HttpRequest` 구조체로 파싱한다.
7. 라우터가 path와 method를 보고 `/health` 또는 `/query`로 분기한다.
8. `/query`면 JSON body에서 SQL 문자열을 추출한다.
9. `execute_query_with_lock`이 lock을 획득한다.
10. `db_engine_facade`가 tokenizer / parser / executor 경로를 호출한다.
11. `executor`는 기존 DB 엔진을 사용해 결과를 만든다.
12. 결과는 `DbResult`로 정리된다.
13. lock을 해제한다.
14. `response_builder`가 `DbResult`를 JSON 응답으로 변환한다.
15. HTTP 응답을 socket에 전송한다.
16. client socket을 닫는다.

### 4. 테이블 로딩 흐름

단일 활성 테이블 정책을 따르되, 요청 시 메모리 적재 여부를 먼저 확인한다.

`SELECT` 처리 흐름:

1. 요청 테이블 이름을 확인한다.
2. 현재 활성 테이블이 같은 테이블인지 확인한다.
3. 같고 메모리에 올라와 있으면 메모리에서 바로 조회한다.
4. 다르거나 아직 메모리에 없으면 디스크에서 읽는다.
5. 읽은 내용을 메모리 런타임 구조에 올린다.
6. 이후 SELECT를 수행한다.

`INSERT` 처리 흐름:

1. 대상 테이블이 현재 활성 테이블인지 확인한다.
2. 필요하면 디스크에서 현재 테이블 상태를 읽어 메모리에 올린다.
3. 메모리 row 저장
4. B+Tree 갱신
5. 필요하면 디스크 반영 정책 수행

이번 과제에서는 다중 테이블 동시 상주 cache보다, "필요할 때 로드하는 단일 활성 테이블 모델"을 유지하는 것이 중요하다.

---

## Part 4. 구현 단계 순서

### Step 0. 폴더 구조와 빌드 재정리

목표:

- 평면 `src/` 구조를 계층형 폴더 구조로 분리한다.
- CLI와 API 서버 바이너리를 분리한다.
- 테스트 빌드 경로도 함께 정리한다.

작업 내용:

- `Makefile` 수정
- include 경로 수정
- 소스 파일 이동 또는 재배치
- 테스트 경로 재정리

완료 조건:

- `src/api`, `src/concurrency`, `src/db`, `src/common`, `src/cli` 구조가 잡혀 있다.
- CLI 바이너리와 API 서버 바이너리를 각각 빌드할 수 있다.

### Step 1. DB 엔진 facade와 결과 구조 분리

목표:

- 기존 엔진을 API 서버에서 사용할 수 있는 형태로 정리한다.
- `executor`가 표 출력과 `DbResult` 반환을 둘 다 지원하게 만든다.

작업 내용:

- `DbResult` 정의
- `db_engine_facade` 추가
- `executor_execute_into_result` 계열 구조 설계
- 기존 CLI 출력 경로 유지

완료 조건:

- SQL 문자열 하나를 입력하면 구조화된 `DbResult`를 받을 수 있다.
- 기존 CLI 경로에서는 표 출력이 유지된다.

### Step 2. 테이블 메모리 / 디스크 로딩 정책 반영

목표:

- 요청한 테이블이 메모리에 있으면 메모리 사용
- 없으면 디스크에서 로드

작업 내용:

- `table_runtime`에 로딩 정책 반영
- `storage`와 연결
- SELECT 시 메모리 적중 / 디스크 로딩 분기

완료 조건:

- 동일 테이블 재조회 시 메모리를 활용할 수 있다.
- 메모리에 없는 테이블 요청 시 디스크 로딩 후 정상 조회된다.

### Step 3. 싱글 스레드 API 서버

목표:

- HTTP 요청을 받고 DB 엔진까지 한 번 연결한다.

작업 내용:

- `GET /health`
- `POST /query`
- HTTP 요청 읽기
- JSON body 파싱
- JSON 응답 반환

완료 조건:

- 단일 스레드 서버가 뜬다.
- `/health`, `/query`가 정상 응답한다.
- `INSERT`, `SELECT`가 API 경로로 동작한다.

### Step 4. Thread Pool + Job Queue

목표:

- 요청 처리 방식을 병렬 구조로 바꾼다.

작업 내용:

- `thread_pool`
- `job_queue`
- worker 기반 요청 처리
- queue full 즉시 실패 정책 반영

완료 조건:

- 메인 스레드는 accept와 job 제출만 담당한다.
- worker가 요청을 처리한다.
- queue가 가득 차면 즉시 실패하고 503을 보낸다.

### Step 5. 전역 DB mutex 적용 + 테스트 / 데모

목표:

- 최소한의 정합성을 먼저 확보한다.

작업 내용:

- 전역 DB mutex 적용
- DB 엔진 진입 구간 보호
- 동시 요청 smoke test
- 데모 시나리오 준비

완료 조건:

- 동시 `INSERT`, `SELECT`에서도 crash가 없다.
- `next_id`, row 배열, B+Tree 상태가 깨지지 않는다.
- 기본 API / 동시성 테스트가 통과한다.

### Step 6. 2차 개선: tokenizer cache mutex + DB read/write lock

목표:

- 전역 mutex보다 나은 병렬성을 확보한다.

작업 내용:

- tokenizer 캐시에 전용 mutex 적용
- DB 엔진 단에 read/write lock 적용
- `SELECT`는 read lock
- `INSERT`는 write lock

완료 조건:

- tokenizer 캐시 경쟁 조건이 별도로 보호된다.
- DB 엔진 락이 rwlock 구조로 개선된다.
- 동시 SELECT 병렬성이 향상된다.

중요:

- 이 단계는 설계상 "반드시 이어서 할 개선"이다.
- 이번 과제 제출의 핵심 안정 버전은 Step 5까지지만, 팀 내부 정책상 Step 6도 목표 범위에 포함한다.

---

## Part 5. 핵심 모듈 / 함수 목록

### 1. 진입점 / 서버 초기화

- `main`
- `load_server_config`
- `create_server_socket`
- `shutdown_server`

### 2. 요청 수신 / 라우팅

- `server_accept_loop`
- `read_http_request`
- `parse_http_request`
- `extract_sql_from_json`
- `route_request`
- `handle_health_request`
- `handle_query_request`

### 3. thread pool / job queue

- `thread_pool_init`
- `thread_pool_submit`
- `thread_pool_shutdown`
- `worker_main`
- `queue_init`
- `queue_push`
- `queue_pop`
- `queue_shutdown`

### 4. DB 엔진 facade

- `db_engine_init`
- `db_execute_sql`
- `db_engine_shutdown`
- `execute_query_with_lock`
- `executor_execute_into_result`
- `executor_render_result_for_cli`

### 5. 기존 엔진 재사용 함수

- `tokenizer_tokenize`
- `parser_parse`
- `table_get_or_load`
- `table_load_from_storage_if_needed`
- `table_insert_row`
- `table_linear_scan_by_field`
- `table_get_row_by_slot`
- `bptree_search`
- `bptree_insert`

### 6. 락 관련

- `init_lock_manager`
- `lock_db_for_query`
- `unlock_db_for_query`
- `lock_tokenizer_cache`
- `unlock_tokenizer_cache`
- `destroy_lock_manager`

### 7. 응답 생성 / 전송

- `build_json_response`
- `build_json_error_response`
- `send_http_response`
- `send_error_response`

### 8. 로그 / 테스트

- `log_request_start`
- `log_request_end`
- `log_server_error`
- `test_http_parser`
- `test_thread_pool`
- `test_db_engine_facade`
- `test_api_smoke`

---

## Part 6. 각 함수 / 모듈의 상세 설명

아래 설명은 요청 흐름 순서대로 정리한다.
즉, "클라이언트 요청 -> 서버 초기화 -> 수신 -> 파싱 -> 큐 -> worker -> DB 실행 -> 응답" 흐름이 눈에 보이도록 배치한다.

### 1. `main`

#### (1) 이름

`main`

#### (2) 역할

- API 서버 프로세스의 최상위 진입점이다.
- 서버 설정을 읽고, 필요한 모듈을 순서대로 초기화한다.
- API 서버 바이너리의 전체 생명주기를 관리한다.

#### (3) 입력 / 출력

- 입력: 실행 인자, 환경변수, 기본 설정
- 출력: 종료 코드

#### (4) 호출 위치와 흐름

- 프로그램 시작 시 호출된다.
- 내부에서 `load_server_config -> db_engine_init -> init_lock_manager -> thread_pool_init -> create_server_socket -> server_accept_loop` 순으로 이어진다.

#### (5) 한글 의사코드

- 실행 인자를 확인한다.
- 포트, worker 수, queue 크기를 정리한다.
- DB 엔진 facade를 초기화한다.
- lock manager를 초기화한다.
- thread pool과 queue를 초기화한다.
- 서버 소켓을 생성한다.
- accept loop에 진입한다.
- 종료 신호가 오면 자원을 역순으로 정리한다.

#### (6) 구현 시 주의점

- 초기화 중간에 실패하면 부분적으로 만든 자원을 역순으로 해제해야 한다.
- CLI 진입점과 API 서버 진입점은 별도 바이너리로 유지해야 한다.

### 2. 서버 초기화 관련 함수

#### 2-1. `load_server_config`

##### (1) 이름

`load_server_config`

##### (2) 역할

- 서버 전체 설정을 하나의 구조체로 정리한다.
- 포트, worker 수, queue 크기 같은 값을 흩어지지 않게 관리한다.

##### (3) 입력 / 출력

- 입력: argv, 환경 변수, 기본값
- 출력: `ServerConfig`

##### (4) 호출 위치와 흐름

- `main`이 가장 먼저 호출한다.

##### (5) 한글 의사코드

- 기본 포트를 설정한다.
- 기본 worker 수를 정한다.
- 기본 queue 크기를 정한다.
- 사용자 입력이 있으면 설정값을 덮어쓴다.
- 유효 범위를 검사한다.
- 설정 구조체를 반환한다.

##### (6) 구현 시 주의점

- 너무 큰 queue나 worker 수를 허용하면 디버깅이 어려워질 수 있다.

#### 2-2. `create_server_socket`

##### (1) 이름

`create_server_socket`

##### (2) 역할

- 클라이언트 요청을 받을 listening socket을 생성한다.

##### (3) 입력 / 출력

- 입력: 포트, backlog
- 출력: 서버 socket fd

##### (4) 호출 위치와 흐름

- `main`에서 호출되고, 성공 시 `server_accept_loop`로 넘어간다.

##### (5) 한글 의사코드

- TCP 소켓을 생성한다.
- 포트 재사용 옵션을 설정한다.
- 주소 구조체를 채운다.
- 포트에 bind 한다.
- listen 상태로 전환한다.
- 서버 socket fd를 반환한다.

##### (6) 구현 시 주의점

- bind 실패 시 fd를 정리해야 한다.
- backlog는 queue full 정책과 함께 고려해야 한다.

#### 2-3. `shutdown_server`

##### (1) 이름

`shutdown_server`

##### (2) 역할

- 서버 종료 시 필요한 자원 정리를 한 곳에서 수행한다.

##### (3) 입력 / 출력

- 입력: 서버 소켓, thread pool, queue, lock manager, DB 컨텍스트
- 출력: 없음

##### (4) 호출 위치와 흐름

- `main` 종료 직전에 호출된다.

##### (5) 한글 의사코드

- 새 요청 수신을 멈춘다.
- queue 종료 플래그를 켠다.
- 대기 중인 worker를 깨운다.
- worker thread를 모두 join 한다.
- 소켓을 닫는다.
- lock manager를 정리한다.
- DB 엔진을 정리한다.

##### (6) 구현 시 주의점

- worker가 아직 실행 중인데 DB 엔진 자원을 먼저 해제하면 안 된다.

### 3. 요청 수신 관련 함수

#### 3-1. `server_accept_loop`

##### (1) 이름

`server_accept_loop`

##### (2) 역할

- 서버가 계속해서 클라이언트 연결을 받는 루프다.
- 메인 스레드의 핵심 작업은 accept와 queue 전달이다.

##### (3) 입력 / 출력

- 입력: 서버 socket fd, thread pool 또는 queue 핸들
- 출력: 없음

##### (4) 호출 위치와 흐름

- `main`에서 호출된다.
- 각 요청은 `thread_pool_submit`으로 넘어간다.

##### (5) 한글 의사코드

- 무한 루프를 돈다.
- 클라이언트 연결을 accept 한다.
- `ClientJob` 구조를 만든다.
- queue에 제출한다.
- queue가 가득 차면 즉시 503 응답을 보내고 소켓을 닫는다.
- 종료 조건이면 루프를 빠져나온다.

##### (6) 구현 시 주의점

- queue 제출 실패 시 소켓을 닫지 않으면 fd 누수가 생긴다.
- accept 루프에서 무거운 처리를 하지 말아야 한다.

#### 3-2. `read_http_request`

##### (1) 이름

`read_http_request`

##### (2) 역할

- 클라이언트 소켓에서 raw HTTP 요청 전체를 읽는다.

##### (3) 입력 / 출력

- 입력: `client_fd`
- 출력: raw HTTP 요청 문자열

##### (4) 호출 위치와 흐름

- `handle_client_job`이 가장 먼저 호출한다.
- 완료 후 `parse_http_request`로 이어진다.

##### (5) 한글 의사코드

- 소켓에서 데이터를 읽는다.
- 헤더 종료 구분자까지 읽는다.
- `Content-Length`를 확인한다.
- body 길이만큼 추가로 읽는다.
- 전체 요청 버퍼를 반환한다.

##### (6) 구현 시 주의점

- `recv`는 부분 읽기가 가능하다.
- body 크기 상한이 필요하다.
- timeout이 없으면 worker가 오래 묶일 수 있다.

### 4. HTTP 파싱 관련 함수

#### 4-1. `parse_http_request`

##### (1) 이름

`parse_http_request`

##### (2) 역할

- raw HTTP 문자열을 `HttpRequest` 구조체로 바꾼다.

##### (3) 입력 / 출력

- 입력: raw HTTP 요청
- 출력: `HttpRequest`

##### (4) 호출 위치와 흐름

- `read_http_request` 뒤에 호출된다.
- 이후 `route_request`로 이어진다.

##### (5) 한글 의사코드

- 첫 줄에서 method, path, protocol을 읽는다.
- 헤더를 줄 단위로 분리한다.
- 필요한 헤더만 저장한다.
- body 위치를 기록한다.
- 구조체로 정리한다.

##### (6) 구현 시 주의점

- malformed request를 적절히 거부해야 한다.
- CRLF 규칙을 명확히 지켜야 한다.

#### 4-2. `extract_sql_from_json`

##### (1) 이름

`extract_sql_from_json`

##### (2) 역할

- `/query` body에서 SQL 문자열을 꺼낸다.

##### (3) 입력 / 출력

- 입력: JSON body 문자열
- 출력: SQL 문자열

##### (4) 호출 위치와 흐름

- `handle_query_request`에서 호출된다.
- 이후 `execute_query_with_lock`으로 넘어간다.

##### (5) 한글 의사코드

- body가 비어 있지 않은지 확인한다.
- `"sql"` 키를 찾는다.
- 문자열 값을 추출한다.
- escape 문자를 해석한다.
- SQL 문자열을 반환한다.

##### (6) 구현 시 주의점

- 직접 JSON 파싱 시 문자열 escape 처리에 특히 주의해야 한다.
- 요청 형식을 고정해 두었기 때문에 그 범위 안에서 단순 파싱을 유지하는 것이 좋다.

### 5. thread pool / job queue 관련 함수

#### 5-1. `thread_pool_init`

##### (1) 이름

`thread_pool_init`

##### (2) 역할

- worker thread들을 생성하고 실행 준비를 마친다.

##### (3) 입력 / 출력

- 입력: worker 수, queue, 공용 컨텍스트
- 출력: 초기화된 thread pool

##### (4) 호출 위치와 흐름

- `main`에서 호출된다.
- 각 worker는 `worker_main`을 실행한다.

##### (5) 한글 의사코드

- thread pool 구조체를 초기화한다.
- worker 수만큼 thread를 생성한다.
- 각 worker에 queue와 공용 컨텍스트를 넘긴다.
- 준비가 끝나면 성공을 반환한다.

##### (6) 구현 시 주의점

- 일부 thread만 생성된 상태에서 실패하면 정리가 필요하다.

#### 5-2. `thread_pool_submit`

##### (1) 이름

`thread_pool_submit`

##### (2) 역할

- 새 클라이언트 요청을 queue에 넣는다.

##### (3) 입력 / 출력

- 입력: `ClientJob`
- 출력: 성공 / 실패

##### (4) 호출 위치와 흐름

- `server_accept_loop`에서 호출된다.
- 내부적으로 `queue_push`를 사용한다.

##### (5) 한글 의사코드

- job이 유효한지 확인한다.
- queue에 자리가 있는지 확인한다.
- 자리가 있으면 job을 push 한다.
- worker를 깨운다.
- 실패하면 호출자에게 실패를 돌려준다.

##### (6) 구현 시 주의점

- queue full일 때 block하지 않는 정책을 분명히 지켜야 한다.

#### 5-3. `worker_main`

##### (1) 이름

`worker_main`

##### (2) 역할

- worker thread의 메인 루프다.
- queue에서 job을 꺼내 실제 요청 처리 함수로 넘긴다.

##### (3) 입력 / 출력

- 입력: worker 컨텍스트
- 출력: thread 종료

##### (4) 호출 위치와 흐름

- `thread_pool_init`이 worker 시작 함수로 사용한다.
- 내부에서 `queue_pop -> handle_client_job`을 반복한다.

##### (5) 한글 의사코드

- 무한 루프를 시작한다.
- queue에서 job을 꺼낸다.
- 종료 상태인지 확인한다.
- 정상 job이면 `handle_client_job`을 호출한다.
- 다음 job을 기다린다.

##### (6) 구현 시 주의점

- shutdown 시 무한 대기 상태에 빠지지 않도록 condition variable 처리가 중요하다.

#### 5-4. `queue_init`

##### (1) 이름

`queue_init`

##### (2) 역할

- job queue와 내부 동기화 도구를 초기화한다.

##### (3) 입력 / 출력

- 입력: queue 크기
- 출력: 초기화된 queue

##### (4) 호출 위치와 흐름

- `main` 또는 `thread_pool_init` 전에 호출된다.

##### (5) 한글 의사코드

- 큐 버퍼를 할당한다.
- head, tail, count를 0으로 초기화한다.
- mutex를 초기화한다.
- condition variable을 초기화한다.

##### (6) 구현 시 주의점

- queue 크기는 0이 될 수 없다.

#### 5-5. `queue_push`

##### (1) 이름

`queue_push`

##### (2) 역할

- 생산자 쪽에서 새 job을 큐에 넣는다.

##### (3) 입력 / 출력

- 입력: queue, job
- 출력: 성공 / 실패

##### (4) 호출 위치와 흐름

- `thread_pool_submit`이 호출한다.

##### (5) 한글 의사코드

- queue mutex를 잡는다.
- 종료 상태인지 확인한다.
- 큐가 가득 찼는지 확인한다.
- 가득 찼으면 즉시 실패를 반환한다.
- 비어 있으면 job을 tail에 넣는다.
- count와 tail을 갱신한다.
- worker를 깨운다.
- mutex를 푼다.

##### (6) 구현 시 주의점

- 이번 정책은 "즉시 실패"다.
- block 동작을 넣지 않는다.

#### 5-6. `queue_pop`

##### (1) 이름

`queue_pop`

##### (2) 역할

- worker가 처리할 다음 job을 꺼낸다.

##### (3) 입력 / 출력

- 입력: queue
- 출력: `ClientJob`

##### (4) 호출 위치와 흐름

- `worker_main`에서 반복 호출된다.

##### (5) 한글 의사코드

- queue mutex를 잡는다.
- 큐가 비어 있으면 기다린다.
- 종료 상태인지 확인한다.
- job 하나를 꺼낸다.
- head와 count를 갱신한다.
- mutex를 푼다.
- job을 반환한다.

##### (6) 구현 시 주의점

- condition wait는 항상 while 루프로 감싸야 한다.

### 6. worker가 실제 요청 처리하는 함수

#### 6-1. `handle_client_job`

##### (1) 이름

`handle_client_job`

##### (2) 역할

- worker thread가 요청 하나를 실제로 끝까지 처리하는 중심 함수다.
- 네트워크 요청과 DB 엔진 실행을 연결한다.

##### (3) 입력 / 출력

- 입력: `ClientJob`
- 출력: 직접 HTTP 응답을 보내고 소켓을 닫는다.

##### (4) 호출 위치와 흐름

- `worker_main`에서 호출된다.
- 내부에서 `read_http_request -> parse_http_request -> route_request` 흐름으로 이어진다.

##### (5) 한글 의사코드

- 클라이언트 소켓에서 요청을 읽는다.
- HTTP 요청을 파싱한다.
- path와 method를 확인한다.
- `/health` 또는 `/query`로 분기한다.
- 응답을 만든다.
- 응답을 전송한다.
- 소켓과 임시 메모리를 정리한다.

##### (6) 구현 시 주의점

- 실패 경로에서도 소켓은 반드시 닫혀야 한다.
- DB lock을 네트워크 I/O 전체에 걸지 않아야 한다.

#### 6-2. `route_request`

##### (1) 이름

`route_request`

##### (2) 역할

- 요청을 path와 method 기준으로 적절한 handler에 넘긴다.

##### (3) 입력 / 출력

- 입력: `HttpRequest`
- 출력: handler 실행 결과

##### (4) 호출 위치와 흐름

- `handle_client_job`이 호출한다.
- `handle_health_request` 또는 `handle_query_request`로 이어진다.

##### (5) 한글 의사코드

- path가 `/health`인지 확인한다.
- 맞으면 health handler를 호출한다.
- path가 `/query`인지 확인한다.
- 맞으면 query handler를 호출한다.
- 아니면 404 응답을 보낸다.

##### (6) 구현 시 주의점

- path뿐 아니라 method도 함께 검사해야 한다.

#### 6-3. `handle_health_request`

##### (1) 이름

`handle_health_request`

##### (2) 역할

- 서버 상태 확인 요청을 처리한다.

##### (3) 입력 / 출력

- 입력: 요청 정보
- 출력: 상태 확인용 JSON 응답

##### (4) 호출 위치와 흐름

- `route_request`에서 `/health`로 분기될 때 호출된다.

##### (5) 한글 의사코드

- 성공 상태를 나타내는 응답 구조를 만든다.
- JSON body를 만든다.
- HTTP 200 응답을 보낸다.

##### (6) 구현 시 주의점

- DB lock을 잡지 않는다.
- health 응답은 가능한 가볍고 안정적이어야 한다.

#### 6-4. `handle_query_request`

##### (1) 이름

`handle_query_request`

##### (2) 역할

- `/query` 요청의 실제 업무 처리를 수행한다.

##### (3) 입력 / 출력

- 입력: `HttpRequest`
- 출력: SQL 실행 결과에 대한 HTTP 응답

##### (4) 호출 위치와 흐름

- `route_request`에서 `/query`로 분기될 때 호출된다.
- 내부에서 `extract_sql_from_json -> execute_query_with_lock -> build_json_response` 흐름을 탄다.

##### (5) 한글 의사코드

- 요청 형식이 올바른지 확인한다.
- JSON body에서 SQL을 추출한다.
- SQL 실행 함수를 호출한다.
- 결과를 JSON으로 바꾼다.
- HTTP 응답을 보낸다.

##### (6) 구현 시 주의점

- SQL 추출 실패와 SQL 실행 실패를 구분해서 응답해야 한다.

### 7. DB 엔진 facade 관련 함수

#### 7-1. `db_engine_init`

##### (1) 이름

`db_engine_init`

##### (2) 역할

- DB 엔진 facade가 사용할 공용 상태를 초기화한다.

##### (3) 입력 / 출력

- 입력: 설정 또는 기본값
- 출력: 성공 / 실패

##### (4) 호출 위치와 흐름

- `main`에서 서버 시작 시 호출된다.

##### (5) 한글 의사코드

- 엔진 상태 구조체를 준비한다.
- 기본값을 채운다.
- 초기화 성공 상태를 기록한다.

##### (6) 구현 시 주의점

- 지금은 단순하더라도 이후 lock 정책, cache 정책을 연결할 지점이 된다.

#### 7-2. `execute_query_with_lock`

##### (1) 이름

`execute_query_with_lock`

##### (2) 역할

- DB 엔진 진입 전후에 lock을 감싼다.
- 동시성 보호의 핵심 진입점이다.

##### (3) 입력 / 출력

- 입력: SQL 문자열
- 출력: `DbResult`

##### (4) 호출 위치와 흐름

- `handle_query_request`가 호출한다.
- 내부에서 `lock_db_for_query -> db_execute_sql -> unlock_db_for_query`를 호출한다.

##### (5) 한글 의사코드

- SQL 종류를 대략 확인한다.
- 현재 정책에 맞는 lock을 획득한다.
- DB facade 실행 함수를 호출한다.
- 실행 결과를 저장한다.
- lock을 해제한다.
- 결과를 반환한다.

##### (6) 구현 시 주의점

- early return 경로에서도 unlock이 빠지면 안 된다.
- JSON 응답 생성은 가능하면 lock 밖에서 수행한다.

#### 7-3. `db_execute_sql`

##### (1) 이름

`db_execute_sql`

##### (2) 역할

- SQL 문자열을 기존 엔진 실행 경로에 태워 `DbResult`를 만든다.

##### (3) 입력 / 출력

- 입력: SQL 문자열
- 출력: `DbResult`

##### (4) 호출 위치와 흐름

- `execute_query_with_lock`이 호출한다.
- 내부적으로 `tokenizer_tokenize -> parser_parse -> executor_execute_into_result`를 호출한다.

##### (5) 한글 의사코드

- SQL 문자열을 정리한다.
- tokenizer로 토큰 배열을 만든다.
- parser로 `SqlStatement`를 만든다.
- executor로 실행한다.
- 실행 결과를 `DbResult`에 채운다.
- 임시 메모리를 정리하고 결과를 반환한다.

##### (6) 구현 시 주의점

- parser와 tokenizer의 오류 메시지가 API 친화적으로 전달되도록 정리해야 한다.

#### 7-4. `executor_execute_into_result`

##### (1) 이름

`executor_execute_into_result`

##### (2) 역할

- 기존 실행 경로를 유지하면서, 결과를 `DbResult`로 수집한다.
- API 서버용 핵심 함수다.

##### (3) 입력 / 출력

- 입력: `SqlStatement`
- 출력: `DbResult`

##### (4) 호출 위치와 흐름

- `db_execute_sql`이 호출한다.
- 내부적으로 `table_get_or_load`, `table_load_from_storage_if_needed`, `table_insert_row`, `table_linear_scan_by_field`, `bptree_search` 등을 호출한다.

##### (5) 한글 의사코드

- statement 타입을 확인한다.
- `INSERT`면 활성 테이블을 준비한다.
- 필요하면 디스크에서 테이블을 로드한다.
- row를 삽입하고 B+Tree를 갱신한다.
- `SELECT`면 projection을 준비한다.
- 테이블이 메모리에 없으면 디스크에서 올린다.
- `WHERE id = 값`이면 B+Tree 경로를 사용한다.
- 아니면 선형 탐색을 사용한다.
- 결과 row와 column을 `DbResult`에 저장한다.

##### (6) 구현 시 주의점

- CLI 출력용 로직과 API 결과 수집 로직을 섞지 않도록 구조를 나눠야 한다.

#### 7-5. `executor_render_result_for_cli`

##### (1) 이름

`executor_render_result_for_cli`

##### (2) 역할

- `DbResult`를 기존처럼 터미널 표 형태로 출력한다.
- 기존 CLI 사용성을 유지하기 위한 함수다.

##### (3) 입력 / 출력

- 입력: `DbResult`
- 출력: 터미널 출력

##### (4) 호출 위치와 흐름

- CLI 실행 경로에서 `db_execute_sql` 또는 executor 결과 처리 뒤 호출된다.

##### (5) 한글 의사코드

- 결과 타입이 SELECT인지 확인한다.
- column 폭을 계산한다.
- 표 경계선을 출력한다.
- 헤더를 출력한다.
- row를 순서대로 출력한다.
- row 수 메시지를 출력한다.

##### (6) 구현 시 주의점

- API 서버 경로와 CLI 경로가 서로를 방해하지 않도록 분리해야 한다.

### 8. 런타임 / 테이블 관련 함수

#### 8-1. `table_get_or_load`

##### (1) 이름

`table_get_or_load`

##### (2) 역할

- 현재 활성 테이블을 반환하거나, 새 활성 테이블로 전환한다.

##### (3) 입력 / 출력

- 입력: `table_name`
- 출력: `TableRuntime *`

##### (4) 호출 위치와 흐름

- `executor_execute_into_result`가 호출한다.

##### (5) 한글 의사코드

- 활성 테이블이 아직 없으면 초기화한다.
- 현재 활성 테이블 이름과 요청 테이블 이름을 비교한다.
- 같으면 그대로 반환한다.
- 다르면 기존 활성 테이블을 정리하고 새 이름으로 준비한다.
- 활성 테이블 포인터를 반환한다.

##### (6) 구현 시 주의점

- 단일 활성 테이블 정책이라는 제약을 항상 염두에 둬야 한다.

#### 8-2. `table_load_from_storage_if_needed`

##### (1) 이름

`table_load_from_storage_if_needed`

##### (2) 역할

- 요청한 테이블이 메모리에 올라와 있지 않으면 디스크에서 읽어 메모리에 올린다.

##### (3) 입력 / 출력

- 입력: `TableRuntime *`, `table_name`
- 출력: 성공 / 실패

##### (4) 호출 위치와 흐름

- `SELECT`, `INSERT` 진입 전에 `executor_execute_into_result`가 호출한다.

##### (5) 한글 의사코드

- 현재 테이블이 로드되어 있는지 확인한다.
- 이미 로드되어 있으면 바로 반환한다.
- 디스크에서 테이블 파일을 읽는다.
- row와 column을 런타임 구조에 채운다.
- 필요하면 B+Tree 인덱스를 재구축한다.
- 로드 완료 상태를 표시한다.

##### (6) 구현 시 주의점

- 디스크에서 읽어온 후 메모리 구조와 인덱스가 일관되게 맞아야 한다.

#### 8-3. `table_insert_row`

##### (1) 이름

`table_insert_row`

##### (2) 역할

- 새 row를 메모리 테이블에 넣고 auto id와 B+Tree를 함께 갱신한다.

##### (3) 입력 / 출력

- 입력: `TableRuntime`, `InsertStatement`
- 출력: 새 `row_index`

##### (4) 호출 위치와 흐름

- `INSERT` 경로에서 `executor_execute_into_result`가 호출한다.

##### (5) 한글 의사코드

- 스키마를 확인한다.
- 필요하면 용량을 늘린다.
- auto id를 계산한다.
- 새 row 메모리를 만든다.
- row를 배열에 넣는다.
- B+Tree에 `(id -> row_index)`를 넣는다.
- row_count와 next_id를 증가시킨다.

##### (6) 구현 시 주의점

- row 저장 후 index 삽입이 실패하면 rollback이 필요하다.

#### 8-4. `table_linear_scan_by_field`

##### (1) 이름

`table_linear_scan_by_field`

##### (2) 역할

- 일반 `WHERE` 또는 전체 스캔을 처리한다.

##### (3) 입력 / 출력

- 입력: `TableRuntime`, `WhereClause`
- 출력: row index 목록

##### (4) 호출 위치와 흐름

- `SELECT`에서 B+Tree를 쓰지 않는 경우 호출된다.

##### (5) 한글 의사코드

- WHERE가 없으면 전체 row index를 모은다.
- WHERE가 있으면 대상 컬럼 인덱스를 찾는다.
- 모든 row를 순회하며 비교한다.
- 조건에 맞는 row index만 결과에 담는다.

##### (6) 구현 시 주의점

- 반환 배열 해제 책임이 명확해야 한다.

#### 8-5. `bptree_search` / `bptree_insert`

##### (1) 이름

`bptree_search`, `bptree_insert`

##### (2) 역할

- `WHERE id = ?` 조회를 빠르게 처리하고, INSERT 시 인덱스를 유지한다.

##### (3) 입력 / 출력

- `search`: key 입력, row index 출력
- `insert`: key와 row index 입력, 트리 갱신

##### (4) 호출 위치와 흐름

- `SELECT WHERE id = ?` 경로에서 `bptree_search`
- `INSERT` 경로에서 `bptree_insert`

##### (5) 한글 의사코드

- `search`는 root에서 leaf까지 내려간다.
- leaf에서 key를 찾아 row index를 반환한다.
- `insert`는 leaf를 찾는다.
- 공간이 있으면 정렬 삽입한다.
- 공간이 없으면 split 하고 부모에 전파한다.

##### (6) 구현 시 주의점

- 내부 동기화가 없으므로 외부 lock 보호가 필수다.

### 9. lock 관련 함수

#### 9-1. `init_lock_manager`

##### (1) 이름

`init_lock_manager`

##### (2) 역할

- 전체 락 정책을 초기화한다.
- 현재 단계가 mutex인지 rwlock인지에 따라 내부 상태를 준비한다.

##### (3) 입력 / 출력

- 입력: lock 정책 설정
- 출력: 성공 / 실패

##### (4) 호출 위치와 흐름

- `main`에서 서버 시작 시 호출된다.

##### (5) 한글 의사코드

- 현재 락 정책을 확인한다.
- 전역 DB mutex를 초기화한다.
- 2차 개선용 필드도 기본값으로 준비한다.

##### (6) 구현 시 주의점

- 처음부터 인터페이스를 일반화해 두면 Step 6 개선이 쉬워진다.

#### 9-2. `lock_db_for_query`

##### (1) 이름

`lock_db_for_query`

##### (2) 역할

- SQL 실행 직전에 DB 엔진 보호용 lock을 획득한다.

##### (3) 입력 / 출력

- 입력: SQL 종류 또는 lock mode
- 출력: 성공 / 실패

##### (4) 호출 위치와 흐름

- `execute_query_with_lock`이 호출한다.

##### (5) 한글 의사코드

- 현재 락 정책을 확인한다.
- 1차 단계면 전역 mutex를 잡는다.
- 2차 단계면 SQL 종류에 따라 read 또는 write lock을 잡는다.

##### (6) 구현 시 주의점

- 초기 단계에서는 무조건 전역 mutex를 쓴다.
- rwlock 전환은 tokenizer cache 보호가 분리된 이후에 적용한다.

#### 9-3. `unlock_db_for_query`

##### (1) 이름

`unlock_db_for_query`

##### (2) 역할

- SQL 실행 후 DB lock을 해제한다.

##### (3) 입력 / 출력

- 입력: lock mode
- 출력: 없음

##### (4) 호출 위치와 흐름

- `execute_query_with_lock`이 `db_execute_sql` 뒤에 호출한다.

##### (5) 한글 의사코드

- 현재 정책에 맞는 lock을 해제한다.

##### (6) 구현 시 주의점

- unlock 누락은 deadlock으로 이어질 수 있다.

#### 9-4. `lock_tokenizer_cache` / `unlock_tokenizer_cache`

##### (1) 이름

`lock_tokenizer_cache`, `unlock_tokenizer_cache`

##### (2) 역할

- tokenizer 전역 캐시를 별도 보호하는 함수다.
- Step 6 개선 단계에서 사용한다.

##### (3) 입력 / 출력

- 입력: 없음
- 출력: 없음

##### (4) 호출 위치와 흐름

- `tokenizer_lookup_cache`, `tokenizer_store_cache` 진입 전후에 사용한다.

##### (5) 한글 의사코드

- 캐시 접근 직전에 mutex를 잡는다.
- 캐시 hit / miss 처리와 통계 갱신을 수행한다.
- 캐시 접근이 끝나면 mutex를 푼다.

##### (6) 구현 시 주의점

- read path도 캐시 구조를 수정할 수 있으므로 별도 보호가 필요하다.

### 10. 응답 생성 관련 함수

#### 10-1. `build_json_response`

##### (1) 이름

`build_json_response`

##### (2) 역할

- 성공 결과를 JSON body로 바꾼다.

##### (3) 입력 / 출력

- 입력: `DbResult`
- 출력: JSON 문자열

##### (4) 호출 위치와 흐름

- `handle_query_request`가 SQL 실행 후 호출한다.

##### (5) 한글 의사코드

- 결과 타입을 확인한다.
- 공통 필드인 success를 넣는다.
- SELECT면 columns, rows, row_count를 넣는다.
- INSERT면 affected_rows와 message를 넣는다.
- JSON 문자열을 반환한다.

##### (6) 구현 시 주의점

- 문자열 escape 처리가 중요하다.

#### 10-2. `build_json_error_response`

##### (1) 이름

`build_json_error_response`

##### (2) 역할

- 실패 결과를 통일된 JSON 형식으로 만든다.

##### (3) 입력 / 출력

- 입력: 에러 코드, 메시지
- 출력: JSON 문자열

##### (4) 호출 위치와 흐름

- 파싱 실패, SQL 실행 실패, queue full 등에서 호출된다.

##### (5) 한글 의사코드

- `success: false`를 넣는다.
- 에러 코드와 메시지를 넣는다.
- JSON 문자열을 반환한다.

##### (6) 구현 시 주의점

- 내부 구현 세부 정보를 과도하게 노출하지 않도록 조절해야 한다.

#### 10-3. `send_http_response`

##### (1) 이름

`send_http_response`

##### (2) 역할

- HTTP 상태 코드, 헤더, body를 합쳐 socket으로 전송한다.

##### (3) 입력 / 출력

- 입력: `client_fd`, status, content type, body
- 출력: 성공 / 실패

##### (4) 호출 위치와 흐름

- `handle_health_request`, `handle_query_request`, `send_error_response`에서 호출된다.

##### (5) 한글 의사코드

- status line을 만든다.
- `Content-Type`과 `Content-Length`를 계산한다.
- 헤더를 전송한다.
- body를 끝까지 전송한다.

##### (6) 구현 시 주의점

- 부분 write 가능성을 고려해야 한다.

#### 10-4. `send_error_response`

##### (1) 이름

`send_error_response`

##### (2) 역할

- 공통 에러 응답 전송 함수다.

##### (3) 입력 / 출력

- 입력: 상태 코드, 에러 코드, 메시지
- 출력: HTTP 에러 응답 전송

##### (4) 호출 위치와 흐름

- 라우팅 실패, 파싱 실패, DB 실패, queue full 등에서 호출된다.

##### (5) 한글 의사코드

- 에러 JSON을 만든다.
- 상태 코드에 맞는 응답을 조합한다.
- socket에 전송한다.

##### (6) 구현 시 주의점

- queue full은 반드시 503으로 내려야 한다.

### 11. 테스트 / 로그 관련 모듈

#### 11-1. `server_logger`

##### (1) 이름

`server_logger`

##### (2) 역할

- 요청 흐름과 에러를 추적한다.
- 멀티스레드 환경 디버깅에 중요한 역할을 한다.

##### (3) 입력 / 출력

- 입력: 로그 레벨, 요청 정보, 에러 정보
- 출력: 로그 출력

##### (4) 호출 위치와 흐름

- 요청 시작
- 요청 종료
- 에러 발생
- queue full

##### (5) 한글 의사코드

- 로그 포맷을 만든다.
- 스레드 정보와 요청 정보를 붙인다.
- 콘솔에 기록한다.

##### (6) 구현 시 주의점

- 여러 스레드 로그가 섞일 수 있어 한 줄 단위 정리가 중요하다.

#### 11-2. `test_http_parser`

##### (1) 이름

`test_http_parser`

##### (2) 역할

- HTTP 요청 파싱이 정상 / 비정상 케이스를 모두 처리하는지 검증한다.

##### (3) 입력 / 출력

- 입력: 샘플 raw HTTP 요청
- 출력: 테스트 성공 / 실패

##### (4) 호출 위치와 흐름

- 단위 테스트에서 독립적으로 실행된다.

##### (5) 한글 의사코드

- 정상 요청 문자열을 준비한다.
- 파싱 결과가 기대값과 같은지 확인한다.
- 비정상 요청 문자열도 넣어 본다.
- 실패 응답이 올바른지 확인한다.

##### (6) 구현 시 주의점

- body 길이 불일치, 잘못된 method, 잘못된 JSON도 테스트해야 한다.

#### 11-3. `test_thread_pool`

##### (1) 이름

`test_thread_pool`

##### (2) 역할

- queue와 worker가 요청을 안정적으로 주고받는지 검증한다.

##### (3) 입력 / 출력

- 입력: 테스트용 job
- 출력: 테스트 성공 / 실패

##### (4) 호출 위치와 흐름

- 단위 테스트에서 실행된다.

##### (5) 한글 의사코드

- queue를 초기화한다.
- thread pool을 만든다.
- 여러 job을 제출한다.
- worker가 모두 처리했는지 확인한다.
- shutdown이 정상적인지 확인한다.

##### (6) 구현 시 주의점

- queue full 즉시 실패 정책도 같이 검증해야 한다.

#### 11-4. `test_db_engine_facade`

##### (1) 이름

`test_db_engine_facade`

##### (2) 역할

- SQL 문자열에서 `DbResult`까지 이어지는 핵심 경로를 검증한다.

##### (3) 입력 / 출력

- 입력: 샘플 SQL
- 출력: 테스트 성공 / 실패

##### (4) 호출 위치와 흐름

- 단위 테스트에서 실행된다.

##### (5) 한글 의사코드

- INSERT SQL을 실행한다.
- 반환된 `DbResult`를 확인한다.
- SELECT SQL을 실행한다.
- columns와 rows를 확인한다.
- `WHERE id = ?` 인덱스 경로도 검증한다.

##### (6) 구현 시 주의점

- CLI 출력 유지와 API 결과 반환이 함께 가능한지도 확인해야 한다.

#### 11-5. `test_api_smoke`

##### (1) 이름

`test_api_smoke`

##### (2) 역할

- 실제 API 서버 프로세스를 띄워 end-to-end 흐름을 검증한다.

##### (3) 입력 / 출력

- 입력: curl 또는 간단한 HTTP 클라이언트 요청
- 출력: 테스트 성공 / 실패

##### (4) 호출 위치와 흐름

- 통합 테스트에서 서버를 띄운 뒤 실행한다.

##### (5) 한글 의사코드

- 서버를 백그라운드로 실행한다.
- `/health` 요청을 보낸다.
- INSERT 요청을 보낸다.
- SELECT 요청을 보낸다.
- 동시 요청을 여러 개 보낸다.
- queue를 넘는 요청도 보내 본다.
- 503 응답을 확인한다.
- 서버를 종료한다.

##### (6) 구현 시 주의점

- 테스트 시작 전 DB 상태 초기화가 필요하다.

---

## Part 7. 동시성 제어 전략 상세

### 1. 1차: 전역 DB mutex

초기 구현은 전역 DB mutex 하나로 간다.
이유는 아래와 같다.

- `table_runtime`가 단일 활성 테이블 상태를 갖고 있다.
- `table_runtime` 내부 row 배열과 `next_id`는 공유 상태다.
- `bptree`는 내부 락이 없다.
- `tokenizer`는 전역 캐시를 수정한다.

따라서 MVP에서는 아래 구간 전체를 보호한다.

- tokenizer
- parser
- executor
- runtime
- bptree

즉, "DB 엔진 진입부터 `DbResult` 확보 직전까지"를 mutex로 감싼다.

### 2. 왜 SELECT도 완전한 read-only가 아닌가

현재 `SELECT`는 DB 의미상 읽기 요청이다.
하지만 내부 구현 경로를 보면 `tokenizer`의 전역 캐시를 수정할 수 있다.

예를 들어:

- cache miss면 새 캐시 엔트리를 추가한다.
- cache hit면 LRU처럼 캐시 순서를 갱신하거나 hit count를 올릴 수 있다.

즉, DB 데이터는 안 바꿔도 프로세스 내부 공유 상태는 바뀔 수 있다.
그래서 초기에는 `SELECT`도 완전한 read-only 경로라고 볼 수 없다.

### 3. 2차: tokenizer cache mutex + DB read/write lock

2차 개선에서는 책임을 나눈다.

- tokenizer cache 접근: tokenizer 전용 mutex
- DB 엔진 실행:
  - `SELECT` -> read lock
  - `INSERT` -> write lock

이 구조가 필요한 이유는 아래와 같다.

- tokenizer 캐시 경쟁 조건을 별도 보호할 수 있다.
- DB 읽기 요청끼리는 병렬성을 확보할 수 있다.
- write 요청은 기존처럼 배타적으로 처리한다.

### 4. lock 범위 정책

MVP에서 lock 범위는 아래로 잡는다.

- lock 획득: SQL 실행 직전
- lock 해제: `DbResult`가 완성된 직후
- JSON 직렬화와 socket write는 lock 밖에서 수행

이유는 아래와 같다.

- 네트워크 I/O까지 lock 안에 넣으면 병목이 너무 커진다.
- 하지만 DB 내부 상태를 읽고 쓰는 동안은 보호가 필요하다.

---

## Part 8. 테스트 및 데모 기준

### 1. 최소 테스트 기준

이번 구현의 최소 테스트 기준은 아래와 같다.

- tokenizer / parser / executor 기존 테스트 유지
- `DbResult` 반환 경로 테스트 추가
- HTTP 파싱 테스트 추가
- thread pool / job queue 테스트 추가
- API smoke test 추가

### 2. 반드시 확인해야 하는 시나리오

- `GET /health` 정상 응답
- `POST /query` INSERT 성공
- `POST /query` SELECT 성공
- `SELECT ... WHERE id = ?` 인덱스 경로 정상 동작
- 잘못된 JSON 요청 처리
- 지원하지 않는 method 처리
- queue full 시 즉시 실패
- queue full 시 503 응답
- 동시 INSERT / SELECT에서 crash 없음

### 3. 데모 시나리오 추천

발표 / 시연에서는 아래 순서가 가장 자연스럽다.

1. `/health` 성공
2. INSERT 두세 건 수행
3. 일반 SELECT 수행
4. `WHERE id = ?` SELECT 수행
5. 동시 요청 또는 queue full 실패 시나리오 확인

---

## Part 9. 팀 체크리스트

아래 항목을 모두 만족하면 팀 기준으로 이 문서대로 구현해도 된다.

- [ ] 이 문서를 최종 구현 기준 문서로 사용한다.
- [ ] 폴더 구조를 계층형으로 재정리한다.
- [ ] CLI와 API 서버 바이너리를 분리한다.
- [ ] `/health`, `/query` 두 엔드포인트만 지원한다.
- [ ] 요청 포맷을 `{"sql":"..."}`로 고정한다.
- [ ] MVP SQL 범위를 `INSERT`, `SELECT`로 제한한다.
- [ ] queue full 시 즉시 실패하고 503을 반환한다.
- [ ] `executor`는 표 출력과 `DbResult` 반환을 둘 다 지원한다.
- [ ] 단일 활성 테이블 + 필요 시 디스크 로딩 정책을 따른다.
- [ ] 1차는 전역 DB mutex를 적용한다.
- [ ] 이후 tokenizer cache mutex + DB rwlock으로 개선한다.

---

## Part 10. 최종 정리

이번 `plan.md`는 요약본이 아니라, `API_SERVER_DESIGN.md`를 실제 구현 지침으로 다시 정리한 최종본이다.

핵심은 아래 다섯 가지로 압축된다.

1. 기존 SQL 처리기와 B+ Tree를 재사용한다.
2. API 서버는 `GET /health`, `POST /query` 두 엔드포인트로 시작한다.
3. thread pool + job queue 구조로 병렬 처리하되, queue full은 즉시 503으로 실패시킨다.
4. `executor`는 CLI 출력과 API용 `DbResult` 반환을 동시에 지원하게 정리한다.
5. 1차는 전역 mutex, 이후 tokenizer cache mutex + DB read/write lock으로 개선한다.

즉, 팀이 이 문서대로 구현하라고 지시받았을 때의 의미는 아래와 같다.

- 아키텍처는 이 문서대로 간다.
- 함수 책임과 호출 흐름도 이 문서 기준으로 맞춘다.
- TODO는 더 이상 메모가 아니라 이미 반영된 정책이다.

이제 구현자는 [API_SERVER_DESIGN.md](/Users/jinhyuk/krafton/project/week8_mini_dbms/docs/API_SERVER_DESIGN.md)를 배경 설명으로 참고할 수는 있지만, 실제 구현 기준은 이 `plan.md`를 따라가면 된다.
