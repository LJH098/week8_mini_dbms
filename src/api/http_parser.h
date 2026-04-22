#ifndef HTTP_PARSER_H
#define HTTP_PARSER_H

#include <stddef.h>

#define MAX_HTTP_METHOD_LEN 8
#define MAX_HTTP_PATH_LEN 128
#define MAX_HTTP_PROTOCOL_LEN 16

typedef struct {
    char method[MAX_HTTP_METHOD_LEN];
    char path[MAX_HTTP_PATH_LEN];
    char protocol[MAX_HTTP_PROTOCOL_LEN];
    char *body;
    size_t body_length;
} HttpRequest;

/*
 * raw HTTP 요청을 구조화된 HttpRequest로 파싱한다.
 */
int parse_http_request(const char *raw_request, HttpRequest *out_request);

/*
 * HttpRequest가 소유한 동적 메모리를 해제한다.
 */
void http_request_free(HttpRequest *request);

#endif
