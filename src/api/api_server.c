#include "api_server.h"

#include "thread_pool.h"
#include "http_parser.h"
#include "request_router.h"
#include "response_builder.h"
#include "utils.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define API_SERVER_READ_CHUNK 4096
#define API_SERVER_MAX_REQUEST_SIZE 65536

typedef struct {
    RequestRouterContext router_context;
} ApiServerHandlerContext;

static int api_server_append_bytes(char **buffer, size_t *length, size_t *capacity,
                                   const char *chunk, size_t chunk_length) {
    char *new_buffer;
    size_t new_capacity;

    if (buffer == NULL || length == NULL || capacity == NULL ||
        chunk == NULL || chunk_length == 0) {
        return FAILURE;
    }

    if (*buffer == NULL) {
        new_capacity = chunk_length + 1;
        if (new_capacity < 1024) {
            new_capacity = 1024;
        }
        *buffer = (char *)malloc(new_capacity);
        if (*buffer == NULL) {
            return FAILURE;
        }
        *length = 0;
        *capacity = new_capacity;
    } else if (*length + chunk_length + 1 > *capacity) {
        new_capacity = *capacity;
        while (*length + chunk_length + 1 > new_capacity) {
            new_capacity *= 2;
        }
        new_buffer = (char *)realloc(*buffer, new_capacity);
        if (new_buffer == NULL) {
            return FAILURE;
        }
        *buffer = new_buffer;
        *capacity = new_capacity;
    }

    memcpy(*buffer + *length, chunk, chunk_length);
    *length += chunk_length;
    (*buffer)[*length] = '\0';
    return SUCCESS;
}

static int api_server_extract_content_length(const char *headers, size_t *out_length) {
    const char *cursor;

    if (headers == NULL || out_length == NULL) {
        return FAILURE;
    }

    *out_length = 0;
    cursor = headers;
    while (*cursor != '\0') {
        const char *line_end;

        line_end = strstr(cursor, "\r\n");
        if (line_end == NULL) {
            break;
        }

        if (line_end == cursor) {
            return SUCCESS;
        }

        if (strncasecmp(cursor, "Content-Length:", 15) == 0) {
            cursor += 15;
            while (*cursor == ' ' || *cursor == '\t') {
                cursor++;
            }
            *out_length = (size_t)strtoull(cursor, NULL, 10);
            return SUCCESS;
        }

        cursor = line_end + 2;
    }

    return SUCCESS;
}

static int api_server_read_http_request(int client_fd, char **out_raw_request) {
    char chunk[API_SERVER_READ_CHUNK];
    char *buffer;
    size_t length;
    size_t capacity;
    size_t expected_total;
    int header_complete;

    if (out_raw_request == NULL) {
        return FAILURE;
    }

    *out_raw_request = NULL;
    buffer = NULL;
    length = 0;
    capacity = 0;
    expected_total = 0;
    header_complete = 0;

    while (1) {
        ssize_t bytes_read;
        char *header_end;
        size_t content_length;

        bytes_read = recv(client_fd, chunk, sizeof(chunk), 0);
        if (bytes_read < 0) {
            free(buffer);
            return FAILURE;
        }
        if (bytes_read == 0) {
            break;
        }

        if (length + (size_t)bytes_read > API_SERVER_MAX_REQUEST_SIZE) {
            free(buffer);
            return FAILURE;
        }

        if (api_server_append_bytes(&buffer, &length, &capacity,
                                    chunk, (size_t)bytes_read) != SUCCESS) {
            free(buffer);
            return FAILURE;
        }

        if (!header_complete) {
            header_end = strstr(buffer, "\r\n\r\n");
            if (header_end != NULL) {
                header_complete = 1;
                content_length = 0;
                if (api_server_extract_content_length(buffer, &content_length) != SUCCESS) {
                    free(buffer);
                    return FAILURE;
                }
                expected_total = (size_t)(header_end - buffer) + 4 + content_length;
            }
        }

        if (header_complete && length >= expected_total) {
            break;
        }
    }

    if (!header_complete) {
        free(buffer);
        return FAILURE;
    }

    *out_raw_request = buffer;
    return SUCCESS;
}

