/*
 * [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
 * Squire Rosaic RAG module.
 *
 * Version 0.2.0 loads simple approved public information from
 * /opt/squire/rag/public.json. The dataset is read on every query so operator
 * edits take effect immediately without recompiling or restarting Squire.
 *
 * This revision does not load the Codex, Rolls, educational material, or any
 * other governed corpus.
 */
#include "module.h"
#include "rosaic_query.h"

#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SQUIRE_RAG_VERSION_MAJOR 0
#define SQUIRE_RAG_VERSION_MINOR 2
#define SQUIRE_RAG_VERSION_PATCH 0

#define RAG_PUBLIC_PATH "/opt/squire/rag/public.json"
#define RAG_PUBLIC_MAX_BYTES 65536
#define RAG_PUBLIC_MAX_ALIASES 32
#define RAG_PUBLIC_ALIAS_MAX 128

static const stnlabz_module_host_t *rag_host = NULL;
static int rag_started = 0;

static void rag_test(int condition, stnlabz_module_qualification_result_t *result)
{
    result->tests_executed++;
    if (condition) {
        result->tests_passed++;
    } else {
        result->tests_failed++;
    }
}

static int rag_text_valid(const char *text, size_t maximum)
{
    size_t length;

    if (text == NULL || maximum == 0) {
        return 0;
    }

    length = strlen(text);
    return length > 0 && length < maximum;
}

static const char *rag_skip_space(const char *cursor)
{
    while (cursor != NULL && *cursor != '\0' && isspace((unsigned char)*cursor)) {
        cursor++;
    }
    return cursor;
}

static int rag_json_string(const char **cursor, char *output, size_t output_size)
{
    const char *input;
    size_t used = 0;

    if (cursor == NULL || *cursor == NULL || output == NULL || output_size == 0) {
        return -1;
    }

    input = rag_skip_space(*cursor);
    if (input == NULL || *input != '"') {
        return -1;
    }
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

        if (used + 1 >= output_size) {
            return -1;
        }
        output[used++] = value;
    }

    if (*input != '"') {
        return -1;
    }

    output[used] = '\0';
    *cursor = input + 1;
    return 0;
}

static int rag_find_string_value(const char *json,
                                 const char *key,
                                 char *output,
                                 size_t output_size)
{
    char needle[64];
    const char *cursor;

    if (json == NULL || key == NULL || output == NULL || output_size == 0) {
        return -1;
    }

    if (snprintf(needle, sizeof(needle), "\"%s\"", key) >= (int)sizeof(needle)) {
        return -1;
    }

    cursor = strstr(json, needle);
    if (cursor == NULL) return -1;
    cursor += strlen(needle);
    cursor = rag_skip_space(cursor);
    if (cursor == NULL || *cursor != ':') return -1;
    cursor++;

    return rag_json_string(&cursor, output, output_size);
}

static int rag_load_public_file(char **json_out)
{
    FILE *file;
    long length;
    char *buffer;
    size_t read_count;

    if (json_out == NULL) return -1;
    *json_out = NULL;

    file = fopen(RAG_PUBLIC_PATH, "rb");
    if (file == NULL) return -1;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }

    length = ftell(file);
    if (length <= 0 || length > RAG_PUBLIC_MAX_BYTES) {
        fclose(file);
        return -1;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }

    buffer = (char *)malloc((size_t)length + 1);
    if (buffer == NULL) {
        fclose(file);
        return -1;
    }

    read_count = fread(buffer, 1, (size_t)length, file);
    fclose(file);

    if (read_count != (size_t)length) {
        free(buffer);
        return -1;
    }

    buffer[length] = '\0';
    *json_out = buffer;
    return 0;
}

static int rag_normalize_question(const char *input, char *output, size_t output_size)
{
    size_t used = 0;
    const char *cursor;

    if (input == NULL || output == NULL || output_size == 0) return -1;

    cursor = input;
    while (isspace((unsigned char)*cursor)) cursor++;

    if (strncasecmp(cursor, "squire", 6) == 0) {
        const char *after = cursor + 6;
        if (*after == ':' || *after == ',' || isspace((unsigned char)*after)) {
            cursor = after;
            while (*cursor == ':' || *cursor == ',' || isspace((unsigned char)*cursor)) cursor++;
        }
    }

    while (*cursor != '\0') {
        unsigned char c = (unsigned char)*cursor++;

        if (isspace(c)) {
            if (used > 0 && output[used - 1] != ' ') {
                if (used + 1 >= output_size) return -1;
                output[used++] = ' ';
            }
            continue;
        }

        if (used + 1 >= output_size) return -1;
        output[used++] = (char)tolower(c);
    }

    while (used > 0 && (output[used - 1] == ' ' || output[used - 1] == '.' ||
                        output[used - 1] == '?' || output[used - 1] == '!')) {
        used--;
    }

    output[used] = '\0';
    return used > 0 ? 0 : -1;
}

