#include "response_builder.h"

#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int response_builder_append_char(char **buffer, size_t *length,
                                        size_t *capacity, char value) {
    char chunk[2];

    chunk[0] = value;
    chunk[1] = '\0';
    return utils_append_buffer(buffer, length, capacity, chunk);
}

static int response_builder_append_json_string(char **buffer, size_t *length,
                                               size_t *capacity,
                                               const char *text) {
    size_t i;

    if (response_builder_append_char(buffer, length, capacity, '"') != SUCCESS) {
        return FAILURE;
    }

    if (text != NULL) {
        for (i = 0; text[i] != '\0'; i++) {
            switch (text[i]) {
                case '\\':
                    if (utils_append_buffer(buffer, length, capacity, "\\\\") != SUCCESS) {
                        return FAILURE;
                    }
                    break;
                case '"':
                    if (utils_append_buffer(buffer, length, capacity, "\\\"") != SUCCESS) {
                        return FAILURE;
                    }
                    break;
                case '\n':
                    if (utils_append_buffer(buffer, length, capacity, "\\n") != SUCCESS) {
                        return FAILURE;
                    }
                    break;
                case '\r':
                    if (utils_append_buffer(buffer, length, capacity, "\\r") != SUCCESS) {
                        return FAILURE;
                    }
                    break;
                case '\t':
                    if (utils_append_buffer(buffer, length, capacity, "\\t") != SUCCESS) {
                        return FAILURE;
                    }
                    break;
                default:
                    if (response_builder_append_char(buffer, length, capacity, text[i]) != SUCCESS) {
                        return FAILURE;
                    }
                    break;
            }
        }
    }

    return response_builder_append_char(buffer, length, capacity, '"');
}

static const char *response_builder_result_type_name(DbResultType type) {
    switch (type) {
        case DB_RESULT_INSERT:
            return "insert";
        case DB_RESULT_SELECT:
            return "select";
        case DB_RESULT_DELETE:
            return "delete";
        default:
            return "unknown";
    }
}

static const char *response_builder_status_text(int status_code) {
    switch (status_code) {
        case 200:
            return "OK";
        case 400:
            return "Bad Request";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        default:
            return "Internal Server Error";
    }
}

