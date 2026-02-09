#ifndef VIDEO_CONTROLLER_H
#define VIDEO_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Video/UI-related user settings.
 *
 * Persistence:
 * - `uiOverscanX/Y` are persisted in EEPROM via `save_controller.*`.
 * - `hdAspect` is currently runtime-only (not persisted yet).
 */

// Render aspect toggle used by camera/projection and menu setting.
extern bool hdAspect;

// Additional user-controlled UI overscan padding (pixels).
// Positive values move edge-anchored UI further inward to avoid CRT clipping.
extern int8_t uiOverscanX;
extern int8_t uiOverscanY;

#endif

