include config.mk

BUILD ?= build

SRC = src/main.c \
      src/log.c \
      src/util.c \
      src/window.c \
      src/sa.c \
      src/sprite.c \
      src/avatar.c \
      src/stage.c
VENDOR_SRC = vendor/cjson/cJSON.c
OBJ = $(patsubst %.c,$(BUILD)/%.o,$(SRC) $(VENDOR_SRC))

ALL_CFLAGS  = $(STDFLAGS) $(OPTFLAGS) $(SANFLAGS) $(PKG_CFLAGS) $(CFLAGS)
ALL_LDFLAGS = $(SANFLAGS) $(LDFLAGS)

.PHONY: all run asan asan-run install uninstall lint clean

all: $(BUILD)/kikarinhas

$(BUILD)/kikarinhas: $(OBJ)
	$(CC) $(ALL_LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/vendor/%.o: vendor/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) $(VENDOR_WARNFLAGS) -MMD -MP -c -o $@ $<

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(ALL_CFLAGS) $(WARNFLAGS) -MMD -MP -c -o $@ $<

run: $(BUILD)/kikarinhas
	$(BUILD)/kikarinhas $(ARGS)

asan:
	$(MAKE) BUILD=$(BUILD)/asan SANITIZE=1 all

asan-run: asan
	LSAN_OPTIONS=suppressions=$(CURDIR)/tools/lsan.supp $(BUILD)/asan/kikarinhas $(ARGS)

install: $(BUILD)/kikarinhas
	install -Dm755 $< $(DESTDIR)$(BINDIR)/kikarinhas

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/kikarinhas

lint:
	@if grep -nE '\b(strcpy|strcat|sprintf|vsprintf|gets)[[:space:]]*\(' src/*.c src/*.h; then \
		echo "*** banned function used"; exit 1; fi
	@command -v cppcheck >/dev/null || { echo "*** cppcheck not installed"; exit 1; }
	cppcheck --std=c11 --enable=warning,performance,portability \
		--error-exitcode=1 --inline-suppr --quiet -Isrc -Ivendor/cjson \
		-D_XOPEN_SOURCE=700 -DKK_VERSION='"lint"' src

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
