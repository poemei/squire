/*
 * [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
 * Squire Core module orchestration using the shared STN-LABZ ABI.
 */
#include "squire.h"
#include "abi.h"
#include "module_loader.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

static stnlabz_module_loader_t squire_loader;
static stnlabz_module_registry_t squire_registry;
static int squire_modules_initialized = 0;

static int squire_host_send_message(const char *message)
{
    (void)message;
    return -1;
}

static int squire_host_register_command(const char *name,
                                        stnlabz_module_command_handler_fn handler,
                                        void *handler_context)
{
    (void)name;
    (void)handler;
    (void)handler_context;
    return -1;
}

static int squire_host_unregister_command(const char *name, void *handler_context)
{
    (void)name;
    (void)handler_context;
    return -1;
}

static int squire_host_register_service(const char *name,
                                        stnlabz_module_service_handler_fn handler,
                                        void *handler_context)
{
    (void)name;
    (void)handler;
    (void)handler_context;
    return -1;
}

static int squire_host_unregister_service(const char *name, void *handler_context)
{
    (void)name;
    (void)handler_context;
    return -1;
}

static stnlabz_module_result_t squire_host_invoke_service(const char *name,
                                                           const void *request,
                                                           size_t request_size,
                                                           void *response,
                                                           size_t response_size,
                                                           size_t *response_used)
{
    (void)name;
    (void)request;
    (void)request_size;
    (void)response;
    (void)response_size;
    if (response_used != NULL) {
        *response_used = 0;
    }
    return STNLABZ_MODULE_ERR_NOT_FOUND;
}

static const stnlabz_module_host_t SQUIRE_MODULE_HOST = {
    squire_host_send_message,
    squire_host_register_command,
    squire_host_unregister_command,
    squire_host_register_service,
    squire_host_unregister_service,
    squire_host_invoke_service
};

static void squire_module_version(const stnlabz_module_descriptor_t *descriptor,
                                  char *buffer,
                                  size_t size)
{
    if (buffer == NULL || size == 0) {
        return;
    }

    if (descriptor == NULL) {
        snprintf(buffer, size, "%s", "unknown");
        return;
    }

    snprintf(buffer,
             size,
             "%u.%u.%u",
             descriptor->version_major,
             descriptor->version_minor,
             descriptor->version_patch);
}

static int squire_module_candidate(const char *filename, char *module_id, size_t module_id_size)
{
    size_t length;
    size_t id_length;

    if (filename == NULL || module_id == NULL || module_id_size == 0) {
        return 0;
    }

    length = strlen(filename);
    if (length <= 3 || strcmp(filename + length - 3, ".so") != 0) {
        return 0;
    }

    id_length = length - 3;
    if (id_length == 0 || id_length >= module_id_size || id_length >= STNLABZ_MODULE_ID_MAX) {
        return 0;
    }

    memcpy(module_id, filename, id_length);
    module_id[id_length] = '\0';
    return 1;
}

static void squire_module_cleanup_failed(const char *module_id)
{
    const stnlabz_module_record_t *record;

    if (module_id == NULL || module_id[0] == '\0') {
        return;
    }

    record = stnlabz_module_registry_find(&squire_registry, module_id);
    if (record != NULL) {
        if (record->state != STNLABZ_MODULE_STATE_FAILED &&
            record->state != STNLABZ_MODULE_STATE_QUARANTINED &&
            record->state != STNLABZ_MODULE_STATE_STOPPED) {
            (void)stnlabz_module_registry_fail(&squire_registry, module_id);
        }
        (void)stnlabz_module_abi_unregister(&squire_registry, module_id);
    }

    (void)stnlabz_module_loader_unload(&squire_loader, module_id);
}

