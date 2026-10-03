/*
 * [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
 * Squire Core module orchestration using the shared STN-LABZ ABI.
 */
#include "squire.h"
#include "abi.h"
#include "module_loader.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define SQUIRE_TRACKED_MODULES STNLABZ_MODULE_LOADER_MAX

typedef struct squire_tracked_module {
    int in_use;
    char id[STNLABZ_MODULE_ID_MAX];
    char live_path[SQUIRE_PATH_MAX];
    char rollback_path[SQUIRE_PATH_MAX];
    char version[64];
    off_t active_size;
    time_t active_mtime;
    int rejected_valid;
    off_t rejected_size;
    time_t rejected_mtime;
} squire_tracked_module;

static stnlabz_module_loader_t squire_loader;
static stnlabz_module_registry_t squire_registry;
static squire_tracked_module squire_tracking[SQUIRE_TRACKED_MODULES];
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

static int squire_file_fingerprint(const char *path, off_t *size, time_t *mtime)
{
    struct stat st;

    if (path == NULL || size == NULL || mtime == NULL) {
        return -1;
    }

    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return -1;
    }

    *size = st.st_size;
    *mtime = st.st_mtime;
    return 0;
}

static int squire_copy_file(const char *source, const char *destination)
{
    FILE *input;
    FILE *output;
    unsigned char buffer[16384];
    size_t count;
    int result = 0;

    if (source == NULL || destination == NULL) {
        return -1;
    }

    input = fopen(source, "rb");
    if (input == NULL) {
        return -1;
    }

    output = fopen(destination, "wb");
    if (output == NULL) {
        fclose(input);
        return -1;
    }

    while ((count = fread(buffer, 1, sizeof(buffer), input)) > 0) {
        if (fwrite(buffer, 1, count, output) != count) {
            result = -1;
            break;
        }
    }

    if (ferror(input)) {
        result = -1;
    }

    if (fclose(output) != 0) {
        result = -1;
    }

    fclose(input);

    if (result != 0) {
        (void)remove(destination);
    }

    return result;
}

static int squire_ensure_state_directory(const char *module_id,
                                         char *rollback_path,
                                         size_t rollback_size,
                                         char *candidate_path,
                                         size_t candidate_size)
{
    char module_state_dir[SQUIRE_PATH_MAX];

    if (module_id == NULL || rollback_path == NULL || candidate_path == NULL) {
        return -1;
    }

    if (mkdir(SQUIRE_MODULE_STATE_DIR, 0750) != 0 && errno != EEXIST) {
        return -1;
    }

    if (snprintf(module_state_dir,
                 sizeof(module_state_dir),
                 "%s/%s",
                 SQUIRE_MODULE_STATE_DIR,
                 module_id) >= (int)sizeof(module_state_dir)) {
        return -1;
    }

    if (mkdir(module_state_dir, 0750) != 0 && errno != EEXIST) {
        return -1;
    }

    if (snprintf(rollback_path,
                 rollback_size,
                 "%s/%s.so",
                 module_state_dir,
                 module_id) >= (int)rollback_size) {
        return -1;
    }

    if (snprintf(candidate_path,
                 candidate_size,
                 "%s/%s.candidate.so",
                 module_state_dir,
                 module_id) >= (int)candidate_size) {
        return -1;
    }

    return 0;
}

static squire_tracked_module *squire_tracking_find(const char *module_id)
{
    size_t index;

    if (module_id == NULL) {
        return NULL;
    }

    for (index = 0; index < SQUIRE_TRACKED_MODULES; index++) {
        if (squire_tracking[index].in_use &&
            strcmp(squire_tracking[index].id, module_id) == 0) {
            return &squire_tracking[index];
        }
    }

    return NULL;
}

static squire_tracked_module *squire_tracking_allocate(void)
{
    size_t index;

    for (index = 0; index < SQUIRE_TRACKED_MODULES; index++) {
        if (!squire_tracking[index].in_use) {
            memset(&squire_tracking[index], 0, sizeof(squire_tracking[index]));
            squire_tracking[index].in_use = 1;
            return &squire_tracking[index];
        }
    }

    return NULL;
}

