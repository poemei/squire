# [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -D_POSIX_C_SOURCE=200809L
ABI_DIR ?= ../ABI
CPPFLAGS ?= -Iincludes -I$(ABI_DIR)/includes
LDFLAGS ?= -ldl

TARGET := build/squire
SOURCES := src/main.c src/log.c src/config.c src/modules.c
OBJECTS := $(SOURCES:src/%.c=build/%.o)
ABI_OBJECTS := build/abi.o build/abi_module.o build/abi_registry.o build/abi_loader_linux.o
TEST_MODULE := build/test_module.so

PREFIX ?= /opt/squire
BINDIR := $(PREFIX)/bin
CONFIGDIR := $(PREFIX)/config
LOGDIR := $(PREFIX)/logs
MODULEDIR := $(PREFIX)/modules
RAGDIR := $(PREFIX)/rag
STATEDIR := $(PREFIX)/state
SYSTEMD_DIR ?= /etc/systemd/system
SERVICE := squire.service

.PHONY: all clean install uninstall test-module install-test-module uninstall-test-module

all: $(TARGET)

build:
	mkdir -p build

build/%.o: src/%.c includes/squire.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/abi.o: $(ABI_DIR)/src/abi.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/abi_module.o: $(ABI_DIR)/src/module.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/abi_registry.o: $(ABI_DIR)/src/module_registry.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/abi_loader_linux.o: $(ABI_DIR)/src/loader_linux.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS) $(ABI_OBJECTS)
	$(CC) $(OBJECTS) $(ABI_OBJECTS) $(LDFLAGS) -o $@

$(TEST_MODULE): tests/modules/test_module.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared $< -o $@

test-module: $(TEST_MODULE)
	@echo "Built $(TEST_MODULE)"

install-test-module: $(TEST_MODULE)
	install -d $(MODULEDIR)
	install -m 0755 $(TEST_MODULE) $(MODULEDIR)/test_module.so
	@echo "Installed test module to $(MODULEDIR)/test_module.so"

uninstall-test-module:
	rm -f $(MODULEDIR)/test_module.so
	@echo "Removed test module from $(MODULEDIR)/test_module.so"

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
