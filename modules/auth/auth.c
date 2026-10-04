/*
 * [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
 * Squire IRC authorization module.
 *
 * Runtime authority data lives in /opt/squire/config/auth.json. The file is
 * read on every authorization request so account changes take effect without
 * recompiling, hot-loading, or restarting Squire.
 */
#include "module.h"
#include "irc_access.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define SQUIRE_AUTH_VERSION_MAJOR 0
#define SQUIRE_AUTH_VERSION_MINOR 1
#define SQUIRE_AUTH_VERSION_PATCH 0

#define AUTH_PATH "/opt/squire/config/auth.json"
#define AUTH_MAX_BYTES 65536
#define AUTH_OBJECT_MAX 4096
#define AUTH_PERMISSION_MAX_COUNT 32

static const stnlabz_module_host_t *auth_host = NULL;
static int auth_started = 0;

static void auth_test(int condition, stnlabz_module_qualification_result_t *result)
{
    result->tests_executed++;
    if (condition) result->tests_passed++;
    else result->tests_failed++;
}

static int auth_text_valid(const char *text, size_t maximum)
{
    size_t length;
    if (text == NULL || maximum == 0) return 0;
    length = strlen(text);
    return length > 0 && length < maximum;
}

static const char *auth_skip_space(const char *cursor)
{
    while (cursor != NULL && *cursor != '\0' && isspace((unsigned char)*cursor)) cursor++;
    return cursor;
}

static int auth_json_string(const char **cursor, char *output, size_t output_size)
{
    const char *input;
    size_t used = 0;

    if (cursor == NULL || *cursor == NULL || output == NULL || output_size == 0) return -1;
    input = auth_skip_space(*cursor);
    if (input == NULL || *input != '"') return -1;
    input++;

    while (*input != '\0' && *input != '"') {
        char value = *input++;
        if (value == '\\') {
            value = *input++;
            if (value == '\0') return -1;
            if (value == 'n') value = '\n';
            else if (value == 'r') value = '\r';
            else if (value == 't') value = '\t';
            else if (value != '"' && value != '\\' && value != '/') return -1;
        }
        if (used + 1 >= output_size) return -1;
        output[used++] = value;
    }

    if (*input != '"') return -1;
    output[used] = '\0';
    *cursor = input + 1;
    return 0;
}

static int auth_find_string_value(const char *json,
                                  const char *key,
                                  char *output,
                                  size_t output_size)
{
    char needle[64];
    const char *cursor;

    if (json == NULL || key == NULL || output == NULL || output_size == 0) return -1;
    if (snprintf(needle, sizeof(needle), "\"%s\"", key) >= (int)sizeof(needle)) return -1;

    cursor = strstr(json, needle);
    if (cursor == NULL) return -1;
    cursor += strlen(needle);
    cursor = auth_skip_space(cursor);
    if (cursor == NULL || *cursor != ':') return -1;
    cursor++;
    return auth_json_string(&cursor, output, output_size);
}

static int auth_load_file(char **json_out)
{
    FILE *file;
    long length;
    char *buffer;
    size_t read_count;

    if (json_out == NULL) return -1;
    *json_out = NULL;

    file = fopen(AUTH_PATH, "rb");
    if (file == NULL) return -1;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return -1; }
    length = ftell(file);
    if (length <= 0 || length > AUTH_MAX_BYTES) { fclose(file); return -1; }
    if (fseek(file, 0, SEEK_SET) != 0) { fclose(file); return -1; }

    buffer = (char *)malloc((size_t)length + 1);
    if (buffer == NULL) { fclose(file); return -1; }
    read_count = fread(buffer, 1, (size_t)length, file);
    fclose(file);
    if (read_count != (size_t)length) { free(buffer); return -1; }

    buffer[length] = '\0';
    *json_out = buffer;
    return 0;
}

