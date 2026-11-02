#include "badge_portal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

static void local_address_tests(void)
{
    assert(badge_portal_host_valid("192.168.4.1"));
    assert(badge_portal_host_valid("192.168.4.1:80"));
    const char *invalid_hosts[] = {NULL, "", "attacker.example", "192.168.4.1.attacker.example",
        "192.168.4.1:8080", "192.168.4.1@attacker.example", "192.168.4.1\r\n", "[::ffff:192.168.4.1]"};
    for (size_t i = 0; i < sizeof(invalid_hosts) / sizeof(invalid_hosts[0]); ++i)
        assert(!badge_portal_host_valid(invalid_hosts[i]));
    const uint32_t ap = htonl(0xc0a80401U); /* 192.168.4.1 */
    struct sockaddr_in ipv4 = {.sin_family = AF_INET};
    ipv4.sin_addr.s_addr = ap;
    assert(badge_portal_local_address_matches((const struct sockaddr *) &ipv4, sizeof(ipv4), ap));
    for (size_t length = 0; length < sizeof(ipv4); ++length)
        assert(!badge_portal_local_address_matches((const struct sockaddr *) &ipv4, length, ap));
    ipv4.sin_addr.s_addr = htonl(0xc0a80114U); /* STA address must stay inaccessible. */
    assert(!badge_portal_local_address_matches((const struct sockaddr *) &ipv4, sizeof(ipv4), ap));
    ipv4.sin_addr.s_addr = htonl(INADDR_ANY);
    assert(!badge_portal_local_address_matches((const struct sockaddr *) &ipv4, sizeof(ipv4), ap));

    struct sockaddr_in6 ipv6 = {.sin6_family = AF_INET6};
    ipv6.sin6_addr.s6_addr[10] = 0xff;
    ipv6.sin6_addr.s6_addr[11] = 0xff;
    memcpy(ipv6.sin6_addr.s6_addr + 12, &ap, sizeof(ap));
    assert(badge_portal_local_address_matches((const struct sockaddr *) &ipv6, sizeof(ipv6), ap));
    for (size_t length = 0; length < sizeof(ipv6); ++length)
        assert(!badge_portal_local_address_matches((const struct sockaddr *) &ipv6, length, ap));

    /* Mirror the full-size getsockname buffer used by HTTP handlers. */
    struct sockaddr_storage storage = {0};
    _Static_assert(sizeof(storage) >= sizeof(ipv6), "IPv6 socket address must fit");
    memcpy(&storage, &ipv6, sizeof(ipv6));
    assert(badge_portal_local_address_matches((const struct sockaddr *) &storage, sizeof(ipv6), ap));

    ipv6.sin6_addr.s6_addr[15] = 2; /* ::ffff:192.168.4.2 */
    assert(!badge_portal_local_address_matches((const struct sockaddr *) &ipv6, sizeof(ipv6), ap));
    uint32_t sta = htonl(0xc0a80114U);
    memcpy(ipv6.sin6_addr.s6_addr + 12, &sta, sizeof(sta));
    assert(!badge_portal_local_address_matches((const struct sockaddr *) &ipv6, sizeof(ipv6), ap));
    memcpy(ipv6.sin6_addr.s6_addr + 12, &ap, sizeof(ap));
    ipv6.sin6_addr.s6_addr[10] = ipv6.sin6_addr.s6_addr[11] = 0; /* IPv4-compatible, not mapped. */
    assert(!badge_portal_local_address_matches((const struct sockaddr *) &ipv6, sizeof(ipv6), ap));
    ipv6.sin6_addr.s6_addr[0] = 0x20; /* Native IPv6 ending in the same four bytes. */
    ipv6.sin6_addr.s6_addr[1] = 0x01;
    assert(!badge_portal_local_address_matches((const struct sockaddr *) &ipv6, sizeof(ipv6), ap));
    memset(&ipv6.sin6_addr, 0, sizeof(ipv6.sin6_addr));
    assert(!badge_portal_local_address_matches((const struct sockaddr *) &ipv6, sizeof(ipv6), ap));
    ipv6.sin6_addr.s6_addr[15] = 1; /* IPv6 loopback. */
    assert(!badge_portal_local_address_matches((const struct sockaddr *) &ipv6, sizeof(ipv6), ap));
    storage.ss_family = AF_UNSPEC;
    assert(!badge_portal_local_address_matches((const struct sockaddr *) &storage, sizeof(storage), ap));
    assert(!badge_portal_local_address_matches(NULL, sizeof(storage), ap));
}

