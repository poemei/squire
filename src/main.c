/*
 * [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
 * Squire Core entry point.
 */
#include "squire.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t squire_running = 1;

static void squire_signal_handler(int signal_number)
{
    (void)signal_number;
    squire_running = 0;
}

int main(int argc, char **argv)
{
    squire_config config;
    const char *config_path = SQUIRE_DEFAULT_CONFIG;

    if (argc > 1) {
        if (strcmp(argv[1], "--version") == 0) {
            printf("%s %s\n", SQUIRE_NAME, SQUIRE_VERSION);
            return EXIT_SUCCESS;
        }

        if (strcmp(argv[1], "--config") == 0 && argc > 2) {
            config_path = argv[2];
        } else {
            fprintf(stderr, "Usage: %s [--version | --config <path>]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    squire_config_defaults(&config);

    if (squire_config_load(config_path, &config) != 0) {
        fprintf(stderr, "Squire Core: unable to load configuration: %s\n", config_path);
        return EXIT_FAILURE;
    }

    if (squire_log_open(config.log_path) != 0) {
        fprintf(stderr, "Squire Core: unable to open log: %s\n", config.log_path);
        return EXIT_FAILURE;
    }

    signal(SIGINT, squire_signal_handler);
    signal(SIGTERM, squire_signal_handler);

    squire_logf("INFO", "CORE_START name=%s version=%s pid=%ld", SQUIRE_NAME, SQUIRE_VERSION, (long)getpid());
    squire_logf("INFO", "CONFIG_LOADED path=%s", config_path);

    while (squire_running) {
        pause();
    }

    squire_log("INFO", "CORE_STOP");
    squire_log_close();

    return EXIT_SUCCESS;
}