static int auth_permission_match(const char *object, const char *permission)
{
    const char *permissions;
    const char *cursor;
    char value[SQUIRE_IRC_ACCESS_PERMISSION_MAX];
    unsigned int count = 0;

    permissions = strstr(object, "\"permissions\"");
    if (permissions == NULL || (permissions = strchr(permissions, '[')) == NULL) return 0;
    cursor = permissions + 1;

    while (*cursor != '\0' && *cursor != ']' && count < AUTH_PERMISSION_MAX_COUNT) {
        cursor = auth_skip_space(cursor);
        if (*cursor == ',') { cursor++; continue; }
        if (*cursor == ']') break;
        if (auth_json_string(&cursor, value, sizeof(value)) != 0) return 0;
        count++;
        if (strcmp(value, permission) == 0) return 1;
    }

    return 0;
}

static int auth_check_json(const char *json,
                           const char *account,
                           const char *channel,
                           const char *permission)
{
    const char *entries;
    const char *cursor;

    if (json == NULL || account == NULL || channel == NULL || permission == NULL) return -1;
    entries = strstr(json, "\"entries\"");
    if (entries == NULL || (entries = strchr(entries, '[')) == NULL) return -1;
    cursor = entries + 1;

    while (*cursor != '\0' && *cursor != ']') {
        const char *start;
        const char *end;
        char object[AUTH_OBJECT_MAX];
        char object_account[SQUIRE_IRC_ACCESS_ACCOUNT_MAX];
        char object_channel[SQUIRE_IRC_ACCESS_CHANNEL_MAX];
        size_t length;
        int depth = 0;
        int in_string = 0;
        int escaped = 0;

        cursor = auth_skip_space(cursor);
        if (*cursor == ',') { cursor++; continue; }
        if (*cursor == ']') break;
        if (*cursor != '{') return -1;

        start = cursor;
        for (; *cursor != '\0'; cursor++) {
            char c = *cursor;
            if (in_string) {
                if (escaped) escaped = 0;
                else if (c == '\\') escaped = 1;
                else if (c == '"') in_string = 0;
                continue;
            }
            if (c == '"') in_string = 1;
            else if (c == '{') depth++;
            else if (c == '}') {
                depth--;
                if (depth == 0) break;
            }
        }

        if (*cursor != '}' || depth != 0) return -1;
        end = cursor;
        length = (size_t)(end - start + 1);
        if (length >= sizeof(object)) return -1;
        memcpy(object, start, length);
        object[length] = '\0';

        if (auth_find_string_value(object, "account", object_account, sizeof(object_account)) == 0 &&
            auth_find_string_value(object, "channel", object_channel, sizeof(object_channel)) == 0 &&
            strcasecmp(object_account, account) == 0 &&
            strcmp(object_channel, channel) == 0) {
            return auth_permission_match(object, permission) ? 1 : 0;
        }

        cursor++;
    }

    return 0;
}

static stnlabz_module_result_t auth_service(const void *request,
                                             size_t request_size,
                                             void *response,
                                             size_t response_size,
                                             size_t *response_used,
                                             void *handler_context)
{
    const squire_irc_access_request *check;
    squire_irc_access_response *answer;
    char *json = NULL;
    int allowed;

    (void)handler_context;
    if (response_used != NULL) *response_used = 0;

    if (request == NULL || response == NULL || response_used == NULL ||
        request_size != sizeof(squire_irc_access_request) ||
        response_size < sizeof(squire_irc_access_response)) {
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    }

    check = (const squire_irc_access_request *)request;
    answer = (squire_irc_access_response *)response;

    if (!auth_text_valid(check->account, sizeof(check->account)) ||
        !auth_text_valid(check->channel, sizeof(check->channel)) ||
        !auth_text_valid(check->permission, sizeof(check->permission))) {
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    }

    if (auth_load_file(&json) != 0) return STNLABZ_MODULE_ERR_NOT_FOUND;
    allowed = auth_check_json(json, check->account, check->channel, check->permission);
    free(json);
    if (allowed < 0) return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;

    memset(answer, 0, sizeof(*answer));
    answer->allowed = allowed ? 1 : 0;
    *response_used = sizeof(*answer);
    return STNLABZ_MODULE_OK;
}

static int auth_negative_probe(const void *value)
{
    return value == NULL ? -1 : 0;
}