static const char *nonce = "0123456789abcdef0123456789abcdef";

static void network_list_tests(void)
{
    badge_portal_network_t list[3] = {0};
    size_t count = 0;
#define ADD(name, signal, is_open) count = badge_portal_network_add(list, count, 3, (const uint8_t *) name, strlen(name), signal, is_open)
    ADD("Home", -70, false);
    ADD("Cafe", -50, true);
    ADD("Home", -40, true);
    assert(count == 2 && !strcmp(list[0].ssid, "Home") && list[0].rssi == -40 && list[0].open);
    ADD("Home", -80, false);
    assert(count == 2 && list[0].rssi == -40 && list[0].open);
    ADD("Far", -90, false);
    ADD("家庭 WiFi", -30, false);
    assert(count == 3 && !strcmp(list[0].ssid, "家庭 WiFi") && !strcmp(list[2].ssid, "Cafe"));
    ADD("TooFar", -100, true);
    assert(count == 3 && !strcmp(list[2].ssid, "Cafe"));
    ADD("", -1, false);
    ADD("bad\nSSID", -1, false);
    ADD("\xc0\xaf", -1, false);
    assert(count == 3 && !strcmp(list[0].ssid, "家庭 WiFi"));
    ADD("12345678901234567890123456789012", -20, false);
    assert(count == 3 && strlen(list[0].ssid) == 32);
    ADD("123456789012345678901234567890123", -10, false);
    assert(count == 3 && list[0].rssi == -20);
#undef ADD
}

static bool parse(const char *body, const badge_config_t *current, badge_config_t *result)
{
    char error[160];
    bool valid = badge_portal_parse_form(body, strlen(body), current, nonce, result, error, sizeof(error));
    assert(valid ? !error[0] : error[0] != 0);
    return valid;
}

static void form_tests(void)
{
    badge_config_t current, parsed;
    badge_config_defaults(&current);
    strcpy(current.ssid, "Home");
    strcpy(current.password, "old-password");
    strcpy(current.username, "old-user");
    current.theme = 3;
    const char *base = "ssid=Home&password=&username=octocat&interval=6&utc_offset=480&nonce=0123456789abcdef0123456789abcdef";
    assert(parse(base, &current, &parsed));
    assert(!strcmp(parsed.password, "old-password"));
    assert(!strcmp(parsed.username, "octocat"));
    assert(parsed.interval_sec == 21600 && parsed.utc_offset_min == 480 && parsed.theme == 3);
    assert(!strcmp(current.username, "old-user"));
    assert(parse("ssid=%E5%AE%B6%E5%BA%AD+WiFi&password=a%26b%3Dc123&username=+octocat+&interval=168&utc_offset=-720&nonce=0123456789abcdef0123456789abcdef", &current, &parsed));
    assert(!strcmp(parsed.ssid, "家庭 WiFi") && !strcmp(parsed.password, "a&b=c123"));
    assert(parsed.interval_sec == 604800 && !strcmp(parsed.username, "octocat"));
    assert(parse("ssid=Home&password=&username=octocat&interval=1&utc_offset=840&nonce=0123456789abcdef0123456789abcdef&open_network=1", &current, &parsed));
    assert(parsed.password[0] == 0);
    const char *bad[] = {
        "ssid=Home&password=&username=octocat&interval=0&utc_offset=480&nonce=0123456789abcdef0123456789abcdef",
        "ssid=Home&password=&username=octocat&interval=169&utc_offset=480&nonce=0123456789abcdef0123456789abcdef",
        "ssid=Home&password=&username=octocat&interval=6.0&utc_offset=480&nonce=0123456789abcdef0123456789abcdef",
        "ssid=Home&password=&username=octocat&interval=6&utc_offset=841&nonce=0123456789abcdef0123456789abcdef",
        "ssid=Home&password=&username=octocat&interval=6&utc_offset=480&nonce=wrong",
        "ssid=Home&password=&username=octocat&interval=6&utc_offset=480",
        "ssid=Home&password=&username=octocat&interval=6&utc_offset=480&nonce=0123456789abcdef0123456789abcdef&ssid=Other",
        "ssid=Home&password=&username=https%3A%2F%2Fgithub.com%2Foctocat&interval=6&utc_offset=480&nonce=0123456789abcdef0123456789abcdef",
        "ssid=Home&password=short&username=octocat&interval=6&utc_offset=480&nonce=0123456789abcdef0123456789abcdef",
        "ssid=%00Home&password=&username=octocat&interval=6&utc_offset=480&nonce=0123456789abcdef0123456789abcdef",
        "ssid=Home%0a&password=&username=octocat&interval=6&utc_offset=480&nonce=0123456789abcdef0123456789abcdef",
        "ssid=%C0%AF&password=&username=octocat&interval=6&utc_offset=480&nonce=0123456789abcdef0123456789abcdef",
        "ssid=%E5&password=&username=octocat&interval=6&utc_offset=480&nonce=0123456789abcdef0123456789abcdef",
        "ssid=%ED%A0%80&password=&username=octocat&interval=6&utc_offset=480&nonce=0123456789abcdef0123456789abcdef",
        "ssid=%xx&password=&username=octocat&interval=6&utc_offset=480&nonce=0123456789abcdef0123456789abcdef",
        "ssid=Home&password=&username=octocat&interval=6&utc_offset=480&nonce=0123456789abcdef0123456789abcdef&",
        "ssid=Home&password=&username=octocat&interval=6&utc_offset=480&nonce=0123456789abcdef0123456789abcdef&unknown=1",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        memset(&parsed, 0xa5, sizeof(parsed));
        badge_config_t before = parsed;
        assert(!parse(bad[i], &current, &parsed));
        assert(!memcmp(&parsed, &before, sizeof(parsed)));
    }
    char oversized[BADGE_PORTAL_BODY_MAX + 2], error[80];
    memset(oversized, 'x', sizeof(oversized));
    assert(!badge_portal_parse_form(oversized, sizeof(oversized), &current, nonce, &parsed, error, sizeof(error)));
    assert(!badge_portal_parse_form("x=\0z", 4, &current, nonce, &parsed, error, sizeof(error)));
}

