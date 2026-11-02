#include "badge_sync_logic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    badge_scope_t scope = {0};
    const char *names[] = {"Rust", "C", "Python", "Shell", "C++", "HTML", "JavaScript"};
    const uint64_t values[] = {30, 10, 50, 2, 20, 3, 40};
    for (unsigned i = 0; i < 7; ++i) badge_scope_add(&scope, names[i], values[i]);
    badge_scope_finish(&scope);
    assert(scope.valid && scope.count == 6 && scope.total_bytes == 155);
    assert(!strcmp(scope.languages[0].name, "Python") && scope.languages[0].bytes == 50);
    assert(!strcmp(scope.languages[5].name, "OTHER") && scope.languages[5].bytes == 5);
    badge_scope_t empty = {0}; badge_scope_finish(&empty);
    assert(empty.valid && !empty.count && !empty.total_bytes);
    badge_repo_t top[BADGE_REPOS] = {0}; badge_scope_t scopes[BADGE_REPOS] = {0}; uint8_t count = 0;
    for (unsigned i = 0; i < 9; ++i) {
        badge_repo_t repo = {.stars = i % 3, .fork = i == 8};
        snprintf(repo.full_name, sizeof(repo.full_name), "u/repo%u", i);
        scope.total_bytes = i;
        badge_repo_rank(top, scopes, &count, &repo, &scope);
    }
    assert(count == 5 && !strcmp(top[0].full_name, "u/repo2"));
    assert(!strcmp(top[2].full_name, "u/repo8") && top[2].fork && scopes[2].total_bytes == 8);
    badge_data_t data = {0};
    for (unsigned i = 0; i < BADGE_DAYS; ++i) data.days[i].day = 1000 + (int32_t)i;
    uint64_t ids[600] = {0}; uint32_t seen = 0, next = 0;
    assert(badge_history_observe(&data, ids, 600, &seen, &next, 11, 1160, true));
    assert(!badge_history_observe(&data, ids, 600, &seen, &next, 11, 1160, true));
    assert(badge_history_observe(&data, ids, 600, &seen, &next, 12, 1160, false));
    assert(data.days[160].coverage == BADGE_PARTIAL && data.days[160].events == 2 && data.days[160].pushes == 1);
    badge_history_coverage(&data, 1164, 1160, false);
    assert(data.days[159].coverage == BADGE_UNKNOWN && data.days[160].coverage == BADGE_PARTIAL);
    assert(data.days[161].coverage == BADGE_KNOWN && data.days[164].coverage == BADGE_PARTIAL);
    assert(data.days[165].coverage == BADGE_UNKNOWN); /* future, never known zero */
    badge_history_coverage(&data, 1164, 1160, true);
    assert(data.days[134].coverage == BADGE_UNKNOWN && data.days[135].coverage == BADGE_KNOWN);
    badge_history_totals(&data);
    assert(data.events_fetched == 2 && data.push_events == 1 && data.push_days == 1);
    data.days[160].events = UINT16_MAX;
    assert(badge_history_observe(&data, ids, 600, &seen, &next, 13, 1160, false));
    assert(data.days[160].events == UINT16_MAX);
    puts("badge language/ranking/history tests passed");
    return 0;
}