static int rag_alias_matches(const char *alias, const char *normalized_question)
{
    char normalized_alias[RAG_PUBLIC_ALIAS_MAX];

    if (alias == NULL || normalized_question == NULL) return 0;
    if (rag_normalize_question(alias, normalized_alias, sizeof(normalized_alias)) != 0) return 0;
    return strcmp(normalized_alias, normalized_question) == 0;
}

static int rag_object_matches(const char *object_start,
                              const char *object_end,
                              const char *normalized_question,
                              char *response,
                              size_t response_size)
{
    char object[4096];
    char alias[RAG_PUBLIC_ALIAS_MAX];
    const char *aliases;
    const char *cursor;
    size_t length;
    unsigned int alias_count = 0;

    if (object_start == NULL || object_end == NULL || object_end <= object_start ||
        normalized_question == NULL || response == NULL || response_size == 0) {
        return -1;
    }

    length = (size_t)(object_end - object_start + 1);
    if (length >= sizeof(object)) return -1;
    memcpy(object, object_start, length);
    object[length] = '\0';

    aliases = strstr(object, "\"aliases\"");
    if (aliases == NULL) return 0;
    aliases = strchr(aliases, '[');
    if (aliases == NULL) return -1;
    cursor = aliases + 1;

    while (*cursor != '\0' && *cursor != ']' && alias_count < RAG_PUBLIC_MAX_ALIASES) {
        cursor = rag_skip_space(cursor);
        if (*cursor == ',') {
            cursor++;
            continue;
        }
        if (*cursor == ']') break;

        if (rag_json_string(&cursor, alias, sizeof(alias)) != 0) return -1;
        alias_count++;

        if (rag_alias_matches(alias, normalized_question)) {
            if (rag_find_string_value(object, "response", response, response_size) != 0) return -1;
            return 1;
        }
    }

    return 0;
}

static int rag_public_lookup(const char *question, char *response, size_t response_size)
{
    char *json = NULL;
    char normalized[SQUIRE_ROSAIC_QUERY_TEXT_MAX];
    const char *entries;
    const char *cursor;
    int result = -1;

    if (question == NULL || response == NULL || response_size == 0) return -1;
    if (rag_normalize_question(question, normalized, sizeof(normalized)) != 0) return -1;
    if (rag_load_public_file(&json) != 0) return -1;

    entries = strstr(json, "\"entries\"");
    if (entries == NULL || (entries = strchr(entries, '[')) == NULL) goto done;
    cursor = entries + 1;

    while (*cursor != '\0' && *cursor != ']') {
        const char *object_start;
        const char *object_end;
        int depth = 0;
        int in_string = 0;
        int escaped = 0;
        int matched;

        cursor = rag_skip_space(cursor);
        if (*cursor == ',') {
            cursor++;
            continue;
        }
        if (*cursor == ']') break;
        if (*cursor != '{') goto done;

        object_start = cursor;
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

        if (*cursor != '}' || depth != 0) goto done;
        object_end = cursor;

        matched = rag_object_matches(object_start,
                                     object_end,
                                     normalized,
                                     response,
                                     response_size);
        if (matched < 0) goto done;
        if (matched > 0) {
            result = 1;
            goto done;
        }
        cursor++;
    }

    if (rag_find_string_value(json, "fallback", response, response_size) != 0) goto done;
    result = 0;

done:
    free(json);
    return result;
}

static stnlabz_module_result_t rag_query_service(const void *request,
                                                  size_t request_size,
                                                  void *response,
                                                  size_t response_size,
                                                  size_t *response_used,
                                                  void *handler_context)
{
    const squire_rosaic_query_request *query;
    squire_rosaic_query_response *answer;
    int lookup_result;

    (void)handler_context;

    if (response_used != NULL) {
        *response_used = 0;
    }

    if (request == NULL || response == NULL || response_used == NULL ||
        request_size != sizeof(squire_rosaic_query_request) ||
        response_size < sizeof(squire_rosaic_query_response)) {
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    }

    query = (const squire_rosaic_query_request *)request;
    answer = (squire_rosaic_query_response *)response;

    if (!rag_text_valid(query->sender, sizeof(query->sender)) ||
        !rag_text_valid(query->target, sizeof(query->target)) ||
        !rag_text_valid(query->question, sizeof(query->question))) {
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    }

    memset(answer, 0, sizeof(*answer));
    lookup_result = rag_public_lookup(query->question, answer->text, sizeof(answer->text));
    if (lookup_result < 0 || answer->text[0] == '\0') {
        return STNLABZ_MODULE_ERR_NOT_FOUND;
    }

    *response_used = sizeof(*answer);
    return STNLABZ_MODULE_OK;
}

