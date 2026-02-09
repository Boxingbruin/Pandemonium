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

#define DEBUG_DRAW false
#define DEV_MODE true
#define SHOW_FPS false
#define HARDWARE_MODE false
#define PAL_MODE false
#define DRAW_CRT_SAFE_AREA false

#define DITHER_ENABLED false
#define ARES_AA_ENABLED false

enum {
    FONT_UNBALANCED = 2,
};

#endif