CC = gcc
UNAME_S := $(shell uname -s)
CFLAGS = -Wall -Wextra -g -I./src -D_XOPEN_SOURCE
ifeq ($(UNAME_S),Linux)
  CFLAGS += -DHAVE_VALGRIND
endif
LIB = libthread.so
SRC = src/thread.c
OBJ = build/thread.o
BIN_DIR = install/bin
LIB_DIR = install/lib
TESTS = $(wildcard tests/*.c)

all: build $(LIB_DIR) $(BIN_DIR) $(LIB)
	$(foreach test,$(TESTS),$(CC) $(CFLAGS) $(test) -o $(BIN_DIR)/$(basename $(notdir $(test))) -L$(LIB_DIR) -lthread;)

build:
	mkdir -p build

$(LIB_DIR):
	mkdir -p $(LIB_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(OBJ): $(SRC)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(LIB): $(OBJ)
	$(CC) -shared $< -o $(LIB_DIR)/$@

check:
	@for t in $(BIN_DIR)/*; do [ -x $$t ] && ./$$t || true; done

clean:
	rm -rf build install

.PHONY: all check clean