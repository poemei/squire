# [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -D_POSIX_C_SOURCE=200809L
ABI_DIR ?= ../../../ABI
CPPFLAGS ?= -I$(ABI_DIR)/includes
LDLIBS ?= -lssl -lcrypto -lpthread

MODULE := irc
SOURCE := $(MODULE).c
BUILD_DIR := build
TARGET := $(BUILD_DIR)/$(MODULE).so

PREFIX ?= /opt/squire
INSTALL_DIR := $(PREFIX)/modules/$(MODULE)
INSTALL_TARGET := $(INSTALL_DIR)/$(MODULE).so
INSTALL_TEMP := $(INSTALL_DIR)/.$(MODULE).so.tmp
CONFIG_DIR := $(PREFIX)/config
CONFIG_TARGET := $(CONFIG_DIR)/irc.conf
CONFIG_EXAMPLE := irc.conf.example

.PHONY: all clean install uninstall

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(SOURCE) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared $< -o $@ $(LDLIBS)

install: $(TARGET)
	mkdir -p $(INSTALL_DIR)
	mkdir -p $(CONFIG_DIR)
	@if [ ! -e $(CONFIG_TARGET) ]; then \
		install -m 0640 $(CONFIG_EXAMPLE) $(CONFIG_TARGET); \
		echo "Created $(CONFIG_TARGET). This file will never be overwritten by module install/update."; \
		echo "Set the Libera account password, then rerun make install."; \
		exit 1; \
	else \
		echo "Preserving existing $(CONFIG_TARGET) unchanged"; \
	fi
	@if ! grep -Eq '^password=.+$$' $(CONFIG_TARGET); then \
		echo "Refusing to expose $(MODULE).so: $(CONFIG_TARGET) has no Libera account password."; \
		exit 1; \
	fi
	install -m 0755 $(TARGET) $(INSTALL_TEMP)
	mv -f $(INSTALL_TEMP) $(INSTALL_TARGET)
	@echo "Installed $(MODULE) 0.2.4 to $(INSTALL_TARGET)"

uninstall:
	rm -f $(INSTALL_TEMP) $(INSTALL_TARGET)
	-rmdir $(INSTALL_DIR)
	@echo "Removed $(INSTALL_TARGET); preserved $(CONFIG_TARGET)"

clean:
	rm -rf $(BUILD_DIR)
