/*
logo_t3d under the same licensing as the rest of the code
logo_libdragon and logo_n64brew sourced from the N64brew-GameJam2024 repository

Copyright (c) 2024 N64brew

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "opening_credits_scene.h"

#include <libdragon.h>
#include <math.h>

#include "../../controllers/audio_controller.h"
#include "../../utilities/joypad_utility.h"

typedef enum {
    OPENING_CREDITS_STATE_INACTIVE = 0,
    OPENING_CREDITS_STATE_LIBDRAGON,
    OPENING_CREDITS_STATE_TINY3D,
    OPENING_CREDITS_STATE_DONE,
} OpeningCreditsState;

typedef enum {
    OPENING_CREDITS_SFX_LIBDRAGON = 0,
    OPENING_CREDITS_SFX_COUNT,
} OpeningCreditsSfx;

static const char *OPENING_CREDITS_SFX_PATHS[OPENING_CREDITS_SFX_COUNT] = {
    [OPENING_CREDITS_SFX_LIBDRAGON] = "rom:/audio/sfx/dragon_22k.wav64",
};

static OpeningCreditsState s_state = OPENING_CREDITS_STATE_INACTIVE;
static OpeningCreditsSceneResult s_result = OPENING_CREDITS_SCENE_RESULT_NONE;

static uint32_t s_logo_start_ms = 0;

static sprite_t *s_dragon_1 = NULL;
static sprite_t *s_dragon_2 = NULL;
static sprite_t *s_dragon_3 = NULL;
static sprite_t *s_dragon_4 = NULL;

static sprite_t *s_tiny3d_logo = NULL;

static float s_angle1 = 0.0f;
static float s_angle2 = 0.0f;
static float s_angle3 = 0.0f;
static float s_scale1 = 0.0f;
static float s_scale2 = 0.0f;
static float s_scale3 = 0.0f;
static float s_scroll4 = 0.0f;

static int s_anim_part = 0;

static void opening_credits_clear_screen(void)
{
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_set_prim_color(RGBA32(0, 0, 0, 0xFF));
    rdpq_fill_rectangle(0, 0, display_get_width(), display_get_height());
}

static void opening_credits_free_libdragon_assets(void)
{
    if (s_dragon_1) {
        sprite_free(s_dragon_1);
        s_dragon_1 = NULL;
    }

    if (s_dragon_2) {
        sprite_free(s_dragon_2);
        s_dragon_2 = NULL;
    }

    if (s_dragon_3) {
        sprite_free(s_dragon_3);
        s_dragon_3 = NULL;
    }

    if (s_dragon_4) {
        sprite_free(s_dragon_4);
        s_dragon_4 = NULL;
    }

    audio_stop_all_sfx();
}

static void opening_credits_free_tiny3d_assets(void)
{
    if (s_tiny3d_logo) {
        sprite_free(s_tiny3d_logo);
        s_tiny3d_logo = NULL;
    }
}

static void opening_credits_enter_tiny3d(void)
{
    opening_credits_free_libdragon_assets();

    s_tiny3d_logo = sprite_load("rom:/logos/tiny3d/t3d-logo.sprite");

    if (!s_tiny3d_logo) {
        debugf("[OPENING] failed to load rom:/logos/tiny3d/t3d-logo.sprite\n");

        s_state = OPENING_CREDITS_STATE_DONE;
        s_result = OPENING_CREDITS_SCENE_RESULT_DONE;
        return;
    }

    s_logo_start_ms = get_ticks_ms();
    s_state = OPENING_CREDITS_STATE_TINY3D;
}

static void opening_credits_enter_libdragon(void)
{
    s_dragon_1 = sprite_load("rom:/logos/libdragon/dragon1.sprite");
    s_dragon_2 = sprite_load("rom:/logos/libdragon/dragon2.sprite");
    s_dragon_3 = sprite_load("rom:/logos/libdragon/dragon3.sprite");
    s_dragon_4 = sprite_load("rom:/logos/libdragon/dragon4.sprite");

    if (!s_dragon_1 || !s_dragon_2 || !s_dragon_3 || !s_dragon_4) {
        debugf("[OPENING] failed to load one or more libdragon logo sprites\n");

        opening_credits_enter_tiny3d();
        return;
    }

    audio_scene_load_paths(OPENING_CREDITS_SFX_PATHS, OPENING_CREDITS_SFX_COUNT);

    const int W = display_get_width();
    const float sx = (float)W / 640.0f;
    const int H = display_get_height();
    const float sy = (float)H / 480.0f;
    const float s = (sx < sy) ? sx : sy;
    const float scroll4_start = 400.0f * s;

    s_logo_start_ms = get_ticks_ms();
    s_anim_part = 0;

    s_angle1 = 3.2f;
    s_angle2 = 1.9f;
    s_angle3 = 0.9f;
    s_scale1 = 0.0f;
    s_scale2 = 0.4f;
    s_scale3 = 0.8f;
    s_scroll4 = scroll4_start;

    audio_play_scene_sfx_dist(OPENING_CREDITS_SFX_LIBDRAGON, 1.0f, 0.0f);

    s_state = OPENING_CREDITS_STATE_LIBDRAGON;
}

void opening_credits_scene_enter(void)
{
    s_result = OPENING_CREDITS_SCENE_RESULT_NONE;
    s_state = OPENING_CREDITS_STATE_INACTIVE;

    s_logo_start_ms = 0;
    s_anim_part = 0;

    opening_credits_free_libdragon_assets();
    opening_credits_free_tiny3d_assets();

    opening_credits_enter_libdragon();
}

void opening_credits_scene_exit(void)
{
    opening_credits_free_libdragon_assets();
    opening_credits_free_tiny3d_assets();

    s_state = OPENING_CREDITS_STATE_INACTIVE;
    s_result = OPENING_CREDITS_SCENE_RESULT_NONE;

    rspq_wait();
}

void opening_credits_scene_update(void)
{
    if (s_state == OPENING_CREDITS_STATE_INACTIVE || s_state == OPENING_CREDITS_STATE_DONE) {
        return;
    }

    if (btn.a || btn.start) {
        s_state = OPENING_CREDITS_STATE_DONE;
        s_result = OPENING_CREDITS_SCENE_RESULT_DONE;
        return;
    }

    const uint32_t elapsed = get_ticks_ms() - s_logo_start_ms;

    switch (s_state) {
        case OPENING_CREDITS_STATE_LIBDRAGON:
            if (elapsed < 1000) {
                s_anim_part = 0;
            } else if (elapsed < 1500) {
                s_anim_part = 1;
            } else if (elapsed < 4000) {
                s_anim_part = 2;
            } else if (elapsed < 5000) {
                s_anim_part = 3;
            } else {
                opening_credits_enter_tiny3d();
                return;
            }

            s_angle1 -= s_angle1 * 0.04f;
            if (s_angle1 < 0.010f) {
                s_angle1 = 0.0f;
            }

            if (s_anim_part >= 1) {
                s_angle2 -= s_angle2 * 0.06f;
                if (s_angle2 < 0.01f) {
                    s_angle2 = 0.0f;
                }

                s_angle3 -= s_angle3 * 0.06f;
                if (s_angle3 < 0.01f) {
                    s_angle3 = 0.0f;
                }

                s_scale2 -= s_scale2 * 0.06f;
                if (s_scale2 < 0.01f) {
                    s_scale2 = 0.0f;
                }

                s_scale3 -= s_scale3 * 0.06f;
                if (s_scale3 < 0.01f) {
                    s_scale3 = 0.0f;
                }
            }

            if (s_anim_part >= 2) {
                s_scroll4 -= s_scroll4 * 0.08f;
            }
            break;

        case OPENING_CREDITS_STATE_TINY3D:
            if (elapsed >= 3000) {
                s_state = OPENING_CREDITS_STATE_DONE;
                s_result = OPENING_CREDITS_SCENE_RESULT_DONE;
            }
            break;

        default:
            break;
    }
}

static void opening_credits_draw_libdragon(void)
{
    if (!s_dragon_1 || !s_dragon_2 || !s_dragon_3 || !s_dragon_4) {
        return;
    }

    const color_t RED = RGBA32(221, 46, 26, 255);
    const color_t WHITE = RGBA32(255, 255, 255, 255);

    const uint32_t elapsed = get_ticks_ms() - s_logo_start_ms;

    const int W = display_get_width();
    const int H = display_get_height();

    const float sx = (float)W / 640.0f;
    const float sy = (float)H / 480.0f;
    const float s = (sx < sy) ? sx : sy;

    const int X0 = (int)(10.0f * s);
    const int Y0 = (int)(30.0f * s);

    color_t red = RED;
    color_t white = WHITE;

    if (s_anim_part >= 3) {
        uint32_t fade_elapsed = elapsed - 4000;

        if (fade_elapsed > 1000) {
            fade_elapsed = 1000;
        }

        uint8_t alpha = (uint8_t)(255 - ((fade_elapsed * 255) / 1000));

        red.a = alpha;
        white.a = alpha;
    }

    if (s_angle1 > 1.0f) {
        rdpq_set_scissor(0, 0, X0 + (int)(300.0f * s), Y0 + (int)(240.0f * s));
    } else {
        rdpq_set_scissor(0, 0, W, Y0 + (int)(240.0f * s));
    }

    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_combiner(RDPQ_COMBINER1((0,0,0,PRIM),(TEX0,0,PRIM,0)));
    rdpq_set_prim_color(red);

    rdpq_sprite_blit(
        s_dragon_1,
        X0 + (int)(216.0f * s),
        Y0 + (int)(205.0f * s),
        &(rdpq_blitparms_t){
            .theta = s_angle1,
            .scale_x = (s_scale1 + 1.0f) * s,
            .scale_y = (s_scale1 + 1.0f) * s,
            .cx = 176,
            .cy = 171,
        }
    );

    rdpq_set_scissor(0, 0, W, H);

    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    rdpq_mode_dithering(DITHER_NOISE_NOISE);

    float vtx[4][6] = {
        { X0 + (float)(0.0f   * s), Y0 + (float)(180.0f * s), 0, 0, 0, 0 },
        { X0 + (float)(200.0f * s), Y0 + (float)(180.0f * s), 0, 0, 0, 0 },
        { X0 + (float)(200.0f * s), Y0 + (float)(240.0f * s), 0, 0, 0, 1 },
        { X0 + (float)(0.0f   * s), Y0 + (float)(240.0f * s), 0, 0, 0, 1 },
    };

    rdpq_triangle(&TRIFMT_SHADE, vtx[0], vtx[1], vtx[2]);
    rdpq_triangle(&TRIFMT_SHADE, vtx[0], vtx[2], vtx[3]);

    if (s_anim_part >= 1) {
        rdpq_set_mode_standard();
        rdpq_mode_alphacompare(1);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_combiner(RDPQ_COMBINER1((0,0,0,PRIM),(TEX0,0,PRIM,0)));

        color_t color = red;
        color.r *= 1.0f - s_scale3;
        color.g *= 1.0f - s_scale3;
        color.b *= 1.0f - s_scale3;

        rdpq_set_prim_color(color);

        rdpq_sprite_blit(
            s_dragon_2,
            X0 + (int)(246.0f * s),
            Y0 + (int)(230.0f * s),
            &(rdpq_blitparms_t){
                .theta = s_angle2,
                .scale_x = (1.0f - s_scale2) * s,
                .scale_y = (1.0f - s_scale2) * s,
                .cx = 145,
                .cy = 113,
            }
        );

        rdpq_sprite_blit(
            s_dragon_3,
            X0 + (int)(266.0f * s),
            Y0 + (int)(256.0f * s),
            &(rdpq_blitparms_t){
                .theta = -s_angle3,
                .scale_x = (1.0f - s_scale3) * s,
                .scale_y = (1.0f - s_scale3) * s,
                .cx = 91,
                .cy = 24,
            }
        );
    }

    if (s_anim_part >= 2) {
        rdpq_set_prim_color(white);

        rdpq_sprite_blit(
            s_dragon_4,
            X0 + (int)(161.0f * s) + (int)s_scroll4,
            Y0 + (int)(182.0f * s),
            &(rdpq_blitparms_t){
                .scale_x = s,
                .scale_y = s,
            }
        );
    }

    rdpq_set_scissor(0, 0, W, H);
}

static void opening_credits_draw_tiny3d(void)
{
    if (!s_tiny3d_logo) {
        return;
    }

    const uint32_t FADE_IN_MS  = 500;
    const uint32_t HOLD_MS     = 2000;
    const uint32_t FADE_OUT_MS = 500;

    const uint32_t elapsed = get_ticks_ms() - s_logo_start_ms;

    uint8_t alpha = 255;

    if (elapsed < FADE_IN_MS) {
        alpha = (uint8_t)((elapsed * 255) / FADE_IN_MS);
    } else if (elapsed < (FADE_IN_MS + HOLD_MS)) {
        alpha = 255;
    } else {
        const uint32_t u = elapsed - (FADE_IN_MS + HOLD_MS);
        alpha = (uint8_t)(255 - ((u * 255) / FADE_OUT_MS));
    }

    const int W = display_get_width();
    const int H = display_get_height();

    const float maxW = (float)W * 0.70f;
    const float maxH = (float)H * 0.45f;

    float scale = 1.0f;

    if (s_tiny3d_logo->width > 0 && s_tiny3d_logo->height > 0) {
        const float sx = maxW / (float)s_tiny3d_logo->width;
        const float sy = maxH / (float)s_tiny3d_logo->height;

        scale = fminf(1.0f, fminf(sx, sy));

        if (scale < 0.01f) {
            scale = 0.01f;
        }
    }

    const int drawW = (int)((float)s_tiny3d_logo->width * scale);
    const int drawH = (int)((float)s_tiny3d_logo->height * scale);

    int x = (W - drawW) / 2;
    int y = (H - drawH) / 2;

    if (x < 0) {
        x = 0;
    }

    if (y < 0) {
        y = 0;
    }

    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(0);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_combiner(RDPQ_COMBINER1((0,0,0,PRIM),(TEX0,0,PRIM,0)));
    rdpq_set_prim_color(RGBA32(255, 255, 255, alpha));

    rdpq_sprite_blit(
        s_tiny3d_logo,
        x,
        y,
        &(rdpq_blitparms_t){
            .scale_x = scale,
            .scale_y = scale,
        }
    );
}

void opening_credits_scene_draw(T3DViewport *viewport)
{
    (void)viewport;

    if (s_state == OPENING_CREDITS_STATE_INACTIVE) {
        return;
    }

    opening_credits_clear_screen();

    switch (s_state) {
        case OPENING_CREDITS_STATE_LIBDRAGON:
            opening_credits_draw_libdragon();
            break;

        case OPENING_CREDITS_STATE_TINY3D:
            opening_credits_draw_tiny3d();
            break;

        case OPENING_CREDITS_STATE_DONE:
            break;

        default:
            break;
    }
}

OpeningCreditsSceneResult opening_credits_scene_get_result(void)
{
    return s_result;
}

void opening_credits_scene_clear_result(void)
{
    s_result = OPENING_CREDITS_SCENE_RESULT_NONE;
}

bool opening_credits_scene_is_active(void)
{
    return s_state != OPENING_CREDITS_STATE_INACTIVE;
}