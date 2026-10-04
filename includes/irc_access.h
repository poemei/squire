/*
 * [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
 * Squire Core-routed IRC authorization service contract.
 */
#ifndef SQUIRE_IRC_ACCESS_H
#define SQUIRE_IRC_ACCESS_H

#define SQUIRE_IRC_ACCESS_SERVICE "irc.access.check"
#define SQUIRE_IRC_ACCESS_ACCOUNT_MAX 128
#define SQUIRE_IRC_ACCESS_CHANNEL_MAX 256
#define SQUIRE_IRC_ACCESS_PERMISSION_MAX 64

typedef struct squire_irc_access_request {
    char account[SQUIRE_IRC_ACCESS_ACCOUNT_MAX];
    char channel[SQUIRE_IRC_ACCESS_CHANNEL_MAX];
    char permission[SQUIRE_IRC_ACCESS_PERMISSION_MAX];
} squire_irc_access_request;

typedef struct squire_irc_access_response {
    int allowed;
} squire_irc_access_response;

#endif
