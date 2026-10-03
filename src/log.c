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

static void squire_sanitize_field(const char *input, char *output, size_t size)
{
    size_t i;
    size_t j;

    if (output == NULL || size == 0) {
        return;
    }

    output[0] = '\0';
    if (input == NULL) {
        return;
    }

    for (i = 0, j = 0; input[i] != '\0' && j + 1 < size; i++) {
        char ch = input[i];

        if (ch == '\n' || ch == '\r' || ch == '\t') {
            ch = ' ';
        } else if (ch == '"') {
            ch = '\'';
        }

        output[j++] = ch;
    }

    output[j] = '\0';
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

void squire_audit_event(const char *event,
                        const char *status,
                        const char *subject,
                        const char *version,
                        const char *detail)
{
    char clean_event[128];
    char clean_status[64];
    char clean_subject[256];
    char clean_version[128];
    char clean_detail[1024];
    char message[2048];

    squire_sanitize_field(event, clean_event, sizeof(clean_event));
    squire_sanitize_field(status, clean_status, sizeof(clean_status));
    squire_sanitize_field(subject, clean_subject, sizeof(clean_subject));
    squire_sanitize_field(version, clean_version, sizeof(clean_version));
    squire_sanitize_field(detail, clean_detail, sizeof(clean_detail));

    snprintf(message,
             sizeof(message),
             "event=\"%s\" status=\"%s\" subject=\"%s\" version=\"%s\" detail=\"%s\"",
             clean_event,
             clean_status,
             clean_subject,
             clean_version,
             clean_detail);

    squire_log("AUDIT", message);
}
