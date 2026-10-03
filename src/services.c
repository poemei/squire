/*
 * [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
 * Squire Core ABI service router.
 *
 * Modules register named services only through the host ABI supplied by Core.
 * Core records the module that owns each service and removes all remaining
 * service registrations before that module can be unloaded or replaced.
 *
 * Registration and explicit unregistration are lifecycle operations: they are
 * accepted while Core is executing that module's start/stop boundary.
 */
#include "squire.h"
#include "abi.h"
#include "module.h"

#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define SQUIRE_SERVICE_MAX 32

typedef struct squire_service_record {
    int in_use;
    char name[STNLABZ_MODULE_SERVICE_NAME_MAX];
    char owner[STNLABZ_MODULE_ID_MAX];
    stnlabz_module_service_handler_fn handler;
    void *handler_context;
} squire_service_record;

static squire_service_record squire_services[SQUIRE_SERVICE_MAX];
static pthread_mutex_t squire_services_lock = PTHREAD_MUTEX_INITIALIZER;
static char squire_service_owner[STNLABZ_MODULE_ID_MAX];

/* Real ABI functions supplied by the linked shared ABI objects. */
stnlabz_module_result_t __real_stnlabz_module_abi_authorize_and_activate(
    stnlabz_module_registry_t *registry,
    const char *module_id,
    const stnlabz_module_host_t *host
);

stnlabz_module_result_t __real_stnlabz_module_abi_stop(
    stnlabz_module_registry_t *registry,
    const char *module_id
);

stnlabz_module_result_t __real_stnlabz_module_abi_prepare_replacement(
    stnlabz_module_registry_t *registry,
    const char *module_id
);

static int squire_service_text_valid(const char *text, size_t maximum)
{
    size_t length;

    if (text == NULL || maximum == 0) {
        return 0;
    }

    length = strlen(text);
    return length > 0 && length < maximum;
}

static void squire_service_set_owner(const char *module_id)
{
    pthread_mutex_lock(&squire_services_lock);
    memset(squire_service_owner, 0, sizeof(squire_service_owner));
    if (module_id != NULL) {
        snprintf(squire_service_owner, sizeof(squire_service_owner), "%s", module_id);
    }
    pthread_mutex_unlock(&squire_services_lock);
}

static void squire_service_clear_owner(void)
{
    pthread_mutex_lock(&squire_services_lock);
    memset(squire_service_owner, 0, sizeof(squire_service_owner));
    pthread_mutex_unlock(&squire_services_lock);
}

static unsigned int squire_service_remove_owner(const char *module_id)
{
    unsigned int removed = 0;
    size_t index;

    if (!squire_service_text_valid(module_id, STNLABZ_MODULE_ID_MAX)) {
        return 0;
    }

    pthread_mutex_lock(&squire_services_lock);
    for (index = 0; index < SQUIRE_SERVICE_MAX; index++) {
        if (squire_services[index].in_use &&
            strcmp(squire_services[index].owner, module_id) == 0) {
            memset(&squire_services[index], 0, sizeof(squire_services[index]));
            removed++;
        }
    }
    pthread_mutex_unlock(&squire_services_lock);

    if (removed > 0) {
        char detail[128];
        snprintf(detail, sizeof(detail), "removed=%u", removed);
        squire_audit_event("MODULE_SERVICES_CLEARED",
                           "SUCCESS",
                           module_id,
                           "unknown",
                           detail);
    }

    return removed;
}

static int squire_service_register(const char *name,
                                   stnlabz_module_service_handler_fn handler,
                                   void *handler_context)
{
    size_t index;
    size_t free_index = SQUIRE_SERVICE_MAX;
    char owner[STNLABZ_MODULE_ID_MAX];

    if (!squire_service_text_valid(name, STNLABZ_MODULE_SERVICE_NAME_MAX) ||
        handler == NULL) {
        return -1;
    }

    pthread_mutex_lock(&squire_services_lock);

    if (!squire_service_text_valid(squire_service_owner, sizeof(squire_service_owner))) {
        pthread_mutex_unlock(&squire_services_lock);
        squire_audit_event("MODULE_SERVICE_REJECTED",
                           "FAILURE",
                           "unknown",
                           "unknown",
                           "service registration attempted outside module start/stop lifecycle boundary");
        return -1;
    }

    snprintf(owner, sizeof(owner), "%s", squire_service_owner);

    for (index = 0; index < SQUIRE_SERVICE_MAX; index++) {
        if (squire_services[index].in_use) {
            if (strcmp(squire_services[index].name, name) == 0) {
                pthread_mutex_unlock(&squire_services_lock);
                squire_audit_event("MODULE_SERVICE_REJECTED",
                                   "FAILURE",
                                   owner,
                                   "unknown",
                                   "duplicate service name");
                return -1;
            }
        } else if (free_index == SQUIRE_SERVICE_MAX) {
            free_index = index;
        }
    }

    if (free_index == SQUIRE_SERVICE_MAX) {
        pthread_mutex_unlock(&squire_services_lock);
        squire_audit_event("MODULE_SERVICE_REJECTED",
                           "FAILURE",
                           owner,
                           "unknown",
                           "Core service registry full");
        return -1;
    }

    memset(&squire_services[free_index], 0, sizeof(squire_services[free_index]));
    squire_services[free_index].in_use = 1;
    snprintf(squire_services[free_index].name,
             sizeof(squire_services[free_index].name),
             "%s",
             name);
    snprintf(squire_services[free_index].owner,
             sizeof(squire_services[free_index].owner),
             "%s",
             owner);
    squire_services[free_index].handler = handler;
    squire_services[free_index].handler_context = handler_context;

    pthread_mutex_unlock(&squire_services_lock);

    squire_audit_event("MODULE_SERVICE_REGISTERED",
                       "SUCCESS",
                       owner,
                       "unknown",
                       name);
    return 0;
}