static int squire_tracking_commit(const char *module_id,
                                  const char *live_path,
                                  const stnlabz_module_descriptor_t *descriptor)
{
    squire_tracked_module *tracked;
    char rollback_path[SQUIRE_PATH_MAX];
    char candidate_path[SQUIRE_PATH_MAX];
    off_t size;
    time_t mtime;

    if (squire_file_fingerprint(live_path, &size, &mtime) != 0) {
        return -1;
    }

    if (squire_ensure_state_directory(module_id,
                                      rollback_path,
                                      sizeof(rollback_path),
                                      candidate_path,
                                      sizeof(candidate_path)) != 0) {
        return -1;
    }

    (void)remove(candidate_path);

    if (squire_copy_file(live_path, rollback_path) != 0) {
        return -1;
    }

    tracked = squire_tracking_find(module_id);
    if (tracked == NULL) {
        tracked = squire_tracking_allocate();
    }

    if (tracked == NULL) {
        return -1;
    }

    snprintf(tracked->id, sizeof(tracked->id), "%s", module_id);
    snprintf(tracked->live_path, sizeof(tracked->live_path), "%s", live_path);
    snprintf(tracked->rollback_path, sizeof(tracked->rollback_path), "%s", rollback_path);
    squire_module_version(descriptor, tracked->version, sizeof(tracked->version));
    tracked->active_size = size;
    tracked->active_mtime = mtime;
    tracked->rejected_valid = 0;
    return 0;
}

static void squire_module_cleanup_failed(const char *module_id)
{
    const stnlabz_module_record_t *record;

    if (module_id == NULL || module_id[0] == '\0') {
        return;
    }

    record = stnlabz_module_registry_find(&squire_registry, module_id);
    if (record != NULL) {
        if (record->state == STNLABZ_MODULE_STATE_ACTIVE) {
            (void)stnlabz_module_abi_stop(&squire_registry, module_id);
        } else if (record->state != STNLABZ_MODULE_STATE_FAILED &&
                   record->state != STNLABZ_MODULE_STATE_QUARANTINED &&
                   record->state != STNLABZ_MODULE_STATE_STOPPED) {
            (void)stnlabz_module_registry_fail(&squire_registry, module_id);
        }

        (void)stnlabz_module_abi_unregister(&squire_registry, module_id);
    }

    (void)stnlabz_module_loader_unload(&squire_loader, module_id);
}

static int squire_module_load_one(const char *module_id,
                                  const char *path,
                                  int establish_tracking)
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

    if (establish_tracking && squire_tracking_commit(descriptor->id, path, descriptor) != 0) {
        squire_audit_event("MODULE_LOAD_FAIL", "FAILURE", descriptor->id, version,
                           "unable to establish known-good rollback snapshot");
        squire_module_cleanup_failed(descriptor->id);
        return -1;
    }

    squire_audit_event("MODULE_LOAD_SUCCESS", "SUCCESS", descriptor->id, version,
                       "module qualified, authorized, and active");
    return 0;
}

