#include "tokenizer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct SoftParserCacheEntry {
    char *sql;
    Token *tokens;
    int token_count;
    struct SoftParserCacheEntry *next;
} SoftParserCacheEntry;

#define SOFT_PARSER_CACHE_LIMIT 64

static SoftParserCacheEntry *tokenizer_cache_head = NULL;
static int tokenizer_cache_entry_count = 0;
static int tokenizer_cache_hit_count = 0;

/*
 * 캐시에 저장된 SQL 엔트리 하나와 그 내부 메모리를 해제한다.
 */
static void tokenizer_free_cache_entry(SoftParserCacheEntry *entry) {
    if (entry == NULL) {
        return;
    }

    free(entry->sql);
    free(entry->tokens);
    free(entry);
}

/*
 * 토큰 배열을 복제해 캐시 소유권과 호출자 소유권을 분리한다.
 * 반환된 배열은 호출자가 소유한다.
 */
static Token *tokenizer_clone_tokens(const Token *tokens, int token_count) {
    Token *copy;

    if (tokens == NULL || token_count <= 0) {
        return NULL;
    }

    copy = (Token *)malloc((size_t)token_count * sizeof(Token));
    if (copy == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for tokenizer cache.\n");
        return NULL;
    }

    memcpy(copy, tokens, (size_t)token_count * sizeof(Token));
    return copy;
}

/*
 * 캐시 크기 제한을 넘으면 가장 오래 안 쓰인 엔트리를 제거한다.
 */
static void tokenizer_evict_oldest_cache_entry(void) {
    SoftParserCacheEntry *previous;
    SoftParserCacheEntry *entry;

    if (tokenizer_cache_head == NULL) {
        return;
    }

    previous = NULL;
    entry = tokenizer_cache_head;
    while (entry->next != NULL) {
        previous = entry;
        entry = entry->next;
    }

    if (previous == NULL) {
        tokenizer_cache_head = NULL;
    } else {
        previous->next = NULL;
    }

    tokenizer_free_cache_entry(entry);
    tokenizer_cache_entry_count--;
}

/*
 * 정규화된 SQL 문자열을 파서 캐시에서 조회한다.
 * 성공 시 반환된 토큰 복제본은 호출자가 소유한다.
 */
static Token *tokenizer_lookup_cache(const char *sql, int *token_count) {
    SoftParserCacheEntry *entry;
    SoftParserCacheEntry *previous;
    Token *copy;

    if (sql == NULL || token_count == NULL) {
        return NULL;
    }

    previous = NULL;
    entry = tokenizer_cache_head;
    while (entry != NULL) {
        if (strcmp(entry->sql, sql) == 0) {
            if (previous != NULL) {
                previous->next = entry->next;
                entry->next = tokenizer_cache_head;
                tokenizer_cache_head = entry;
            }

            copy = tokenizer_clone_tokens(entry->tokens, entry->token_count);
            if (copy == NULL) {
                return NULL;
            }

            *token_count = entry->token_count;
            tokenizer_cache_hit_count++;
            return copy;
        }

        previous = entry;
        entry = entry->next;
    }

    return NULL;
}

/*
 * 파싱된 SQL 문 하나를 내부 캐시에 저장한다.
 * 캐시 저장은 부가 최적화이며 호출자 소유권은 이동하지 않는다.
 */
static int tokenizer_store_cache(const char *sql, const Token *tokens,
                                   int token_count) {
    SoftParserCacheEntry *entry;

    if (sql == NULL || tokens == NULL || token_count <= 0) {
        return FAILURE;
    }

    entry = (SoftParserCacheEntry *)calloc(1, sizeof(SoftParserCacheEntry));
    if (entry == NULL) {
        fprintf(stderr, "Error: Failed to allocate memory for tokenizer cache.\n");
        return FAILURE;
    }

    entry->sql = utils_strdup(sql);
    if (entry->sql == NULL) {
        free(entry);
        return FAILURE;
    }

    entry->tokens = tokenizer_clone_tokens(tokens, token_count);
    if (entry->tokens == NULL) {
        free(entry->sql);
        free(entry);
        return FAILURE;
    }

    entry->token_count = token_count;
    entry->next = tokenizer_cache_head;
    tokenizer_cache_head = entry;
    tokenizer_cache_entry_count++;

    if (tokenizer_cache_entry_count > SOFT_PARSER_CACHE_LIMIT) {
        tokenizer_evict_oldest_cache_entry();
    }

    return SUCCESS;
}

