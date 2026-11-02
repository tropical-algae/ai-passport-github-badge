#pragma once
#include "badge_model.h"

void badge_scope_add(badge_scope_t *scope, const char *name, uint64_t bytes);
void badge_scope_finish(badge_scope_t *scope);
void badge_repo_rank(badge_repo_t top[BADGE_REPOS], badge_scope_t scopes[BADGE_REPOS],
                     uint8_t *count, const badge_repo_t *repo, const badge_scope_t *scope);
/* Returns true only for a new observation. ID and count state must be saved
 * together; the caller owns their persistent per-account journal. */
bool badge_history_observe(badge_data_t *data, uint64_t *ids, uint32_t capacity,
                           uint32_t *count, uint32_t *next, uint64_t id, int32_t day, bool push);
void badge_history_coverage(badge_data_t *data, int32_t today, int32_t oldest, bool exhausted);
void badge_history_totals(badge_data_t *data);
