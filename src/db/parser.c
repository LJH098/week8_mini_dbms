#include "parser.h"

#include <stdio.h>
#include <string.h>

/*
 * 토큰 하나가 기대한 타입과 선택적 문자열 값에 맞는지 확인한다.
 * 일치하면 1, 아니면 0을 반환한다.
 */
static int parser_is_token(const Token *tokens, int token_count, int index,
                                TokenType type, const char *value) {
    if (tokens == NULL || index < 0 || index >= token_count) {
        return 0;
    }

    if (tokens[index].type != type) {
        return 0;
    }

    if (value == NULL) {
        return 1;
    }

    return strcmp(tokens[index].value, value) == 0;
}

/*
 * 파서 오류 메시지 하나를 stderr로 출력한다.
 */
static void parser_print_error(const char *message) {
    fprintf(stderr, "Error: %s\n", message);
}

/*
 * 필수 키워드 토큰 하나를 소비하고 파서 위치를 앞으로 이동한다.
 * 일치하면 SUCCESS, 아니면 FAILURE를 반환한다.
 */
static int parser_expect_keyword(const Token *tokens, int token_count,
                                      int *index, const char *keyword) {
    if (!parser_is_token(tokens, token_count, *index, TOKEN_KEYWORD, keyword)) {
        parser_print_error("Unexpected SQL syntax.");
        return FAILURE;
    }

    (*index)++;
    return SUCCESS;
}

/*
 * 필수 식별자 토큰 하나를 읽어 dest에 복사한다.
 * 일치하면 SUCCESS, 아니면 FAILURE를 반환한다.
 */
static int parser_expect_identifier(const Token *tokens, int token_count,
                                         int *index, char *dest, size_t dest_size) {
    if (!parser_is_token(tokens, token_count, *index, TOKEN_IDENTIFIER, NULL)) {
        parser_print_error("Expected identifier.");
        return FAILURE;
    }

    if (utils_safe_strcpy(dest, dest_size, tokens[*index].value) != SUCCESS) {
        parser_print_error("Identifier is too long.");
        return FAILURE;
    }

    (*index)++;
    return SUCCESS;
}

/*
 * 정수 또는 문자열 리터럴 토큰 하나를 읽어 dest에 복사한다.
 * 일치하면 SUCCESS, 아니면 FAILURE를 반환한다.
 */
static int parser_expect_literal(const Token *tokens, int token_count,
                                      int *index, char *dest, size_t dest_size) {
    TokenType type;

    if (tokens == NULL || index == NULL || dest == NULL) {
        return FAILURE;
    }

    if (*index >= token_count) {
        parser_print_error("Expected literal value.");
        return FAILURE;
    }

    type = tokens[*index].type;
    if (type != TOKEN_INT_LITERAL && type != TOKEN_STR_LITERAL) {
        parser_print_error("Expected literal value.");
        return FAILURE;
    }

    if (utils_safe_strcpy(dest, dest_size, tokens[*index].value) != SUCCESS) {
        parser_print_error("Literal value is too long.");
        return FAILURE;
    }

    (*index)++;
    return SUCCESS;
}

/*
 * 선택적인 마지막 세미콜론을 소비하고 남는 토큰이 있으면 오류로 처리한다.
 */
static int parser_consume_optional_semicolon(const Token *tokens,
                                                  int token_count, int *index) {
    if (parser_is_token(tokens, token_count, *index, TOKEN_SEMICOLON, ";")) {
        (*index)++;
    }

    if (*index != token_count) {
        parser_print_error("Unexpected trailing tokens.");
        return FAILURE;
    }

    return SUCCESS;
}

/*
 * INSERT 토큰 흐름을 InsertStatement 구조체로 파싱한다.
 */
