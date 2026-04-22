#ifndef RESPONSE_BUILDER_H
#define RESPONSE_BUILDER_H

#include "executor_result.h"

/*
 * DbResult를 JSON body와 HTTP 상태 코드로 변환한다.
 */
int build_query_json_response(const DbResult *result, int *out_status_code,
                              char **out_body);

/*
 * 단순 에러 JSON body를 생성한다.
 */
int build_json_error_response(int status_code, const char *message, char **out_body);

/*
 * 상태 확인용 JSON body를 생성한다.
 */
int build_health_json_response(char **out_body);

/*
 * JSON body를 완전한 HTTP/1.1 응답 문자열로 감싼다.
 */
int build_http_response(int status_code, const char *body, char **out_response);

#endif
