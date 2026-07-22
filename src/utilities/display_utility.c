#include "display_utility.h"

#include <libdragon.h>

#include "cheap_bloom.h"
#include "game_time.h"
#include "globals.h"

surface_t offscreenBuffer;
bool startScreenFade = false;
bool displayBloomEnabled = false;

static int s_fadeBlackAlpha = 255;

static uint8_t s_bloomAlpha = 48;
static bool s_bloomInitialized = false;

static int display_utility_clampi(int x, int lo, int hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static void display_utility_ensure_bloom_initialized(void)
{
    if (s_bloomInitialized) return;

    cheap_bloom_init();
    cheap_bloom_set_alpha(s_bloomAlpha);

    s_bloomInitialized = true;
}

void display_utility_init(void)
{
    s_fadeBlackAlpha = 255;
    startScreenFade = false;

    displayBloomEnabled = false;
    s_bloomAlpha = 48;
    s_bloomInitialized = false;
}

void display_utility_cleanup(void)
{
    s_fadeBlackAlpha = 255;
    startScreenFade = false;

    displayBloomEnabled = false;

    if (s_bloomInitialized) {
        cheap_bloom_cleanup();
        s_bloomInitialized = false;
    }
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

void display_utility_set_bloom_enabled(bool enabled)
{
    displayBloomEnabled = CHEAP_BLOOM_ENABLED && enabled;
}

void display_utility_set_bloom_alpha(uint8_t alpha)
{
    s_bloomAlpha = alpha;

    if (s_bloomInitialized) {
        cheap_bloom_set_alpha(s_bloomAlpha);
    }
}

void display_utility_apply_postprocess(surface_t *framebuffer)
{
    if (!CHEAP_BLOOM_ENABLED) return;
    if (!displayBloomEnabled) return;
    if (!framebuffer) return;

    display_utility_ensure_bloom_initialized();

    cheap_bloom_set_alpha(s_bloomAlpha);
    cheap_bloom_apply(framebuffer);
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