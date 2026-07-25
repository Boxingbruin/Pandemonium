#ifndef GLOBALS_H
#define GLOBALS_H

#include <t3d/t3d.h>
#include <stdint.h>

#define MODEL_SCALE 0.0625f
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define FIXED_TIMESTEP_MS 33.3f
#define ANIM_SPEED 1.0f
#define FRAME_BUFFER_COUNT 3

#define DEV_MODE false
#define SHOW_FPS true
#define DEBUG_DRAW false // requires dev_mode
#define DEBUG_DRAW_ENVIRONMENTAL_HAZARDS false
#define DEBUG_MEMORY false // requires dev_mode
#define TESTING_SCENE false

#define SKIP_OPENING_CREDITS true
#define PAL_MODE false
#define DRAW_CRT_SAFE_AREA false

#define PHASE_2_ENABLED false

#define DITHER_ENABLED false
#define ARES_AA_ENABLED false
#define CHEAP_BLOOM_ENABLED false

enum {
    FONT_UNBALANCED = 2,
};

#endif