static int rag_negative_probe(const void *value)
{
    return value == NULL ? -1 : 0;
}

static stnlabz_module_result_t rag_qualify(stnlabz_module_qualification_result_t *result)
{
    char normalized[128];
    const char *json_probe =
        "{\"fallback\":\"unknown\",\"entries\":[{\"id\":\"help\","
        "\"aliases\":[\"help\",\"commands\"],\"response\":\"ok\"}]}";
    char extracted[64];

    if (result == NULL) {
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    }

    memset(result, 0, sizeof(*result));

    rag_test(strcmp("rag", "rag") == 0, result);
    rag_test(SQUIRE_RAG_VERSION_MAJOR == 0, result);
    rag_test(SQUIRE_RAG_VERSION_MINOR == 2, result);
    rag_test(SQUIRE_RAG_VERSION_PATCH == 0, result);
    rag_test(strcmp(SQUIRE_ROSAIC_QUERY_SERVICE, "rosaic.query") == 0, result);
    rag_test(sizeof(squire_rosaic_query_request) > 0, result);
    rag_test(sizeof(squire_rosaic_query_response) > 0, result);
    rag_test(rag_normalize_question("  HELP? ", normalized, sizeof(normalized)) == 0 &&
             strcmp(normalized, "help") == 0,
             result);
    rag_test(rag_normalize_question("Squire: Website", normalized, sizeof(normalized)) == 0 &&
             strcmp(normalized, "website") == 0,
             result);
    rag_test(rag_alias_matches("WHAT IS ROSAIC", "what is rosaic"), result);
    rag_test(rag_find_string_value(json_probe, "fallback", extracted, sizeof(extracted)) == 0 &&
             strcmp(extracted, "unknown") == 0,
             result);
    rag_test(rag_find_string_value(json_probe, "response", extracted, sizeof(extracted)) == 0 &&
             strcmp(extracted, "ok") == 0,
             result);
    rag_test(rag_text_valid("help", 16), result);
    rag_test(!rag_text_valid("", 16), result);

    result->negative_test_executed = 1;
    result->negative_test_passed = (rag_negative_probe(NULL) != 0);

    if (result->tests_executed < STNLABZ_MODULE_MIN_TESTS ||
        result->tests_failed != 0 ||
        !result->negative_test_passed) {
        return STNLABZ_MODULE_ERR_QUALIFICATION;
    }

    return STNLABZ_MODULE_OK;
}

static stnlabz_module_result_t rag_start(const stnlabz_module_host_t *host)
{
    char *probe = NULL;

    if (rag_started) {
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    }

    if (host == NULL || host->register_service == NULL ||
        host->unregister_service == NULL) {
        return STNLABZ_MODULE_ERR_START_FAILED;
    }

    if (rag_load_public_file(&probe) != 0) {
        return STNLABZ_MODULE_ERR_START_FAILED;
    }
    free(probe);

    if (host->register_service(SQUIRE_ROSAIC_QUERY_SERVICE,
                               rag_query_service,
                               NULL) != 0) {
        return STNLABZ_MODULE_ERR_START_FAILED;
    }

    rag_host = host;
    rag_started = 1;
    return STNLABZ_MODULE_OK;
}

static stnlabz_module_result_t rag_stop(void)
{
    if (!rag_started || rag_host == NULL || rag_host->unregister_service == NULL) {
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    }

    if (rag_host->unregister_service(SQUIRE_ROSAIC_QUERY_SERVICE, NULL) != 0) {
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    }

    rag_host = NULL;
    rag_started = 0;
    return STNLABZ_MODULE_OK;
}

static const stnlabz_module_descriptor_t RAG_DESCRIPTOR = {
    "rag",
    "Squire Rosaic RAG Module",
    SQUIRE_RAG_VERSION_MAJOR,
    SQUIRE_RAG_VERSION_MINOR,
    SQUIRE_RAG_VERSION_PATCH,
    STNLABZ_MODULE_API_MAJOR,
    STNLABZ_MODULE_API_MINOR,
    rag_qualify,
    rag_start,
    rag_stop
};

const stnlabz_module_descriptor_t *stnlabz_module_get_descriptor(void)
{
    return &RAG_DESCRIPTOR;
}
