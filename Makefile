# [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -D_POSIX_C_SOURCE=200809L
CPPFLAGS ?= -Iincludes
LDFLAGS ?=

TARGET := build/squire
SOURCES := src/main.c src/log.c src/config.c
OBJECTS := $(SOURCES:src/%.c=build/%.o)

PREFIX ?= /opt/squire
BINDIR := $(PREFIX)/bin
CONFIGDIR := $(PREFIX)/config
LOGDIR := $(PREFIX)/logs
MODULEDIR := $(PREFIX)/modules
RAGDIR := $(PREFIX)/rag
STATEDIR := $(PREFIX)/state
SYSTEMD_DIR ?= /etc/systemd/system
SERVICE := squire.service

.PHONY: all clean install uninstall

all: $(TARGET)

build:
	mkdir -p build

build/%.o: src/%.c includes/squire.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

install: $(TARGET)
	install -d $(BINDIR) $(CONFIGDIR) $(LOGDIR) $(MODULEDIR) $(RAGDIR) $(STATEDIR)
	install -m 0755 $(TARGET) $(BINDIR)/squire
	@if [ ! -f $(CONFIGDIR)/squire.conf ]; then \
		install -m 0640 config/squire.conf.example $(CONFIGDIR)/squire.conf; \
	else \
		echo "Preserving existing $(CONFIGDIR)/squire.conf"; \
	fi
	install -m 0644 deploy/$(SERVICE) $(SYSTEMD_DIR)/$(SERVICE)
	systemctl daemon-reload
	@echo "Squire installed to $(PREFIX)"
	@echo "Enable and start with: systemctl enable --now $(SERVICE)"

uninstall:
	-systemctl stop $(SERVICE)
	-systemctl disable $(SERVICE)
	rm -f $(SYSTEMD_DIR)/$(SERVICE)
	systemctl daemon-reload
	rm -rf $(PREFIX)
	@echo "Squire uninstalled from $(PREFIX)"

clean:
	rm -rf build
