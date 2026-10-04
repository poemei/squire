/*
 * [AI: GPT-5.6 Sol | 2026-10-03 | Human approval pending]
 * Squire Core-routed Rosaic query service contract.
 *
 * The service name is stable. The request and response are fixed-size ABI
 * records so transport modules and knowledge modules do not bind directly to
 * one another.
 */
#ifndef SQUIRE_ROSAIC_QUERY_H
#define SQUIRE_ROSAIC_QUERY_H

#define SQUIRE_ROSAIC_QUERY_SERVICE "rosaic.query"
#define SQUIRE_ROSAIC_QUERY_SENDER_MAX 128
#define SQUIRE_ROSAIC_QUERY_TARGET_MAX 256
#define SQUIRE_ROSAIC_QUERY_TEXT_MAX 512
#define SQUIRE_ROSAIC_RESPONSE_TEXT_MAX 512

typedef struct squire_rosaic_query_request {
    char sender[SQUIRE_ROSAIC_QUERY_SENDER_MAX];
    char target[SQUIRE_ROSAIC_QUERY_TARGET_MAX];
    char question[SQUIRE_ROSAIC_QUERY_TEXT_MAX];
} squire_rosaic_query_request;

typedef struct squire_rosaic_query_response {
    char text[SQUIRE_ROSAIC_RESPONSE_TEXT_MAX];
} squire_rosaic_query_response;

#endif
