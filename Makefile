# [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -D_POSIX_C_SOURCE=200809L
CPPFLAGS ?= -Iincludes
LDFLAGS ?=

TARGET := build/squire
SOURCES := src/main.c src/log.c src/config.c
OBJECTS := $(SOURCES:src/%.c=build/%.o)

.PHONY: all clean

all: $(TARGET)

build:
	mkdir -p build

build/%.o: src/%.c includes/squire.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) -o $@

clean:
	rm -rf build
