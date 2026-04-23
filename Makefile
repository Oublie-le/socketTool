## socketTool — BusyBox-style multi-call binary
##
## Build:   make
## Install: make install PREFIX=/usr/local
## Clean:   make clean

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter -std=gnu99
LDFLAGS ?=
LDLIBS  ?= -lpthread

SRC_DIR := src
BIN     := socketTool

SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(SOURCES:.c=.o)

APPLETS := tcp-client tcp-server udp-client udp-server \
           ws-client ws-server bping btest

PREFIX  ?= /usr/local
BINDIR  := $(PREFIX)/bin

.PHONY: all clean install uninstall links

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

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)
	@for a in $(APPLETS); do rm -f $(DESTDIR)$(BINDIR)/$$a; done

clean:
	rm -f $(OBJECTS) $(BIN)
	@for a in $(APPLETS); do rm -f $$a; done
