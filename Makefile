CC = gcc
UNAME_S := $(shell uname -s)
CFLAGS = -Wall -Wextra -g -O2 -I./src -D_XOPEN_SOURCE=600 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
LDFLAGS = -L$(LIB_DIR) -Wl,-rpath,'$$ORIGIN/../lib'
ifeq ($(UNAME_S),Linux)
CFLAGS += -DHAVE_VALGRIND
endif
LIB = libthread.so
SRC = src/thread.c
SRC_PREEMPT = src/thread_preempt.c
OBJ = build/thread.o
OBJ_PREEMPT = build/thread_preemption.o
BIN_DIR = install/bin
PTHREAD_BIN_DIR = install/bin-pthread
LIB_DIR = install/lib
TESTS = $(wildcard tests/*.c)
TESTS_NO_DEADLOCK = $(filter-out tests/81-deadlock.c,$(TESTS))
TESTS_NORMAL = $(filter-out tests/71-preemption.c,$(TESTS_NO_DEADLOCK))

all: build $(LIB_DIR) $(BIN_DIR) $(LIB) $(LIB_DIR)/libthread_preemption.so
	$(foreach test,$(TESTS_NORMAL),$(CC) $(CFLAGS) $(test) -o $(BIN_DIR)/$(basename $(notdir $(test))) $(LDFLAGS) -lthread;)
	$(CC) $(CFLAGS) -DDEADLOCK_DETECTION tests/81-deadlock.c $(SRC) -o $(BIN_DIR)/81-deadlock
	$(CC) $(CFLAGS) tests/71-preemption.c -o $(BIN_DIR)/71-preemption -L$(LIB_DIR) -Wl,-rpath,'$$ORIGIN/../lib' -lthread_preemption

preemption: build $(LIB_DIR) $(BIN_DIR) $(LIB_DIR)/libthread_preemption.so
	$(CC) $(CFLAGS) tests/71-preemption.c -o $(BIN_DIR)/71-preemption -L$(LIB_DIR) -Wl,-rpath,'$$ORIGIN/../lib' -lthread_preemption
	$(BIN_DIR)/71-preemption

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
$(OBJ_PREEMPT): $(SRC_PREEMPT)
	$(CC) $(CFLAGS) -DPREEMPTION -fPIC -c $< -o $@
$(LIB): $(OBJ)
	$(CC) -shared $< -o $(LIB_DIR)/$@
$(LIB_DIR)/libthread_preemption.so: $(OBJ_PREEMPT)
	$(CC) -shared $< -o $@

bench-pthread: build $(PTHREAD_BIN_DIR)
	$(foreach test,$(TESTS),$(CC) $(CFLAGS) -DUSE_PTHREAD $(test) -o $(PTHREAD_BIN_DIR)/$(basename $(notdir $(test))) -lpthread;)

check:
	@for t in $(BIN_DIR)/*; do [ -x $$t ] && ./$$t || true; done

clean:
	rm -rf build install

.PHONY: all preemption bench-pthread check clean
