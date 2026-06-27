# MICP reference stack — portable Makefile (fallback to CMake/CTest).
#
#   make            build static lib, tests and the demo
#   make test       build and run all unit tests + the loopback demo
#   make demo       build and run the loopback demo
#   make clean      remove build artifacts
#
# Pure C11, no external dependencies.

CC      ?= cc
CSTD    ?= -std=c11
CFLAGS  ?= $(CSTD) -Wall -Wextra -Wpedantic -O2
INCLUDE := -Iinclude -Itests
BUILD   := build

LIB_SRC := src/micp_crc.c src/micp_frame.c src/micp_session.c src/micp_types.c
LIB_OBJ := $(patsubst src/%.c,$(BUILD)/%.o,$(LIB_SRC))
LIB     := $(BUILD)/libmicp.a

TESTS   := test_crc test_frame test_session
TEST_BIN := $(addprefix $(BUILD)/,$(TESTS))
DEMO    := $(BUILD)/micp_loopback_demo

.PHONY: all test demo clean

all: $(LIB) $(TEST_BIN) $(DEMO)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDE) -c $< -o $@

$(LIB): $(LIB_OBJ)
	$(AR) rcs $@ $^

$(BUILD)/test_%: tests/test_%.c $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDE) $< $(LIB) -o $@

$(DEMO): examples/loopback_demo.c $(LIB) | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDE) $< $(LIB) -o $@

test: $(TEST_BIN) $(DEMO)
	@fail=0; \
	for t in $(TEST_BIN); do \
		echo "==> $$t"; \
		$$t || fail=1; \
	done; \
	echo "==> $(DEMO)"; $(DEMO) || fail=1; \
	if [ $$fail -eq 0 ]; then echo "ALL TESTS PASSED"; else echo "TESTS FAILED"; exit 1; fi

demo: $(DEMO)
	@$(DEMO)

clean:
	rm -rf $(BUILD)
