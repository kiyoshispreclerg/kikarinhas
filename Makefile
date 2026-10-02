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
      src/twitch.c \
      src/demochat.c \
      src/users.c \
      src/commands.c \
      src/actions.c \
      src/ini.c \
      src/config.c \
      src/control.c \
      src/sample.c \
      src/audio.c \
      src/soundboard.c \
      src/emoji.c \
      src/emotes.c \
      src/emotewall.c
VENDOR_SRC = vendor/cjson/cJSON.c vendor/decoders.c
LIBKK = $(BUILD)/libkk.a

UNIT_TESTS = $(patsubst tests/unit/%.c,$(BUILD)/tests/%,$(wildcard tests/unit/test_*.c))

ALL_CFLAGS  = $(STDFLAGS) $(OPTFLAGS) $(SANFLAGS) $(PKG_CFLAGS) $(CFLAGS)
ALL_LDFLAGS = $(SANFLAGS) $(LDFLAGS)

.PHONY: all test run asan asan-run install uninstall lint clean locale pot update-po

# kikarinhas-config is optional: built only when GTK2 is there.
HAVE_GTK2 := $(shell $(PKG_CONFIG) --exists gtk+-2.0 && echo 1)
CONFIG_BIN = $(if $(HAVE_GTK2),$(BUILD)/kikarinhas-config)
GTK_CFLAGS = $(shell $(PKG_CONFIG) --cflags gtk+-2.0 2>/dev/null | sed 's/-I/-isystem /g')
GTK_LIBS   = $(shell $(PKG_CONFIG) --libs gtk+-2.0 2>/dev/null)

# Translations of kikarinhas-config (po/LINGUAS lists the languages). Built
# into $(BUILD)/locale, which the program finds next to its own executable.
LINGUAS = $(shell sed 's/\#.*//' po/LINGUAS)
HAVE_MSGFMT := $(shell command -v msgfmt >/dev/null 2>&1 && echo 1)
MO_FILES = $(if $(and $(HAVE_GTK2),$(HAVE_MSGFMT)),$(foreach l,$(LINGUAS),$(BUILD)/locale/$(l)/LC_MESSAGES/kikarinhas.mo))

all: $(BUILD)/kikarinhas $(CONFIG_BIN) $(MO_FILES)
locale: $(MO_FILES)

$(BUILD)/locale/%/LC_MESSAGES/kikarinhas.mo: po/%.po
	@mkdir -p $(@D)
	msgfmt -c -o $@ $<

# Rebuilds the template from the sources and merges it into every .po. No
# --package-version: config.mk's VERSION is the only place the version
# lives, so bumping it doesn't touch the .po files too.
pot update-po:
	xgettext --from-code=UTF-8 -L C --keyword=_ --keyword=N_ --keyword=ngettext:1,2 \
		--add-comments=TRANSLATORS -f po/POTFILES.in -o po/kikarinhas.pot \
		--package-name=kikarinhas \
		--copyright-holder="Kikarinhas contributors" --msgid-bugs-address=""
	for l in $(LINGUAS); do msgmerge -q -U --backup=none po/$$l.po po/kikarinhas.pot; done

$(BUILD)/kikarinhas: $(BUILD)/$(MAIN:.c=.o) $(LIBKK)
	$(CC) $(ALL_LDFLAGS) -o $@ $^ $(LDLIBS)

CONFIG_SRC = config/main.c config/sounds.c config/wall.c config/audience.c
CONFIG_OBJ = $(patsubst %.c,$(BUILD)/%.o,$(CONFIG_SRC))

$(BUILD)/config/%.o: config/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -DLOCALEDIR='"$(LOCALEDIR)"' $(ALL_CFLAGS) $(GTK_CFLAGS) $(WARNFLAGS) -MMD -MP -c -o $@ $<

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

install: $(BUILD)/kikarinhas $(MO_FILES)
	install -Dm755 $< $(DESTDIR)$(BINDIR)/kikarinhas
	$(if $(CONFIG_BIN),install -Dm755 $(CONFIG_BIN) $(DESTDIR)$(BINDIR)/kikarinhas-config)
	install -Dm644 data/kikarinhas.ini $(DESTDIR)$(PREFIX)/share/doc/kikarinhas/kikarinhas.ini
	install -Dm644 data/kikarinhas.desktop $(DESTDIR)$(DATADIR)/applications/kikarinhas.desktop
	$(if $(CONFIG_BIN),install -Dm644 data/kikarinhas-config.desktop $(DESTDIR)$(DATADIR)/applications/kikarinhas-config.desktop)
	install -Dm644 data/kikarinhas.svg $(DESTDIR)$(DATADIR)/icons/hicolor/scalable/apps/kikarinhas.svg
	$(foreach l,$(if $(MO_FILES),$(LINGUAS)),install -Dm644 $(BUILD)/locale/$(l)/LC_MESSAGES/kikarinhas.mo \
		$(DESTDIR)$(LOCALEDIR)/$(l)/LC_MESSAGES/kikarinhas.mo;)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/kikarinhas $(DESTDIR)$(BINDIR)/kikarinhas-config
	rm -f $(DESTDIR)$(PREFIX)/share/doc/kikarinhas/kikarinhas.ini
	for l in $(LINGUAS); do rm -f $(DESTDIR)$(LOCALEDIR)/$$l/LC_MESSAGES/kikarinhas.mo; done
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