int build_query_json_response(const DbResult *result, int *out_status_code,
                              char **out_body) {
    char *buffer;
    size_t length;
    size_t capacity;
    int i;
    int j;
    char number_buffer[64];

    if (result == NULL || out_status_code == NULL || out_body == NULL) {
        return FAILURE;
    }

    buffer = NULL;
    length = 0;
    capacity = 0;

    if (utils_append_buffer(&buffer, &length, &capacity, "{\"ok\":true,\"type\":") != SUCCESS ||
        response_builder_append_json_string(&buffer, &length, &capacity,
                                            response_builder_result_type_name(result->type)) != SUCCESS ||
        utils_append_buffer(&buffer, &length, &capacity, ",\"message\":") != SUCCESS ||
        response_builder_append_json_string(&buffer, &length, &capacity, result->message) != SUCCESS) {
        free(buffer);
        return FAILURE;
    }

    if (result->type == DB_RESULT_INSERT) {
        snprintf(number_buffer, sizeof(number_buffer), ",\"rows_affected\":%d}",
                 result->rows_affected);
        if (utils_append_buffer(&buffer, &length, &capacity, number_buffer) != SUCCESS) {
            free(buffer);
            return FAILURE;
        }
    } else if (result->type == DB_RESULT_SELECT) {
        snprintf(number_buffer, sizeof(number_buffer),
                 ",\"used_id_index\":%s,\"row_count\":%d,\"columns\":[",
                 result->used_id_index ? "true" : "false", result->row_count);
        if (utils_append_buffer(&buffer, &length, &capacity, number_buffer) != SUCCESS) {
            free(buffer);
            return FAILURE;
        }

        for (i = 0; i < result->column_count; i++) {
            if (i > 0 &&
                response_builder_append_char(&buffer, &length, &capacity, ',') != SUCCESS) {
                free(buffer);
                return FAILURE;
            }
            if (response_builder_append_json_string(&buffer, &length, &capacity,
                                                    result->columns[i]) != SUCCESS) {
                free(buffer);
                return FAILURE;
            }
        }

        if (utils_append_buffer(&buffer, &length, &capacity, "],\"rows\":[") != SUCCESS) {
            free(buffer);
            return FAILURE;
        }

        for (i = 0; i < result->row_count; i++) {
            if (i > 0 &&
                response_builder_append_char(&buffer, &length, &capacity, ',') != SUCCESS) {
                free(buffer);
                return FAILURE;
            }
            if (response_builder_append_char(&buffer, &length, &capacity, '[') != SUCCESS) {
                free(buffer);
                return FAILURE;
            }
            for (j = 0; j < result->column_count; j++) {
                if (j > 0 &&
                    response_builder_append_char(&buffer, &length, &capacity, ',') != SUCCESS) {
                    free(buffer);
                    return FAILURE;
                }
                if (response_builder_append_json_string(&buffer, &length, &capacity,
                                                        result->rows[i][j]) != SUCCESS) {
                    free(buffer);
                    return FAILURE;
                }
            }
            if (response_builder_append_char(&buffer, &length, &capacity, ']') != SUCCESS) {
                free(buffer);
                return FAILURE;
            }
        }

        if (response_builder_append_char(&buffer, &length, &capacity, '}') != SUCCESS) {
            free(buffer);
            return FAILURE;
        }
    } else {
        if (response_builder_append_char(&buffer, &length, &capacity, '}') != SUCCESS) {
            free(buffer);
            return FAILURE;
        }
    }

    *out_status_code = 200;
    *out_body = buffer;
    return SUCCESS;
}

int build_json_error_response(int status_code, const char *message, char **out_body) {
    char *buffer;
    size_t length;
    size_t capacity;
    char number_buffer[64];

    if (out_body == NULL) {
        return FAILURE;
    }

    buffer = NULL;
    length = 0;
    capacity = 0;

    snprintf(number_buffer, sizeof(number_buffer), "{\"ok\":false,\"status\":%d,\"error\":",
             status_code);
    if (utils_append_buffer(&buffer, &length, &capacity, number_buffer) != SUCCESS ||
        response_builder_append_json_string(&buffer, &length, &capacity, message) != SUCCESS ||
        response_builder_append_char(&buffer, &length, &capacity, '}') != SUCCESS) {
        free(buffer);
        return FAILURE;
    }

    *out_body = buffer;
    return SUCCESS;
}

int build_health_json_response(char **out_body) {
    if (out_body == NULL) {
        return FAILURE;
    }

    *out_body = utils_strdup("{\"status\":\"ok\"}");
    return *out_body == NULL ? FAILURE : SUCCESS;
}

int build_http_response(int status_code, const char *body, char **out_response) {
    const char *status_text;
    int written;
    size_t body_length;
    size_t response_size;

    if (body == NULL || out_response == NULL) {
        return FAILURE;
    }

    status_text = response_builder_status_text(status_code);
    body_length = strlen(body);
    response_size = body_length + 256;
    *out_response = (char *)malloc(response_size);
    if (*out_response == NULL) {
        return FAILURE;
    }

    written = snprintf(*out_response, response_size,
                       "HTTP/1.1 %d %s\r\n"
                       "Content-Type: application/json\r\n"
                       "Content-Length: %zu\r\n"
                       "Connection: close\r\n"
                       "\r\n"
                       "%s",
                       status_code, status_text, body_length, body);
    if (written < 0 || (size_t)written >= response_size) {
        free(*out_response);
        *out_response = NULL;
        return FAILURE;
    }

    return SUCCESS;
}
