#include "cheap_bloom.h"

#include <libdragon.h>
#include <rdpq.h>
#include <rdpq_mode.h>
#include <rdpq_rect.h>
#include <rdpq_tex.h>
#include <surface.h>

#define CHEAP_BLOOM_SCREEN_W 320
#define CHEAP_BLOOM_SCREEN_H 240
#define CHEAP_BLOOM_SCALE    4
#define CHEAP_BLOOM_LOW_W    (CHEAP_BLOOM_SCREEN_W / CHEAP_BLOOM_SCALE)
#define CHEAP_BLOOM_LOW_H    (CHEAP_BLOOM_SCREEN_H / CHEAP_BLOOM_SCALE)

/*
 * Extra safe rows match the style of the RSPFX postprocess sample. They are not
 * strictly required for this RDP-only pass, but keeping them gives us room for
 * filtering/chunking edge behavior if we change the pass later.
 */
#define CHEAP_BLOOM_SAFE_PAD_Y 4

static bool s_bloomInitialized = false;
static bool s_bloomEnabled = true;
static uint8_t s_bloomAlpha = 48;

static surface_t s_bloomLow;
static surface_t s_bloomLowSafe;

static void cheap_bloom_assert_display(void)
{
    assertf(display_get_width() == CHEAP_BLOOM_SCREEN_W,
        "cheap_bloom expects 320x240 display width");
    assertf(display_get_height() == CHEAP_BLOOM_SCREEN_H,
        "cheap_bloom expects 320x240 display height");
    assertf(display_get_bitdepth() == 4,
        "cheap_bloom expects a 32-bit / RGBA32 framebuffer");
}

void cheap_bloom_init(void)
{
    if (s_bloomInitialized) return;

    cheap_bloom_assert_display();

    s_bloomLow = surface_alloc(
        FMT_RGBA32,
        CHEAP_BLOOM_LOW_W,
        CHEAP_BLOOM_LOW_H + CHEAP_BLOOM_SAFE_PAD_Y
    );

    /* Use only the visible/safe 80x60 region for rendering and blitting. */
    s_bloomLowSafe = surface_make_sub(
        &s_bloomLow,
        0,
        CHEAP_BLOOM_SAFE_PAD_Y / 2,
        CHEAP_BLOOM_LOW_W,
        CHEAP_BLOOM_LOW_H
    );

    s_bloomInitialized = true;
}

void cheap_bloom_cleanup(void)
{
    if (!s_bloomInitialized) return;

    rspq_wait();

    surface_free(&s_bloomLow);

    s_bloomInitialized = false;
}

void cheap_bloom_set_enabled(bool enabled)
{
    s_bloomEnabled = enabled;
}

bool cheap_bloom_is_enabled(void)
{
    return s_bloomEnabled;
}

void cheap_bloom_set_alpha(uint8_t alpha)
{
    s_bloomAlpha = alpha;
}

uint8_t cheap_bloom_get_alpha(void)
{
    return s_bloomAlpha;
}

static void cheap_bloom_downscale_4x(surface_t *framebuffer)
{
    if (!framebuffer) return;

    /*
     * Cheap 4x downscale adapted from the RDP path in the RSPFX postprocess
     * sample:
     *   - render target is 80x60
     *   - for each low-res output line, sample two source lines from a 4-line group
     *   - TILE0 and TILE1 are the same framebuffer strip with an X offset
     *   - PRIM_ALPHA = 0x80 blends the two horizontal samples
     *
     * This is not a full blur. The 4x downscale is the blur.
     */
    rdpq_sync_pipe();
    rdpq_set_color_image(&s_bloomLowSafe);

    rdpq_set_mode_standard();
    rdpq_mode_begin();
        rdpq_mode_filter(FILTER_MEDIAN);
        rdpq_mode_antialias(AA_NONE);
        rdpq_mode_dithering(DITHER_NONE_NONE);
        rdpq_mode_blender(0);
        rdpq_mode_combiner(RDPQ_COMBINER2(
            (TEX0, TEX1, PRIM_ALPHA, TEX1),       (0, 0, 0, 1),
            (0,    0,    0,          COMBINED),   (0, 0, 0, 1)
        ));
    rdpq_mode_end();

    rdpq_texparms_t texParam0 = {0};
    texParam0.s.scale_log = -2;
    texParam0.s.translate = 1.5f;
    texParam0.t.translate = 0.5f;

    rdpq_texparms_t texParam1 = texParam0;
    texParam1.s.translate += 2.0f;

    rdpq_set_prim_color(RGBA32(0, 0, 0, 0x80));

    for (int y = 0; y < CHEAP_BLOOM_LOW_H; ++y) {
        surface_t srcStrip = surface_make_sub(
            framebuffer,
            0,
            y * CHEAP_BLOOM_SCALE,
            CHEAP_BLOOM_SCREEN_W,
            2
        );

        /* Sample every other source line inside the 4-line source group. */
        srcStrip.stride *= 2;

        rdpq_tex_multi_begin();
            rdpq_tex_upload(TILE0, &srcStrip, &texParam0);
            rdpq_tex_reuse(TILE1, &texParam1);
        rdpq_tex_multi_end();

        rdpq_texture_rectangle(
            TILE0,
            0,
            y,
            CHEAP_BLOOM_LOW_W,
            y + 1,
            1,
            1
        );
    }

    rdpq_sync_pipe();
}

static void cheap_bloom_overlay(surface_t *framebuffer)
{
    if (!framebuffer) return;

    rdpq_sync_pipe();
    rdpq_set_color_image(framebuffer);

    rdpq_set_mode_standard();
    rdpq_mode_begin();
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_antialias(AA_NONE);
        rdpq_mode_dithering(DITHER_NONE_NONE);

        /*
         * TEX_FLAT multiplies the bloom texture by PRIM.
         *
         * For additive bloom, do not rely on alpha as normal transparency.
         * Instead, use s_bloomAlpha as an RGB brightness scale:
         *
         *   bloom contribution = low-res texture * (s_bloomAlpha / 255)
         *
         * Then add that contribution onto the framebuffer.
         */
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_ADDITIVE);
    rdpq_mode_end();

    /*
     * This is now brightness strength, not normal source-over alpha.
     *
     * Start lower than before:
     *   16 = subtle
     *   24 = visible
     *   40 = strong
     *   60+ = probably very heavy
     */
    rdpq_set_prim_color(RGBA32(
        s_bloomAlpha,
        s_bloomAlpha,
        s_bloomAlpha,
        0xFF
    ));

    rdpq_tex_blit(&s_bloomLowSafe, 0, 0, &(rdpq_blitparms_t){
        .scale_x = (float)CHEAP_BLOOM_SCALE,
        .scale_y = (float)CHEAP_BLOOM_SCALE,
        .filtering = true,
    });

    rdpq_sync_pipe();
}

void cheap_bloom_apply(surface_t *framebuffer)
{
    if (!s_bloomEnabled) return;
    if (!framebuffer) return;
    if (s_bloomAlpha == 0) return;

    cheap_bloom_init();

    cheap_bloom_downscale_4x(framebuffer);
    cheap_bloom_overlay(framebuffer);
}