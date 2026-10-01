# Kikarinhas build configuration. Override any variable on the command line,
# e.g. `make CC=clang PREFIX=/usr`.

VERSION = 0.0.1

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin
DESTDIR ?=

PKG_CONFIG ?= pkg-config

# C11 without GNU extensions; XSI (implies POSIX.1-2008) for shmget/shmat.
STDFLAGS  = -std=c11 -D_XOPEN_SOURCE=700
WARNFLAGS = -Wall -Wextra -Werror -Wpedantic -Wshadow -Wformat=2 \
            -Wstrict-prototypes -Wmissing-prototypes -Wvla

OPTFLAGS ?= -O2 -g

# Library headers go in as -isystem so -Wpedantic/-Werror only judge our code.
PKGS       = x11 xext cairo pangocairo
PKG_CFLAGS = $(shell $(PKG_CONFIG) --cflags $(PKGS) | sed 's/-I/-isystem /g')
PKG_LIBS   = $(shell $(PKG_CONFIG) --libs $(PKGS))

CPPFLAGS += -Isrc -DKK_VERSION='"$(VERSION)"'
LDLIBS   += $(PKG_LIBS) -lm

# `make asan` sets SANITIZE=1.
SANITIZE ?= 0
ifeq ($(SANITIZE),1)
OPTFLAGS = -O1 -g -fno-omit-frame-pointer
SANFLAGS = -fsanitize=address,undefined -fno-sanitize-recover=all
endif