/*
 * 늘어나는 토큰 배열에 토큰 하나를 추가한다.
 * tokens에 정상 저장되면 SUCCESS를 반환한다.
 */
static int tokenizer_append_token(Token **tokens, int *count, int *capacity,
                                    TokenType type, const char *value) {
    Token *new_tokens;

    if (tokens == NULL || count == NULL || capacity == NULL || value == NULL) {
        return FAILURE;
    }

    if (*tokens == NULL) {
        *capacity = INITIAL_TOKEN_CAPACITY;
        *tokens = (Token *)malloc((size_t)(*capacity) * sizeof(Token));
        if (*tokens == NULL) {
            fprintf(stderr, "Error: Failed to allocate memory for tokens.\n");
            return FAILURE;
        }
    } else if (*count >= *capacity) {
        *capacity *= 2;
        new_tokens = (Token *)realloc(*tokens, (size_t)(*capacity) * sizeof(Token));
        if (new_tokens == NULL) {
            fprintf(stderr, "Error: Failed to allocate memory for tokens.\n");
            return FAILURE;
        }
        *tokens = new_tokens;
    }

    (*tokens)[*count].type = type;
    if (utils_safe_strcpy((*tokens)[*count].value, sizeof((*tokens)[*count].value),
                          value) != SUCCESS) {
        fprintf(stderr, "Error: Token value is too long.\n");
        return FAILURE;
    }

    (*count)++;
    return SUCCESS;
}

/*
 * 현재 위치에서 식별자 또는 키워드 후보 하나를 읽는다.
 * 성공 시 index는 읽은 뒤 위치로 이동한다.
 */
static int tokenizer_read_word(const char *sql, size_t *index, char *buffer,
                                 size_t buffer_size) {
    size_t length;

    length = 0;
    while (sql[*index] != '\0' &&
           (isalnum((unsigned char)sql[*index]) || sql[*index] == '_')) {
        if (length + 1 >= buffer_size) {
            return FAILURE;
        }
        buffer[length++] = sql[*index];
        (*index)++;
    }
    buffer[length] = '\0';
    return SUCCESS;
}

/*
 * 작은따옴표로 감싼 SQL 문자열 리터럴 하나를 읽는다.
 * 바깥 따옴표는 제외하고, 내부의 연속 작은따옴표 이스케이프도 처리한다.
 */
static int tokenizer_read_string(const char *sql, size_t *index, char *buffer,
                                   size_t buffer_size) {
    size_t length;

    length = 0;
    (*index)++;
    while (sql[*index] != '\0') {
        if (sql[*index] == '\'') {
            if (sql[*index + 1] == '\'') {
                if (length + 1 >= buffer_size) {
                    return FAILURE;
                }
                buffer[length++] = '\'';
                *index += 2;
                continue;
            }
            (*index)++;
            buffer[length] = '\0';
            return SUCCESS;
        }

        if (length + 1 >= buffer_size) {
            return FAILURE;
        }

        buffer[length++] = sql[*index];
        (*index)++;
    }

    return FAILURE;
}

/*
 * 부호를 포함할 수 있는 정수 리터럴 하나를 읽어 buffer에 복사한다.
 */
static int tokenizer_read_number(const char *sql, size_t *index, char *buffer,
                                   size_t buffer_size) {
    size_t length;

    length = 0;
    if (sql[*index] == '-' || sql[*index] == '+') {
        if (length + 1 >= buffer_size) {
            return FAILURE;
        }
        buffer[length++] = sql[*index];
        (*index)++;
    }

    while (isdigit((unsigned char)sql[*index])) {
        if (length + 1 >= buffer_size) {
            return FAILURE;
        }
        buffer[length++] = sql[*index];
        (*index)++;
    }

    buffer[length] = '\0';
    return SUCCESS;
}

/*
 * sql[index]가 정수 리터럴 시작이면 1, 아니면 0을 반환한다.
 */
