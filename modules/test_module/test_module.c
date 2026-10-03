/*
 * [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
 * Squire ABI lifecycle qualification test module.
 */
#include "module.h"

#include <string.h>

#ifndef TEST_MODULE_VERSION_MAJOR
#define TEST_MODULE_VERSION_MAJOR 0
#endif

#ifndef TEST_MODULE_VERSION_MINOR
#define TEST_MODULE_VERSION_MINOR 4
#endif

#ifndef TEST_MODULE_VERSION_PATCH
#define TEST_MODULE_VERSION_PATCH 0
#endif

static int test_started = 0;

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
    (void)host;

    if (test_started) {
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    }

    test_started = 1;
    return STNLABZ_MODULE_OK;
}

static stnlabz_module_result_t test_stop(void)
{
    if (!test_started) {
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    }

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