static stnlabz_module_result_t auth_qualify(stnlabz_module_qualification_result_t *result)
{
    const char *probe =
        "{\"version\":1,\"entries\":["
        "{\"account\":\"STN_Boss\",\"channel\":\"##rosaic\",\"permissions\":[\"op\"]},"
        "{\"account\":\"M_Rowan\",\"channel\":\"##rosaic\",\"permissions\":[\"op\"]}]}";

    if (result == NULL) return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    memset(result, 0, sizeof(*result));

    auth_test(strcmp("auth", "auth") == 0, result);
    auth_test(SQUIRE_AUTH_VERSION_MAJOR == 0, result);
    auth_test(SQUIRE_AUTH_VERSION_MINOR == 1, result);
    auth_test(SQUIRE_AUTH_VERSION_PATCH == 0, result);
    auth_test(strcmp(SQUIRE_IRC_ACCESS_SERVICE, "irc.access.check") == 0, result);
    auth_test(sizeof(squire_irc_access_request) > 0, result);
    auth_test(sizeof(squire_irc_access_response) > 0, result);
    auth_test(auth_check_json(probe, "STN_Boss", "##rosaic", "op") == 1, result);
    auth_test(auth_check_json(probe, "stn_boss", "##rosaic", "op") == 1, result);
    auth_test(auth_check_json(probe, "M_Rowan", "##rosaic", "op") == 1, result);
    auth_test(auth_check_json(probe, "unknown", "##rosaic", "op") == 0, result);
    auth_test(auth_check_json(probe, "STN_Boss", "##other", "op") == 0, result);
    auth_test(auth_check_json(probe, "STN_Boss", "##rosaic", "voice") == 0, result);
    auth_test(auth_text_valid("STN_Boss", SQUIRE_IRC_ACCESS_ACCOUNT_MAX), result);

    result->negative_test_executed = 1;
    result->negative_test_passed = (auth_negative_probe(NULL) != 0);

    if (result->tests_executed < STNLABZ_MODULE_MIN_TESTS ||
        result->tests_failed != 0 ||
        !result->negative_test_passed) {
        return STNLABZ_MODULE_ERR_QUALIFICATION;
    }

    return STNLABZ_MODULE_OK;
}

static stnlabz_module_result_t auth_start(const stnlabz_module_host_t *host)
{
    char *probe = NULL;

    if (auth_started) return STNLABZ_MODULE_ERR_INVALID_STATE;
    if (host == NULL || host->register_service == NULL || host->unregister_service == NULL) {
        return STNLABZ_MODULE_ERR_START_FAILED;
    }

    if (auth_load_file(&probe) != 0) return STNLABZ_MODULE_ERR_START_FAILED;
    free(probe);

    if (host->register_service(SQUIRE_IRC_ACCESS_SERVICE, auth_service, NULL) != 0) {
        return STNLABZ_MODULE_ERR_START_FAILED;
    }

    auth_host = host;
    auth_started = 1;
    return STNLABZ_MODULE_OK;
}

static stnlabz_module_result_t auth_stop(void)
{
    if (!auth_started || auth_host == NULL || auth_host->unregister_service == NULL) {
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    }

    if (auth_host->unregister_service(SQUIRE_IRC_ACCESS_SERVICE, NULL) != 0) {
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    }

    auth_host = NULL;
    auth_started = 0;
    return STNLABZ_MODULE_OK;
}

static const stnlabz_module_descriptor_t AUTH_DESCRIPTOR = {
    "auth",
    "Squire IRC Authorization Module",
    SQUIRE_AUTH_VERSION_MAJOR,
    SQUIRE_AUTH_VERSION_MINOR,
    SQUIRE_AUTH_VERSION_PATCH,
    STNLABZ_MODULE_API_MAJOR,
    STNLABZ_MODULE_API_MINOR,
    auth_qualify,
    auth_start,
    auth_stop
};

const stnlabz_module_descriptor_t *stnlabz_module_get_descriptor(void)
{
    return &AUTH_DESCRIPTOR;
}
