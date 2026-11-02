#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Call under bsp_lvgl_lock. Encoded PNG/JPEG bytes are only borrowed during
 * this call. On success the UI owns a normalized 54x54 RGB565 image (with an
 * alpha plane only when needed), so callers may immediately free the input.
 * NULL removes the avatar. A failed non-NULL replacement preserves the last
 * decoded image; true means the supplied replacement decoded completely. */
bool badge_ui_set_avatar(const void *bytes, size_t size);
