# [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -D_POSIX_C_SOURCE=200809L
ABI_DIR ?= ../../../ABI
CPPFLAGS ?= -I$(ABI_DIR)/includes

MODULE := test_module
SOURCE := $(MODULE).c
BUILD_DIR := build
TARGET := $(BUILD_DIR)/$(MODULE).so

VERSION_MAJOR ?= 0
VERSION_MINOR ?= 5
VERSION_PATCH ?= 0
FAIL_QUALIFICATION ?= 0

PREFIX ?= /opt/squire
INSTALL_DIR := $(PREFIX)/modules/$(MODULE)
INSTALL_TARGET := $(INSTALL_DIR)/$(MODULE).so

DEFINES := \
	-DTEST_MODULE_VERSION_MAJOR=$(VERSION_MAJOR) \
	-DTEST_MODULE_VERSION_MINOR=$(VERSION_MINOR) \
	-DTEST_MODULE_VERSION_PATCH=$(VERSION_PATCH)

ifeq ($(FAIL_QUALIFICATION),1)
DEFINES += -DTEST_MODULE_FORCE_QUALIFICATION_FAILURE=1
endif

.PHONY: all clean install uninstall

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(SOURCE) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEFINES) -fPIC -shared $< -o $@

install: $(TARGET)
	install -d $(INSTALL_DIR)
	install -m 0755 $(TARGET) $(INSTALL_TARGET)
	@echo "Installed $(MODULE) $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_PATCH) to $(INSTALL_TARGET)"

uninstall:
	rm -f $(INSTALL_TARGET)
	-rmdir $(INSTALL_DIR)
	@echo "Removed $(INSTALL_TARGET)"

clean:
	rm -rf $(BUILD_DIR)