static int squire_module_path_from_directory(const char *root,
                                             const char *entry_name,
                                             char *module_id,
                                             size_t module_id_size,
                                             char *path,
                                             size_t path_size)
{
    char directory_path[SQUIRE_PATH_MAX];
    struct stat st;
    size_t id_length;

    if (root == NULL || entry_name == NULL || module_id == NULL || path == NULL) {
        return 0;
    }

    if (strcmp(entry_name, ".") == 0 || strcmp(entry_name, "..") == 0) {
        return 0;
    }

    if (snprintf(directory_path, sizeof(directory_path), "%s/%s", root, entry_name) >=
        (int)sizeof(directory_path)) {
        return 0;
    }

    if (stat(directory_path, &st) != 0) {
        return 0;
    }

    if (S_ISREG(st.st_mode)) {
        id_length = strlen(entry_name);
        if (id_length > 3 && strcmp(entry_name + id_length - 3, ".so") == 0) {
            squire_audit_event("MODULE_REJECTED", "FAILURE", entry_name, "unknown",
                               "flat module files are forbidden; expected modules/<module>/<module>.so");
        }
        return 0;
    }

    if (!S_ISDIR(st.st_mode)) {
        return 0;
    }

    id_length = strlen(entry_name);
    if (id_length == 0 || id_length >= module_id_size || id_length >= STNLABZ_MODULE_ID_MAX) {
        squire_audit_event("MODULE_REJECTED", "FAILURE", entry_name, "unknown",
                           "module directory name exceeds ABI module id limit");
        return 0;
    }

    memcpy(module_id, entry_name, id_length + 1);

    if (snprintf(path, path_size, "%s/%s.so", directory_path, entry_name) >= (int)path_size) {
        squire_audit_event("MODULE_REJECTED", "FAILURE", module_id, "unknown",
                           "module path exceeds Core path limit");
        return 0;
    }

    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        squire_audit_event("MODULE_REJECTED", "FAILURE", module_id, "unknown",
                           "module directory does not contain matching <module>.so");
        return 0;
    }

    return 1;
}

static int squire_module_prequalify_candidate(const char *module_id,
                                               const char *candidate_path,
                                               char *candidate_version,
                                               size_t candidate_version_size)
{
    stnlabz_module_loader_t test_loader;
    stnlabz_module_registry_t test_registry;
    const stnlabz_module_descriptor_t *descriptor = NULL;
    const stnlabz_module_record_t *record;
    stnlabz_module_loader_result_t loader_result;
    stnlabz_module_result_t lifecycle_result;
    char detail[512];

    if (module_id == NULL || candidate_path == NULL ||
        candidate_version == NULL || candidate_version_size == 0) {
        return -1;
    }

    stnlabz_module_loader_init(&test_loader);
    stnlabz_module_registry_init(&test_registry);

    loader_result = stnlabz_module_loader_load(&test_loader,
                                                module_id,
                                                candidate_path,
                                                &descriptor);
    if (loader_result != STNLABZ_MODULE_LOADER_OK || descriptor == NULL) {
        snprintf(detail,
                 sizeof(detail),
                 "candidate ABI load failed: %s",
                 stnlabz_module_loader_result_string(loader_result));
        squire_audit_event("MODULE_QUALIFICATION_FAIL", "FAILURE", module_id, "unknown", detail);
        stnlabz_module_loader_unload_all(&test_loader);
        return -1;
    }

    squire_module_version(descriptor, candidate_version, candidate_version_size);

    if (descriptor->start == NULL || descriptor->stop == NULL) {
        squire_audit_event("MODULE_QUALIFICATION_FAIL", "FAILURE", module_id, candidate_version,
                           "candidate missing required start or stop callback");
        stnlabz_module_loader_unload_all(&test_loader);
        return -1;
    }

    squire_audit_event("MODULE_QUALIFICATION_BEGIN", "BEGIN", module_id, candidate_version,
                       "Core initiated candidate qualification while current revision remains active");

    lifecycle_result = stnlabz_module_abi_prepare(&test_registry, descriptor);
    if (lifecycle_result != STNLABZ_MODULE_OK) {
        snprintf(detail,
                 sizeof(detail),
                 "candidate qualification failed: %s; current revision remains active",
                 stnlabz_module_result_string(lifecycle_result));
        squire_audit_event("MODULE_QUALIFICATION_FAIL", "FAILURE", module_id, candidate_version, detail);
        (void)stnlabz_module_abi_unregister(&test_registry, module_id);
        stnlabz_module_loader_unload_all(&test_loader);
        return -1;
    }

    record = stnlabz_module_registry_find(&test_registry, module_id);
    if (record == NULL) {
        squire_audit_event("MODULE_QUALIFICATION_FAIL", "FAILURE", module_id, candidate_version,
                           "candidate qualification record missing; current revision remains active");
        stnlabz_module_loader_unload_all(&test_loader);
        return -1;
    }

    snprintf(detail,
             sizeof(detail),
             "tests_executed=%u tests_passed=%u tests_failed=%u negative_test=%s; current revision still active",
             record->qualification.tests_executed,
             record->qualification.tests_passed,
             record->qualification.tests_failed,
             record->qualification.negative_test_passed ? "PASS" : "FAIL");
    squire_audit_event("MODULE_QUALIFICATION_PASS", "SUCCESS", module_id, candidate_version, detail);

    (void)stnlabz_module_abi_unregister(&test_registry, module_id);
    stnlabz_module_loader_unload_all(&test_loader);
    return 0;
}