static int api_server_send_all(int client_fd, const char *response) {
    size_t total_sent;
    size_t response_length;

    if (response == NULL) {
        return FAILURE;
    }

    total_sent = 0;
    response_length = strlen(response);
    while (total_sent < response_length) {
        ssize_t sent;

        sent = send(client_fd, response + total_sent,
                    response_length - total_sent, 0);
        if (sent <= 0) {
            return FAILURE;
        }
        total_sent += (size_t)sent;
    }

    return SUCCESS;
}

static int api_server_send_json_error(int client_fd, int status_code,
                                      const char *message) {
    char *body;
    char *response;
    int status;

    body = NULL;
    response = NULL;

    if (build_json_error_response(status_code, message, &body) != SUCCESS) {
        return FAILURE;
    }

    status = build_http_response(status_code, body, &response);
    free(body);
    if (status != SUCCESS) {
        return FAILURE;
    }

    status = api_server_send_all(client_fd, response);
    free(response);
    return status;
}

static int api_server_handle_client(int client_fd,
                                    const RequestRouterContext *router_context) {
    char *raw_request;
    HttpRequest request;
    char *body;
    char *response;
    int status_code;
    int status;

    raw_request = NULL;
    body = NULL;
    response = NULL;
    memset(&request, 0, sizeof(request));

    if (api_server_read_http_request(client_fd, &raw_request) != SUCCESS) {
        return api_server_send_json_error(client_fd, 400, "Failed to read HTTP request.");
    }

    if (parse_http_request(raw_request, &request) != SUCCESS) {
        free(raw_request);
        return api_server_send_json_error(client_fd, 400, "Malformed HTTP request.");
    }
    free(raw_request);

    status = route_request(router_context, &request, &status_code, &body);
    http_request_free(&request);
    if (status != SUCCESS) {
        free(body);
        return api_server_send_json_error(client_fd, 500, "Failed to build response.");
    }

    status = build_http_response(status_code, body, &response);
    free(body);
    if (status != SUCCESS) {
        free(response);
        return api_server_send_json_error(client_fd, 500, "Failed to serialize HTTP response.");
    }

    status = api_server_send_all(client_fd, response);
    free(response);
    return status;
}

static void api_server_worker_handle_client(int client_fd, void *context) {
    ApiServerHandlerContext *handler_context;

    handler_context = (ApiServerHandlerContext *)context;
    api_server_handle_client(client_fd, &handler_context->router_context);
    close(client_fd);
}

static int api_server_create_socket(int port) {
    int server_fd;
    int reuse_addr;
    struct sockaddr_in address;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        return FAILURE;
    }

    reuse_addr = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse_addr, sizeof(reuse_addr)) != 0) {
        close(server_fd);
        return FAILURE;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        close(server_fd);
        return FAILURE;
    }

    if (listen(server_fd, 16) != 0) {
        close(server_fd);
        return FAILURE;
    }

    return server_fd;
}

int api_server_run(DbEngine *engine, const ApiServerConfig *config) {
    int server_fd;
    ThreadPool pool;
    ApiServerHandlerContext handler_context;

    if (engine == NULL || config == NULL || config->port <= 0 ||
        config->worker_count <= 0 || config->queue_capacity <= 0) {
        return FAILURE;
    }

    memset(&handler_context, 0, sizeof(handler_context));
    handler_context.router_context.engine = engine;
    handler_context.router_context.thread_pool = &pool;

    server_fd = api_server_create_socket(config->port);
    if (server_fd == FAILURE) {
        fprintf(stderr, "Error: Failed to start API server on port %d: %s\n",
                config->port, strerror(errno));
        return FAILURE;
    }

    printf("API server listening on port %d\n", config->port);
    fflush(stdout);

    if (thread_pool_init(&pool, config->worker_count, config->queue_capacity,
                         api_server_worker_handle_client, &handler_context) != SUCCESS) {
        close(server_fd);
        fprintf(stderr, "Error: Failed to initialize thread pool.\n");
        return FAILURE;
    }

    while (1) {
        int client_fd;

        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(server_fd);
            thread_pool_shutdown(&pool);
            return FAILURE;
        }

        if (thread_pool_submit(&pool, client_fd) != SUCCESS) {
            api_server_send_json_error(client_fd, 503, "Server is busy.");
            close(client_fd);
        }
    }

    close(server_fd);
    thread_pool_shutdown(&pool);
    return SUCCESS;
}
