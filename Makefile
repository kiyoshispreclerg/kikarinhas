include config.mk

BUILD ?= build

MAIN = src/main.c
# Everything but main.c goes into a static lib the unit tests link against.
LIB_SRC = src/log.c \
      src/util.c \
      src/window.c \
      src/sa.c \
      src/sprite.c \
      src/avatar.c \
      src/stage.c \
      src/json.c \
      src/http.c \
      src/youtube.c \
      src/demochat.c
VENDOR_SRC = vendor/cjson/cJSON.c
LIBKK = $(BUILD)/libkk.a

UNIT_TESTS = $(patsubst tests/unit/%.c,$(BUILD)/tests/%,$(wildcard tests/unit/test_*.c))

ALL_CFLAGS  = $(STDFLAGS) $(OPTFLAGS) $(SANFLAGS) $(PKG_CFLAGS) $(CFLAGS)
ALL_LDFLAGS = $(SANFLAGS) $(LDFLAGS)

.PHONY: all test run asan asan-run install uninstall lint clean

all: $(BUILD)/kikarinhas

$(BUILD)/kikarinhas: $(BUILD)/$(MAIN:.c=.o) $(LIBKK)
	$(CC) $(ALL_LDFLAGS) -o $@ $^ $(LDLIBS)

$(LIBKK): $(patsubst %.c,$(BUILD)/%.o,$(LIB_SRC) $(VENDOR_SRC))
	$(AR) rcs $@ $^

$(BUILD)/vendor/%.o: vendor/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) $(VENDOR_WARNFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) $(WARNFLAGS) -MMD -MP -c -o $@ $<

# ---- tests ----------------------------------------------------------------

TEST_CPPFLAGS = -DKK_FIXTURES_DIR='"$(CURDIR)/tests/fixtures"'

$(BUILD)/tests/%: tests/unit/%.c tests/harness.h $(LIBKK)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(TEST_CPPFLAGS) $(ALL_CFLAGS) $(WARNFLAGS) -MMD -MP \
		$(ALL_LDFLAGS) -o $@ $< $(LIBKK) $(LDLIBS)

test: all $(UNIT_TESTS)
	@fail=0; for t in $(UNIT_TESTS); do \
		$$t || fail=1; \
	done; \
	if [ $$fail -ne 0 ]; then echo "*** unit tests FAILED"; exit 1; fi; \
	echo "*** all unit tests passed"

run: $(BUILD)/kikarinhas
	$(BUILD)/kikarinhas $(ARGS)

asan:
	LSAN_OPTIONS=suppressions=$(CURDIR)/tools/lsan.supp \
		$(MAKE) BUILD=$(BUILD)/asan SANITIZE=1 test

asan-run: asan
	LSAN_OPTIONS=suppressions=$(CURDIR)/tools/lsan.supp $(BUILD)/asan/kikarinhas $(ARGS)

install: $(BUILD)/kikarinhas
	install -Dm755 $< $(DESTDIR)$(BINDIR)/kikarinhas

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/kikarinhas

lint:
	@if grep -nE '\b(strcpy|strcat|sprintf|vsprintf|gets)[[:space:]]*\(' src/*.c src/*.h tests/unit/*.c; then \
		echo "*** banned function used"; exit 1; fi
	@command -v cppcheck >/dev/null || { echo "*** cppcheck not installed"; exit 1; }
	cppcheck --std=c11 --enable=warning,performance,portability \
		--error-exitcode=1 --inline-suppr --quiet -Isrc -Ivendor/cjson \
		-D_XOPEN_SOURCE=700 -DKK_VERSION='"lint"' -DKK_FIXTURES_DIR='"."' src tests/unit

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
