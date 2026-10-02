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
      src/demochat.c \
      src/users.c \
      src/commands.c \
      src/actions.c \
      src/ini.c \
      src/config.c \
      src/control.c \
      src/sample.c \
      src/audio.c \
      src/soundboard.c
VENDOR_SRC = vendor/cjson/cJSON.c vendor/decoders.c
LIBKK = $(BUILD)/libkk.a

UNIT_TESTS = $(patsubst tests/unit/%.c,$(BUILD)/tests/%,$(wildcard tests/unit/test_*.c))

ALL_CFLAGS  = $(STDFLAGS) $(OPTFLAGS) $(SANFLAGS) $(PKG_CFLAGS) $(CFLAGS)
ALL_LDFLAGS = $(SANFLAGS) $(LDFLAGS)

.PHONY: all test run asan asan-run install uninstall lint clean

# kikarinhas-config is optional: built only when GTK2 is there.
HAVE_GTK2 := $(shell $(PKG_CONFIG) --exists gtk+-2.0 && echo 1)
CONFIG_BIN = $(if $(HAVE_GTK2),$(BUILD)/kikarinhas-config)
GTK_CFLAGS = $(shell $(PKG_CONFIG) --cflags gtk+-2.0 2>/dev/null | sed 's/-I/-isystem /g')
GTK_LIBS   = $(shell $(PKG_CONFIG) --libs gtk+-2.0 2>/dev/null)

all: $(BUILD)/kikarinhas $(CONFIG_BIN)

$(BUILD)/kikarinhas: $(BUILD)/$(MAIN:.c=.o) $(LIBKK)
	$(CC) $(ALL_LDFLAGS) -o $@ $^ $(LDLIBS)

CONFIG_SRC = config/main.c config/sounds.c config/audience.c
CONFIG_OBJ = $(patsubst %.c,$(BUILD)/%.o,$(CONFIG_SRC))

$(BUILD)/config/%.o: config/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) $(GTK_CFLAGS) $(WARNFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/kikarinhas-config: $(CONFIG_OBJ) $(LIBKK)
	$(CC) $(ALL_LDFLAGS) -o $@ $^ $(GTK_LIBS) $(LDLIBS)

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
	$(if $(CONFIG_BIN),install -Dm755 $(CONFIG_BIN) $(DESTDIR)$(BINDIR)/kikarinhas-config)
	install -Dm644 data/kikarinhas.ini $(DESTDIR)$(PREFIX)/share/doc/kikarinhas/kikarinhas.ini
	install -Dm644 data/kikarinhas.desktop $(DESTDIR)$(DATADIR)/applications/kikarinhas.desktop
	$(if $(CONFIG_BIN),install -Dm644 data/kikarinhas-config.desktop $(DESTDIR)$(DATADIR)/applications/kikarinhas-config.desktop)
	install -Dm644 data/kikarinhas.svg $(DESTDIR)$(DATADIR)/icons/hicolor/scalable/apps/kikarinhas.svg

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/kikarinhas $(DESTDIR)$(BINDIR)/kikarinhas-config
	rm -f $(DESTDIR)$(PREFIX)/share/doc/kikarinhas/kikarinhas.ini
	rm -f $(DESTDIR)$(DATADIR)/applications/kikarinhas.desktop \
	      $(DESTDIR)$(DATADIR)/applications/kikarinhas-config.desktop \
	      $(DESTDIR)$(DATADIR)/icons/hicolor/scalable/apps/kikarinhas.svg

lint:
	@if grep -nE '\b(strcpy|strcat|sprintf|vsprintf|gets)[[:space:]]*\(' src/*.c src/*.h tests/unit/*.c config/*.c; then \
		echo "*** banned function used"; exit 1; fi
	@command -v cppcheck >/dev/null || { echo "*** cppcheck not installed"; exit 1; }
	cppcheck --std=c11 --enable=warning,performance,portability \
		--error-exitcode=1 --inline-suppr --quiet -Isrc -Ivendor/cjson \
		-D_XOPEN_SOURCE=700 -DKK_VERSION='"lint"' -DKK_FIXTURES_DIR='"."' src tests/unit

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
