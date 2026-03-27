CC = gcc
CFLAGS = -Wall -Wextra -Werror -g -I./src
LDFLAGS = -L./build -Wl,-rpath=./build
LIB = libthread.so
SRC = src/thread.c
OBJ = build/thread.o
BIN_DIR = install/bin
LIB_DIR = install/lib
TESTS = $(wildcard tests/*.c)
PTHREAD_TESTS = $(addprefix $(BIN_DIR)/,$(notdir $(basename $(TESTS))-pthread))
THREAD_TESTS = $(addprefix $(BIN_DIR)/,$(notdir $(basename $(TESTS))))

all: $(BIN_DIR) $(LIB_DIR) $(LIB) $(THREAD_TESTS)

$(LIB_DIR):
	mkdir -p $(LIB_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

build:
	mkdir -p build

$(OBJ): $(SRC) | build
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(LIB): $(OBJ) | $(LIB_DIR)
	$(CC) -shared $< -o $(LIB_DIR)/$@

$(BIN_DIR)/%: tests/%.c $(LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS) -lthread

$(BIN_DIR)/%-pthread: tests/%.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -DUSE_PTHREAD $< -o $@ -pthread

check: $(THREAD_TESTS)
	for t in $(THREAD_TESTS); do ./$$t || exit 1; done

valgrind: $(THREAD_TESTS)
	for t in $(THREAD_TESTS); do valgrind --leak-check=full --show-reachable=yes --track-origins=yes ./$$t || exit 1; done

pthreads: $(PTHREAD_TESTS)

graphs:
	@echo "Implement graphs generation"

clean:
	rm -rf build install

.PHONY: all check valgrind pthreads graphs clean