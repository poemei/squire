/*
 * [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
 * Squire Core generic module discovery, qualification, load, and unload.
 */
#include "squire.h"
#include "module.h"

#include <ctype.h>
#include <dirent.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SQUIRE_MAX_MODULES 64u

typedef struct loaded_module {
    void *handle;
    char path[SQUIRE_PATH_MAX];
    char name[128];
    char version[128];
    squire_module_shutdown_fn shutdown;
} loaded_module;

static loaded_module squire_modules[SQUIRE_MAX_MODULES];
static size_t squire_module_count = 0;

static int squire_module_name_valid(const char *name)
{
    size_t i;

    if (name == NULL || name[0] == '\0') {
        return 0;
    }

    if (!islower((unsigned char)name[0])) {
        return 0;
    }

    for (i = 0; name[i] != '\0'; i++) {
        unsigned char ch = (unsigned char)name[i];
        if (!(islower(ch) || isdigit(ch) || ch == '_' || ch == '-')) {
            return 0;
        }
    }

    return 1;
}

static int squire_module_file_candidate(const char *name)
{
    size_t length;

    if (name == NULL) {
        return 0;
    }

    length = strlen(name);
    return length > 3 && strcmp(name + length - 3, ".so") == 0;
}

static int squire_module_already_loaded(const char *name)
{
    size_t i;

    for (i = 0; i < squire_module_count; i++) {
        if (strcmp(squire_modules[i].name, name) == 0) {
            return 1;
        }
    }

    return 0;
}

static int squire_module_load_one(const char *path)
{
    struct stat st;
    void *handle;
    squire_module_manifest_get_fn manifest_get;
    squire_module_qualify_fn qualify;
    squire_module_init_fn init;
    squire_module_shutdown_fn shutdown;
    const squire_module_manifest *manifest;
    squire_module_qualification qualification;
    squire_module_host host;
    const char *error_text;
    char detail[512];

    if (path == NULL || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        squire_audit_event("MODULE_REJECTED", "FAILURE", path != NULL ? path : "unknown", "unknown",
                           "module path is not a regular file");
        return -1;
    }

    squire_audit_event("MODULE_DISCOVERED", "SUCCESS", path, "unknown", "candidate shared object discovered");

    dlerror();
    handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL) {
        error_text = dlerror();
        squire_audit_event("MODULE_LOAD_FAIL", "FAILURE", path, "unknown",
                           error_text != NULL ? error_text : "dlopen failed");
        return -1;
    }

    *(void **)(&manifest_get) = dlsym(handle, "squire_module_manifest_get");
    *(void **)(&qualify) = dlsym(handle, "squire_module_qualify");
    *(void **)(&init) = dlsym(handle, "squire_module_init");
    *(void **)(&shutdown) = dlsym(handle, "squire_module_shutdown");

    if (manifest_get == NULL || qualify == NULL || init == NULL || shutdown == NULL) {
        squire_audit_event("MODULE_REJECTED", "FAILURE", path, "unknown",
                           "required module ABI symbol missing");
        dlclose(handle);
        return -1;
    }

    manifest = manifest_get();
    if (manifest == NULL) {
        squire_audit_event("MODULE_REJECTED", "FAILURE", path, "unknown", "manifest is null");
        dlclose(handle);
        return -1;
    }

    if (manifest->abi_version != SQUIRE_MODULE_ABI_VERSION) {
        squire_audit_event("MODULE_REJECTED", "FAILURE",
                           manifest->name != NULL ? manifest->name : path,
                           manifest->version != NULL ? manifest->version : "unknown",
                           "module ABI version mismatch");
        dlclose(handle);
        return -1;
    }

    if (!squire_module_name_valid(manifest->name)) {
        squire_audit_event("MODULE_REJECTED", "FAILURE", path,
                           manifest->version != NULL ? manifest->version : "unknown",
                           "invalid module name");
        dlclose(handle);
        return -1;
    }

    if (manifest->version == NULL || manifest->version[0] == '\0') {
        squire_audit_event("MODULE_REJECTED", "FAILURE", manifest->name, "unknown",
                           "module version is required");
        dlclose(handle);
        return -1;
    }

    if (squire_module_already_loaded(manifest->name)) {
        squire_audit_event("MODULE_REJECTED", "FAILURE", manifest->name, manifest->version,
                           "module with this name is already loaded");
        dlclose(handle);
        return -1;
    }

    if (squire_module_count >= SQUIRE_MAX_MODULES) {
        squire_audit_event("MODULE_REJECTED", "FAILURE", manifest->name, manifest->version,
                           "Core module capacity reached");
        dlclose(handle);
        return -1;
    }

    memset(&qualification, 0, sizeof(qualification));
    squire_audit_event("MODULE_QUALIFICATION_BEGIN", "BEGIN", manifest->name, manifest->version,
                       "Core ABI checks passed; internal qualification starting");

    if (qualify(&qualification) != 0) {
        squire_audit_event("MODULE_QUALIFICATION_FAIL", "FAILURE", manifest->name, manifest->version,
                           qualification.detail != NULL ? qualification.detail : "qualification callback failed");
        dlclose(handle);
        return -1;
    }

    if (qualification.tests_run < SQUIRE_MODULE_MIN_INTERNAL_TESTS ||
        qualification.tests_failed != 0 ||
        qualification.tests_passed != qualification.tests_run) {
        snprintf(detail, sizeof(detail),
                 "tests_run=%u tests_passed=%u tests_failed=%u minimum=%u",
                 qualification.tests_run,
                 qualification.tests_passed,
                 qualification.tests_failed,
                 SQUIRE_MODULE_MIN_INTERNAL_TESTS);
        squire_audit_event("MODULE_QUALIFICATION_FAIL", "FAILURE", manifest->name, manifest->version, detail);
        dlclose(handle);
        return -1;
    }

    snprintf(detail, sizeof(detail),
             "tests_run=%u tests_passed=%u tests_failed=%u",
             qualification.tests_run,
             qualification.tests_passed,
             qualification.tests_failed);
    squire_audit_event("MODULE_QUALIFICATION_PASS", "SUCCESS", manifest->name, manifest->version, detail);

    host.log = squire_log;
    host.audit = squire_audit_event;

    squire_audit_event("MODULE_LOAD_BEGIN", "BEGIN", manifest->name, manifest->version, path);
    if (init(&host) != 0) {
        squire_audit_event("MODULE_LOAD_FAIL", "FAILURE", manifest->name, manifest->version,
                           "module init returned failure");
        dlclose(handle);
        return -1;
    }

    squire_modules[squire_module_count].handle = handle;
    squire_modules[squire_module_count].shutdown = shutdown;
    snprintf(squire_modules[squire_module_count].path,
             sizeof(squire_modules[squire_module_count].path), "%s", path);
    snprintf(squire_modules[squire_module_count].name,
             sizeof(squire_modules[squire_module_count].name), "%s", manifest->name);
    snprintf(squire_modules[squire_module_count].version,
             sizeof(squire_modules[squire_module_count].version), "%s", manifest->version);
    squire_module_count++;

    squire_audit_event("MODULE_LOAD_SUCCESS", "SUCCESS", manifest->name, manifest->version,
                       "module qualified and activated");
    return 0;
}

