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

TEST_MODULE_DIR := build/modules/test_module
TEST_MODULE := $(TEST_MODULE_DIR)/test_module.so
TEST_MODULE_V4 := $(TEST_MODULE_DIR)/test_module_v4.so
TEST_MODULE_BAD := $(TEST_MODULE_DIR)/test_module_bad.so

PREFIX ?= /opt/squire
BINDIR := $(PREFIX)/bin
CONFIGDIR := $(PREFIX)/config
LOGDIR := $(PREFIX)/logs
MODULEDIR := $(PREFIX)/modules
RAGDIR := $(PREFIX)/rag
STATEDIR := $(PREFIX)/state
SYSTEMD_DIR ?= /etc/systemd/system
SERVICE := squire.service

.PHONY: all clean install uninstall test-module test-module-v4 test-module-bad \
	install-test-module install-test-module-v4 install-test-module-bad uninstall-test-module

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

$(TEST_MODULE): tests/modules/test_module.c
	mkdir -p $(TEST_MODULE_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared \
		-DTEST_MODULE_VERSION_MINOR=1 $< -o $@

$(TEST_MODULE_V4): tests/modules/test_module.c
	mkdir -p $(TEST_MODULE_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared \
		-DTEST_MODULE_VERSION_MINOR=4 $< -o $@

$(TEST_MODULE_BAD): tests/modules/test_module.c
	mkdir -p $(TEST_MODULE_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared \
		-DTEST_MODULE_VERSION_MINOR=3 \
		-DTEST_MODULE_FORCE_QUALIFICATION_FAILURE=1 $< -o $@

test-module: $(TEST_MODULE)
	@echo "Built $(TEST_MODULE)"

test-module-v4: $(TEST_MODULE_V4)
	@echo "Built $(TEST_MODULE_V4)"

test-module-bad: $(TEST_MODULE_BAD)
	@echo "Built $(TEST_MODULE_BAD)"

install-test-module: $(TEST_MODULE)
	install -d $(MODULEDIR)/test_module
	install -m 0755 $(TEST_MODULE) $(MODULEDIR)/test_module/test_module.so
	@echo "Installed test module v0.1.0 to $(MODULEDIR)/test_module/test_module.so"

install-test-module-v4: $(TEST_MODULE_V4)
	install -d $(MODULEDIR)/test_module
	install -m 0755 $(TEST_MODULE_V4) $(MODULEDIR)/test_module/test_module.so
	@echo "Installed test module v0.4.0 to $(MODULEDIR)/test_module/test_module.so"

install-test-module-bad: $(TEST_MODULE_BAD)
	install -d $(MODULEDIR)/test_module
	install -m 0755 $(TEST_MODULE_BAD) $(MODULEDIR)/test_module/test_module.so
	@echo "Installed intentionally failing test module v0.3.0 to $(MODULEDIR)/test_module/test_module.so"

uninstall-test-module:
	rm -rf $(MODULEDIR)/test_module
	@echo "Removed test module directory $(MODULEDIR)/test_module"

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
