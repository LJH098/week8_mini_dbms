CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -g \
	-Isrc \
	-Isrc/api \
	-Isrc/concurrency \
	-Isrc/db \
	-Isrc/common \
	-Isrc/cli \
	-pthread
DEPFLAGS = -MMD -MP
SRC_DIR = src
TEST_DIR = tests
BUILD_DIR = build
CLI_TARGET = sql_processor
API_TARGET = api_server

SRC_FILES = $(shell find $(SRC_DIR) -name '*.c' -print | sort)
HEADERS = $(shell find $(SRC_DIR) -name '*.h' -print | sort)
CLI_ENTRY = $(SRC_DIR)/cli/main.c
API_ENTRY = $(SRC_DIR)/api/api_main.c
CORE_SRCS = $(filter-out $(CLI_ENTRY) $(API_ENTRY),$(SRC_FILES))
CORE_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(CORE_SRCS))
CLI_OBJ = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(CLI_ENTRY))
API_OBJ = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(API_ENTRY))
ALL_OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRC_FILES))
DEPS = $(ALL_OBJS:.o=.d)
TEST_SRCS = $(shell find $(TEST_DIR) -name 'test_*.c' -print | sort)
TEST_BINS = $(patsubst $(TEST_DIR)/%.c,$(BUILD_DIR)/tests/%,$(TEST_SRCS))

all: $(CLI_TARGET) $(API_TARGET)

$(CLI_TARGET): $(CORE_OBJS) $(CLI_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(API_TARGET): $(CORE_OBJS) $(API_OBJ)
	$(CC) $(CFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/%: $(TEST_DIR)/%.c $(CORE_OBJS) $(HEADERS)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $< $(CORE_OBJS)

tests: $(CLI_TARGET) $(API_TARGET) $(TEST_BINS)
	bash $(TEST_DIR)/integration/run_tests.sh

clean:
	rm -rf $(BUILD_DIR) $(CLI_TARGET) $(API_TARGET) data/*.csv

.PHONY: all tests clean

-include $(DEPS)
