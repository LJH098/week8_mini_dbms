#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdio.h>

#define SUCCESS 0
#define FAILURE -1

#define MAX_SQL_LENGTH 8192
#define MAX_TOKEN_VALUE 256
#define MAX_IDENTIFIER_LEN 64
#define MAX_COLUMNS 32
#define MAX_VALUE_LEN 256
#define MAX_PATH_LEN 256
#define MAX_CSV_LINE_LENGTH 16384
#define INITIAL_TOKEN_CAPACITY 16
#define INITIAL_ROW_CAPACITY 16
#define HASH_BUCKET_COUNT 101

/*
 * 문자열을 복제해 새 메모리에 저장한다.
 * 성공 시 새 문자열을 반환하고, 실패 시 NULL을 반환한다.
 * 반환된 메모리는 호출자가 소유한다.
 */
char *utils_strdup(const char *src);

/*
 * src를 dest로 복사하고 항상 널 종료를 보장한다.
 * 성공 시 SUCCESS, 잘림이나 잘못된 입력이면 FAILURE를 반환한다.
 */
int utils_safe_strcpy(char *dest, size_t dest_size, const char *src);

/*
 * 문자열 앞뒤 공백을 제자리에서 제거한다.
 */
void utils_trim(char *text);

/*
 * src를 대문자로 바꿔 dest에 저장한다.
 * 성공 시 SUCCESS, 잘못된 입력이나 잘림이 있으면 FAILURE를 반환한다.
 */
int utils_to_upper_copy(const char *src, char *dest, size_t dest_size);

/*
 * 대소문자를 무시하고 두 문자열이 같으면 1, 아니면 0을 반환한다.
 */
int utils_equals_ignore_case(const char *lhs, const char *rhs);

/*
 * 지원하는 SQL 키워드이면 1, 아니면 0을 반환한다.
 */
int utils_is_sql_keyword(const char *text);

/*
 * 유효한 정수 문자열이면 1, 아니면 0을 반환한다.
 */
int utils_is_integer(const char *text);

/*
 * 정수 문자열을 숫자로 변환한다.
 * 호출 전 utils_is_integer()로 검증하는 것을 전제로 한다.
 */
long long utils_parse_integer(const char *text);

/*
 * SQL 리터럴 두 개를 비교한다.
 * 양쪽이 모두 정수면 숫자로 비교하고, 아니면 문자열 사전순으로 비교한다.
 * 반환값은 strcmp처럼 음수, 0, 양수 중 하나다.
 */
int utils_compare_values(const char *lhs, const char *rhs);

/*
 * 텍스트 파일 전체를 메모리로 읽어온다.
 * 성공 시 새 버퍼를 반환하고, 실패 시 NULL을 반환한다.
 * 반환된 메모리는 호출자가 해제해야 한다.
 */
char *utils_read_file(const char *path);

/*
 * 동적으로 관리되는 버퍼 뒤에 문자열을 덧붙인다.
 * 성공 시 SUCCESS, 메모리 할당 실패 시 FAILURE를 반환한다.
 */
int utils_append_buffer(char **buffer, size_t *length, size_t *capacity,
                        const char *suffix);

/*
 * 작은따옴표 밖에 있는 다음 SQL 문 종료 세미콜론 위치를 찾는다.
 * start_index 이후에 종료자가 없으면 FAILURE를 반환한다.
 */
int utils_find_statement_terminator(const char *text, size_t start_index);

/*
 * 버퍼 안에 완전한 SQL 문이 있으면 1, 아니면 0을 반환한다.
 */
int utils_has_statement_terminator(const char *text);

/*
 * 부분 문자열을 새 메모리에 복사해 반환한다.
 * 반환된 메모리는 호출자가 해제해야 한다.
 */
char *utils_substring(const char *text, size_t start, size_t length);

/*
 * UTF-8 문자열의 터미널 표시 폭을 계산해 반환한다.
 * ASCII 문자는 1칸, 일반적인 CJK 문자는 2칸으로 계산한다.
 */
int utils_display_width(const char *text);

/*
 * 문자열을 출력한 뒤 목표 표시 폭에 맞을 때까지 공백을 채운다.
 */
void utils_print_padded(FILE *stream, const char *text, int target_width);

#endif
