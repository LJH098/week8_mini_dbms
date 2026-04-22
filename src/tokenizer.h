#ifndef TOKENIZER_H
#define TOKENIZER_H

#include "utils.h"

typedef enum {
    TOKEN_KEYWORD,
    TOKEN_IDENTIFIER,
    TOKEN_INT_LITERAL,
    TOKEN_STR_LITERAL,
    TOKEN_OPERATOR,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_COMMA,
    TOKEN_SEMICOLON,
    TOKEN_UNKNOWN
} TokenType;

typedef struct {
    TokenType type;
    char value[MAX_TOKEN_VALUE];
} Token;

/*
 * 원본 SQL 문자열을 동적으로 할당된 토큰 배열로 변환한다.
 * 성공 시 토큰 배열을 반환하고 token_count에 개수를 저장한다.
 * 반환된 메모리는 호출자가 free()로 해제해야 한다.
 */
Token *tokenizer_tokenize(const char *sql, int *token_count);

/*
 * 토크나이저가 보관 중인 캐시를 모두 해제한다.
 */
void tokenizer_cleanup_cache(void);

/*
 * 현재 캐시에 저장된 SQL 문 개수를 반환한다.
 */
int tokenizer_get_cache_entry_count(void);

/*
 * 마지막 캐시 정리 이후 발생한 캐시 히트 수를 반환한다.
 */
int tokenizer_get_cache_hit_count(void);

/*
 * 토큰 타입을 사람이 읽기 쉬운 문자열로 반환한다.
 */
const char *tokenizer_token_type_name(TokenType type);

#endif