static int squire_module_rollback(squire_tracked_module *tracked,
                                  off_t rejected_size,
                                  time_t rejected_mtime)
{
    const stnlabz_loaded_module_t *loaded;
    char detail[256];

    if (tracked == NULL) {
        return -1;
    }

    squire_audit_event("MODULE_ROLLBACK_BEGIN", "BEGIN", tracked->id, tracked->version,
                       tracked->rollback_path);

    if (squire_module_load_one(tracked->id, tracked->rollback_path, 0) != 0) {
        squire_audit_event("MODULE_ROLLBACK_FAIL", "FAILURE", tracked->id, tracked->version,
                           "known-good binary could not be reactivated");
        tracked->in_use = 0;
        return -1;
    }

    loaded = stnlabz_module_loader_find(&squire_loader, tracked->id);
    if (loaded != NULL && loaded->descriptor != NULL) {
        squire_module_version(loaded->descriptor, tracked->version, sizeof(tracked->version));
    }

    tracked->rejected_valid = 1;
    tracked->rejected_size = rejected_size;
    tracked->rejected_mtime = rejected_mtime;

    snprintf(detail,
             sizeof(detail),
             "known-good version=%s restored; rejected candidate suppressed until file changes",
             tracked->version);
    squire_audit_event("MODULE_ROLLBACK_SUCCESS", "SUCCESS", tracked->id, tracked->version, detail);
    return 0;
}

