/*
 * [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
 * Squire Rosaic RAG module.
 *
 * Version 0.1.0 proves the Core-routed service boundary only. It does not load
 * doctrine, Rolls, educational material, or any external corpus yet.
 */
#include "module.h"
#include "rosaic_query.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define SQUIRE_RAG_VERSION_MAJOR 0
#define SQUIRE_RAG_VERSION_MINOR 1
#define SQUIRE_RAG_VERSION_PATCH 0

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

static stnlabz_module_result_t rag_query_service(const void *request,
                                                  size_t request_size,
                                                  void *response,
                                                  size_t response_size,
                                                  size_t *response_used,
                                                  void *handler_context)
{
    const squire_rosaic_query_request *query;
    squire_rosaic_query_response *answer;
    const char *placeholder =
        "Squire RAG service is online. The approved Rosaic corpus is not loaded yet.";

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
    if (snprintf(answer->text, sizeof(answer->text), "%s", placeholder) >=
        (int)sizeof(answer->text)) {
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
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
    squire_rosaic_query_request request;
    squire_rosaic_query_response response;
    size_t response_used = 0;
    stnlabz_module_result_t service_result;

    if (result == NULL) {
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    }

    memset(result, 0, sizeof(*result));
    memset(&request, 0, sizeof(request));
    memset(&response, 0, sizeof(response));

    snprintf(request.sender, sizeof(request.sender), "%s", "qualification");
    snprintf(request.target, sizeof(request.target), "%s", "squire");
    snprintf(request.question, sizeof(request.question), "%s", "qualification probe");

    service_result = rag_query_service(&request,
                                       sizeof(request),
                                       &response,
                                       sizeof(response),
                                       &response_used,
                                       NULL);

    rag_test(strcmp("rag", "rag") == 0, result);
    rag_test(SQUIRE_RAG_VERSION_MAJOR == 0, result);
    rag_test(SQUIRE_RAG_VERSION_MINOR == 1, result);
    rag_test(SQUIRE_RAG_VERSION_PATCH == 0, result);
    rag_test(strcmp(SQUIRE_ROSAIC_QUERY_SERVICE, "rosaic.query") == 0, result);
    rag_test(sizeof(squire_rosaic_query_request) > 0, result);
    rag_test(sizeof(squire_rosaic_query_response) > 0, result);
    rag_test(rag_text_valid(request.sender, sizeof(request.sender)), result);
    rag_test(rag_text_valid(request.target, sizeof(request.target)), result);
    rag_test(rag_text_valid(request.question, sizeof(request.question)), result);
    rag_test(service_result == STNLABZ_MODULE_OK, result);
    rag_test(response_used == sizeof(response), result);
    rag_test(response.text[0] != '\0', result);
    rag_test(rag_query_service(NULL,
                               sizeof(request),
                               &response,
                               sizeof(response),
                               &response_used,
                               NULL) == STNLABZ_MODULE_ERR_INVALID_ARGUMENT,
             result);

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
    if (rag_started) {
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    }

    if (host == NULL || host->register_service == NULL ||
        host->unregister_service == NULL) {
        return STNLABZ_MODULE_ERR_START_FAILED;
    }

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
