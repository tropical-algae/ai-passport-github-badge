#include "badge_sync_logic.h"
#include <stdio.h>
#include <string.h>

void badge_scope_add(badge_scope_t *scope, const char *name, uint64_t bytes)
{
    scope->total_bytes += bytes;
    unsigned position = 0;
    while (position < scope->count && position < 5 && scope->languages[position].bytes >= bytes) ++position;
    if (position >= 5) return;
    unsigned last = scope->count < 5 ? scope->count++ : 4;
    for (unsigned i = last; i > position; --i) scope->languages[i] = scope->languages[i - 1];
    snprintf(scope->languages[position].name, sizeof(scope->languages[position].name), "%s", name);
    scope->languages[position].bytes = bytes;
}

void badge_scope_finish(badge_scope_t *scope)
{
    uint64_t shown = 0;
    for (unsigned i = 0; i < scope->count; ++i) shown += scope->languages[i].bytes;
    if (scope->total_bytes > shown && scope->count < BADGE_LANGS) {
        strcpy(scope->languages[scope->count].name, "OTHER");
        scope->languages[scope->count++].bytes = scope->total_bytes - shown;
    }
    scope->valid = true;
}

void badge_repo_rank(badge_repo_t top[BADGE_REPOS], badge_scope_t scopes[BADGE_REPOS],
                     uint8_t *count, const badge_repo_t *repo, const badge_scope_t *scope)
{
    /* Repository metadata is ranked as soon as a page arrives. Its language
     * scope can be filled later without inserting the same repository twice. */
    for (unsigned i = 0; i < *count; ++i) if (!strcmp(top[i].full_name, repo->full_name)) {
        top[i] = *repo; scopes[i] = *scope;
        return;
    }
    unsigned at = 0;
    while (at < *count && (top[at].stars > repo->stars ||
           (top[at].stars == repo->stars && strcmp(top[at].full_name, repo->full_name) < 0))) ++at;
    if (at >= BADGE_REPOS) return;
    unsigned last = *count < BADGE_REPOS ? (*count)++ : BADGE_REPOS - 1;
    for (unsigned i = last; i > at; --i) { top[i] = top[i-1]; scopes[i] = scopes[i-1]; }
    top[at] = *repo; scopes[at] = *scope;
}

bool badge_history_observe(badge_data_t *data, uint64_t *ids, uint32_t capacity,
                           uint32_t *count, uint32_t *next, uint64_t id, int32_t day, bool push)
{
    if (!id || !capacity || *count > capacity || *next >= capacity) return false;
    for (uint32_t i = 0; i < *count; ++i) if (ids[i] == id) return false;
    ids[*next] = id;
    *next = (*next + 1) % capacity;
    if (*count < capacity) ++*count;
    for (unsigned i = 0; i < BADGE_DAYS; ++i) if (data->days[i].day == day) {
        if (data->days[i].events < UINT16_MAX) ++data->days[i].events;
        if (push && data->days[i].pushes < UINT16_MAX) ++data->days[i].pushes;
        if (data->days[i].coverage == BADGE_UNKNOWN) data->days[i].coverage = BADGE_PARTIAL;
        break;
    }
    return true;
}

void badge_history_coverage(badge_data_t *data, int32_t today, int32_t oldest, bool exhausted)
{
    int32_t first_known = exhausted ? today - 29 : oldest + 1;
    for (unsigned i = 0; i < BADGE_DAYS; ++i) {
        badge_day_t *day = &data->days[i];
        if (day->day >= first_known && day->day < today) day->coverage = BADGE_KNOWN;
        else if (day->day == today && day->coverage == BADGE_UNKNOWN) day->coverage = BADGE_PARTIAL;
    }
}

void badge_history_totals(badge_data_t *data)
{
    data->events_fetched = 0; data->push_events = 0; data->push_days = 0;
    for (unsigned i = 0; i < BADGE_DAYS; ++i) {
        data->events_fetched += data->days[i].events;
        data->push_events += data->days[i].pushes;
        data->push_days += data->days[i].pushes != 0;
    }
}
