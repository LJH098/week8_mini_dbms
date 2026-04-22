#include "api_server.h"
#include "db_engine_facade.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>

static int api_main_parse_port(const char *text, int *out_port) {
    long long parsed;

    if (text == NULL || out_port == NULL || !utils_is_integer(text)) {
        return FAILURE;
    }

    parsed = utils_parse_integer(text);
    if (parsed <= 0 || parsed > 65535) {
        return FAILURE;
    }

    *out_port = (int)parsed;
    return SUCCESS;
}

int main(int argc, char *argv[]) {
    DbEngine engine;
    ApiServerConfig config;

    config.port = 8080;

    if (argc > 2) {
        fprintf(stderr, "Usage: %s [port]\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (argc == 2 && api_main_parse_port(argv[1], &config.port) != SUCCESS) {
        fprintf(stderr, "Error: Invalid port '%s'.\n", argv[1]);
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
