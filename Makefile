CC = gcc
UNAME_S := $(shell uname -s)
CFLAGS = -Wall -Wextra -g -O2 -I./src -D_XOPEN_SOURCE
LDFLAGS = -L$(LIB_DIR) -Wl,-rpath,'$$ORIGIN/../lib'
ifeq ($(UNAME_S),Linux)
  CFLAGS += -DHAVE_VALGRIND
endif
LIB = libthread.so
SRC = src/thread.c
OBJ = build/thread.o
BIN_DIR = install/bin
PTHREAD_BIN_DIR = install/bin-pthread
LIB_DIR = install/lib
TESTS = $(wildcard tests/*.c)

all: build $(LIB_DIR) $(BIN_DIR) $(LIB)
	$(foreach test,$(TESTS),$(CC) $(CFLAGS) $(test) -o $(BIN_DIR)/$(basename $(notdir $(test))) $(LDFLAGS) -lthread;)

build:
	mkdir -p build

$(LIB_DIR):
	mkdir -p $(LIB_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(PTHREAD_BIN_DIR):
	mkdir -p $(PTHREAD_BIN_DIR)

$(OBJ): $(SRC)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(LIB): $(OBJ)
	$(CC) -shared $< -o $(LIB_DIR)/$@

bench-pthread: build $(PTHREAD_BIN_DIR)
	$(foreach test,$(TESTS),$(CC) $(CFLAGS) -DUSE_PTHREAD $(test) -o $(PTHREAD_BIN_DIR)/$(basename $(notdir $(test))) -lpthread;)

check:
	@for t in $(BIN_DIR)/*; do [ -x $$t ] && ./$$t || true; done

clean:
	rm -rf build install

.PHONY: all bench-pthread check clean