int squire_modules_load_directory(const char *directory)
{
    DIR *dir;
    struct dirent *entry;
    unsigned int candidates = 0;
    unsigned int loaded = 0;
    char path[SQUIRE_PATH_MAX];

    if (directory == NULL || directory[0] == '\0') {
        return -1;
    }

    dir = opendir(directory);
    if (dir == NULL) {
        squire_audit_event("MODULE_DISCOVERY_FAIL", "FAILURE", "core", SQUIRE_VERSION,
                           "unable to open module directory");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (!squire_module_file_candidate(entry->d_name)) {
            continue;
        }

        candidates++;
        if (snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name) >= (int)sizeof(path)) {
            squire_audit_event("MODULE_REJECTED", "FAILURE", entry->d_name, "unknown",
                               "module path exceeds Core path limit");
            continue;
        }

        if (squire_module_load_one(path) == 0) {
            loaded++;
        }
    }

    closedir(dir);
    squire_logf("INFO", "MODULE_DISCOVERY_COMPLETE directory=%s candidates=%u loaded=%u",
                directory, candidates, loaded);
    return 0;
}

void squire_modules_unload_all(void)
{
    while (squire_module_count > 0) {
        loaded_module *module;

        squire_module_count--;
        module = &squire_modules[squire_module_count];
        squire_audit_event("MODULE_UNLOAD_BEGIN", "BEGIN", module->name, module->version,
                           "Core shutdown");

        module->shutdown();
        if (dlclose(module->handle) != 0) {
            squire_audit_event("MODULE_UNLOAD_FAIL", "FAILURE", module->name, module->version,
                               "dlclose failed");
        } else {
            squire_audit_event("MODULE_UNLOAD_SUCCESS", "SUCCESS", module->name, module->version,
                               "module deactivated");
        }

        memset(module, 0, sizeof(*module));
    }
}
