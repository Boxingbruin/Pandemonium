#ifndef VIDEO_CONTROLLER_H
#define VIDEO_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Video/UI-related user settings.
 *
 * Persistence:
 * - `uiOverscanX/Y` and `hdAspect` are persisted in EEPROM via `save_controller.*`.
 */

// Render aspect toggle used by camera/projection and menu setting.
extern bool hdAspect;

// Additional user-controlled UI overscan padding (pixels).
// Positive values move edge-anchored UI further inward to avoid CRT clipping.
// Negative values widen UI toward the screen edges (min = widest).
#define UI_OVERSCAN_MIN -15

extern int8_t uiOverscanX;
extern int8_t uiOverscanY;

#endif

