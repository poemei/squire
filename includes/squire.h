/*
 * [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
 * Squire Core public interface.
 */
#ifndef SQUIRE_H
#define SQUIRE_H

#include <stddef.h>

#define SQUIRE_NAME "Squire"
#define SQUIRE_VERSION "0.1.0-dev"
#define SQUIRE_ROOT "/opt/squire"
#define SQUIRE_DEFAULT_CONFIG "/opt/squire/config/squire.conf"
#define SQUIRE_DEFAULT_LOG "/opt/squire/logs/squire.log"
#define SQUIRE_DEFAULT_MODULE_DIR "/opt/squire/modules"
#define SQUIRE_MODULE_STATE_DIR "/opt/squire/state/modules"

#define SQUIRE_PATH_MAX 4096
#define SQUIRE_VALUE_MAX 1024

typedef struct squire_config {
    char log_path[SQUIRE_PATH_MAX];
    char log_level[32];
    char module_dir[SQUIRE_PATH_MAX];
} squire_config;

int squire_config_load(const char *path, squire_config *config);
void squire_config_defaults(squire_config *config);

int squire_log_open(const char *path);
void squire_log_close(void);
void squire_log(const char *level, const char *message);
void squire_logf(const char *level, const char *format, ...);
void squire_audit_event(const char *event,
                        const char *status,
                        const char *subject,
                        const char *version,
                        const char *detail);

int squire_modules_load_directory(const char *directory);
int squire_modules_reconcile_directory(const char *directory);
void squire_modules_unload_all(void);

#endif
