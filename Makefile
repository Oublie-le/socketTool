## socketTool — BusyBox-style multi-call binary
##
## Build:        make            (default: English UI)
## Build (zh):   make LANG=zh    (Chinese UI)
## Install:      make install PREFIX=/usr/local
## Tests:        make test
## Clean:        make clean

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation \
           -std=gnu99 -Isrc
LDFLAGS ?=
LDLIBS  ?= -lpthread

SRC_DIR := src
BIN     := socketTool

# language: en (default) or zh
LANG    ?= en
ifeq ($(LANG),zh)
CFLAGS  += -DST_LANG_ZH=1
else
CFLAGS  += -DST_LANG_EN=1
endif

# layered sources
SOURCES := \
    $(wildcard $(SRC_DIR)/core/*.c)    \
    $(wildcard $(SRC_DIR)/ui/*.c)      \
    $(wildcard $(SRC_DIR)/i18n/*.c)    \
    $(wildcard $(SRC_DIR)/net/*.c)     \
    $(wildcard $(SRC_DIR)/applets/*.c)
OBJECTS := $(SOURCES:.c=.o)

APPLETS := tcp-client tcp-server udp-client udp-server \
           ws-client ws-server bping btest

PREFIX     ?= /usr/local
BINDIR     := $(PREFIX)/bin
COMPDIR    := $(PREFIX)/share/bash-completion/completions

.PHONY: all clean install uninstall links test

all: $(BIN)

$(BIN): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

links: $(BIN)
	@for a in $(APPLETS); do \
	  ln -sf $(BIN) $$a; \
	  echo "  LN  $$a -> $(BIN)"; \
	done

install: $(BIN)
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	@for a in $(APPLETS); do \
	  ln -sf $(BIN) $(DESTDIR)$(BINDIR)/$$a; \
	  echo "  LN  $(DESTDIR)$(BINDIR)/$$a"; \
	done
	install -d $(DESTDIR)$(COMPDIR)
	install -m 0644 scripts/socketTool.bash-completion \
	    $(DESTDIR)$(COMPDIR)/socketTool

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)
	@for a in $(APPLETS); do rm -f $(DESTDIR)$(BINDIR)/$$a; done
	rm -f $(DESTDIR)$(COMPDIR)/socketTool

test: $(BIN) links
	@bash tests/run_all.sh

clean:
	rm -f $(OBJECTS) $(BIN)
	@for a in $(APPLETS); do rm -f $$a; done
