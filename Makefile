# BLG 337E Project 1: sender, receiver and emulator.
#
#   make          build bin/sender, bin/receiver, bin/emulator
#   make test     build and run every tests/*.c
#   make smoke    end-to-end smoke test: the real sender and receiver over loopback
#   make clean    remove build/ and bin/
#
# Extra flags can be passed without losing the required ones, e.g.
#   make CFLAGS="-O0 -g -Werror"

CFLAGS ?= -O2 -g

BASE_CFLAGS   := -std=c11 -Wall -Wextra
BASE_CPPFLAGS := -D_POSIX_C_SOURCE=200809L -Isrc/common -MMD -MP

BUILD := build
BIN   := bin

PROGRAMS   := sender receiver emulator
BINARIES   := $(addprefix $(BIN)/,$(PROGRAMS))

# Shared code: every .c in src/common is linked into all programs and tests.
COMMON_SRC := $(wildcard src/common/*.c)
COMMON_OBJ := $(COMMON_SRC:src/%.c=$(BUILD)/%.o)

# Tests: every tests/<name>.c becomes its own executable.
TEST_SRC   := $(wildcard tests/*.c)
TEST_BIN   := $(TEST_SRC:tests/%.c=$(BUILD)/tests/%)

COMPILE = $(CC) $(BASE_CPPFLAGS) $(CPPFLAGS) $(BASE_CFLAGS) $(CFLAGS)

.PHONY: all test smoke clean
.SECONDARY:

all: $(BINARIES)

$(BIN)/%: $(BUILD)/%.o $(COMMON_OBJ)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/%.o: src/%.c
	@mkdir -p $(@D)
	$(COMPILE) -c -o $@ $<

# The .d file adds headers to $^, so only the .c and .o files are passed to the compiler.
$(BUILD)/tests/%: tests/%.c $(COMMON_OBJ)
	@mkdir -p $(@D)
	$(COMPILE) $(LDFLAGS) -o $@ $(filter %.c %.o,$^) $(LDLIBS)

test: $(TEST_BIN)
	@if [ -z "$(TEST_BIN)" ]; then echo "no tests yet"; fi
	@for t in $(TEST_BIN); do echo "== $$t"; ./$$t || exit 1; done

smoke: $(BIN)/sender $(BIN)/receiver
	@bash tests/smoke.sh

clean:
	rm -rf $(BUILD) $(BIN)

-include $(wildcard $(BUILD)/*.d $(BUILD)/*/*.d)
