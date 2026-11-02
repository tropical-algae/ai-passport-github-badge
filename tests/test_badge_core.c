#include "badge_model.h"
#include "badge_input.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    badge_config_t c;
    badge_config_defaults(&c);
    assert(c.interval_sec == 21600 && c.brightness == 100 && c.theme == 0 && c.screen_timeout_min == 10);
    assert(!badge_config_valid(&c));
    assert(badge_username_valid("octocat") && badge_username_valid("a-b-0"));
    assert(!badge_username_valid("-a") && !badge_username_valid("a-") && !badge_username_valid("a--b"));
    assert(!badge_username_valid("a/b") && !badge_username_valid("../x") && !badge_username_valid(""));
    char unterminated[40]; memset(unterminated, 'a', sizeof(unterminated));
    assert(!badge_username_valid(unterminated));
    char max[41]; memset(max, 'a', 40); max[40] = 0;
    assert(!badge_username_valid(max)); max[39] = 0; assert(badge_username_valid(max));
    strcpy(c.username, "octocat"); strcpy(c.ssid, "local-network");
    assert(badge_config_valid(&c));
    for (unsigned brightness = 0; brightness <= 255; ++brightness) {
        c.brightness = (uint8_t)brightness;
        bool offered = brightness == 10 || (brightness >= 20 && brightness <= 100 && brightness % 20 == 0);
        assert(badge_config_valid(&c) == offered);
    }
    c.brightness = 10;
    const uint16_t timeouts[] = {1, 5, 10, 30, 60, 0};
    for (size_t i = 0; i < sizeof(timeouts) / sizeof(timeouts[0]); ++i) {
        c.screen_timeout_min = timeouts[i]; assert(badge_config_valid(&c));
    }
    c.screen_timeout_min = 2; assert(!badge_config_valid(&c));
    c.screen_timeout_min = UINT16_MAX; assert(!badge_config_valid(&c));
    c.screen_timeout_min = 60;
    badge_config_t decoded;
    assert(badge_config_decode(&c, sizeof(c), &decoded) && decoded.screen_timeout_min == 60 && decoded.brightness == 10);
    assert(badge_config_decode(&c, BADGE_LEGACY_CONFIG_SIZE, &decoded) && decoded.screen_timeout_min == 10);
    assert(!badge_config_decode(&c, BADGE_LEGACY_CONFIG_SIZE + 1, &decoded));
    assert(!badge_config_decode(NULL, sizeof(c), &decoded));
    assert(!badge_config_decode(&c, sizeof(c), NULL));
    c.interval_sec = 3599; assert(!badge_config_valid(&c)); c.interval_sec = 21600;
    badge_data_t d;
    badge_data_reset(&d, c.username);
    const int64_t now = 1793620800;
    assert(!badge_sync_due(&d, &c, 0)); assert(badge_sync_due(&d, &c, now));
    d.last_success = now;
    assert(!badge_sync_due(&d, &c, now + 21599));
    assert(badge_sync_due(&d, &c, now + 21600));
    d.retry_after = now + 3600;
    assert(!badge_sync_due(&d, &c, now + 3599));
    assert(badge_sync_due(&d, &c, now + 3600));
    badge_history_roll(&d, 20759);
    assert((d.days[0].day + 4) % 7 == 0);
    assert(d.days[167].day >= 20759);
    d.days[100].events = 17; d.days[100].coverage = BADGE_KNOWN;
    int32_t preserved = d.days[100].day;
    badge_history_roll(&d, 20766);
    assert(d.days[93].day == preserved && d.days[93].events == 17 && d.days[93].coverage == BADGE_KNOWN);
    assert(d.days[167].coverage == BADGE_UNKNOWN);
    badge_history_roll(&d, 21000);
    for (unsigned i = 0; i < BADGE_DAYS; ++i) assert(!d.days[i].events && d.days[i].coverage == BADGE_UNKNOWN);
    char number[20]; badge_format_number(1380, number, sizeof(number)); assert(!strcmp(number, "1,380"));
    badge_format_wait(now + 252, now, number, sizeof(number)); assert(!strcmp(number, "4M 12S"));
    badge_format_wait(now + 3720, now, number, sizeof(number)); assert(!strcmp(number, "1H 02M"));
    badge_format_wait(now + 97200, now, number, sizeof(number)); assert(!strcmp(number, "1D 03H"));
    badge_format_wait(now, now, number, sizeof(number)); assert(!strcmp(number, "0S"));
    badge_format_wait(now - 1, now, number, sizeof(number)); assert(!strcmp(number, "0S"));
    badge_format_wait(INT64_MAX, INT64_MIN, number, sizeof(number)); assert(!strcmp(number, "999D+"));
    badge_format_wait(now + 5, now, number, 1); assert(!number[0]);
    badge_input_t input; badge_input_init(&input);
    assert(!badge_input_edge(&input, 2, true, 1000).ready);
    assert(!badge_input_poll(&input, 600999).ready);
    badge_input_action_t action = badge_input_poll(&input, 601000);
    assert(action.ready && action.long_press && action.key == 2);
    assert(!badge_input_poll(&input, 900000).ready);
    assert(!badge_input_edge(&input, 2, false, 950000).ready);
    badge_input_edge(&input, 0, true, 1000000);
    action = badge_input_edge(&input, 0, false, 1200000);
    assert(action.ready && !action.long_press && action.key == 0);
    badge_input_edge(&input, 1, true, 2000000);
    action = badge_input_edge(&input, 1, false, 2700000);
    assert(action.ready && action.long_press && action.key == 1);
    assert(!badge_input_edge(&input, 0, false, 3000000).ready);
    puts("badge core: PASS (configuration, due/retry, history, hold/release)");
}