static int squire_module_update(squire_tracked_module *tracked)
{
    const stnlabz_loaded_module_t *loaded;
    stnlabz_module_result_t lifecycle_result;
    stnlabz_module_loader_result_t loader_result;
    off_t candidate_size;
    time_t candidate_mtime;
    char candidate_path[SQUIRE_PATH_MAX];
    char rollback_path[SQUIRE_PATH_MAX];
    char old_version[64];
    char candidate_version[64];
    char new_version[64];
    char detail[512];

    if (tracked == NULL) {
        return -1;
    }

    if (squire_file_fingerprint(tracked->live_path, &candidate_size, &candidate_mtime) != 0) {
        return -1;
    }

    if (candidate_size == tracked->active_size && candidate_mtime == tracked->active_mtime) {
        return 0;
    }

    if (tracked->rejected_valid &&
        candidate_size == tracked->rejected_size &&
        candidate_mtime == tracked->rejected_mtime) {
        return 0;
    }

    snprintf(old_version, sizeof(old_version), "%s", tracked->version);
    snprintf(detail,
             sizeof(detail),
             "old_version=%s candidate=%s",
             old_version,
             tracked->live_path);
    squire_audit_event("MODULE_UPDATE_BEGIN", "BEGIN", tracked->id, old_version, detail);

    if (squire_ensure_state_directory(tracked->id,
                                      rollback_path,
                                      sizeof(rollback_path),
                                      candidate_path,
                                      sizeof(candidate_path)) != 0 ||
        squire_copy_file(tracked->live_path, candidate_path) != 0) {
        squire_audit_event("MODULE_UPDATE_FAIL", "FAILURE", tracked->id, old_version,
                           "unable to stage candidate binary in Core state");
        return -1;
    }

    if (squire_module_prequalify_candidate(tracked->id,
                                            candidate_path,
                                            candidate_version,
                                            sizeof(candidate_version)) != 0) {
        tracked->rejected_valid = 1;
        tracked->rejected_size = candidate_size;
        tracked->rejected_mtime = candidate_mtime;
        snprintf(detail,
                 sizeof(detail),
                 "candidate rejected; active version=%s retained",
                 old_version);
        squire_audit_event("MODULE_UPDATE_FAIL", "FAILURE", tracked->id, old_version, detail);
        (void)remove(candidate_path);
        return 0;
    }

    snprintf(detail,
             sizeof(detail),
             "old_version=%s candidate_version=%s qualification=PASS",
             old_version,
             candidate_version);
    squire_audit_event("MODULE_HOTLOAD_BEGIN", "BEGIN", tracked->id, candidate_version, detail);

    lifecycle_result = stnlabz_module_abi_prepare_replacement(&squire_registry, tracked->id);
    if (lifecycle_result != STNLABZ_MODULE_OK) {
        snprintf(detail,
                 sizeof(detail),
                 "ABI replacement preparation failed: %s",
                 stnlabz_module_result_string(lifecycle_result));
        squire_audit_event("MODULE_UPDATE_FAIL", "FAILURE", tracked->id, old_version, detail);
        (void)remove(candidate_path);
        return -1;
    }

    loader_result = stnlabz_module_loader_unload(&squire_loader, tracked->id);
    if (loader_result != STNLABZ_MODULE_LOADER_OK) {
        snprintf(detail,
                 sizeof(detail),
                 "old module unload failed: %s",
                 stnlabz_module_loader_result_string(loader_result));
        squire_audit_event("MODULE_UPDATE_FAIL", "FAILURE", tracked->id, old_version, detail);
        (void)remove(candidate_path);
        return squire_module_rollback(tracked, candidate_size, candidate_mtime);
    }

    if (squire_module_load_one(tracked->id, candidate_path, 0) != 0) {
        squire_audit_event("MODULE_UPDATE_FAIL", "FAILURE", tracked->id, old_version,
                           "qualified candidate failed activation; rolling back known-good revision");
        (void)remove(candidate_path);
        return squire_module_rollback(tracked, candidate_size, candidate_mtime);
    }

    loaded = stnlabz_module_loader_find(&squire_loader, tracked->id);
    if (loaded == NULL || loaded->descriptor == NULL) {
        squire_audit_event("MODULE_UPDATE_FAIL", "FAILURE", tracked->id, old_version,
                           "candidate active but descriptor unavailable");
        squire_module_cleanup_failed(tracked->id);
        (void)remove(candidate_path);
        return squire_module_rollback(tracked, candidate_size, candidate_mtime);
    }

    squire_module_version(loaded->descriptor, new_version, sizeof(new_version));

    if (rename(candidate_path, rollback_path) != 0) {
        squire_audit_event("MODULE_UPDATE_FAIL", "FAILURE", tracked->id, old_version,
                           "candidate activated but known-good snapshot promotion failed");
        squire_module_cleanup_failed(tracked->id);
        (void)remove(candidate_path);
        return squire_module_rollback(tracked, candidate_size, candidate_mtime);
    }

    snprintf(tracked->rollback_path, sizeof(tracked->rollback_path), "%s", rollback_path);
    snprintf(tracked->version, sizeof(tracked->version), "%s", new_version);
    tracked->active_size = candidate_size;
    tracked->active_mtime = candidate_mtime;
    tracked->rejected_valid = 0;

    snprintf(detail,
             sizeof(detail),
             "old_version=%s new_version=%s",
             old_version,
             new_version);
    squire_audit_event("MODULE_HOTLOAD_SUCCESS", "SUCCESS", tracked->id, new_version, detail);
    squire_audit_event("MODULE_UPDATE_SUCCESS", "SUCCESS", tracked->id, new_version, detail);
    return 0;
}

