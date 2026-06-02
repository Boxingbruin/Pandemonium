#include "display_utility.h"

#include <libdragon.h>

#include "game_time.h"

surface_t offscreenBuffer;
bool startScreenFade = false;

static int s_fadeBlackAlpha = 255;

static int display_utility_clampi(int x, int lo, int hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

void display_utility_init(void)
{
    s_fadeBlackAlpha = 255;
    startScreenFade = false;
}

void display_utility_cleanup(void)
{
    s_fadeBlackAlpha = 255;
    startScreenFade = false;
}

void display_manager_draw_rectangle(int x, int y, int width, int height, color_t color)
{
    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_set_prim_color(color);

    rdpq_fill_rectangle(x, y, width, height);
}

void display_utility_solid_black_transition(bool fadeIn, float speed)
{
    if (speed < 0.0f) {
        speed = 0.0f;
    }

    if (startScreenFade) {
        s_fadeBlackAlpha = fadeIn ? 255 : 0;
        startScreenFade = false;
    }

    int delta = (int)(deltaTime * speed);

    if (delta < 1 && speed > 0.0f) {
        delta = 1;
    }

    if (fadeIn) {
        s_fadeBlackAlpha -= delta;
        s_fadeBlackAlpha = display_utility_clampi(s_fadeBlackAlpha, 0, 255);

        if (s_fadeBlackAlpha <= 0) {
            return;
        }
    } else {
        s_fadeBlackAlpha += delta;
        s_fadeBlackAlpha = display_utility_clampi(s_fadeBlackAlpha, 0, 255);
    }

    display_manager_draw_rectangle(
        0,
        0,
        display_get_width(),
        display_get_height(),
        RGBA32(0, 0, 0, s_fadeBlackAlpha)
    );
}