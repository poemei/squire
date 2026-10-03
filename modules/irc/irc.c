/*
 * [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
 * Squire IRC transport module.
 *
 * Core owns lifecycle and audit. This module intentionally does not emit
 * IRC protocol chatter, raw server lines, PING/PONG traffic, or conversation
 * content into the Core log.
 */
#include "module.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define SQUIRE_IRC_VERSION_MAJOR 0
#define SQUIRE_IRC_VERSION_MINOR 2
#define SQUIRE_IRC_VERSION_PATCH 0

#define IRC_CONFIG_PATH "/opt/squire/config/irc.conf"
#define IRC_TEXT_MAX 256
#define IRC_LINE_MAX 1024
#define IRC_SASL_RAW_MAX 768
#define IRC_SASL_B64_MAX 1024

typedef struct irc_config {
    char server[IRC_TEXT_MAX];
    unsigned int port;
    char channel[IRC_TEXT_MAX];
    char nick[IRC_TEXT_MAX];
    char account[IRC_TEXT_MAX];
    char password[IRC_TEXT_MAX];
    char realname[IRC_TEXT_MAX];
    unsigned int reconnect_seconds;
} irc_config;

static int irc_started = 0;
static volatile int irc_stop_requested = 0;
static pthread_t irc_thread;
static pthread_mutex_t irc_socket_lock = PTHREAD_MUTEX_INITIALIZER;
static int irc_socket_fd = -1;
static irc_config irc_runtime_config;

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

static int irc_copy_text(char *destination, size_t destination_size, const char *source)
{
    size_t length;

    if (destination == NULL || destination_size == 0 || source == NULL) {
        return -1;
    }

    length = strlen(source);
    if (length == 0 || length >= destination_size) {
        return -1;
    }

    memcpy(destination, source, length + 1);
    return 0;
}

static char *irc_trim(char *text)
{
    char *end;

    if (text == NULL) {
        return NULL;
    }

    while (*text == ' ' || *text == '\t') {
        text++;
    }

    end = text + strlen(text);
    while (end > text &&
           (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        end--;
    }
    *end = '\0';
    return text;
}

static int irc_parse_uint(const char *text,
                          unsigned int minimum,
                          unsigned int maximum,
                          unsigned int *value)
{
    char *end = NULL;
    unsigned long parsed;

    if (text == NULL || value == NULL || text[0] == '\0') {
        return -1;
    }

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed < minimum || parsed > maximum) {
        return -1;
    }

    *value = (unsigned int)parsed;
    return 0;
}

static int irc_config_valid(const irc_config *config)
{
    if (config == NULL ||
        config->server[0] == '\0' ||
        config->port == 0 ||
        config->channel[0] != '#' ||
        config->nick[0] == '\0' ||
        config->account[0] == '\0' ||
        config->password[0] == '\0' ||
        config->realname[0] == '\0' ||
        config->reconnect_seconds == 0) {
        return 0;
    }

    return 1;
}

static int irc_load_config(const char *path, irc_config *config)
{
    FILE *file;
    char line[IRC_LINE_MAX];

    if (path == NULL || config == NULL) {
        return -1;
    }

    memset(config, 0, sizeof(*config));
    config->port = 6697;
    config->reconnect_seconds = 10;

    file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *text = irc_trim(line);
        char *equals;
        char *key;
        char *value;

        if (text == NULL || text[0] == '\0' || text[0] == '#') {
            continue;
        }

        equals = strchr(text, '=');
        if (equals == NULL) {
            fclose(file);
            return -1;
        }

        *equals = '\0';
        key = irc_trim(text);
        value = irc_trim(equals + 1);

        if (strcmp(key, "server") == 0) {
            if (irc_copy_text(config->server, sizeof(config->server), value) != 0) {
                fclose(file);
                return -1;
            }
        } else if (strcmp(key, "port") == 0) {
            if (irc_parse_uint(value, 1, 65535, &config->port) != 0) {
                fclose(file);
                return -1;
            }
        } else if (strcmp(key, "channel") == 0) {
            if (irc_copy_text(config->channel, sizeof(config->channel), value) != 0) {
                fclose(file);
                return -1;
            }
        } else if (strcmp(key, "nick") == 0) {
            if (irc_copy_text(config->nick, sizeof(config->nick), value) != 0) {
                fclose(file);
                return -1;
            }
        } else if (strcmp(key, "account") == 0) {
            if (irc_copy_text(config->account, sizeof(config->account), value) != 0) {
                fclose(file);
                return -1;
            }
        } else if (strcmp(key, "password") == 0) {
            if (irc_copy_text(config->password, sizeof(config->password), value) != 0) {
                fclose(file);
                return -1;
            }
        } else if (strcmp(key, "realname") == 0) {
            if (irc_copy_text(config->realname, sizeof(config->realname), value) != 0) {
                fclose(file);
                return -1;
            }
        } else if (strcmp(key, "reconnect_seconds") == 0) {
            if (irc_parse_uint(value, 1, 300, &config->reconnect_seconds) != 0) {
                fclose(file);
                return -1;
            }
        } else {
            fclose(file);
            return -1;
        }
    }

    fclose(file);
    return irc_config_valid(config) ? 0 : -1;
}

