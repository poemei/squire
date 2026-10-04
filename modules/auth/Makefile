# [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -D_POSIX_C_SOURCE=200809L
ABI_DIR ?= ../../../ABI
SQUIRE_INCLUDE ?= ../../includes
CPPFLAGS ?= -I$(ABI_DIR)/includes -I$(SQUIRE_INCLUDE)

MODULE := auth
SOURCE := $(MODULE).c
BUILD_DIR := build
TARGET := $(BUILD_DIR)/$(MODULE).so

PREFIX ?= /opt/squire
INSTALL_DIR := $(PREFIX)/modules/$(MODULE)
INSTALL_TARGET := $(INSTALL_DIR)/$(MODULE).so
INSTALL_TEMP := $(INSTALL_DIR)/.$(MODULE).so.tmp
CONFIG_DIR := $(PREFIX)/config
AUTH_DATA := $(CONFIG_DIR)/auth.json
AUTH_EXAMPLE := auth.json.example

.PHONY: all clean install uninstall

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(SOURCE) $(SQUIRE_INCLUDE)/irc_access.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared $< -o $@

install: $(TARGET)
	mkdir -p $(INSTALL_DIR)
	mkdir -p $(CONFIG_DIR)
	@if [ ! -e $(AUTH_DATA) ]; then \
		install -m 0640 $(AUTH_EXAMPLE) $(AUTH_DATA); \
		echo "Created $(AUTH_DATA) from $(AUTH_EXAMPLE)."; \
		echo "Future module installs/updates will preserve this authorization file unchanged."; \
	else \
		echo "Preserving existing $(AUTH_DATA) unchanged"; \
	fi
	install -m 0755 $(TARGET) $(INSTALL_TEMP)
	mv -f $(INSTALL_TEMP) $(INSTALL_TARGET)
	@echo "Installed $(MODULE) 0.1.0 to $(INSTALL_TARGET)"

uninstall:
	rm -f $(INSTALL_TEMP) $(INSTALL_TARGET)
	-rmdir $(INSTALL_DIR)
	@echo "Removed $(INSTALL_TARGET); preserved $(AUTH_DATA)"

clean:
	rm -rf $(BUILD_DIR)
