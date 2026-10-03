/*
 * [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
 * Squire Core configuration loader.
 */
#include "squire.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static char *trim(char *text)
{
    char *end;

    while (isspace((unsigned char)*text)) {
        text++;
    }

    if (*text == '\0') {
        return text;
    }

    end = text + strlen(text) - 1;
    while (end > text && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }

    return text;
}

void squire_config_defaults(squire_config *config)
{
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    snprintf(config->log_path, sizeof(config->log_path), "%s", SQUIRE_DEFAULT_LOG);
    snprintf(config->log_level, sizeof(config->log_level), "%s", "INFO");
    snprintf(config->module_dir, sizeof(config->module_dir), "%s", SQUIRE_DEFAULT_MODULE_DIR);
}

int squire_config_load(const char *path, squire_config *config)
{
    FILE *file;
    char line[2048];

    if (path == NULL || config == NULL) {
        return -1;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *key;
        char *value;
        char *equals;

        key = trim(line);
        if (*key == '\0' || *key == '#') {
            continue;
        }

        equals = strchr(key, '=');
        if (equals == NULL) {
            fclose(file);
            return -1;
        }

        *equals = '\0';
        value = trim(equals + 1);
        key = trim(key);

        if (strcmp(key, "log_path") == 0) {
            if (*value == '\0' || strlen(value) >= sizeof(config->log_path)) {
                fclose(file);
                return -1;
            }
            snprintf(config->log_path, sizeof(config->log_path), "%s", value);
        } else if (strcmp(key, "log_level") == 0) {
            if (*value == '\0' || strlen(value) >= sizeof(config->log_level)) {
                fclose(file);
                return -1;
            }
            snprintf(config->log_level, sizeof(config->log_level), "%s", value);
        } else if (strcmp(key, "module_dir") == 0) {
            if (*value == '\0' || strlen(value) >= sizeof(config->module_dir)) {
                fclose(file);
                return -1;
            }
            snprintf(config->module_dir, sizeof(config->module_dir), "%s", value);
        } else {
            fclose(file);
            return -1;
        }
    }

    fclose(file);
    return 0;
}
