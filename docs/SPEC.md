# SQL Processor Specification

이 프로젝트는 SQL 입력을 파싱하고, 파일 기반 CSV 저장소에 대해 `INSERT` / `SELECT` / `DELETE`를 수행하는 경량 SQL 처리기입니다.

## 핵심 특징

- C99 기반 구현
- 파일 모드와 REPL 모드 지원
- Soft parser / Hard parser 계층 분리
- `data/<table>.csv` 파일 기반 저장
- `WHERE` 절 단일 조건 지원
- 메모리 내 해시/정렬 인덱스를 사용한 조건 검색
- `flock()` 기반 파일 잠금

## 지원 SQL

```sql
INSERT INTO users (name, age) VALUES ('Alice', 30);
SELECT * FROM users;
SELECT name, age FROM users WHERE age > 27;
SELECT * FROM users WHERE name = 'Bob';
DELETE FROM users WHERE name = 'Bob';
```

상세 구현 내용은 소스 코드의 각 모듈 주석과 테스트 케이스를 기준으로 확인할 수 있습니다.

## 데모 시나리오

발표나 시연에서는 `jungle_menu` 단일 테이블 구성이 가장 다루기 쉽습니다.

```sql
INSERT INTO jungle_menu (slot_key, menu_date, meal_type, dish_order, dish_name)
VALUES ('20260408_lunch', 20260408, 'lunch', 1, '경양식돈가스');

INSERT INTO jungle_menu (slot_key, menu_date, meal_type, dish_order, dish_name)
VALUES ('20260408_dinner', 20260408, 'dinner', 1, '뚝배기소불고기');

SELECT dish_order, dish_name
FROM jungle_menu
WHERE slot_key = '20260408_lunch';
```

이 방식은 날짜와 식사 시간대를 `slot_key` 한 컬럼으로 묶어서, 현재 지원하는 단일 `WHERE` 조건만으로도 "특정 날짜의 점심 메뉴 조회"를 자연스럽게 시연할 수 있습니다.