static int parser_parse_insert(const Token *tokens, int token_count,
                                    SqlStatement *out) {
    int index;
    int value_count;

    index = 0;
    value_count = 0;
    memset(out, 0, sizeof(*out));
    out->type = SQL_INSERT;

    if (parser_expect_keyword(tokens, token_count, &index, "INSERT") != SUCCESS ||
        parser_expect_keyword(tokens, token_count, &index, "INTO") != SUCCESS) {
        return FAILURE;
    }

    if (parser_expect_identifier(tokens, token_count, &index,
                                      out->insert.table_name,
                                      sizeof(out->insert.table_name)) != SUCCESS) {
        return FAILURE;
    }

    if (!parser_is_token(tokens, token_count, index, TOKEN_LPAREN, "(")) {
        parser_print_error("Expected '(' after table name.");
        return FAILURE;
    }
    index++;

    while (index < token_count) {
        if (out->insert.column_count >= MAX_COLUMNS) {
            parser_print_error("Too many columns in INSERT statement.");
            return FAILURE;
        }

        if (parser_expect_identifier(tokens, token_count, &index,
                                          out->insert.columns[out->insert.column_count],
                                          sizeof(out->insert.columns[0])) != SUCCESS) {
            return FAILURE;
        }
        out->insert.column_count++;

        if (parser_is_token(tokens, token_count, index, TOKEN_COMMA, ",")) {
            index++;
            continue;
        }
        break;
    }

    if (!parser_is_token(tokens, token_count, index, TOKEN_RPAREN, ")")) {
        parser_print_error("Expected ')' after column list.");
        return FAILURE;
    }
    index++;

    if (parser_expect_keyword(tokens, token_count, &index, "VALUES") != SUCCESS) {
        return FAILURE;
    }

    if (!parser_is_token(tokens, token_count, index, TOKEN_LPAREN, "(")) {
        parser_print_error("Expected '(' before VALUES list.");
        return FAILURE;
    }
    index++;

    while (index < token_count) {
        if (value_count >= MAX_COLUMNS) {
            parser_print_error("Too many values in INSERT statement.");
            return FAILURE;
        }

        if (parser_expect_literal(tokens, token_count, &index,
                                       out->insert.values[value_count],
                                       sizeof(out->insert.values[0])) != SUCCESS) {
            return FAILURE;
        }
        value_count++;

        if (parser_is_token(tokens, token_count, index, TOKEN_COMMA, ",")) {
            index++;
            continue;
        }
        break;
    }

    if (!parser_is_token(tokens, token_count, index, TOKEN_RPAREN, ")")) {
        parser_print_error("Expected ')' after VALUES list.");
        return FAILURE;
    }
    index++;

    if (out->insert.column_count != value_count) {
        parser_print_error("Column count doesn't match value count.");
        return FAILURE;
    }

    return parser_consume_optional_semicolon(tokens, token_count, &index);
}

/*
 * SELECT 컬럼 목록을 파싱한다.
 * `SELECT *` 형태도 여기서 처리한다.
 */
static int parser_parse_select_columns(const Token *tokens, int token_count,
                                            int *index, SelectStatement *stmt) {
    if (parser_is_token(tokens, token_count, *index, TOKEN_IDENTIFIER, "*")) {
        stmt->column_count = 0;
        (*index)++;
        return SUCCESS;
    }

    while (*index < token_count) {
        if (stmt->column_count >= MAX_COLUMNS) {
            parser_print_error("Too many columns in SELECT statement.");
            return FAILURE;
        }

        if (parser_expect_identifier(tokens, token_count, index,
                                          stmt->columns[stmt->column_count],
                                          sizeof(stmt->columns[0])) != SUCCESS) {
            return FAILURE;
        }
        stmt->column_count++;

        if (parser_is_token(tokens, token_count, *index, TOKEN_COMMA, ",")) {
            (*index)++;
            continue;
        }
        break;
    }

    return SUCCESS;
}

/*
 * 단일 조건 WHERE 절을 파싱해 대상 구조체에 채운다.
 */