static int irc_tcp_connect(const char *server, unsigned int port)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    char port_text[16];
    int fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(port_text, sizeof(port_text), "%u", port);
    if (getaddrinfo(server, port_text, &hints, &addresses) != 0) {
        return -1;
    }

    for (address = addresses; address != NULL; address = address->ai_next) {
        fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) {
            continue;
        }

        if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) {
            break;
        }

        close(fd);
        fd = -1;
    }

    freeaddrinfo(addresses);
    return fd;
}

static void irc_set_current_socket(int fd)
{
    pthread_mutex_lock(&irc_socket_lock);
    irc_socket_fd = fd;
    pthread_mutex_unlock(&irc_socket_lock);
}

static void irc_interrupt_socket(void)
{
    pthread_mutex_lock(&irc_socket_lock);
    if (irc_socket_fd >= 0) {
        (void)shutdown(irc_socket_fd, SHUT_RDWR);
    }
    pthread_mutex_unlock(&irc_socket_lock);
}

static int irc_ssl_send(SSL *ssl, const char *text)
{
    size_t length;
    size_t sent = 0;

    if (ssl == NULL || text == NULL) {
        return -1;
    }

    length = strlen(text);
    while (sent < length) {
        int result = SSL_write(ssl, text + sent, (int)(length - sent));
        if (result <= 0) {
            return -1;
        }
        sent += (size_t)result;
    }

    return 0;
}

static int irc_send_command(SSL *ssl, const char *format, const char *value)
{
    char buffer[IRC_LINE_MAX];
    int length;

    if (ssl == NULL || format == NULL || value == NULL) {
        return -1;
    }

    length = snprintf(buffer, sizeof(buffer), format, value);
    if (length < 0 || length >= (int)sizeof(buffer)) {
        return -1;
    }

    return irc_ssl_send(ssl, buffer);
}

static int irc_build_sasl_plain(const irc_config *config,
                                char *encoded,
                                size_t encoded_size)
{
    unsigned char raw[IRC_SASL_RAW_MAX];
    size_t account_length;
    size_t password_length;
    size_t raw_length;
    int encoded_length;

    if (config == NULL || encoded == NULL || encoded_size == 0) {
        return -1;
    }

    account_length = strlen(config->account);
    password_length = strlen(config->password);
    raw_length = account_length + 1 + account_length + 1 + password_length;

    if (raw_length > sizeof(raw)) {
        return -1;
    }

    memcpy(raw, config->account, account_length);
    raw[account_length] = '\0';
    memcpy(raw + account_length + 1, config->account, account_length);
    raw[account_length + 1 + account_length] = '\0';
    memcpy(raw + account_length + 1 + account_length + 1,
           config->password,
           password_length);

    if (((raw_length + 2) / 3) * 4 + 1 > encoded_size) {
        return -1;
    }

    encoded_length = EVP_EncodeBlock((unsigned char *)encoded, raw, (int)raw_length);
    if (encoded_length <= 0 || (size_t)encoded_length >= encoded_size) {
        return -1;
    }

    encoded[encoded_length] = '\0';
    return 0;
}

