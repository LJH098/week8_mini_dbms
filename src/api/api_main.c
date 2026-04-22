#include "api_server.h"
#include "db_engine_facade.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>

static int api_main_parse_positive_int(const char *text, int max_value, int *out_value) {
    long long parsed;

    if (text == NULL || out_value == NULL || !utils_is_integer(text)) {
        return FAILURE;
    }

    parsed = utils_parse_integer(text);
    if (parsed <= 0 || parsed > max_value) {
        return FAILURE;
    }

    *out_value = (int)parsed;
    return SUCCESS;
}

int main(int argc, char *argv[]) {
    DbEngine engine;
    ApiServerConfig config;

    config.port = 8080;
    config.worker_count = 4;
    config.queue_capacity = 16;

    if (argc > 4) {
        fprintf(stderr, "Usage: %s [port] [worker_count] [queue_capacity]\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (argc >= 2 && api_main_parse_positive_int(argv[1], 65535, &config.port) != SUCCESS) {
        fprintf(stderr, "Error: Invalid port '%s'.\n", argv[1]);
        return EXIT_FAILURE;
    }

    if (argc >= 3 &&
        api_main_parse_positive_int(argv[2], 256, &config.worker_count) != SUCCESS) {
        fprintf(stderr, "Error: Invalid worker count '%s'.\n", argv[2]);
        return EXIT_FAILURE;
    }

    if (argc >= 4 &&
        api_main_parse_positive_int(argv[3], 4096, &config.queue_capacity) != SUCCESS) {
        fprintf(stderr, "Error: Invalid queue capacity '%s'.\n", argv[3]);
        return EXIT_FAILURE;
    }

    if (db_engine_init(&engine) != SUCCESS) {
        fprintf(stderr, "Error: Failed to initialize DB engine.\n");
        return EXIT_FAILURE;
    }

    if (api_server_run(&engine, &config) != SUCCESS) {
        db_engine_shutdown(&engine);
        return EXIT_FAILURE;
    }

    db_engine_shutdown(&engine);
    return EXIT_SUCCESS;
}
