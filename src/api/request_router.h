#ifndef REQUEST_ROUTER_H
#define REQUEST_ROUTER_H

#include "db_engine_facade.h"
#include "http_parser.h"

/*
 * 메서드와 경로 기준으로 요청을 처리하고 JSON body와 상태 코드를 반환한다.
 */
int route_request(DbEngine *engine, const HttpRequest *request,
                  int *out_status_code, char **out_body);

#endif
