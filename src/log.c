/*
 * [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
 * Squire Core logging implementation.
 */
#include "squire.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static FILE *squire_log_file = NULL;

static void squire_timestamp(char *buffer, size_t size)
{
    time_t now;
    struct tm tm_now;

    now = time(NULL);
    localtime_r(&now, &tm_now);
    strftime(buffer, size, "%Y-%m-%dT%H:%M:%S%z", &tm_now);
}

int squire_log_open(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return -1;
    }

    squire_log_file = fopen(path, "a");
    if (squire_log_file == NULL) {
        return -1;
    }

    setvbuf(squire_log_file, NULL, _IOLBF, 0);
    return 0;
}

void squire_log_close(void)
{
    if (squire_log_file != NULL) {
        fclose(squire_log_file);
        squire_log_file = NULL;
    }
}

void squire_log(const char *level, const char *message)
{
    char timestamp[64];

    if (squire_log_file == NULL || level == NULL || message == NULL) {
        return;
    }

    squire_timestamp(timestamp, sizeof(timestamp));
    fprintf(squire_log_file, "%s [%s] %s\n", timestamp, level, message);
}

void squire_logf(const char *level, const char *format, ...)
{
    char message[2048];
    va_list args;

    if (format == NULL) {
        return;
    }

    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    squire_log(level, message);
}