static int parser_parse_where(const Token *tokens, int token_count, int *index,
                                   WhereClause *where) {
    if (parser_expect_identifier(tokens, token_count, index,
                                      where->column,
                                      sizeof(where->column)) != SUCCESS) {
        return FAILURE;
    }

    if (!parser_is_token(tokens, token_count, *index, TOKEN_OPERATOR, NULL)) {
        parser_print_error("Expected operator in WHERE clause.");
        return FAILURE;
    }

    if (utils_safe_strcpy(where->op, sizeof(where->op),
                          tokens[*index].value) != SUCCESS) {
        parser_print_error("WHERE operator is invalid.");
        return FAILURE;
    }
    (*index)++;

    if (parser_expect_literal(tokens, token_count, index,
                                   where->value,
                                   sizeof(where->value)) != SUCCESS) {
        return FAILURE;
    }

    return SUCCESS;
}

/*
 * SELECT 토큰 흐름을 SelectStatement 구조체로 파싱한다.
 */
static int parser_parse_select(const Token *tokens, int token_count,
                                    SqlStatement *out) {
    int index;

    index = 0;
    memset(out, 0, sizeof(*out));
    out->type = SQL_SELECT;

    if (parser_expect_keyword(tokens, token_count, &index, "SELECT") != SUCCESS) {
        return FAILURE;
    }

    if (parser_parse_select_columns(tokens, token_count, &index,
                                         &out->select) != SUCCESS) {
        return FAILURE;
    }

    if (parser_expect_keyword(tokens, token_count, &index, "FROM") != SUCCESS) {
        return FAILURE;
    }

    if (parser_expect_identifier(tokens, token_count, &index,
                                      out->select.table_name,
                                      sizeof(out->select.table_name)) != SUCCESS) {
        return FAILURE;
    }

    if (parser_is_token(tokens, token_count, index, TOKEN_KEYWORD, "WHERE")) {
        out->select.has_where = 1;
        index++;
        if (parser_parse_where(tokens, token_count, &index,
                                    &out->select.where) != SUCCESS) {
            return FAILURE;
        }
    }

    return parser_consume_optional_semicolon(tokens, token_count, &index);
}

/*
 * DELETE 토큰 흐름을 DeleteStatement 구조체로 파싱한다.
 */
static int parser_parse_delete(const Token *tokens, int token_count,
                                    SqlStatement *out) {
    int index;

    index = 0;
    memset(out, 0, sizeof(*out));
    out->type = SQL_DELETE;

    if (parser_expect_keyword(tokens, token_count, &index, "DELETE") != SUCCESS ||
        parser_expect_keyword(tokens, token_count, &index, "FROM") != SUCCESS) {
        return FAILURE;
    }

    if (parser_expect_identifier(tokens, token_count, &index,
                                      out->delete_stmt.table_name,
                                      sizeof(out->delete_stmt.table_name)) != SUCCESS) {
        return FAILURE;
    }

    if (parser_is_token(tokens, token_count, index, TOKEN_KEYWORD, "WHERE")) {
        out->delete_stmt.has_where = 1;
        index++;
        if (parser_parse_where(tokens, token_count, &index,
                                    &out->delete_stmt.where) != SUCCESS) {
            return FAILURE;
        }
    }

    return parser_consume_optional_semicolon(tokens, token_count, &index);
}

/*
 * 첫 SQL 키워드를 기준으로 적절한 파싱 함수로 분기한다.
 * out이 유효한 문장 구조체로 채워지면 SUCCESS를 반환한다.
 */
int parser_parse(const Token *tokens, int token_count, SqlStatement *out) {
    if (tokens == NULL || token_count <= 0 || out == NULL) {
        parser_print_error("Empty SQL statement.");
        return FAILURE;
    }

    if (parser_is_token(tokens, token_count, 0, TOKEN_KEYWORD, "INSERT")) {
        return parser_parse_insert(tokens, token_count, out);
    }

    if (parser_is_token(tokens, token_count, 0, TOKEN_KEYWORD, "SELECT")) {
        return parser_parse_select(tokens, token_count, out);
    }

    if (parser_is_token(tokens, token_count, 0, TOKEN_KEYWORD, "DELETE")) {
        return parser_parse_delete(tokens, token_count, out);
    }

    parser_print_error("Unsupported SQL statement.");
    return FAILURE;
}
