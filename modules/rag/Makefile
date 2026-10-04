# [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -D_POSIX_C_SOURCE=200809L
ABI_DIR ?= ../../../ABI
SQUIRE_INCLUDE ?= ../../includes
CPPFLAGS ?= -I$(ABI_DIR)/includes -I$(SQUIRE_INCLUDE)

MODULE := rag
SOURCE := $(MODULE).c
BUILD_DIR := build
TARGET := $(BUILD_DIR)/$(MODULE).so

PREFIX ?= /opt/squire
INSTALL_DIR := $(PREFIX)/modules/$(MODULE)
INSTALL_TARGET := $(INSTALL_DIR)/$(MODULE).so
INSTALL_TEMP := $(INSTALL_DIR)/.$(MODULE).so.tmp
RAG_DIR := $(PREFIX)/rag
PUBLIC_DATA := $(RAG_DIR)/public.json
PUBLIC_EXAMPLE := public.json.example

.PHONY: all clean install uninstall

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(SOURCE) $(SQUIRE_INCLUDE)/rosaic_query.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared $< -o $@

install: $(TARGET)
	mkdir -p $(INSTALL_DIR)
	mkdir -p $(RAG_DIR)
	@if [ ! -e $(PUBLIC_DATA) ]; then \
		install -m 0640 $(PUBLIC_EXAMPLE) $(PUBLIC_DATA); \
		echo "Created $(PUBLIC_DATA) from $(PUBLIC_EXAMPLE)."; \
		echo "Future module installs/updates will preserve this runtime dataset unchanged."; \
	else \
		echo "Preserving existing $(PUBLIC_DATA) unchanged"; \
	fi
	install -m 0755 $(TARGET) $(INSTALL_TEMP)
	mv -f $(INSTALL_TEMP) $(INSTALL_TARGET)
	@echo "Installed $(MODULE) 0.2.0 to $(INSTALL_TARGET)"

uninstall:
	rm -f $(INSTALL_TEMP) $(INSTALL_TARGET)
	-rmdir $(INSTALL_DIR)
	@echo "Removed $(INSTALL_TARGET); preserved $(PUBLIC_DATA)"

clean:
	rm -rf $(BUILD_DIR)
