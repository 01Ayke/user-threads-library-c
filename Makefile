CC      = gcc
UNAME_S := $(shell uname -s)
CFLAGS  = -Wall -Wextra -g -I./src -D_XOPEN_SOURCE
ifeq ($(UNAME_S),Linux)
  CFLAGS   += -DHAVE_VALGRIND
  LIBPATH   = LD_LIBRARY_PATH=$(LIB_DIR)
else
  LIBPATH   = DYLD_LIBRARY_PATH=$(LIB_DIR)
endif

LIB     = libthread.so
SRC     = src/thread.c
OBJ     = build/thread.o
BIN_DIR = install/bin
LIB_DIR = install/lib
TESTS   = $(wildcard tests/*.c)
BINS    = $(patsubst tests/%.c,$(BIN_DIR)/%,$(TESTS))

# Arguments par defaut pour chaque test
ARGS_31 = 10 100
ARGS_32 = 10 100
ARGS_33 = 10 100
ARGS_21 = 10
ARGS_22 = 10
ARGS_23 = 10
ARGS_51 = 10
ARGS_61 = 10
ARGS_62 = 10
ARGS_63 = 5
ARGS_64 = 5
ARGS_71 = 4
ARGS_81 =

define test_args
$(or $(ARGS_$(1)),)
endef

.PHONY: all check test valgrind pthreads graphs install clean

all: $(LIB_DIR)/$(LIB) $(BINS)

# Compilation de la bibliotheque
$(LIB_DIR)/$(LIB): $(OBJ) | $(LIB_DIR)
	$(CC) -shared $< -o $@

$(OBJ): $(SRC) | build
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

# Compilation de chaque test
$(BIN_DIR)/%: tests/%.c $(LIB_DIR)/$(LIB) | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ -L$(LIB_DIR) -lthread

build:
	mkdir -p build

$(LIB_DIR):
	mkdir -p $(LIB_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# make check / make test : lance tous les tests avec les bons arguments
check test:
	@pass=0; fail=0; \
	run() { \
	  name=$$1; shift; \
	  printf "%-30s" "$$name"; \
	  out=$$($(LIBPATH) $(BIN_DIR)/$$name "$$@" 2>/dev/null); code=$$?; \
	  if [ $$code -eq 0 ]; then \
	    echo "PASS"; pass=$$((pass+1)); \
	  else \
	    echo "FAIL (exit $$code)"; fail=$$((fail+1)); \
	  fi; \
	}; \
	run 01-main; \
	run 02-switch; \
	run 03-equity; \
	run 11-join; \
	run 12-join-main; \
	run 13-join-switch; \
	run 21-create-many      $(ARGS_21); \
	run 22-create-many-recursive $(ARGS_22); \
	run 23-create-many-once $(ARGS_23); \
	run 31-switch-many      $(ARGS_31); \
	run 32-switch-many-join $(ARGS_32); \
	run 33-switch-many-cascade $(ARGS_33); \
	run 51-fibonacci        $(ARGS_51); \
	run 61-mutex            $(ARGS_61); \
	run 62-mutex            $(ARGS_62); \
	run 63-mutex-equity     $(ARGS_63); \
	run 64-mutex-join       $(ARGS_64); \
	run 71-preemption       $(ARGS_71); \
	run 81-deadlock         $(ARGS_81); \
	echo ""; \
	echo "Résultat: $$pass PASS, $$fail FAIL"

# make valgrind : lance tous les tests sous valgrind
valgrind:
	@run() { \
	  name=$$1; shift; \
	  printf "%-30s" "$$name (valgrind)"; \
	  out=$$($(LIBPATH) valgrind --leak-check=full --show-reachable=yes \
	         --track-origins=yes --error-exitcode=1 \
	         $(BIN_DIR)/$$name "$$@" 2>&1); code=$$?; \
	  if [ $$code -eq 0 ]; then echo "PASS"; \
	  else echo "FAIL (exit $$code)"; echo "$$out" | grep -E "ERROR|LEAK|Invalid" | head -5; fi; \
	}; \
	run 01-main; \
	run 02-switch; \
	run 03-equity; \
	run 11-join; \
	run 12-join-main; \
	run 13-join-switch; \
	run 21-create-many      $(ARGS_21); \
	run 22-create-many-recursive $(ARGS_22); \
	run 23-create-many-once $(ARGS_23); \
	run 31-switch-many      $(ARGS_31); \
	run 32-switch-many-join $(ARGS_32); \
	run 33-switch-many-cascade $(ARGS_33); \
	run 51-fibonacci        $(ARGS_51); \
	run 61-mutex            $(ARGS_61); \
	run 62-mutex            $(ARGS_62); \
	run 63-mutex-equity     $(ARGS_63); \
	run 64-mutex-join       $(ARGS_64); \
	run 71-preemption       $(ARGS_71); \
	run 81-deadlock         $(ARGS_81)

# make pthreads : compile les tests avec -DUSE_PTHREAD
pthreads:
	@mkdir -p $(BIN_DIR)
	$(foreach test,$(TESTS), \
	  $(CC) $(CFLAGS) -DUSE_PTHREAD $(test) \
	    -o $(BIN_DIR)/$(basename $(notdir $(test)))-pthread -lpthread;)

# make graphs : affiche les données GRAPH pour tous les tests
graphs: check
	@echo ""
	@echo "=== Données GRAPH ==="
	@for n in 1 2 5 10 20 50 100; do \
	  for t in 21-create-many 22-create-many-recursive 23-create-many-once \
	           31-switch-many 32-switch-many-join 33-switch-many-cascade \
	           51-fibonacci 61-mutex 62-mutex 71-preemption; do \
	    [ -x $(BIN_DIR)/$$t ] || continue; \
	    case $$t in \
	      31-*|32-*|33-*) args="$$n 100";; \
	      *) args="$$n";; \
	    esac; \
	    $(LIBPATH) $(BIN_DIR)/$$t $$args 2>/dev/null | grep "^GRAPH"; \
	  done; \
	done

install: all

clean:
	rm -rf build install