static int tokenizer_is_numeric_start(const char *sql, size_t index) {
    if (isdigit((unsigned char)sql[index])) {
        return 1;
    }

    if ((sql[index] == '-' || sql[index] == '+') &&
        isdigit((unsigned char)sql[index + 1])) {
        return 1;
    }

    return 0;
}

/*
 * 이미 trim된 SQL 문 하나를 새 토큰 배열로 분해한다.
 * 반환된 배열은 호출자가 소유한다.
 */
static Token *tokenizer_tokenize_sql(const char *sql, int *token_count) {
    Token *tokens;
    int count;
    int capacity;
    char upper_buffer[MAX_TOKEN_VALUE];
    char token_buffer[MAX_TOKEN_VALUE];
    size_t i;

    if (sql == NULL || token_count == NULL) {
        return NULL;
    }

    *token_count = 0;
    tokens = NULL;
    count = 0;
    capacity = 0;
    i = 0;
    while (sql[i] != '\0') {
        if (isspace((unsigned char)sql[i])) {
            i++;
            continue;
        }

        if (sql[i] == '(') {
            if (tokenizer_append_token(&tokens, &count, &capacity, TOKEN_LPAREN,
                                         "(") != SUCCESS) {
                free(tokens);
                return NULL;
            }
            i++;
            continue;
        }

        if (sql[i] == ')') {
            if (tokenizer_append_token(&tokens, &count, &capacity, TOKEN_RPAREN,
                                         ")") != SUCCESS) {
                free(tokens);
                return NULL;
            }
            i++;
            continue;
        }

        if (sql[i] == ',') {
            if (tokenizer_append_token(&tokens, &count, &capacity, TOKEN_COMMA,
                                         ",") != SUCCESS) {
                free(tokens);
                return NULL;
            }
            i++;
            continue;
        }

        if (sql[i] == ';') {
            if (tokenizer_append_token(&tokens, &count, &capacity,
                                         TOKEN_SEMICOLON, ";") != SUCCESS) {
                free(tokens);
                return NULL;
            }
            i++;
            continue;
        }

        if (sql[i] == '*') {
            if (tokenizer_append_token(&tokens, &count, &capacity,
                                         TOKEN_IDENTIFIER, "*") != SUCCESS) {
                free(tokens);
                return NULL;
            }
            i++;
            continue;
        }

        if (sql[i] == '\'') {
            if (tokenizer_read_string(sql, &i, token_buffer,
                                        sizeof(token_buffer)) != SUCCESS) {
                fprintf(stderr, "Error: Unterminated string literal.\n");
                free(tokens);
                return NULL;
            }

            if (tokenizer_append_token(&tokens, &count, &capacity,
                                         TOKEN_STR_LITERAL, token_buffer) != SUCCESS) {
                free(tokens);
                return NULL;
            }
            continue;
        }

        if (sql[i] == '!' || sql[i] == '<' || sql[i] == '>' || sql[i] == '=') {
            token_buffer[0] = sql[i];
            token_buffer[1] = '\0';
            if ((sql[i] == '!' || sql[i] == '<' || sql[i] == '>') &&
                sql[i + 1] == '=') {
                token_buffer[1] = '=';
                token_buffer[2] = '\0';
                i += 2;
            } else {
                i++;
            }

            if (tokenizer_append_token(&tokens, &count, &capacity,
                                         TOKEN_OPERATOR, token_buffer) != SUCCESS) {
                free(tokens);
                return NULL;
            }
            continue;
        }

        if (tokenizer_is_numeric_start(sql, i)) {
            if (tokenizer_read_number(sql, &i, token_buffer,
                                        sizeof(token_buffer)) != SUCCESS) {
                fprintf(stderr, "Error: Integer literal is too long.\n");
                free(tokens);
                return NULL;
            }

            if (tokenizer_append_token(&tokens, &count, &capacity,
                                         TOKEN_INT_LITERAL, token_buffer) != SUCCESS) {
                free(tokens);
                return NULL;
            }
            continue;
        }

        if (isalpha((unsigned char)sql[i]) || sql[i] == '_') {
            if (tokenizer_read_word(sql, &i, token_buffer,
                                      sizeof(token_buffer)) != SUCCESS) {
                fprintf(stderr, "Error: Identifier is too long.\n");
                free(tokens);
                return NULL;
            }

            if (utils_is_sql_keyword(token_buffer)) {
                if (utils_to_upper_copy(token_buffer, upper_buffer,
                                        sizeof(upper_buffer)) != SUCCESS) {
                    free(tokens);
                    return NULL;
                }
                if (tokenizer_append_token(&tokens, &count, &capacity,
                                             TOKEN_KEYWORD, upper_buffer) != SUCCESS) {
                    free(tokens);
                    return NULL;
                }
            } else {
                if (tokenizer_append_token(&tokens, &count, &capacity,
                                             TOKEN_IDENTIFIER, token_buffer) != SUCCESS) {
                    free(tokens);
                    return NULL;
                }
            }
            continue;
        }

        token_buffer[0] = sql[i];
        token_buffer[1] = '\0';
        if (tokenizer_append_token(&tokens, &count, &capacity, TOKEN_UNKNOWN,
                                     token_buffer) != SUCCESS) {
            free(tokens);
            return NULL;
        }
        i++;
    }

    *token_count = count;
    return tokens;
}

