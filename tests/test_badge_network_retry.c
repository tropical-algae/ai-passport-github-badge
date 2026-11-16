#include "badge_network_retry.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

int main(void)
{
    badge_network_retry_t retry = {0};
    int64_t now = 1000;
    badge_network_retry_request(&retry, now);
    assert(retry.at == 1400 && retry.failures == 0);

    /* Losing the saved AP starts promptly, then bounds background radio work.
     * All failure paths share this policy, including API failure and no IP. */
    const unsigned expected[] = {1, 2, 4, 8, 16, 32, 60, 120, 240, 300, 300};
    for (unsigned i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        now = retry.at + 20000;
        retry.at = 0; /* The scheduled connection attempt has been consumed. */
        badge_network_retry_schedule(&retry, now, false);
        assert(retry.at - now == (int64_t) expected[i] * 1000);
        int64_t scheduled = retry.at;
        uint8_t failures = retry.failures;
        /* A queued DISCONNECTED after a timeout must neither shorten the wait
         * nor increment the same attempt's failure count a second time. */
        badge_network_retry_schedule(&retry, now + 100, true);
        assert(retry.at == scheduled && retry.failures == failures);
    }
    for (unsigned i = 0; i < 1000; ++i) {
        retry.at = 0;
        badge_network_retry_schedule(&retry, now, false);
        assert(retry.at == now + 300000 && retry.failures == 9);
    }

    /* Opening setup/new credentials explicitly interrupts the long wait. */
    badge_network_retry_request(&retry, now);
    assert(retry.at == now + 400 && retry.failures == 0);
    const unsigned manual[] = {1, 2, 4, 5, 5, 5, 5, 5, 5, 5, 5};
    for (unsigned i = 0; i < sizeof(manual) / sizeof(manual[0]); ++i) {
        retry.at = 0;
        badge_network_retry_schedule(&retry, now, true);
        assert(retry.at - now == (int64_t) manual[i] * 1000);
    }
    /* Closing setup preserves accumulated failures, without a rapid radio loop. */
    retry.at = 0;
    badge_network_retry_schedule(&retry, now, false);
    assert(retry.at == now + 300000);

    /* Obtaining an IP resets both the pending deadline and failure history. */
    badge_network_retry_reset(&retry);
    assert(!retry.at && !retry.failures);
    badge_network_retry_schedule(&retry, (int64_t) UINT32_MAX * 1000, false);
    assert(retry.at == (int64_t) UINT32_MAX * 1000 + 1000);
    puts("badge network retry: background/manual bounds, duplicate events, recovery PASS");
    return 0;
}
