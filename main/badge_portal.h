#pragma once
#include "badge_model.h"

#define BADGE_PORTAL_BODY_MAX 768
#define BADGE_PORTAL_NETWORK_MAX 16

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool open;
} badge_portal_network_t;

/* Bounded strongest-first list. Duplicate SSIDs retain the strongest record;
 * hidden, malformed UTF-8 and control-character SSIDs are left for manual entry. */
size_t badge_portal_network_add(badge_portal_network_t *networks, size_t count,
                                size_t capacity, const uint8_t *ssid, size_t length,
                                int8_t rssi, bool open);

struct sockaddr;

/* Sensitive setup routes use a fixed origin, preventing DNS rebinding from
 * turning an unrelated website into a same-origin client of this HTTP server. */
bool badge_portal_host_valid(const char *host);

/* Match a socket's local IPv4 address, including an IPv4-mapped IPv6 result.
 * Reject truncated addresses and native IPv6. ipv4 is in network byte order. */
bool badge_portal_local_address_matches(const struct sockaddr *local, size_t length,
                                        uint32_t ipv4);

/* Parse an entire URL-encoded form; never truncate a field or modify current. */
bool badge_portal_parse_form(const char *body, size_t length,
                             const badge_config_t *current, const char *nonce,
                             badge_config_t *result, char *error, size_t error_size);

/* Bounded single-question DNS responder. IPv4 octets are in network order. */
size_t badge_portal_dns_reply(uint8_t *packet, size_t length, size_t capacity,
                              const uint8_t address[4]);
