#ifndef GLOBALS_H
#define GLOBALS_H

// Dimensional / engine-wide constants live in the engine config; this header
// re-exports them so existing game includes keep working.
#include "engine_config.h"

#define DEBUG_DRAW false
#define DEBUG_DRAW_ENVIRONMENTAL_HAZARDS false

#define DEV_MODE true
#define SHOW_FPS true
#define HARDWARE_MODE false
#define PAL_MODE false
#define DRAW_CRT_SAFE_AREA false

#define PHASE_2_ENABLED true

#define DITHER_ENABLED false
#define ARES_AA_ENABLED false

enum {
    FONT_UNBALANCED = 2,
};

#endif