static int squire_service_unregister(const char *name, void *handler_context)
{
    size_t index;
    char owner[STNLABZ_MODULE_ID_MAX];

    if (!squire_service_text_valid(name, STNLABZ_MODULE_SERVICE_NAME_MAX)) {
        return -1;
    }

    pthread_mutex_lock(&squire_services_lock);

    if (!squire_service_text_valid(squire_service_owner, sizeof(squire_service_owner))) {
        pthread_mutex_unlock(&squire_services_lock);
        return -1;
    }

    snprintf(owner, sizeof(owner), "%s", squire_service_owner);

    for (index = 0; index < SQUIRE_SERVICE_MAX; index++) {
        if (squire_services[index].in_use &&
            strcmp(squire_services[index].name, name) == 0 &&
            strcmp(squire_services[index].owner, owner) == 0 &&
            squire_services[index].handler_context == handler_context) {
            memset(&squire_services[index], 0, sizeof(squire_services[index]));
            pthread_mutex_unlock(&squire_services_lock);
            squire_audit_event("MODULE_SERVICE_UNREGISTERED",
                               "SUCCESS",
                               owner,
                               "unknown",
                               name);
            return 0;
        }
    }

    pthread_mutex_unlock(&squire_services_lock);
    return -1;
}

static stnlabz_module_result_t squire_service_invoke(const char *name,
                                                      const void *request,
                                                      size_t request_size,
                                                      void *response,
                                                      size_t response_size,
                                                      size_t *response_used)
{
    size_t index;
    stnlabz_module_result_t result = STNLABZ_MODULE_ERR_NOT_FOUND;

    if (response_used != NULL) {
        *response_used = 0;
    }

    if (!squire_service_text_valid(name, STNLABZ_MODULE_SERVICE_NAME_MAX) ||
        response_used == NULL) {
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    }

    if (request_size > 0 && request == NULL) {
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    }

    if (response_size > 0 && response == NULL) {
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    }

    /*
     * The registry lock remains held across the service call so a concurrent
     * hot-update cannot unload the owning module while its handler is running.
     * Service handlers therefore must not recursively invoke Core services.
     */
    pthread_mutex_lock(&squire_services_lock);
    for (index = 0; index < SQUIRE_SERVICE_MAX; index++) {
        if (squire_services[index].in_use &&
            strcmp(squire_services[index].name, name) == 0) {
            result = squire_services[index].handler(request,
                                                     request_size,
                                                     response,
                                                     response_size,
                                                     response_used,
                                                     squire_services[index].handler_context);
            break;
        }
    }
    pthread_mutex_unlock(&squire_services_lock);

    return result;
}

static int squire_service_send_message(const char *message)
{
    (void)message;
    return -1;
}

static int squire_service_register_command(const char *name,
                                           stnlabz_module_command_handler_fn handler,
                                           void *handler_context)
{
    (void)name;
    (void)handler;
    (void)handler_context;
    return -1;
}

static int squire_service_unregister_command(const char *name, void *handler_context)
{
    (void)name;
    (void)handler_context;
    return -1;
}

static const stnlabz_module_host_t SQUIRE_SERVICE_HOST = {
    squire_service_send_message,
    squire_service_register_command,
    squire_service_unregister_command,
    squire_service_register,
    squire_service_unregister,
    squire_service_invoke
};

stnlabz_module_result_t __wrap_stnlabz_module_abi_authorize_and_activate(
    stnlabz_module_registry_t *registry,
    const char *module_id,
    const stnlabz_module_host_t *host)
{
    stnlabz_module_result_t result;

    (void)host;

    if (!squire_service_text_valid(module_id, STNLABZ_MODULE_ID_MAX)) {
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    }

    squire_service_set_owner(module_id);
    result = __real_stnlabz_module_abi_authorize_and_activate(registry,
                                                              module_id,
                                                              &SQUIRE_SERVICE_HOST);
    squire_service_clear_owner();

    if (result != STNLABZ_MODULE_OK) {
        (void)squire_service_remove_owner(module_id);
    }

    return result;
}

stnlabz_module_result_t __wrap_stnlabz_module_abi_stop(
    stnlabz_module_registry_t *registry,
    const char *module_id)
{
    stnlabz_module_result_t result;

    if (!squire_service_text_valid(module_id, STNLABZ_MODULE_ID_MAX)) {
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    }

    squire_service_set_owner(module_id);
    result = __real_stnlabz_module_abi_stop(registry, module_id);
    squire_service_clear_owner();

    if (result == STNLABZ_MODULE_OK) {
        /* Never leave a callable pointer into a module Core may now unload. */
        (void)squire_service_remove_owner(module_id);
    }

    return result;
}

stnlabz_module_result_t __wrap_stnlabz_module_abi_prepare_replacement(
    stnlabz_module_registry_t *registry,
    const char *module_id)
{
    stnlabz_module_result_t result;

    if (!squire_service_text_valid(module_id, STNLABZ_MODULE_ID_MAX)) {
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    }

    /*
     * The real replacement helper may call the real stop function internally.
     * Keep the owner boundary active for that complete operation.
     */
    squire_service_set_owner(module_id);
    result = __real_stnlabz_module_abi_prepare_replacement(registry, module_id);
    squire_service_clear_owner();

    if (result == STNLABZ_MODULE_OK) {
        (void)squire_service_remove_owner(module_id);
    }

    return result;
}
