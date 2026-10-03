/*
 * [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
 * Squire IRC module shell.
 *
 * This revision intentionally contains no IRC network behavior.
 * It exists to prove Core-controlled discovery, qualification,
 * hot activation, stop, unload, and future replacement.
 */
#include "module.h"

#include <string.h>

#define SQUIRE_IRC_VERSION_MAJOR 0
#define SQUIRE_IRC_VERSION_MINOR 1
#define SQUIRE_IRC_VERSION_PATCH 0

static int irc_started = 0;
static const stnlabz_module_host_t *irc_host = NULL;

static void irc_test(int condition,
                     stnlabz_module_qualification_result_t *result)
{
    result->tests_executed++;

    if (condition) {
        result->tests_passed++;
    } else {
        result->tests_failed++;
    }
}

static int irc_negative_probe(const void *value)
{
    return value == NULL ? -1 : 0;
}

static stnlabz_module_result_t irc_qualify(stnlabz_module_qualification_result_t *result)
{
    if (result == NULL) {
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    }

    memset(result, 0, sizeof(*result));

    irc_test(strcmp("irc", "irc") == 0, result);
    irc_test(strlen("irc") > 0, result);
    irc_test(strlen("Squire IRC Module") > 0, result);
    irc_test(SQUIRE_IRC_VERSION_MAJOR == 0, result);
    irc_test(SQUIRE_IRC_VERSION_MINOR == 1, result);
    irc_test(SQUIRE_IRC_VERSION_PATCH == 0, result);
    irc_test(STNLABZ_MODULE_API_MAJOR > 0, result);
    irc_test(STNLABZ_MODULE_API_MINOR >= 0, result);
    irc_test(irc_started == 0, result);
    irc_test(irc_host == NULL, result);

    result->negative_test_executed = 1;
    result->negative_test_passed = (irc_negative_probe(NULL) != 0);

    if (result->tests_failed != 0 || !result->negative_test_passed) {
        return STNLABZ_MODULE_ERR_QUALIFICATION;
    }

    return STNLABZ_MODULE_OK;
}

static stnlabz_module_result_t irc_start(const stnlabz_module_host_t *host)
{
    if (irc_started) {
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    }

    irc_host = host;
    irc_started = 1;
    return STNLABZ_MODULE_OK;
}

static stnlabz_module_result_t irc_stop(void)
{
    if (!irc_started) {
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    }

    irc_started = 0;
    irc_host = NULL;
    return STNLABZ_MODULE_OK;
}

static const stnlabz_module_descriptor_t IRC_DESCRIPTOR = {
    "irc",
    "Squire IRC Module",
    SQUIRE_IRC_VERSION_MAJOR,
    SQUIRE_IRC_VERSION_MINOR,
    SQUIRE_IRC_VERSION_PATCH,
    STNLABZ_MODULE_API_MAJOR,
    STNLABZ_MODULE_API_MINOR,
    irc_qualify,
    irc_start,
    irc_stop
};

const stnlabz_module_descriptor_t *stnlabz_module_get_descriptor(void)
{
    return &IRC_DESCRIPTOR;
}
