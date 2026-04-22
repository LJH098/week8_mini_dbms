#include "parser.h"
#include "tokenizer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int assert_true(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "[FAIL] %s\n", message);
        return FAILURE;
    }
    return SUCCESS;
}

int main(void) {
    Token *tokens;
    int token_count;
    SqlStatement statement;

    tokens = tokenizer_tokenize(
        "INSERT INTO users (name, age) VALUES ('Alice', 30);",
        &token_count);
    if (tokens == NULL) {
        return EXIT_FAILURE;
    }

    if (assert_true(parser_parse(tokens, token_count, &statement) == SUCCESS,
                    "parser_parse should parse INSERT") != SUCCESS ||
        assert_true(statement.type == SQL_INSERT, "statement type should be INSERT") != SUCCESS ||
        assert_true(strcmp(statement.insert.table_name, "users") == 0,
                    "table name should be users") != SUCCESS ||
        assert_true(statement.insert.column_count == 2,
                    "INSERT column count should be 2 when id is omitted") != SUCCESS ||
        assert_true(strcmp(statement.insert.columns[0], "name") == 0,
                    "first INSERT column should be name") != SUCCESS ||
        assert_true(strcmp(statement.insert.values[0], "Alice") == 0,
                    "INSERT value should keep string literal") != SUCCESS) {
        free(tokens);
        return EXIT_FAILURE;
    }
    free(tokens);

    tokens = tokenizer_tokenize(
        "SELECT name, age FROM users WHERE age >= 27;",
        &token_count);
    if (tokens == NULL) {
        return EXIT_FAILURE;
    }

    if (assert_true(parser_parse(tokens, token_count, &statement) == SUCCESS,
                    "parser_parse should parse SELECT") != SUCCESS ||
        assert_true(statement.type == SQL_SELECT, "statement type should be SELECT") != SUCCESS ||
        assert_true(statement.select.column_count == 2,
                    "SELECT column count should be 2") != SUCCESS ||
        assert_true(statement.select.has_where == 1, "WHERE should be parsed") != SUCCESS ||
        assert_true(strcmp(statement.select.where.column, "age") == 0,
                    "WHERE column should be age") != SUCCESS ||
        assert_true(strcmp(statement.select.where.op, ">=") == 0,
                    "WHERE operator should be >=") != SUCCESS ||
        assert_true(strcmp(statement.select.where.value, "27") == 0,
                    "WHERE value should be 27") != SUCCESS) {
        free(tokens);
        return EXIT_FAILURE;
    }
    free(tokens);

    tokens = tokenizer_tokenize(
        "DELETE FROM users WHERE name = 'Alice';",
        &token_count);
    if (tokens == NULL) {
        return EXIT_FAILURE;
    }

    if (assert_true(parser_parse(tokens, token_count, &statement) == SUCCESS,
                    "parser_parse should parse DELETE") != SUCCESS ||
        assert_true(statement.type == SQL_DELETE, "statement type should be DELETE") != SUCCESS ||
        assert_true(strcmp(statement.delete_stmt.table_name, "users") == 0,
                    "DELETE table name should be users") != SUCCESS ||
        assert_true(statement.delete_stmt.has_where == 1,
                    "DELETE should parse WHERE clause") != SUCCESS ||
        assert_true(strcmp(statement.delete_stmt.where.column, "name") == 0,
                    "DELETE WHERE column should be name") != SUCCESS ||
        assert_true(strcmp(statement.delete_stmt.where.op, "=") == 0,
                    "DELETE WHERE operator should be =") != SUCCESS ||
        assert_true(strcmp(statement.delete_stmt.where.value, "Alice") == 0,
                    "DELETE WHERE value should be Alice") != SUCCESS) {
        free(tokens);
        return EXIT_FAILURE;
    }
    free(tokens);

    puts("[PASS] parser");
    return EXIT_SUCCESS;
}
