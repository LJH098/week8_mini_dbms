#include "request_router.h"

#include "response_builder.h"
#include "utils.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const char *request_router_skip_spaces(const char *cursor) {
    while (cursor != NULL && *cursor != '\0' && isspace((unsigned char)*cursor)) {
        cursor++;
    }
    return cursor;
}

static int request_router_append_json_char(char **buffer, size_t *length,
                                           size_t *capacity, char value) {
    char chunk[2];

    chunk[0] = value;
    chunk[1] = '\0';
    return utils_append_buffer(buffer, length, capacity, chunk);
}

static int extract_sql_from_json(const char *body, char **out_sql) {
    const char *cursor;
    char *sql;
    size_t length;
    size_t capacity;

    if (body == NULL || out_sql == NULL) {
        return FAILURE;
    }

    cursor = strstr(body, "\"sql\"");
    if (cursor == NULL) {
        return FAILURE;
    }

    cursor += 5;
    cursor = request_router_skip_spaces(cursor);
    if (cursor == NULL || *cursor != ':') {
        return FAILURE;
    }

    cursor++;
    cursor = request_router_skip_spaces(cursor);
    if (cursor == NULL || *cursor != '"') {
        return FAILURE;
    }

    cursor++;
    sql = NULL;
    length = 0;
    capacity = 0;

    while (*cursor != '\0') {
        if (*cursor == '"') {
            *out_sql = sql;
            return SUCCESS;
        }

        if (*cursor == '\\') {
            cursor++;
            if (*cursor == '\0') {
                free(sql);
                return FAILURE;
            }

            switch (*cursor) {
                case '"':
                case '\\':
                case '/':
                    if (request_router_append_json_char(&sql, &length, &capacity, *cursor) != SUCCESS) {
                        free(sql);
                        return FAILURE;
                    }
                    break;
                case 'n':
                    if (request_router_append_json_char(&sql, &length, &capacity, '\n') != SUCCESS) {
                        free(sql);
                        return FAILURE;
                    }
                    break;
                case 'r':
                    if (request_router_append_json_char(&sql, &length, &capacity, '\r') != SUCCESS) {
                        free(sql);
                        return FAILURE;
                    }
                    break;
                case 't':
                    if (request_router_append_json_char(&sql, &length, &capacity, '\t') != SUCCESS) {
                        free(sql);
                        return FAILURE;
                    }
                    break;
                default:
                    free(sql);
                    return FAILURE;
            }
            cursor++;
            continue;
        }

        if (request_router_append_json_char(&sql, &length, &capacity, *cursor) != SUCCESS) {
            free(sql);
            return FAILURE;
        }
        cursor++;
    }

    free(sql);
    return FAILURE;
}

static int handle_health_request(int *out_status_code, char **out_body) {
    if (build_health_json_response(out_body) != SUCCESS) {
        return FAILURE;
    }

    *out_status_code = 200;
    return SUCCESS;
}

static int handle_query_request(DbEngine *engine, const HttpRequest *request,
                                int *out_status_code, char **out_body) {
    DbResult result;
    char *sql;
    int status;

    if (engine == NULL || request == NULL || out_status_code == NULL ||
        out_body == NULL) {
        return FAILURE;
    }

    sql = NULL;
    if (extract_sql_from_json(request->body, &sql) != SUCCESS) {
        *out_status_code = 400;
        return build_json_error_response(400,
                                         "Request body must contain a JSON string field named sql.",
                                         out_body);
    }

    db_result_init(&result);
    status = execute_query_with_lock(engine, sql, &result);
    free(sql);

    if (status != SUCCESS) {
        if (build_json_error_response(400,
                                      result.message[0] != '\0' ? result.message : "SQL execution failed.",
                                      out_body) != SUCCESS) {
            db_result_free(&result);
            return FAILURE;
        }
        *out_status_code = 400;
        db_result_free(&result);
        return SUCCESS;
    }

    status = build_query_json_response(&result, out_status_code, out_body);
    db_result_free(&result);
    return status;
}

int route_request(DbEngine *engine, const HttpRequest *request,
                  int *out_status_code, char **out_body) {
    if (engine == NULL || request == NULL || out_status_code == NULL ||
        out_body == NULL) {
        return FAILURE;
    }

    *out_body = NULL;

    if (utils_equals_ignore_case(request->path, "/health")) {
        if (!utils_equals_ignore_case(request->method, "GET")) {
            if (build_json_error_response(405, "Only GET is allowed for /health.",
                                          out_body) != SUCCESS) {
                return FAILURE;
            }
            *out_status_code = 405;
            return SUCCESS;
        }
        return handle_health_request(out_status_code, out_body);
    }

    if (utils_equals_ignore_case(request->path, "/query")) {
        if (!utils_equals_ignore_case(request->method, "POST")) {
            if (build_json_error_response(405, "Only POST is allowed for /query.",
                                          out_body) != SUCCESS) {
                return FAILURE;
            }
            *out_status_code = 405;
            return SUCCESS;
        }
        return handle_query_request(engine, request, out_status_code, out_body);
    }

    if (build_json_error_response(404, "Route not found.", out_body) != SUCCESS) {
        return FAILURE;
    }
    *out_status_code = 404;
    return SUCCESS;
}