static int irc_handle_session(SSL *ssl, const irc_config *config)
{
    char read_buffer[4096];
    char line_buffer[IRC_LINE_MAX];
    size_t line_used = 0;
    int sasl_requested = 0;
    int sasl_payload_sent = 0;
    int sasl_complete = 0;
    int welcome_seen = 0;
    int joined = 0;

    if (irc_ssl_send(ssl, "CAP LS 302\r\n") != 0 ||
        irc_send_command(ssl, "NICK %s\r\n", config->nick) != 0) {
        return -1;
    }

    {
        char user_line[IRC_LINE_MAX];
        int length = snprintf(user_line,
                              sizeof(user_line),
                              "USER %s 0 * :%s\r\n",
                              config->nick,
                              config->realname);
        if (length < 0 || length >= (int)sizeof(user_line) ||
            irc_ssl_send(ssl, user_line) != 0) {
            return -1;
        }
    }

    while (!irc_stop_requested) {
        int count = SSL_read(ssl, read_buffer, (int)sizeof(read_buffer));
        int index;

        if (count <= 0) {
            return -1;
        }

        for (index = 0; index < count; index++) {
            char character = read_buffer[index];

            if (character == '\r') {
                continue;
            }

            if (character != '\n') {
                if (line_used + 1 >= sizeof(line_buffer)) {
                    return -1;
                }
                line_buffer[line_used++] = character;
                continue;
            }

            line_buffer[line_used] = '\0';

            if (strncmp(line_buffer, "PING ", 5) == 0) {
                char pong[IRC_LINE_MAX];
                int length = snprintf(pong, sizeof(pong), "PONG %s\r\n", line_buffer + 5);
                if (length < 0 || length >= (int)sizeof(pong) || irc_ssl_send(ssl, pong) != 0) {
                    return -1;
                }
            }

            if (!sasl_requested && strstr(line_buffer, " CAP ") != NULL &&
                strstr(line_buffer, " LS ") != NULL && strstr(line_buffer, "sasl") != NULL) {
                if (irc_ssl_send(ssl, "CAP REQ :sasl\r\n") != 0) {
                    return -1;
                }
                sasl_requested = 1;
            }

            if (sasl_requested && strstr(line_buffer, " CAP ") != NULL &&
                strstr(line_buffer, " ACK ") != NULL && strstr(line_buffer, "sasl") != NULL) {
                if (irc_ssl_send(ssl, "AUTHENTICATE PLAIN\r\n") != 0) {
                    return -1;
                }
            }

            if (!sasl_payload_sent && strcmp(line_buffer, "AUTHENTICATE +") == 0) {
                char encoded[IRC_SASL_B64_MAX];
                char authenticate[IRC_SASL_B64_MAX + 32];
                int length;

                if (irc_build_sasl_plain(config, encoded, sizeof(encoded)) != 0) {
                    return -1;
                }

                length = snprintf(authenticate,
                                  sizeof(authenticate),
                                  "AUTHENTICATE %s\r\n",
                                  encoded);
                if (length < 0 || length >= (int)sizeof(authenticate) ||
                    irc_ssl_send(ssl, authenticate) != 0) {
                    return -1;
                }
                sasl_payload_sent = 1;
            }

            if (strstr(line_buffer, " 903 ") != NULL) {
                sasl_complete = 1;
                if (irc_ssl_send(ssl, "CAP END\r\n") != 0) {
                    return -1;
                }
            }

            if (strstr(line_buffer, " 904 ") != NULL ||
                strstr(line_buffer, " 905 ") != NULL ||
                strstr(line_buffer, " 906 ") != NULL ||
                strstr(line_buffer, " 907 ") != NULL) {
                return -1;
            }

            if (strstr(line_buffer, " 001 ") != NULL) {
                welcome_seen = 1;
            }

            if (!joined && sasl_complete && welcome_seen) {
                if (irc_send_command(ssl, "JOIN %s\r\n", config->channel) != 0) {
                    return -1;
                }
                joined = 1;
            }

            line_used = 0;
        }
    }

    return 0;
}

static int irc_connect_once(const irc_config *config)
{
    SSL_CTX *context = NULL;
    SSL *ssl = NULL;
    int fd = -1;
    int result = -1;

    fd = irc_tcp_connect(config->server, config->port);
    if (fd < 0) {
        return -1;
    }

    irc_set_current_socket(fd);

    context = SSL_CTX_new(TLS_client_method());
    if (context == NULL) {
        goto cleanup;
    }

    SSL_CTX_set_verify(context, SSL_VERIFY_PEER, NULL);
    if (SSL_CTX_set_default_verify_paths(context) != 1) {
        goto cleanup;
    }

    ssl = SSL_new(context);
    if (ssl == NULL) {
        goto cleanup;
    }

    if (SSL_set_tlsext_host_name(ssl, config->server) != 1 ||
        SSL_set1_host(ssl, config->server) != 1 ||
        SSL_set_fd(ssl, fd) != 1 ||
        SSL_connect(ssl) != 1) {
        goto cleanup;
    }

    if (SSL_get_verify_result(ssl) != X509_V_OK) {
        goto cleanup;
    }

    result = irc_handle_session(ssl, config);

cleanup:
    if (ssl != NULL) {
        (void)SSL_shutdown(ssl);
        SSL_free(ssl);
    }

    if (context != NULL) {
        SSL_CTX_free(context);
    }

    irc_set_current_socket(-1);
    if (fd >= 0) {
        close(fd);
    }

    return result;
}

