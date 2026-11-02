#include "badge_portal.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

bool badge_portal_host_valid(const char *host)
{
    return host && (!strcmp(host, "192.168.4.1") || !strcmp(host, "192.168.4.1:80"));
}

bool badge_portal_local_address_matches(const struct sockaddr *local, size_t length,
                                        uint32_t ipv4)
{
    sa_family_t family;
    const size_t family_offset = offsetof(struct sockaddr, sa_family);
    if (!local || length < family_offset + sizeof(family)) return false;
    memcpy(&family, (const unsigned char *) local + family_offset, sizeof(family));
    if (family == AF_INET) {
        struct sockaddr_in address;
        if (length < sizeof(address)) return false;
        memcpy(&address, local, sizeof(address));
        return address.sin_addr.s_addr == ipv4;
    }
#if defined(AF_INET6) && (!defined(LWIP_IPV6) || LWIP_IPV6)
    if (family == AF_INET6) {
        struct sockaddr_in6 address;
        if (length < sizeof(address)) return false;
        memcpy(&address, local, sizeof(address));
        const uint8_t mapped_prefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
        return !memcmp(address.sin6_addr.s6_addr, mapped_prefix, sizeof(mapped_prefix)) &&
               !memcmp(address.sin6_addr.s6_addr + sizeof(mapped_prefix), &ipv4, sizeof(ipv4));
    }
#endif
    return false;
}

static bool fail(char *error, size_t size, const char *message)
{
    if (size) snprintf(error, size, "%s", message);
    return false;
}

static int hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static bool valid_utf8(const unsigned char *text)
{
    while (*text) {
        uint32_t value = *text++;
        unsigned continuation;
        uint32_t minimum;
        if (value < 0x80) continue;
        if (value >= 0xc2 && value <= 0xdf) { continuation = 1; minimum = 0x80; value &= 0x1f; }
        else if (value >= 0xe0 && value <= 0xef) { continuation = 2; minimum = 0x800; value &= 0x0f; }
        else if (value >= 0xf0 && value <= 0xf4) { continuation = 3; minimum = 0x10000; value &= 7; }
        else return false;
        for (unsigned i = 0; i < continuation; ++i) {
            if ((*text & 0xc0) != 0x80) return false;
            value = (value << 6) | (*text++ & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}

size_t badge_portal_network_add(badge_portal_network_t *networks, size_t count,
                                size_t capacity, const uint8_t *ssid, size_t length,
                                int8_t rssi, bool open)
{
    if (!networks || !ssid || !length || length > 32 || !capacity || count > capacity) return count;
    badge_portal_network_t entry = {.rssi = rssi, .open = open};
    for (size_t i = 0; i < length; ++i) if (ssid[i] < 0x20 || ssid[i] == 0x7f) return count;
    memcpy(entry.ssid, ssid, length);
    if (!valid_utf8((const unsigned char *) entry.ssid)) return count;
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(networks[i].ssid, entry.ssid)) continue;
        if (networks[i].rssi >= rssi) return count;
        memmove(networks + i, networks + i + 1, (count - i - 1) * sizeof(*networks));
        --count;
        break;
    }
    size_t insert = 0;
    while (insert < count && networks[insert].rssi >= rssi) ++insert;
    if (insert >= capacity) return count;
    if (count < capacity) ++count;
    memmove(networks + insert + 1, networks + insert, (count - insert - 1) * sizeof(*networks));
    networks[insert] = entry;
    return count;
}

static bool decode(const char *source, size_t length, char *dest, size_t capacity)
{
    size_t used = 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char) source[i];
        if (ch == '%') {
            if (i + 2 >= length) return false;
            int high = hex_value(source[i + 1]), low = hex_value(source[i + 2]);
            if (high < 0 || low < 0) return false;
            ch = (unsigned char) ((high << 4) | low);
            i += 2;
        } else if (ch == '+') ch = ' ';
        if (!ch || ch < 0x20 || ch == 0x7f || used + 1 >= capacity) return false;
        dest[used++] = (char) ch;
    }
    dest[used] = 0;
    return valid_utf8((const unsigned char *) dest);
}

static bool integer(const char *text, long minimum, long maximum, long *out)
{
    if (!*text || *text == '+' || *text == ' ') return false;
    const char *p = text;
    if (*p == '-') ++p;
    if (!*p) return false;
    for (; *p; ++p) if (*p < '0' || *p > '9') return false;
    char *end;
    long value = strtol(text, &end, 10);
    if (*end || value < minimum || value > maximum) return false;
    *out = value;
    return true;
}

