## socketTool — BusyBox-style multi-call binary
##
## Run `make help` for a list of targets and variables.

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation \
           -std=gnu99 -Isrc
LDFLAGS ?=
LDLIBS  ?= -lpthread

SRC_DIR := src
BIN     := socketTool

# UI language: en (default) or zh — set explicitly to override
UILANG  ?= en
ifeq ($(UILANG),zh)
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
           ws-client ws-server bping btest diag

PREFIX     ?= /usr/local
BINDIR     := $(PREFIX)/bin
COMPDIR    := $(PREFIX)/share/bash-completion/completions

.PHONY: all build help clean install uninstall links test setcap lint coverage

# colors for `make help` (only when stdout is a TTY)
ifneq (,$(findstring xterm,$(TERM))$(findstring screen,$(TERM)))
C_BOLD := \033[1m
C_CYAN := \033[36m
C_DIM  := \033[2m
C_RST  := \033[0m
endif

all: build

##@ Build

build: $(BIN)  ## Build the socketTool binary (alias: make)

$(BIN): $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

links: $(BIN)  ## Create applet symlinks (tcp-client, bping, ...) in cwd
	@for a in $(APPLETS); do \
	  ln -sf $(BIN) $$a; \
	  echo "  LN  $$a -> $(BIN)"; \
	done

##@ Install

install: $(BIN)  ## Install binary, symlinks and bash-completion
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)
	@for a in $(APPLETS); do \
	  ln -sf $(BIN) $(DESTDIR)$(BINDIR)/$$a; \
	  echo "  LN  $(DESTDIR)$(BINDIR)/$$a"; \
	done
	install -d $(DESTDIR)$(COMPDIR)
	install -m 0644 scripts/socketTool.bash-completion \
	    $(DESTDIR)$(COMPDIR)/socketTool

uninstall:  ## Remove installed files
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)
	@for a in $(APPLETS); do rm -f $(DESTDIR)$(BINDIR)/$$a; done
	rm -f $(DESTDIR)$(COMPDIR)/socketTool

setcap: $(BIN)  ## Grant CAP_NET_RAW to ./socketTool (for native ICMP without root)
	sudo setcap cap_net_raw+ep ./$(BIN)

##@ Quality

test: $(BIN) links  ## Run the full test suite (unit + e2e)
	@bash tests/run_all.sh

lint:  ## Static analysis with cppcheck (no warnings allowed)
	@command -v cppcheck >/dev/null || { echo "cppcheck not installed"; exit 1; }
	cppcheck --enable=warning,performance,portability --std=c99 -q \
	    --error-exitcode=1 --inline-suppr \
	    --suppress=memleakOnRealloc \
	    -Isrc $(SRC_DIR)

coverage: clean  ## Build+test with gcov/lcov; writes HTML to coverage/
	@command -v lcov >/dev/null || { echo "lcov not installed"; exit 1; }
	$(MAKE) CFLAGS="-O0 -g --coverage -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation -std=gnu99 -Isrc" LDFLAGS="--coverage" test
	lcov --capture --directory . --output-file coverage.info --quiet
	lcov --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage.info --quiet
	genhtml coverage.info --output-directory coverage --quiet
	@echo "coverage report: coverage/index.html"

clean:  ## Remove build artefacts and applet symlinks
	rm -f $(OBJECTS) $(BIN)
	find $(SRC_DIR) -name '*.gcda' -delete -o -name '*.gcno' -delete 2>/dev/null || true
	rm -rf coverage coverage.info
	@for a in $(APPLETS); do rm -f $$a; done

##@ Help

help:  ## Print this help
	@printf "$(C_BOLD)socketTool — multi-protocol network test toolkit$(C_RST)\n\n"
	@printf "$(C_BOLD)Usage:$(C_RST)\n  make $(C_CYAN)<target>$(C_RST) [VAR=value ...]\n\n"
	@awk 'BEGIN { FS = ":.*##"; cur="" }                                      \
	     /^##@/ { sub(/^##@ */, ""); printf "\n$(C_BOLD)%s$(C_RST)\n", $$0 }  \
	     /^[a-zA-Z0-9_.\/-]+:.*##/ {                                          \
	       printf "  $(C_CYAN)%-14s$(C_RST) %s\n", $$1, $$2                   \
	     }' $(MAKEFILE_LIST)
	@printf "\n$(C_BOLD)Variables:$(C_RST)\n"
	@printf "  $(C_CYAN)%-18s$(C_RST) %s\n" \
	    "UILANG=zh|en"      "compile-time UI language (default: $(UILANG))" \
	    "PREFIX=/usr/local" "install prefix (default: $(PREFIX))"           \
	    "DESTDIR="          "staging dir prepended to PREFIX during install"\
	    "CC=gcc"            "C compiler"                                    \
	    "CFLAGS=..."        "extra C flags"
	@printf "\n$(C_BOLD)Examples:$(C_RST)\n"
	@printf "  $(C_DIM)make$(C_RST)                       # build (UI lang = $(UILANG))\n"
	@printf "  $(C_DIM)make UILANG=zh$(C_RST)             # build with Chinese UI\n"
	@printf "  $(C_DIM)make links$(C_RST)                 # create applet symlinks\n"
	@printf "  $(C_DIM)make setcap$(C_RST)                # enable native ICMP for non-root\n"
	@printf "  $(C_DIM)make test$(C_RST)                  # run all tests\n"
	@printf "  $(C_DIM)make install PREFIX=/opt/st$(C_RST)\n\n"