static void dns_tests(void)
{
    const uint8_t query[] = {0x12,0x34,1,0,0,1,0,0,0,0,0,0,3,'w','w','w',7,'e','x','a','m','p','l','e',3,'c','o','m',0,0,1,0,1};
    const uint8_t address[] = {192,168,4,1};
    uint8_t buffer[512];
    memcpy(buffer, query, sizeof(query));
    size_t size = badge_portal_dns_reply(buffer, sizeof(query), sizeof(buffer), address);
    assert(size == sizeof(query) + 16 && buffer[0] == 0x12 && buffer[1] == 0x34);
    assert(buffer[2] == 0x85 && buffer[7] == 1 && !memcmp(buffer + size - 4, address, 4));
    memcpy(buffer, query, sizeof(query));
    buffer[30] = 28; /* AAAA gets an empty, valid response instead of an IPv4 record. */
    assert(badge_portal_dns_reply(buffer, sizeof(query), sizeof(buffer), address) == sizeof(query));
    assert(buffer[7] == 0);
    for (size_t truncated = 0; truncated < sizeof(query); ++truncated) {
        memcpy(buffer, query, sizeof(query));
        assert(badge_portal_dns_reply(buffer, truncated, sizeof(buffer), address) == 0);
    }
    memcpy(buffer, query, sizeof(query));
    assert(!badge_portal_dns_reply(buffer, sizeof(query), sizeof(query), address));
    memcpy(buffer, query, sizeof(query)); buffer[12] = 0xc0;
    assert(!badge_portal_dns_reply(buffer, sizeof(query), sizeof(buffer), address));
    memcpy(buffer, query, sizeof(query)); buffer[5] = 2;
    assert(!badge_portal_dns_reply(buffer, sizeof(query), sizeof(buffer), address));
    memcpy(buffer, query, sizeof(query)); buffer[2] |= 0x80;
    assert(!badge_portal_dns_reply(buffer, sizeof(query), sizeof(buffer), address));
}

int main(void)
{
    local_address_tests();
    network_list_tests();
    form_tests();
    dns_tests();
    puts("Badge portal address/parser/DNS tests: PASS");
    return 0;
}