/*
 * SQL 문 하나를 정규화한 뒤 가능하면 캐시를 재사용하고,
 * 호출자가 소유하는 토큰 배열을 반환한다.
 */
Token *tokenizer_tokenize(const char *sql, int *token_count) {
    char *working_sql;
    Token *tokens;
    int parsed_token_count;

    if (sql == NULL || token_count == NULL) {
        return NULL;
    }

    *token_count = 0;
    working_sql = utils_strdup(sql);
    if (working_sql == NULL) {
        return NULL;
    }

    utils_trim(working_sql);
    if (working_sql[0] == '\0') {
        free(working_sql);
        return NULL;
    }

    tokens = tokenizer_lookup_cache(working_sql, token_count);
    if (tokens != NULL) {
        free(working_sql);
        return tokens;
    }

    tokens = tokenizer_tokenize_sql(working_sql, &parsed_token_count);
    if (tokens == NULL) {
        free(working_sql);
        return NULL;
    }

    *token_count = parsed_token_count;
    if (tokenizer_store_cache(working_sql, tokens, parsed_token_count) != SUCCESS) {
        /* 파싱 자체는 성공했으므로 캐시 저장 실패는 치명 오류로 보지 않는다. */
    }

    free(working_sql);
    return tokens;
}

/*
 * 캐시에 저장된 토큰화 결과를 모두 해제하고 캐시 통계도 초기화한다.
 */
void tokenizer_cleanup_cache(void) {
    SoftParserCacheEntry *entry;
    SoftParserCacheEntry *next;

    entry = tokenizer_cache_head;
    while (entry != NULL) {
        next = entry->next;
        tokenizer_free_cache_entry(entry);
        entry = next;
    }

    tokenizer_cache_head = NULL;
    tokenizer_cache_entry_count = 0;
    tokenizer_cache_hit_count = 0;
}

/*
 * 현재 파서 캐시에 저장된 SQL 문 개수를 반환한다.
 */
int tokenizer_get_cache_entry_count(void) {
    return tokenizer_cache_entry_count;
}

/*
 * 마지막 캐시 정리 이후 발생한 파서 캐시 히트 수를 반환한다.
 */
int tokenizer_get_cache_hit_count(void) {
    return tokenizer_cache_hit_count;
}

/*
 * 토큰 타입 enum 값을 디버깅이나 테스트용 문자열로 바꾼다.
 */
const char *tokenizer_token_type_name(TokenType type) {
    switch (type) {
        case TOKEN_KEYWORD:
            return "KEYWORD";
        case TOKEN_IDENTIFIER:
            return "IDENTIFIER";
        case TOKEN_INT_LITERAL:
            return "INT_LITERAL";
        case TOKEN_STR_LITERAL:
            return "STR_LITERAL";
        case TOKEN_OPERATOR:
            return "OPERATOR";
        case TOKEN_LPAREN:
            return "LPAREN";
        case TOKEN_RPAREN:
            return "RPAREN";
        case TOKEN_COMMA:
            return "COMMA";
        case TOKEN_SEMICOLON:
            return "SEMICOLON";
        case TOKEN_UNKNOWN:
        default:
            return "UNKNOWN";
    }
}
