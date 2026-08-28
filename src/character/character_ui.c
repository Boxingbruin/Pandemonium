#include "character_ui.h"

#include <libdragon.h>

#include "character.h"
#include "../utilities/globals.h"

#include "../utilities/video_layout.h"

/*
 * Solid bar dimensions. A one-pixel opaque frame surrounds each fill.
 */
#define CHARACTER_UI_HEALTH_FILL_WIDTH 116
#define CHARACTER_UI_HEALTH_FILL_HEIGHT 6
#define CHARACTER_UI_HEALTH_FRAME_WIDTH \
    (CHARACTER_UI_HEALTH_FILL_WIDTH + 2)
#define CHARACTER_UI_HEALTH_FRAME_HEIGHT \
    (CHARACTER_UI_HEALTH_FILL_HEIGHT + 2)

#define CHARACTER_UI_STAMINA_FILL_WIDTH 86
#define CHARACTER_UI_STAMINA_FILL_HEIGHT 2
#define CHARACTER_UI_STAMINA_FRAME_WIDTH \
    (CHARACTER_UI_STAMINA_FILL_WIDTH + 2)
#define CHARACTER_UI_STAMINA_FRAME_HEIGHT \
    (CHARACTER_UI_STAMINA_FILL_HEIGHT + 2)

/*
 * One baked-color down-facing C-button is loaded from ROM. At initialization,
 * it is rotated into a 2x2 RGBA16 atlas:
 *
 *   Down | Right
 *   Up   | Left
 *
 * The atlas is uploaded once per HUD draw, then all four directions are drawn
 * from sub-regions already resident in TMEM.
 */
static sprite_t* s_cButtonSprite = NULL;
static surface_t s_cButtonSurf = {0};
static surface_t s_cButtonAtlasSurf = {0};

static sprite_t* s_flaskSprite = NULL;
static surface_t s_flaskSurf = {0};

static float s_playerUiIntro = 1.0f;

typedef struct {
    int frameLeft;
    int frameTop;
    int frameWidth;
    int frameHeight;

    int fillLeft;
    int fillTop;
    int maxFillWidth;
    int fillHeight;
    int drawWidth;
} CharacterUiBarLayout;