bool badge_portal_parse_form(const char *body, size_t length,
                             const badge_config_t *current, const char *nonce,
                             badge_config_t *result, char *error, size_t error_size)
{
    if (!body || !current || !result || !nonce || !length ||
        length > BADGE_PORTAL_BODY_MAX || memchr(body, 0, length))
        return fail(error, error_size, "Invalid form length.");
    badge_config_t candidate = *current;
    char password[65] = "", submitted_nonce[33] = "";
    char interval[8] = "", offset[8] = "", open_network[2] = "";
    unsigned fields = 0;
    size_t position = 0;
    while (position < length) {
        const char *part = body + position;
        const char *amp = memchr(part, '&', length - position);
        size_t part_size = amp ? (size_t) (amp - part) : length - position;
        const char *equal = memchr(part, '=', part_size);
        if (!equal) return fail(error, error_size, "Invalid form field.");
        char key[20];
        if (!decode(part, (size_t) (equal - part), key, sizeof(key)))
            return fail(error, error_size, "Invalid form field name.");
        char *dest = NULL;
        size_t capacity = 0;
        unsigned bit = 0;
#define FIELD(label, target, index) \
        if (!strcmp(key, label)) { dest = target; capacity = sizeof(target); bit = 1U << index; }
        FIELD("ssid", candidate.ssid, 0)
        else FIELD("password", password, 1)
        else FIELD("username", candidate.username, 2)
        else FIELD("interval", interval, 3)
        else FIELD("utc_offset", offset, 4)
        else FIELD("nonce", submitted_nonce, 5)
        else FIELD("open_network", open_network, 6)
#undef FIELD
        if (!bit || (fields & bit)) return fail(error, error_size, "Unknown or duplicate form field.");
        fields |= bit;
        size_t value_size = part_size - (size_t) (equal - part) - 1;
        if (!decode(equal + 1, value_size, dest, capacity))
            return fail(error, error_size, "A field is too long or contains invalid characters.");
        position += part_size + (amp ? 1 : 0);
        if (amp && position == length) return fail(error, error_size, "Invalid trailing form field.");
    }
    if ((fields & 63U) != 63U) return fail(error, error_size, "A required field is missing.");
    if (strcmp(submitted_nonce, nonce)) return fail(error, error_size, "Setup session expired. Reload this page.");
    if (!candidate.ssid[0]) return fail(error, error_size, "Enter a Wi-Fi network name.");
    size_t name_length = strlen(candidate.username);
    while (name_length && candidate.username[name_length - 1] == ' ') candidate.username[--name_length] = 0;
    const char *start = candidate.username;
    while (*start == ' ') ++start;
    if (start != candidate.username) memmove(candidate.username, start, strlen(start) + 1);
    if (!badge_username_valid(candidate.username)) return fail(error, error_size, "Enter a valid GitHub username, not a URL.");
    long hours, utc_offset;
    if (!integer(interval, 1, 168, &hours)) return fail(error, error_size, "Sync interval must be 1 to 168 hours.");
    if (!integer(offset, -720, 840, &utc_offset)) return fail(error, error_size, "Invalid browser time zone.");
    if (*open_network && strcmp(open_network, "1")) return fail(error, error_size, "Invalid open-network selection.");
    if (*open_network) candidate.password[0] = 0;
    else if (!*password && !strcmp(candidate.ssid, current->ssid)) {
        /* A blank password preserves the last working credential for this SSID. */
    } else snprintf(candidate.password, sizeof(candidate.password), "%s", password);
    size_t password_length = strlen(candidate.password);
    if (password_length > 0 && password_length < 8)
        return fail(error, error_size, "Wi-Fi passwords need 8 to 63 bytes (or a 64-digit hex key).");
    if (password_length == 64) {
        for (size_t i = 0; i < password_length; ++i)
            if (hex_value(candidate.password[i]) < 0) return fail(error, error_size, "A 64-byte Wi-Fi key must be hexadecimal.");
    }
    candidate.interval_sec = (uint32_t) hours * 3600U;
    candidate.utc_offset_min = (int16_t) utc_offset;
    if (!badge_config_valid(&candidate)) return fail(error, error_size, "Invalid configuration.");
    *result = candidate;
    if (error_size) error[0] = 0;
    return true;
}

size_t badge_portal_dns_reply(uint8_t *packet, size_t length, size_t capacity,
                              const uint8_t address[4])
{
    if (!packet || !address || length < 17 || length > capacity ||
        (packet[2] & 0xf8) || packet[4] || packet[5] != 1 ||
        packet[6] || packet[7] || packet[8] || packet[9]) return 0;
    size_t position = 12, name_bytes = 0;
    while (position < length && packet[position]) {
        uint8_t label = packet[position++];
        if (label > 63 || position + label >= length || name_bytes + label + 1 > 254) return 0;
        position += label;
        name_bytes += label + 1;
    }
    if (position >= length || position + 5 > length) return 0;
    ++position;
    unsigned type = ((unsigned) packet[position] << 8) | packet[position + 1];
    unsigned qclass = ((unsigned) packet[position + 2] << 8) | packet[position + 3];
    if (qclass != 1) return 0;
    position += 4;
    bool answer = type == 1;
    if (answer && position + 16 > capacity) return 0;
    packet[2] = (uint8_t) (0x84 | (packet[2] & 1)); /* authoritative, preserve RD */
    packet[3] = 0;
    packet[6] = 0;
    packet[7] = answer ? 1 : 0;
    packet[8] = packet[9] = packet[10] = packet[11] = 0;
    if (answer) {
        const uint8_t header[] = {0xc0, 0x0c, 0, 1, 0, 1, 0, 0, 0, 30, 0, 4};
        memcpy(packet + position, header, sizeof(header));
        memcpy(packet + position + sizeof(header), address, 4);
        position += 16;
    }
    return position;
}
