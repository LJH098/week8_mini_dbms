#include "benchmark.h"
#include "executor.h"
#include "parser.h"
#include "table_runtime.h"
#include "tokenizer.h"
#include "utils.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * 문자열에서 연속된 공백을 건너뛰고 다음 유효 위치를 찾는다.
 * 반환값은 SQL 문이 시작될 수 있는 다음 인덱스다.
 */
static size_t main_skip_whitespace(const char *text, size_t index) {
    while (text[index] != '\0' && isspace((unsigned char)text[index])) {
        index++;
    }
    return index;
}

/*
 * 완전한 SQL 문 하나를 파싱하고 실행한다.
 * 빈 문장이거나 정상 실행되면 SUCCESS를 반환한다.
 */
static int main_process_sql_statement(const char *sql) {
    Token *tokens;
    int token_count;
    SqlStatement statement;
    char *working_sql;
    int status;

    if (sql == NULL) {
        return FAILURE;
    }

    working_sql = utils_strdup(sql);
    if (working_sql == NULL) {
        return FAILURE;
    }

    utils_trim(working_sql);
    if (working_sql[0] == '\0') {
        free(working_sql);
        return SUCCESS;
    }

    tokens = tokenizer_tokenize(working_sql, &token_count);
    if (tokens == NULL || token_count == 0) {
        free(tokens);
        free(working_sql);
        return FAILURE;
    }

    status = parser_parse(tokens, token_count, &statement);
    if (status == SUCCESS) {
        status = executor_execute(&statement);
    }

    free(tokens);
    free(working_sql);
    return status;
}

/*
 * `.sql` 파일을 읽어 세미콜론 기준으로 문장을 나눈 뒤 순서대로 실행한다.
 * 파일 읽기나 내부 메모리 할당에 실패하지 않으면 SUCCESS를 반환한다.
 */
static int main_run_file_mode(const char *path) {
    char *content;
    size_t start;
    int terminator_index;
    char *statement;
    char *remaining;

    content = utils_read_file(path);
    if (content == NULL) {
        return FAILURE;
    }

    start = 0;
    while (content[start] != '\0') {
        start = main_skip_whitespace(content, start);
        if (content[start] == '\0') {
            break;
        }

        terminator_index = utils_find_statement_terminator(content, start);
        if (terminator_index == FAILURE) {
            remaining = utils_strdup(content + start);
            if (remaining == NULL) {
                free(content);
                return FAILURE;
            }
            utils_trim(remaining);
            if (remaining[0] != '\0') {
                fprintf(stderr, "Error: Missing semicolon at end of SQL statement.\n");
            }
            free(remaining);
            break;
        }

        statement = utils_substring(content, start,
                                    (size_t)terminator_index - start + 1);
        if (statement == NULL) {
            free(content);
            return FAILURE;
        }

        main_process_sql_statement(statement);
        free(statement);
        start = (size_t)terminator_index + 1;
    }

    free(content);
    return SUCCESS;
}

/*
 * 한 줄 입력을 공백 제거 후 제어 키워드와 비교한다.
 * 일치하면 1, 아니면 0을 반환한다.
 */
static int main_trimmed_equals(const char *line, const char *keyword) {
    char *copy;
    int result;

    copy = utils_strdup(line);
    if (copy == NULL) {
        return 0;
    }

    utils_trim(copy);
    result = utils_equals_ignore_case(copy, keyword);
    free(copy);
    return result;
}

/*
 * REPL 버퍼에서 처리한 SQL 문을 제거하고 남은 문자열만 유지한다.
 * 성공 시 갱신된 버퍼 소유권은 계속 호출자에게 있다.
 */
static int main_replace_buffer_with_remainder(char **buffer, size_t *length,
                                              size_t *capacity, int end_index) {
    char *remainder;
    size_t remainder_length;

    if (buffer == NULL || *buffer == NULL || length == NULL || capacity == NULL) {
        return FAILURE;
    }

    remainder = utils_strdup(*buffer + end_index + 1);
    if (remainder == NULL) {
        return FAILURE;
    }

    utils_trim(remainder);
    free(*buffer);
    *buffer = remainder;
    remainder_length = strlen(remainder);
    *length = remainder_length;
    *capacity = remainder_length + 1;
    return SUCCESS;
}

/*
 * 사용자가 종료하거나 EOF가 올 때까지 대화형 SQL 셸을 실행한다.
 * 정상 종료면 SUCCESS, 메모리 할당 실패면 FAILURE를 반환한다.
 */
static int main_run_repl_mode(void) {
    char line[MAX_SQL_LENGTH];
    char *buffer;
    size_t buffer_length;
    size_t buffer_capacity;
    int terminator_index;
    char *statement;

    buffer = NULL;
    buffer_length = 0;
    buffer_capacity = 0;

    while (1) {
        printf("%s", buffer_length == 0 ? "SQL> " : "...> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            if (buffer != NULL && buffer[0] != '\0') {
                fprintf(stderr, "Error: Incomplete SQL statement before EOF.\n");
            }
            break;
        }

        if (buffer_length == 0 &&
            (main_trimmed_equals(line, "exit") || main_trimmed_equals(line, "quit"))) {
            break;
        }

        if (utils_append_buffer(&buffer, &buffer_length, &buffer_capacity, line) != SUCCESS) {
            free(buffer);
            return FAILURE;
        }

        while (buffer != NULL &&
               (terminator_index = utils_find_statement_terminator(buffer, 0)) != FAILURE) {
            statement = utils_substring(buffer, 0, (size_t)terminator_index + 1);
            if (statement == NULL) {
                free(buffer);
                return FAILURE;
            }

            main_process_sql_statement(statement);
            free(statement);

            if (main_replace_buffer_with_remainder(&buffer, &buffer_length,
                                                   &buffer_capacity,
                                                   terminator_index) != SUCCESS) {
                free(buffer);
                return FAILURE;
            }

            if (buffer_length == 0) {
                free(buffer);
                buffer = NULL;
                buffer_capacity = 0;
                break;
            }
        }
    }

    free(buffer);
    puts("Bye.");
    return SUCCESS;
}

/*
 * argv에 따라 파일 모드 또는 REPL 모드를 선택하고 종료 전에 파서 캐시를 정리한다.
 * 정상 종료면 EXIT_SUCCESS, 아니면 EXIT_FAILURE를 반환한다.
 */
int main(int argc, char *argv[]) {
    int status;

    if (argc > 2) {
        fprintf(stderr, "Usage: %s [sql_file|--benchmark|benchmark]\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (argc == 2 &&
        (utils_equals_ignore_case(argv[1], "--benchmark") ||
         utils_equals_ignore_case(argv[1], "benchmark"))) {
        BenchmarkConfig config = benchmark_default_config();
        status = benchmark_run(&config);
    } else if (argc == 2) {
        status = main_run_file_mode(argv[1]);
    } else {
        status = main_run_repl_mode();
    }

    table_runtime_cleanup();
    tokenizer_cleanup_cache();
    return status == SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
}