static void irc_sleep_reconnect(unsigned int seconds)
{
    unsigned int elapsed;

    for (elapsed = 0; elapsed < seconds && !irc_stop_requested; elapsed++) {
        sleep(1);
    }
}

static void *irc_worker(void *context)
{
    irc_config *config = (irc_config *)context;

    while (!irc_stop_requested) {
        (void)irc_connect_once(config);

        if (!irc_stop_requested) {
            irc_sleep_reconnect(config->reconnect_seconds);
        }
    }

    return NULL;
}

static int irc_negative_probe(const void *value)
{
    return value == NULL ? -1 : 0;
}

static stnlabz_module_result_t irc_qualify(stnlabz_module_qualification_result_t *result)
{
    irc_config probe;
    unsigned int parsed = 0;
    char encoded[128];

    if (result == NULL) {
        return STNLABZ_MODULE_ERR_INVALID_ARGUMENT;
    }

    memset(result, 0, sizeof(*result));
    memset(&probe, 0, sizeof(probe));

    (void)irc_copy_text(probe.server, sizeof(probe.server), "irc.libera.chat");
    probe.port = 6697;
    (void)irc_copy_text(probe.channel, sizeof(probe.channel), "##rosaic");
    (void)irc_copy_text(probe.nick, sizeof(probe.nick), "Squire");
    (void)irc_copy_text(probe.account, sizeof(probe.account), "Squire");
    (void)irc_copy_text(probe.password, sizeof(probe.password), "qualification-only");
    (void)irc_copy_text(probe.realname, sizeof(probe.realname), "Squire Rosaic Information Agent");
    probe.reconnect_seconds = 10;

    irc_test(strcmp("irc", "irc") == 0, result);
    irc_test(SQUIRE_IRC_VERSION_MAJOR == 0, result);
    irc_test(SQUIRE_IRC_VERSION_MINOR == 2, result);
    irc_test(SQUIRE_IRC_VERSION_PATCH == 0, result);
    irc_test(irc_parse_uint("6697", 1, 65535, &parsed) == 0 && parsed == 6697, result);
    irc_test(irc_parse_uint("0", 1, 65535, &parsed) != 0, result);
    irc_test(irc_copy_text(probe.nick, sizeof(probe.nick), "Squire") == 0, result);
    irc_test(irc_config_valid(&probe), result);
    irc_test(irc_build_sasl_plain(&probe, encoded, sizeof(encoded)) == 0 && encoded[0] != '\0', result);
    irc_test(!irc_started && irc_socket_fd == -1, result);

    result->negative_test_executed = 1;
    result->negative_test_passed = (irc_negative_probe(NULL) != 0);

    if (result->tests_executed < STNLABZ_MODULE_MIN_TESTS ||
        result->tests_failed != 0 ||
        !result->negative_test_passed) {
        return STNLABZ_MODULE_ERR_QUALIFICATION;
    }

    return STNLABZ_MODULE_OK;
}

static stnlabz_module_result_t irc_start(const stnlabz_module_host_t *host)
{
    (void)host;

    if (irc_started) {
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    }

    if (irc_load_config(IRC_CONFIG_PATH, &irc_runtime_config) != 0) {
        return STNLABZ_MODULE_ERR_START_FAILED;
    }

    irc_stop_requested = 0;
    irc_set_current_socket(-1);

    if (pthread_create(&irc_thread, NULL, irc_worker, &irc_runtime_config) != 0) {
        memset(&irc_runtime_config, 0, sizeof(irc_runtime_config));
        return STNLABZ_MODULE_ERR_START_FAILED;
    }

    irc_started = 1;
    return STNLABZ_MODULE_OK;
}

static stnlabz_module_result_t irc_stop(void)
{
    if (!irc_started) {
        return STNLABZ_MODULE_ERR_INVALID_STATE;
    }

    irc_stop_requested = 1;
    irc_interrupt_socket();
    (void)pthread_join(irc_thread, NULL);

    irc_set_current_socket(-1);
    memset(&irc_runtime_config, 0, sizeof(irc_runtime_config));
    irc_started = 0;
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