static int squire_module_load_one(const char *module_id, const char *path)
{
    const stnlabz_module_descriptor_t *descriptor = NULL;
    const stnlabz_module_record_t *record;
    stnlabz_module_loader_result_t loader_result;
    stnlabz_module_result_t lifecycle_result;
    char version[64];
    char detail[512];

    squire_audit_event("MODULE_DISCOVERED", "SUCCESS", module_id, "unknown", path);
    squire_audit_event("MODULE_LOAD_BEGIN", "BEGIN", module_id, "unknown", path);

    loader_result = stnlabz_module_loader_load(&squire_loader, module_id, path, &descriptor);
    if (loader_result != STNLABZ_MODULE_LOADER_OK) {
        snprintf(detail,
                 sizeof(detail),
                 "ABI loader rejected module: %s",
                 stnlabz_module_loader_result_string(loader_result));
        squire_audit_event("MODULE_LOAD_FAIL", "FAILURE", module_id, "unknown", detail);
        return -1;
    }

    squire_module_version(descriptor, version, sizeof(version));

    if (descriptor->start == NULL || descriptor->stop == NULL) {
        squire_audit_event("MODULE_REJECTED", "FAILURE", descriptor->id, version,
                           "module start and stop callbacks are required by Squire Core");
        squire_module_cleanup_failed(descriptor->id);
        return -1;
    }

    squire_audit_event("MODULE_QUALIFICATION_BEGIN", "BEGIN", descriptor->id, version,
                       "STN-LABZ ABI verification and qualification starting");

    lifecycle_result = stnlabz_module_abi_prepare(&squire_registry, descriptor);
    if (lifecycle_result != STNLABZ_MODULE_OK) {
        snprintf(detail,
                 sizeof(detail),
                 "ABI qualification failed: %s",
                 stnlabz_module_result_string(lifecycle_result));
        squire_audit_event("MODULE_QUALIFICATION_FAIL", "FAILURE", descriptor->id, version, detail);
        squire_module_cleanup_failed(descriptor->id);
        return -1;
    }

    record = stnlabz_module_registry_find(&squire_registry, descriptor->id);
    if (record == NULL) {
        squire_audit_event("MODULE_QUALIFICATION_FAIL", "FAILURE", descriptor->id, version,
                           "qualified module missing from ABI registry");
        squire_module_cleanup_failed(descriptor->id);
        return -1;
    }

    snprintf(detail,
             sizeof(detail),
             "tests_executed=%u tests_passed=%u tests_failed=%u negative_test=%s",
             record->qualification.tests_executed,
             record->qualification.tests_passed,
             record->qualification.tests_failed,
             record->qualification.negative_test_passed ? "PASS" : "FAIL");
    squire_audit_event("MODULE_QUALIFICATION_PASS", "SUCCESS", descriptor->id, version, detail);

    lifecycle_result = stnlabz_module_abi_authorize_and_activate(&squire_registry,
                                                                  descriptor->id,
                                                                  &SQUIRE_MODULE_HOST);
    if (lifecycle_result != STNLABZ_MODULE_OK) {
        snprintf(detail,
                 sizeof(detail),
                 "ABI activation failed: %s",
                 stnlabz_module_result_string(lifecycle_result));
        squire_audit_event("MODULE_LOAD_FAIL", "FAILURE", descriptor->id, version, detail);
        squire_module_cleanup_failed(descriptor->id);
        return -1;
    }

    squire_audit_event("MODULE_LOAD_SUCCESS", "SUCCESS", descriptor->id, version,
                       "module qualified, authorized, and active");
    return 0;
}

int squire_modules_load_directory(const char *directory)
{
    DIR *dir;
    struct dirent *entry;
    unsigned int candidates = 0;
    unsigned int loaded = 0;
    char module_id[STNLABZ_MODULE_ID_MAX];
    char path[SQUIRE_PATH_MAX];

    if (directory == NULL || directory[0] == '\0') {
        return -1;
    }

    if (!squire_modules_initialized) {
        stnlabz_module_loader_init(&squire_loader);
        stnlabz_module_registry_init(&squire_registry);
        squire_modules_initialized = 1;
    }

    dir = opendir(directory);
    if (dir == NULL) {
        squire_audit_event("MODULE_DISCOVERY_FAIL", "FAILURE", "core", SQUIRE_VERSION,
                           "unable to open module directory");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (!squire_module_candidate(entry->d_name, module_id, sizeof(module_id))) {
            continue;
        }

        candidates++;
        if (snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name) >= (int)sizeof(path)) {
            squire_audit_event("MODULE_REJECTED", "FAILURE", module_id, "unknown",
                               "module path exceeds Core path limit");
            continue;
        }

        if (squire_module_load_one(module_id, path) == 0) {
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
    while (squire_loader.count > 0) {
        const stnlabz_loaded_module_t *loaded;
        const stnlabz_module_descriptor_t *descriptor;
        stnlabz_module_result_t lifecycle_result;
        stnlabz_module_loader_result_t loader_result;
        char module_id[STNLABZ_MODULE_ID_MAX];
        char version[64];
        char detail[256];

        loaded = &squire_loader.modules[squire_loader.count - 1];
        descriptor = loaded->descriptor;
        snprintf(module_id, sizeof(module_id), "%s", loaded->module_id);
        squire_module_version(descriptor, version, sizeof(version));

        squire_audit_event("MODULE_UNLOAD_BEGIN", "BEGIN", module_id, version, "Core shutdown");

        lifecycle_result = stnlabz_module_abi_stop(&squire_registry, module_id);
        if (lifecycle_result != STNLABZ_MODULE_OK) {
            snprintf(detail,
                     sizeof(detail),
                     "ABI stop failed: %s",
                     stnlabz_module_result_string(lifecycle_result));
            squire_audit_event("MODULE_UNLOAD_FAIL", "FAILURE", module_id, version, detail);
            break;
        }

        lifecycle_result = stnlabz_module_abi_unregister(&squire_registry, module_id);
        if (lifecycle_result != STNLABZ_MODULE_OK) {
            snprintf(detail,
                     sizeof(detail),
                     "ABI unregister failed: %s",
                     stnlabz_module_result_string(lifecycle_result));
            squire_audit_event("MODULE_UNLOAD_FAIL", "FAILURE", module_id, version, detail);
            break;
        }

        loader_result = stnlabz_module_loader_unload(&squire_loader, module_id);
        if (loader_result != STNLABZ_MODULE_LOADER_OK) {
            snprintf(detail,
                     sizeof(detail),
                     "ABI loader unload failed: %s",
                     stnlabz_module_loader_result_string(loader_result));
            squire_audit_event("MODULE_UNLOAD_FAIL", "FAILURE", module_id, version, detail);
            break;
        }

        squire_audit_event("MODULE_UNLOAD_SUCCESS", "SUCCESS", module_id, version,
                           "module stopped, unregistered, and unloaded");
    }
}