static void squire_modules_initialize(void)
{
    if (!squire_modules_initialized) {
        stnlabz_module_loader_init(&squire_loader);
        stnlabz_module_registry_init(&squire_registry);
        memset(squire_tracking, 0, sizeof(squire_tracking));
        squire_modules_initialized = 1;
    }
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

    squire_modules_initialize();

    dir = opendir(directory);
    if (dir == NULL) {
        squire_audit_event("MODULE_DISCOVERY_FAIL", "FAILURE", "core", SQUIRE_VERSION,
                           "unable to open module directory");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (!squire_module_path_from_directory(directory,
                                               entry->d_name,
                                               module_id,
                                               sizeof(module_id),
                                               path,
                                               sizeof(path))) {
            continue;
        }

        candidates++;
        if (squire_module_load_one(module_id, path, 1) == 0) {
            loaded++;
        }
    }

    closedir(dir);
    squire_logf("INFO", "MODULE_DISCOVERY_COMPLETE directory=%s candidates=%u loaded=%u",
                directory, candidates, loaded);
    return 0;
}

int squire_modules_reconcile_directory(const char *directory)
{
    DIR *dir;
    struct dirent *entry;
    unsigned int candidates = 0;
    unsigned int hotloaded = 0;
    unsigned int updated = 0;
    unsigned int failures = 0;
    char module_id[STNLABZ_MODULE_ID_MAX];
    char path[SQUIRE_PATH_MAX];

    if (directory == NULL || directory[0] == '\0') {
        return -1;
    }

    squire_modules_initialize();

    dir = opendir(directory);
    if (dir == NULL) {
        squire_audit_event("MODULE_DISCOVERY_FAIL", "FAILURE", "core", SQUIRE_VERSION,
                           "unable to open module directory during reconcile");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        squire_tracked_module *tracked;
        off_t before_size;
        time_t before_mtime;

        if (!squire_module_path_from_directory(directory,
                                               entry->d_name,
                                               module_id,
                                               sizeof(module_id),
                                               path,
                                               sizeof(path))) {
            continue;
        }

        candidates++;
        tracked = squire_tracking_find(module_id);

        if (tracked == NULL) {
            squire_audit_event("MODULE_HOTLOAD_BEGIN", "BEGIN", module_id, "unknown", path);
            if (squire_module_load_one(module_id, path, 1) == 0) {
                hotloaded++;
                tracked = squire_tracking_find(module_id);
                squire_audit_event("MODULE_HOTLOAD_SUCCESS", "SUCCESS", module_id,
                                   tracked != NULL ? tracked->version : "unknown",
                                   "new module qualified and activated without Core restart");
            } else {
                squire_audit_event("MODULE_HOTLOAD_FAIL", "FAILURE", module_id, "unknown",
                                   "new module failed qualification or activation; Core continues");
            }
            continue;
        }

        before_size = tracked->active_size;
        before_mtime = tracked->active_mtime;
        snprintf(tracked->live_path, sizeof(tracked->live_path), "%s", path);

        if (squire_module_update(tracked) != 0) {
            failures++;
        } else if (tracked->active_size != before_size || tracked->active_mtime != before_mtime) {
            updated++;
        }
    }

    closedir(dir);

    if (hotloaded > 0 || updated > 0 || failures > 0) {
        squire_logf("INFO",
                    "MODULE_RECONCILE_COMPLETE directory=%s candidates=%u hotloaded=%u updated=%u failures=%u",
                    directory,
                    candidates,
                    hotloaded,
                    updated,
                    failures);
    }

    return failures == 0 ? 0 : -1;
}

void squire_modules_unload_all(void)
{
    while (squire_loader.count > 0) {
        const stnlabz_loaded_module_t *loaded;
        const stnlabz_module_descriptor_t *descriptor;
        squire_tracked_module *tracked;
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

        tracked = squire_tracking_find(module_id);
        if (tracked != NULL) {
            tracked->in_use = 0;
        }

        squire_audit_event("MODULE_UNLOAD_SUCCESS", "SUCCESS", module_id, version,
                           "module stopped, unregistered, and unloaded");
    }
}
