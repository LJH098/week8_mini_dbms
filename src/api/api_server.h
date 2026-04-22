#ifndef API_SERVER_H
#define API_SERVER_H

#include "db_engine_facade.h"

typedef struct {
    int port;
    int worker_count;
    int queue_capacity;
} ApiServerConfig;

/*
 * 단일 스레드 API 서버를 시작하고 종료될 때까지 요청을 처리한다.
 */
int api_server_run(DbEngine *engine, const ApiServerConfig *config);

#endif
