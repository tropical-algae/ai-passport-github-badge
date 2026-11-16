#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Worker-owned reconnect state. A disconnect event can follow a failed or
 * timed-out attempt; keeping an existing deadline avoids counting it twice. */
typedef struct {
    int64_t at;
    uint8_t failures;
} badge_network_retry_t;

static inline void badge_network_retry_reset(badge_network_retry_t *retry)
{
    retry->at = 0;
    retry->failures = 0;
}

static inline void badge_network_retry_request(badge_network_retry_t *retry, int64_t now)
{
    badge_network_retry_reset(retry);
    retry->at = now + 400;
}

static inline void badge_network_retry_schedule(badge_network_retry_t *retry,
                                                int64_t now, bool interactive)
{
    if (retry->at) return;
    static const uint16_t seconds[] = {1, 2, 4, 8, 16, 32, 60, 120, 240, 300};
    const unsigned last = sizeof(seconds) / sizeof(seconds[0]) - 1;
    unsigned index = retry->failures < last ? retry->failures : last;
    unsigned delay = seconds[index];
    if (retry->failures < last) ++retry->failures;
    if (interactive && delay > 5) delay = 5;
    retry->at = now + (int64_t) delay * 1000;
}
