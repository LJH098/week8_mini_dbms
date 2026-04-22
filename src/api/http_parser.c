#include "http_parser.h"

#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int parse_http_request(const char *raw_request, HttpRequest *out_request) {
    const char *line_end;
    const char *body_start;
    char request_line[256];
    size_t line_length;

    if (raw_request == NULL || out_request == NULL) {
        return FAILURE;
    }

    memset(out_request, 0, sizeof(*out_request));

    line_end = strstr(raw_request, "\r\n");
    if (line_end == NULL) {
        return FAILURE;
    }

    line_length = (size_t)(line_end - raw_request);
    if (line_length == 0 || line_length >= sizeof(request_line)) {
        return FAILURE;
    }

    memcpy(request_line, raw_request, line_length);
    request_line[line_length] = '\0';

    if (sscanf(request_line, "%7s %127s %15s",
               out_request->method,
               out_request->path,
               out_request->protocol) != 3) {
        return FAILURE;
    }

    body_start = strstr(raw_request, "\r\n\r\n");
    if (body_start == NULL) {
        return FAILURE;
    }

    body_start += 4;
    out_request->body_length = strlen(body_start);
    out_request->body = utils_strdup(body_start);
    if (out_request->body == NULL) {
        return FAILURE;
    }

    return SUCCESS;
}

void http_request_free(HttpRequest *request) {
    if (request == NULL) {
        return;
    }

    free(request->body);
    request->body = NULL;
    request->body_length = 0;
}