static float character_ui_clampf(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static void character_ui_begin(void)
{
    /*
     * Configure the 2D HUD pipeline once. No manual sync-pipe calls are needed;
     * libdragon tracks and resolves mode changes internally.
     */
    rdpq_set_mode_standard();

#ifdef RDPQ_FOG_DISABLED
    rdpq_mode_fog(RDPQ_FOG_DISABLED);
#else
    rdpq_mode_fog(0);
#endif

    rdpq_mode_antialias(AA_NONE);
    rdpq_mode_alphacompare(0);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_filter(FILTER_BILINEAR);
}

static void character_ui_prepare_baked_sprite_pipe(void)
{
    /*
     * Direct baked-color RGBA16 path:
     * - texture color is used as authored
     * - transparent texels are discarded
     * - no framebuffer blending
     * - point filtering avoids unnecessary filtering on pixel-art UI
     */
    rdpq_mode_combiner(RDPQ_COMBINER_TEX);
    rdpq_mode_alphacompare(1);
    rdpq_mode_blender(0);
    rdpq_mode_filter(FILTER_POINT);
}

static void character_ui_load_sprite(sprite_t **sprite, surface_t *surface, const char *path)
{
    if (!sprite || !surface || !path) return;
    if (*sprite) return;

    *sprite = sprite_load(path);

    if (*sprite) {
        *surface = sprite_get_pixels(*sprite);
    } else {
        *surface = (surface_t){0};
    }
}

static void character_ui_free_sprite(sprite_t **sprite, surface_t *surface)
{
    if (!sprite || !surface) return;

    if (*sprite) {
        sprite_free(*sprite);
        *sprite = NULL;
    }

    *surface = (surface_t){0};
}

static void character_ui_copy_rgba16_pixel(
    surface_t *dst,
    int dstX,
    int dstY,
    const surface_t *src,
    int srcX,
    int srcY
) {
    uint8_t *dstPixel =
        (uint8_t *)dst->buffer
        + (size_t)dstY * (size_t)dst->stride
        + (size_t)dstX * 2U;

    const uint8_t *srcPixel =
        (const uint8_t *)src->buffer
        + (size_t)srcY * (size_t)src->stride
        + (size_t)srcX * 2U;

    /*
     * Copy the packed RGBA5551 value byte-for-byte. Its byte order does not
     * matter here because rotation only relocates complete pixels.
     */
    dstPixel[0] = srcPixel[0];
    dstPixel[1] = srcPixel[1];
}

static bool character_ui_build_c_button_atlas(void)
{
    if (!s_cButtonSprite || !s_cButtonSurf.buffer) {
        return false;
    }

    if (sprite_get_format(s_cButtonSprite) != FMT_RGBA16) {
        debugf("character_ui: cbutton sprite must be RGBA16\n");
        return false;
    }

    const int buttonW = s_cButtonSurf.width;
    const int buttonH = s_cButtonSurf.height;

    if (buttonW <= 0 || buttonH <= 0) {
        debugf("character_ui: cbutton sprite has invalid dimensions\n");
        return false;
    }

    /*
     * Ninety-degree rotations keep the same atlas cell dimensions only for a
     * square source. The supplied C-button is expected to be square.
     */
    if (buttonW != buttonH) {
        debugf(
            "character_ui: cbutton sprite must be square; got %dx%d\n",
            buttonW,
            buttonH
        );
        return false;
    }

    const int atlasW = buttonW * 2;
    const int atlasH = buttonH * 2;
    const size_t atlasTexelBytes =
        (size_t)atlasW * (size_t)atlasH * 2U;

    /*
     * RGBA16 has two bytes per texel. Keep the complete atlas within the RDP's
     * 4 KiB TMEM so it can be uploaded once without splitting.
     */
    if (atlasTexelBytes > 4096U) {
        debugf(
            "character_ui: cbutton atlas is too large for TMEM "
            "(%dx%d RGBA16 = %lu bytes)\n",
            atlasW,
            atlasH,
            (unsigned long)atlasTexelBytes
        );
        return false;
    }

    s_cButtonAtlasSurf = surface_alloc(
        FMT_RGBA16,
        (uint16_t)atlasW,
        (uint16_t)atlasH
    );

    if (!s_cButtonAtlasSurf.buffer) {
        debugf("character_ui: failed to allocate cbutton atlas\n");
        s_cButtonAtlasSurf = (surface_t){0};
        return false;
    }

    /*
     * Transparent black for any alignment/padding bytes and any source pixels
     * outside the four copied cells.
     */
    const size_t atlasBufferBytes =
        (size_t)s_cButtonAtlasSurf.stride
        * (size_t)s_cButtonAtlasSurf.height;

    memset(s_cButtonAtlasSurf.buffer, 0, atlasBufferBytes);

    for (int srcY = 0; srcY < buttonH; ++srcY) {
        for (int srcX = 0; srcX < buttonW; ++srcX) {
            /*
             * Down: source copied directly into the top-left cell.
             */
            character_ui_copy_rgba16_pixel(
                &s_cButtonAtlasSurf,
                srcX,
                srcY,
                &s_cButtonSurf,
                srcX,
                srcY
            );

            /*
             * Right: visually rotate the down arrow 90 degrees
             * counter-clockwise in screen coordinates.
             */
            character_ui_copy_rgba16_pixel(
                &s_cButtonAtlasSurf,
                buttonW + srcY,
                buttonW - 1 - srcX,
                &s_cButtonSurf,
                srcX,
                srcY
            );

            /*
             * Up: rotate 180 degrees.
             */
            character_ui_copy_rgba16_pixel(
                &s_cButtonAtlasSurf,
                buttonW - 1 - srcX,
                buttonH + buttonH - 1 - srcY,
                &s_cButtonSurf,
                srcX,
                srcY
            );

            /*
             * Left: visually rotate the down arrow 90 degrees clockwise.
             */
            character_ui_copy_rgba16_pixel(
                &s_cButtonAtlasSurf,
                buttonW + buttonW - 1 - srcY,
                buttonH + srcX,
                &s_cButtonSurf,
                srcX,
                srcY
            );
        }
    }

    data_cache_hit_writeback(
        s_cButtonAtlasSurf.buffer,
        atlasBufferBytes
    );

    return true;
}

static void character_ui_free_c_button_atlas(void)
{
    if (s_cButtonAtlasSurf.buffer) {
        surface_free(&s_cButtonAtlasSurf);
    }

    s_cButtonAtlasSurf = (surface_t){0};
}

static void character_ui_draw_atlas_region_centered(
    int centerX,
    int centerY,
    int drawW,
    int drawH,
    int sourceX,
    int sourceY,
    int sourceW,
    int sourceH
) {
    if (drawW <= 0 || drawH <= 0) return;
    if (sourceW <= 0 || sourceH <= 0) return;

    float left = (float)centerX - (float)drawW * 0.5f;
    float top = (float)centerY - (float)drawH * 0.5f;

    rdpq_texture_rectangle_scaled(
        TILE0,
        left,
        top,
        left + (float)drawW,
        top + (float)drawH,
        sourceX,
        sourceY,
        sourceX + sourceW,
        sourceY + sourceH
    );
}

void character_ui_init(void)
{
    s_playerUiIntro = 1.0f;

    /*
     * One baked-color down-facing button. The other directions are generated
     * once into an atlas, not rotated or re-uploaded independently per frame.
     */
    character_ui_load_sprite(
        &s_cButtonSprite,
        &s_cButtonSurf,
        "rom:/buttons/cbutton.rgba16.sprite"
    );

    if (!character_ui_build_c_button_atlas()) {
        debugf("character_ui: C-button atlas unavailable\n");
    }

    // Baked-color potion flask; no runtime tinting.
    character_ui_load_sprite(
        &s_flaskSprite,
        &s_flaskSurf,
        "rom:/flask.rgba16.sprite"
    );
}

void character_ui_cleanup(void)
{
    /*
     * The atlas contains copies of the source pixels, so it can be freed
     * independently of the source sprite.
     */
    character_ui_free_c_button_atlas();
    character_ui_free_sprite(&s_cButtonSprite, &s_cButtonSurf);
    character_ui_free_sprite(&s_flaskSprite, &s_flaskSurf);

    s_playerUiIntro = 1.0f;
}

void character_ui_reset(void)
{
    s_playerUiIntro = 1.0f;
}

void character_ui_set_intro(float progress)
{
    s_playerUiIntro = character_ui_clampf(progress, 0.0f, 1.0f);
}

static CharacterUiBarLayout character_ui_get_health_bar_layout(float ratio)
{
    ratio = character_ui_clampf(ratio, 0.0f, 1.0f);

    const int marginX = ui_safe_margin_x();
    const int marginY = ui_safe_margin_y();

    float p = s_playerUiIntro;
    const int slideDist = 40;
    int yOffset = (int)((1.0f - p) * (float)slideDist);

    /*
     * Preserve the existing bar position:
     * fill begins at marginX + 2, marginY + 6.
     * The new frame sits exactly one pixel around it.
     */
    CharacterUiBarLayout layout = {
        .frameLeft = marginX + 1,
        .frameTop = marginY + 5 - yOffset,
        .frameWidth = CHARACTER_UI_HEALTH_FRAME_WIDTH,
        .frameHeight = CHARACTER_UI_HEALTH_FRAME_HEIGHT,

        .fillLeft = marginX + 2,
        .fillTop = marginY + 6 - yOffset,
        .maxFillWidth = CHARACTER_UI_HEALTH_FILL_WIDTH,
        .fillHeight = CHARACTER_UI_HEALTH_FILL_HEIGHT,
        .drawWidth =
            (int)((float)CHARACTER_UI_HEALTH_FILL_WIDTH * ratio),
    };

    return layout;
}

static CharacterUiBarLayout character_ui_get_stamina_bar_layout(float ratio)
{
    ratio = character_ui_clampf(ratio, 0.0f, 1.0f);

    const int marginX = ui_safe_margin_x();
    const int marginY = ui_safe_margin_y();

    float p = s_playerUiIntro;
    const int slideDist = 40;
    int yOffset = (int)((1.0f - p) * (float)slideDist);

    /*
     * The stamina fill is exactly two pixels high.
     */
    int healthBarBottom = marginY + 14 - yOffset;

    CharacterUiBarLayout layout = {
        .frameLeft = marginX + 1,
        .frameTop = healthBarBottom + 1,
        .frameWidth = CHARACTER_UI_STAMINA_FRAME_WIDTH,
        .frameHeight = CHARACTER_UI_STAMINA_FRAME_HEIGHT,

        .fillLeft = marginX + 2,
        .fillTop = healthBarBottom + 2,
        .maxFillWidth = CHARACTER_UI_STAMINA_FILL_WIDTH,
        .fillHeight = CHARACTER_UI_STAMINA_FILL_HEIGHT,
        .drawWidth =
            (int)((float)CHARACTER_UI_STAMINA_FILL_WIDTH * ratio),
    };

    return layout;
}

static void character_ui_draw_bar_rectangles(
    const CharacterUiBarLayout *healthLayout,
    const CharacterUiBarLayout *staminaLayout,
    float healthFlash
) {
    if (!healthLayout && !staminaLayout) {
        return;
    }

    /*
     * Draw both HUD bars entirely with opaque fill rectangles:
     *
     *   1. grey one-pixel outer frames
     *   2. dark empty-bar interiors
     *   3. red health fill
     *   4. green stamina fill
     *
     * No bar textures, texture uploads, alpha blending, or alpha compare are
     * involved.
     */
    rdpq_set_mode_fill(RGBA32(128, 128, 128, 255));

    if (healthLayout) {
        rdpq_fill_rectangle(
            healthLayout->frameLeft,
            healthLayout->frameTop,
            healthLayout->frameLeft + healthLayout->frameWidth,
            healthLayout->frameTop + healthLayout->frameHeight
        );
    }

    if (staminaLayout) {
        rdpq_fill_rectangle(
            staminaLayout->frameLeft,
            staminaLayout->frameTop,
            staminaLayout->frameLeft + staminaLayout->frameWidth,
            staminaLayout->frameTop + staminaLayout->frameHeight
        );
    }

    rdpq_set_mode_fill(RGBA32(10, 10, 10, 255));

    if (healthLayout) {
        rdpq_fill_rectangle(
            healthLayout->frameLeft + 1,
            healthLayout->frameTop + 1,
            healthLayout->frameLeft + healthLayout->frameWidth - 1,
            healthLayout->frameTop + healthLayout->frameHeight - 1
        );
    }

    if (staminaLayout) {
        rdpq_fill_rectangle(
            staminaLayout->frameLeft + 1,
            staminaLayout->frameTop + 1,
            staminaLayout->frameLeft + staminaLayout->frameWidth - 1,
            staminaLayout->frameTop + staminaLayout->frameHeight - 1
        );
    }

    if (healthLayout && healthLayout->drawWidth > 0) {
        healthFlash = character_ui_clampf(healthFlash, 0.0f, 1.0f);

        int red = 200 + (int)(55.0f * healthFlash);
        int green = 30 + (int)(20.0f * healthFlash);
        int blue = 30 + (int)(20.0f * healthFlash);

        rdpq_set_mode_fill(RGBA32(red, green, blue, 255));
        rdpq_fill_rectangle(
            healthLayout->fillLeft,
            healthLayout->fillTop,
            healthLayout->fillLeft + healthLayout->drawWidth,
            healthLayout->fillTop + healthLayout->fillHeight
        );
    }

    if (staminaLayout && staminaLayout->drawWidth > 0) {
        rdpq_set_mode_fill(RGBA32(30, 190, 50, 255));
        rdpq_fill_rectangle(
            staminaLayout->fillLeft,
            staminaLayout->fillTop,
            staminaLayout->fillLeft + staminaLayout->drawWidth,
            staminaLayout->fillTop + staminaLayout->fillHeight
        );
    }
}

static void character_ui_restore_baked_sprite_pipe(void)
{
    /*
     * rdpq_set_mode_fill changes the RDP cycle type. Restore the standard
     * opaque textured path before drawing the C-buttons, flask, or text.
     */
    character_ui_begin();
    character_ui_prepare_baked_sprite_pipe();
}

static void character_ui_draw_health_bar_impl(
    const char *name,
    float ratio,
    float flash
) {
    (void)name;

    CharacterUiBarLayout layout =
        character_ui_get_health_bar_layout(ratio);

    character_ui_draw_bar_rectangles(
        &layout,
        NULL,
        flash
    );

    character_ui_restore_baked_sprite_pipe();
}

static void character_ui_draw_stamina_bar_impl(float ratio)
{
    CharacterUiBarLayout layout =
        character_ui_get_stamina_bar_layout(ratio);

    character_ui_draw_bar_rectangles(
        NULL,
        &layout,
        0.0f
    );

    character_ui_restore_baked_sprite_pipe();
}

static void character_ui_draw_c_buttons_impl(void)
{
    if (!s_cButtonAtlasSurf.buffer) {
        return;
    }

    const int sourceW = s_cButtonSurf.width;
    const int sourceH = s_cButtonSurf.height;

    if (sourceW <= 0 || sourceH <= 0) {
        return;
    }

    /*
     * Keep the existing approximate 20x20 onscreen size. The source is expected
     * to already be close to this size, so scaling should be small or absent.
     */
    const int targetButtonPx = 20;
    int sourceMax = (sourceW > sourceH) ? sourceW : sourceH;

    float scale = (sourceMax > 0)
        ? ((float)targetButtonPx / (float)sourceMax)
        : 1.0f;

    int drawW = (int)((float)sourceW * scale + 0.5f);
    int drawH = (int)((float)sourceH * scale + 0.5f);

    const int marginX = ui_safe_margin_x();
    const int marginY = ui_safe_margin_y();

    const float spacingFrac = 0.7f;
    int spacingX = (int)((float)drawW * spacingFrac);
    int spacingY = (int)((float)drawH * spacingFrac);

    int centerX = marginX + spacingX + (drawW / 2);
    int centerY =
        SCREEN_HEIGHT - marginY - spacingY - (drawH / 2);

    int leftX = centerX - spacingX;
    int leftY = centerY;

    int rightX = centerX + spacingX;
    int rightY = centerY;

    int upX = centerX;
    int upY = centerY - spacingY;

    int downX = centerX;
    int downY = centerY + spacingY;

    /*
     * RGBA16 uses baked color and binary alpha. No tint combiner or framebuffer
     * blending is needed. Alpha compare discards transparent atlas pixels.
     */
    character_ui_prepare_baked_sprite_pipe();

    rdpq_texparms_t atlasParams = (rdpq_texparms_t){};
    atlasParams.s.repeats = 1;
    atlasParams.t.repeats = 1;

    /*
     * Upload all four directions once.
     */
    rdpq_tex_upload(
        TILE0,
        &s_cButtonAtlasSurf,
        &atlasParams
    );

    /*
     * Atlas layout:
     *
     *   Down | Right
     *   Up   | Left
     */
    character_ui_draw_atlas_region_centered(
        downX,
        downY,
        drawW,
        drawH,
        0,
        0,
        sourceW,
        sourceH
    );

    character_ui_draw_atlas_region_centered(
        rightX,
        rightY,
        drawW,
        drawH,
        sourceW,
        0,
        sourceW,
        sourceH
    );

    character_ui_draw_atlas_region_centered(
        upX,
        upY,
        drawW,
        drawH,
        0,
        sourceH,
        sourceW,
        sourceH
    );

    character_ui_draw_atlas_region_centered(
        leftX,
        leftY,
        drawW,
        drawH,
        sourceW,
        sourceH,
        sourceW,
        sourceH
    );

    int potionCount = character_get_health_potion_count();

    if (potionCount <= 0) {
        return;
    }

    if (s_flaskSprite
        && s_flaskSurf.width > 0
        && s_flaskSurf.height > 0
    ) {
        float target =
            (float)((drawW < drawH) ? drawW : drawH) * 0.80f;

        float sourceMaxFlask = (float)(
            (s_flaskSurf.width > s_flaskSurf.height)
                ? s_flaskSurf.width
                : s_flaskSurf.height
        );

        float flaskScale = (sourceMaxFlask > 0.0f)
            ? (target / sourceMaxFlask)
            : 1.0f;

        if (flaskScale < 0.05f) flaskScale = 0.05f;
        if (flaskScale > 4.0f) flaskScale = 4.0f;

        /*
         * The flask is baked RGBA16, so draw it directly without TEX_FLAT or a
         * red primitive-color tint.
         */
        character_ui_prepare_baked_sprite_pipe();

        rdpq_sprite_blit(
            s_flaskSprite,
            leftX,
            leftY,
            &(rdpq_blitparms_t){
                .scale_x = flaskScale,
                .scale_y = flaskScale,
                .cx = s_flaskSurf.width / 2,
                .cy = s_flaskSurf.height / 2,
            }
        );
    }

    // Potion count.
    {
        const int textX = leftX + (drawW / 2) + 2;
        const int textY = leftY + 4;

        /*
         * Restore the blended flat path expected by the font renderer.
         */
        rdpq_mode_alphacompare(0);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_set_prim_color(RGBA32(0, 0, 0, 255));

        rdpq_text_printf(
            NULL,
            FONT_UNBALANCED,
            textX,
            textY,
            "%d",
            potionCount
        );
    }
}

void character_ui_draw_health_bar(const char *name, float ratio, float flash)
{
    character_ui_begin();
    character_ui_draw_health_bar_impl(name, ratio, flash);
}

void character_ui_draw_stamina_bar(float ratio)
{
    character_ui_begin();
    character_ui_draw_stamina_bar_impl(ratio);
}

void character_ui_draw_c_buttons(void)
{
    character_ui_begin();
    character_ui_draw_c_buttons_impl();
}

void character_ui_draw(void)
{
    character_ui_begin();

    float hp = character_get_health();
    float maxHp = character_get_max_health();
    float hpRatio = (maxHp > 0.0f) ? (hp / maxHp) : 0.0f;

    float stamina = character_get_stamina();
    float maxStamina = character_get_max_stamina();
    float staminaRatio = (maxStamina > 0.0f)
        ? (stamina / maxStamina)
        : 0.0f;

    CharacterUiBarLayout healthLayout =
        character_ui_get_health_bar_layout(hpRatio);

    CharacterUiBarLayout staminaLayout =
        character_ui_get_stamina_bar_layout(staminaRatio);

    character_ui_draw_bar_rectangles(
        &healthLayout,
        &staminaLayout,
        0.0f
    );

    character_ui_restore_baked_sprite_pipe();
    character_ui_draw_c_buttons_impl();
}
