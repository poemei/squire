/*
 * [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
 * Squire module ABI.
 */
#ifndef SQUIRE_MODULE_H
#define SQUIRE_MODULE_H

#include <stddef.h>

#define SQUIRE_MODULE_ABI_VERSION 1u
#define SQUIRE_MODULE_MIN_INTERNAL_TESTS 10u

typedef struct squire_module_manifest {
    unsigned int abi_version;
    const char *name;
    const char *version;
    const char *description;
} squire_module_manifest;

typedef struct squire_module_qualification {
    unsigned int tests_run;
    unsigned int tests_passed;
    unsigned int tests_failed;
    const char *detail;
} squire_module_qualification;

typedef struct squire_module_host {
    void (*log)(const char *level, const char *message);
    void (*audit)(const char *event,
                  const char *status,
                  const char *subject,
                  const char *version,
                  const char *detail);
} squire_module_host;

typedef const squire_module_manifest *(*squire_module_manifest_get_fn)(void);
typedef int (*squire_module_qualify_fn)(squire_module_qualification *result);
typedef int (*squire_module_init_fn)(const squire_module_host *host);
typedef void (*squire_module_shutdown_fn)(void);

#endif
