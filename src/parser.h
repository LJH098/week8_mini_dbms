#ifndef PARSER_H
#define PARSER_H

#include "tokenizer.h"

typedef enum {
    SQL_INSERT,
    SQL_SELECT,
    SQL_DELETE
} SqlType;

typedef struct {
    char table_name[MAX_IDENTIFIER_LEN];
    int column_count;
    char columns[MAX_COLUMNS][MAX_IDENTIFIER_LEN];
    char values[MAX_COLUMNS][MAX_VALUE_LEN];
} InsertStatement;

typedef struct {
    char column[MAX_IDENTIFIER_LEN];
    char op[4];
    char value[MAX_VALUE_LEN];
} WhereClause;

typedef struct {
    char table_name[MAX_IDENTIFIER_LEN];
    int column_count;
    char columns[MAX_COLUMNS][MAX_IDENTIFIER_LEN];
    int has_where;
    WhereClause where;
} SelectStatement;

typedef struct {
    char table_name[MAX_IDENTIFIER_LEN];
    int has_where;
    WhereClause where;
} DeleteStatement;

typedef struct {
    SqlType type;
    union {
        InsertStatement insert;
        SelectStatement select;
        DeleteStatement delete_stmt;
    };
} SqlStatement;

/*
 * 토큰 배열을 구조화된 SQL 문 구조체로 파싱한다.
 * 성공 시 SUCCESS, 문법 오류 시 FAILURE를 반환한다.
 */
int parser_parse(const Token *tokens, int token_count, SqlStatement *out);

#endif
