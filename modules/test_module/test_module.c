/*
 * [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
 * Squire ABI lifecycle and Core service-routing test module.
 */
#include "module.h"

#include <string.h>

#ifndef TEST_MODULE_VERSION_MAJOR
#define TEST_MODULE_VERSION_MAJOR 0
#endif

#ifndef TEST_MODULE_VERSION_MINOR
#define TEST_MODULE_VERSION_MINOR 6
#endif

#ifndef TEST_MODULE_VERSION_PATCH
#define TEST_MODULE_VERSION_PATCH 0
#endif

#define TEST_SERVICE_NAME "test.echo"

static int test_started = 0;
static const stnlabz_module_host_t *test_host = NULL;

static stnlabz_module_result_t test_echo_service(const void *request,
                                                  size_t request_size,
                                                  void *response,
                                                  size_t response_size,
                                                  size_t *response_used,
                                                  void *handler_context)
{
    (void)handler_context;

    if (response_used == NULL) {
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    }

    *response_used = 0;

    if (request_size > 0 && request == NULL) {
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    }

    if (request_size > response_size || (request_size > 0 && response == NULL)) {
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    }

    if (request_size > 0) {
        memcpy(response, request, request_size);
    }
    *response_used = request_size;
    return STNLABZ_MODULE_OK;
}

static stnlabz_module_result_t test_qualify(stnlabz_module_qualification_result_t *result)
{
    if (result == NULL) {
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    }

    memset(result, 0, sizeof(*result));
    result->tests_executed = STNLABZ_MODULE_MIN_TESTS;
    result->tests_passed = STNLABZ_MODULE_MIN_TESTS;
    result->tests_failed = 0;
    result->negative_test_executed = 1;
    result->negative_test_passed = 1;

#ifdef TEST_MODULE_FORCE_QUALIFICATION_FAILURE
    result->tests_passed = STNLABZ_MODULE_MIN_TESTS - 1;
    result->tests_failed = 1;
#endif

    return STNLABZ_MODULE_OK;
}

static stnlabz_module_result_t test_start(const stnlabz_module_host_t *host)
{
    static const char probe[] = "ping";
    char response[sizeof(probe)] = {0};
    size_t response_used = 0;
    stnlabz_module_result_t result;

    if (test_started) {
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    }

    if (host == NULL ||
        host->register_service == NULL ||
        host->unregister_service == NULL ||
        host->invoke_service == NULL) {
        return STNLABZ_MODULE_ERR_START_FAILED;
    }

    if (host->register_service(TEST_SERVICE_NAME, test_echo_service, NULL) != 0) {
        return STNLABZ_MODULE_ERR_START_FAILED;
    }

    result = host->invoke_service(TEST_SERVICE_NAME,
                                  probe,
                                  sizeof(probe) - 1,
                                  response,
                                  sizeof(response),
                                  &response_used);
    if (result != STNLABZ_MODULE_OK ||
        response_used != sizeof(probe) - 1 ||
        memcmp(response, probe, sizeof(probe) - 1) != 0) {
        (void)host->unregister_service(TEST_SERVICE_NAME, NULL);
        return STNLABZ_MODULE_ERR_START_FAILED;
    }

    test_host = host;
    test_started = 1;
    return STNLABZ_MODULE_OK;
}

static stnlabz_module_result_t test_stop(void)
{
    if (!test_started || test_host == NULL) {
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    }

    if (test_host->unregister_service(TEST_SERVICE_NAME, NULL) != 0) {
        return STNLABZ_MODULE_ERR_STOP_FAILED;
    }

    test_host = NULL;
    test_started = 0;
    return STNLABZ_MODULE_OK;
}

static const stnlabz_module_descriptor_t TEST_DESCRIPTOR = {
    "test_module",
    "Squire ABI Test Module",
    TEST_MODULE_VERSION_MAJOR,
    TEST_MODULE_VERSION_MINOR,
    TEST_MODULE_VERSION_PATCH,
    STNLABZ_MODULE_API_MAJOR,
    STNLABZ_MODULE_API_MINOR,
    test_qualify,
    test_start,
    test_stop
};

const stnlabz_module_descriptor_t *stnlabz_module_get_descriptor(void)
{
    return &TEST_DESCRIPTOR;
}
