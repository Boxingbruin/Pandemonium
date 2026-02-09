#ifndef VIDEO_LAYOUT_H
#define VIDEO_LAYOUT_H

/**
 * Video/UI layout helpers derived from:
 * - `globals.h` (screen size constants)
 * - `video_controller.h` (user-adjustable overscan/aspect settings)
 */

#include "globals.h"
#include "video_controller.h"

// Safe-area margins
// Action safe: 90% area => 5% margin per side
#define ACTION_SAFE_MARGIN_X ((SCREEN_WIDTH * 50) / 1000)
#define ACTION_SAFE_MARGIN_Y ((SCREEN_HEIGHT * 50) / 1000)
// Title/UI safe: 85% area => 7.5% margin per side
#define TITLE_SAFE_MARGIN_X ((SCREEN_WIDTH * 75) / 1000)
#define TITLE_SAFE_MARGIN_Y ((SCREEN_HEIGHT * 75) / 1000)

static inline int ui_safe_margin_x(void)
{
    int m = TITLE_SAFE_MARGIN_X + (int)uiOverscanX;
    if (m < 0) m = 0;
    // Don't allow margins to cross (keep at least 2px drawable area)
    if (m > (SCREEN_WIDTH / 2) - 2) m = (SCREEN_WIDTH / 2) - 2;
    return m;
}

static inline int ui_safe_margin_y(void)
{
    int m = TITLE_SAFE_MARGIN_Y + (int)uiOverscanY;
    if (m < 0) m = 0;
    if (m > (SCREEN_HEIGHT / 2) - 2) m = (SCREEN_HEIGHT / 2) - 2;
    return m;
}

#endif

