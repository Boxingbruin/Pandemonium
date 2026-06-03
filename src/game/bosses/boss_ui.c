#include "boss_ui.h"

#include <libdragon.h>

#include "../../utilities/globals.h"
#include "../../utilities/game_time.h"
#include "../../utilities/video_layout.h"
#include "../../utilities/button_prompt_utility.h"

#define BOSS_UI_TRAIL_HOLD_TIME 0.35f
#define BOSS_UI_TRAIL_DECAY_RATE 0.6f

// Overall decorative frame layout.
// Safe-area anchoring still comes from video_layout/ui_safe_margin_y().
#define BOSS_UI_HEALTH_FRAME_WIDTH 200
#define BOSS_UI_HEALTH_FRAME_SCALE 0.45f

// Health fill layout, independent from the decorative frame width.
#define BOSS_UI_HEALTH_FILL_WIDTH 184
#define BOSS_UI_HEALTH_FILL_X_OFFSET 0
#define BOSS_UI_HEALTH_FILL_Y_OFFSET 2
#define BOSS_UI_HEALTH_FILL_HEIGHT 8

// Boss name layout, relative to the frame.
// X offset is now a fine-tune value after centering.
// 0 = centered, positive = right, negative = left.
#define BOSS_UI_NAME_SCALE 0.5f
#define BOSS_UI_NAME_X_OFFSET 0
#define BOSS_UI_NAME_GAP 2

typedef struct {
    float lossTrail;
    float gainTrail;
    float lastRatio;
    float lossHold;
    float gainHold;
} BossUiBarTrailState;

typedef struct {
    int frameX;
    int frameY;
    int frameW;
    int frameH;

    int fillX;
    int fillY;
    int fillW;
    int fillH;

    int nameX;
    int nameY;
} BossUiHealthLayout;

static sprite_t* s_zTargetIconSprite = NULL;
static surface_t s_zTargetIconSurf = {0};

static sprite_t* s_bossHealthBarEndSprite = NULL;
static surface_t s_bossHealthBarEndSurf = {0};

static sprite_t* s_bossHealthBarMidSprite = NULL;
static surface_t s_bossHealthBarMidSurf = {0};

static sprite_t* s_bossHealthBarNameSprite = NULL;
static surface_t s_bossHealthBarNameSurf = {0};

static float s_bossUiIntro = 1.0f;
static BossUiBarTrailState s_bossHealthBarState = {1.0f, 1.0f, 1.0f, 0.0f, 0.0f};

static float boss_ui_clampf(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static int boss_ui_max_int(int a, int b)
{
    return (a > b) ? a : b;
}

static int boss_ui_scaled_int(float x)
{
    if (x <= 0.0f) return 0;
    return (int)(x + 0.5f);
}

static void boss_ui_reset_bar_trail(BossUiBarTrailState *s, float ratio)
{
    if (!s) return;

    ratio = boss_ui_clampf(ratio, 0.0f, 1.0f);

    s->lossTrail = ratio;
    s->gainTrail = ratio;
    s->lastRatio = ratio;
    s->lossHold = 0.0f;
    s->gainHold = 0.0f;
}

static void boss_ui_update_bar_trails(float ratio, BossUiBarTrailState *s)
{
    if (!s) return;

    ratio = boss_ui_clampf(ratio, 0.0f, 1.0f);

    if (ratio < s->lastRatio) {
        // Just lost health.
        s->lossHold = BOSS_UI_TRAIL_HOLD_TIME;

        if (s->gainTrail < ratio) {
            s->gainTrail = ratio;
        }
    } else if (ratio > s->lastRatio) {
        // Just gained health.
        s->gainHold = BOSS_UI_TRAIL_HOLD_TIME;

        if (s->lossTrail < ratio) {
            s->lossTrail = ratio;
        }
    }

    s->lastRatio = ratio;

    if (s->lossTrail < ratio) {
        s->lossTrail = ratio;
    }

    if (s->gainTrail > ratio) {
        s->gainTrail = ratio;
    }

    if (s->lossHold > 0.0f) {
        s->lossHold -= deltaTime;

        if (s->lossHold < 0.0f) {
            s->lossHold = 0.0f;
        }
    } else if (s->lossTrail > ratio) {
        s->lossTrail -= BOSS_UI_TRAIL_DECAY_RATE * deltaTime;

        if (s->lossTrail < ratio) {
            s->lossTrail = ratio;
        }
    }

    if (s->gainHold > 0.0f) {
        s->gainHold -= deltaTime;

        if (s->gainHold < 0.0f) {
            s->gainHold = 0.0f;
        }
    } else if (s->gainTrail < ratio) {
        s->gainTrail += BOSS_UI_TRAIL_DECAY_RATE * deltaTime;

        if (s->gainTrail > ratio) {
            s->gainTrail = ratio;
        }
    }
}

static void boss_ui_load_sprite(sprite_t **sprite, surface_t *surface, const char *path)
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

static void boss_ui_free_sprite(sprite_t **sprite, surface_t *surface)
{
    if (!sprite || !surface) return;

    if (*sprite) {
        sprite_free(*sprite);
        *sprite = NULL;
    }

    *surface = (surface_t){0};
}

static bool boss_ui_world_to_screen(
    T3DViewport *viewport,
    const T3DVec3 *worldPos,
    T3DVec3 *outScreenPos,
    int margin
) {
    if (!viewport || !worldPos || !outScreenPos) return false;

    t3d_viewport_calc_viewspace_pos(viewport, outScreenPos, worldPos);

    // Tiny3D viewspace depth: >= 1.0f means behind/outside usable view.
    if (outScreenPos->v[2] >= 1.0f) {
        return false;
    }

    int px = (int)outScreenPos->v[0];
    int py = (int)outScreenPos->v[1];

    if (px < -margin || px > SCREEN_WIDTH + margin) {
        return false;
    }

    if (py < -margin || py > SCREEN_HEIGHT + margin) {
        return false;
    }

    return true;
}

static int boss_ui_health_frame_height(void)
{
    int endH = 0;
    int midH = 0;

    if (s_bossHealthBarEndSurf.height > 0) {
        endH = boss_ui_scaled_int((float)s_bossHealthBarEndSurf.height * BOSS_UI_HEALTH_FRAME_SCALE);
    }

    if (s_bossHealthBarMidSurf.height > 0) {
        midH = boss_ui_scaled_int((float)s_bossHealthBarMidSurf.height * BOSS_UI_HEALTH_FRAME_SCALE);
    }

    int h = boss_ui_max_int(endH, midH);

    if (h <= 0) {
        h = 24;
    }

    return h;
}

static int boss_ui_health_name_width(void)
{
    if (!s_bossHealthBarNameSprite || s_bossHealthBarNameSurf.width <= 0) {
        return 0;
    }

    return boss_ui_scaled_int((float)s_bossHealthBarNameSurf.width * BOSS_UI_NAME_SCALE);
}

static int boss_ui_health_name_height(void)
{
    if (!s_bossHealthBarNameSprite || s_bossHealthBarNameSurf.height <= 0) {
        return 0;
    }

    return boss_ui_scaled_int((float)s_bossHealthBarNameSurf.height * BOSS_UI_NAME_SCALE);
}

static BossUiHealthLayout boss_ui_make_health_layout(void)
{
    BossUiHealthLayout layout = {0};

    layout.frameW = BOSS_UI_HEALTH_FRAME_WIDTH;
    layout.frameH = boss_ui_health_frame_height();

    layout.frameX = (SCREEN_WIDTH - layout.frameW) / 2;

    // Anchor the decorative frame to the bottom safe area.
    // video_layout owns this safe/overdraw margin.
    layout.frameY = SCREEN_HEIGHT - ui_safe_margin_y() - layout.frameH;

    // The actual health fill width is independent from the frame width.
    // This lets the decorative overlay be tuned without shrinking/stretching HP.
    layout.fillW = BOSS_UI_HEALTH_FILL_WIDTH;
    layout.fillH = BOSS_UI_HEALTH_FILL_HEIGHT;
    layout.fillX = layout.frameX + ((layout.frameW - layout.fillW) / 2) + BOSS_UI_HEALTH_FILL_X_OFFSET;
    layout.fillY = layout.frameY + BOSS_UI_HEALTH_FILL_Y_OFFSET;

    if (layout.fillW < 1) {
        layout.fillW = 1;
    }

    int nameWidth = boss_ui_health_name_width();
    int nameHeight = boss_ui_health_name_height();

    layout.nameX = layout.frameX + ((layout.frameW - nameWidth) / 2) + BOSS_UI_NAME_X_OFFSET;
    layout.nameY = layout.frameY - nameHeight - BOSS_UI_NAME_GAP;

    return layout;
}

static void boss_ui_prepare_flat_ui_pipe(void)
{
    rdpq_set_mode_standard();

#ifdef RDPQ_FOG_DISABLED
    rdpq_mode_fog(RDPQ_FOG_DISABLED);
#else
    rdpq_mode_fog(0);
#endif

    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
}

static void boss_ui_prepare_sprite_ui_pipe(bool alphaCompare)
{
    rdpq_set_mode_standard();

#ifdef RDPQ_FOG_DISABLED
    rdpq_mode_fog(RDPQ_FOG_DISABLED);
#else
    rdpq_mode_fog(0);
#endif

    rdpq_mode_alphacompare(alphaCompare ? 1 : 0);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_filter(FILTER_POINT);
}

static void boss_ui_draw_health_frame(const BossUiHealthLayout *layout)
{
    if (!layout) return;
    if (layout->frameW <= 0 || layout->frameH <= 0) return;

    const bool hasEnd =
        s_bossHealthBarEndSprite &&
        s_bossHealthBarEndSurf.width > 0 &&
        s_bossHealthBarEndSurf.height > 0;

    const bool hasMid =
        s_bossHealthBarMidSprite &&
        s_bossHealthBarMidSurf.width > 0 &&
        s_bossHealthBarMidSurf.height > 0;

    if (!hasEnd && !hasMid) {
        return;
    }

    boss_ui_prepare_sprite_ui_pipe(true);

    int frameRight = layout->frameX + layout->frameW;

    int endW = hasEnd
        ? boss_ui_scaled_int((float)s_bossHealthBarEndSurf.width * BOSS_UI_HEALTH_FRAME_SCALE)
        : 0;

    int endH = hasEnd
        ? boss_ui_scaled_int((float)s_bossHealthBarEndSurf.height * BOSS_UI_HEALTH_FRAME_SCALE)
        : 0;

    int midSrcW = hasMid ? s_bossHealthBarMidSurf.width : 1;
    int midSrcH = hasMid ? s_bossHealthBarMidSurf.height : 1;

    int midH = hasMid
        ? boss_ui_scaled_int((float)midSrcH * BOSS_UI_HEALTH_FRAME_SCALE)
        : endH;

    int endY = layout->frameY;
    int midY = layout->frameY;

    if (hasEnd && endH < layout->frameH) {
        endY = layout->frameY + ((layout->frameH - endH) / 2);
    }

    if (hasMid && midH < layout->frameH) {
        midY = layout->frameY + ((layout->frameH - midH) / 2);
    }

    int midLeft = layout->frameX + endW;
    int midRight = frameRight - endW;
    int midDrawW = midRight - midLeft;

    if (hasEnd) {
        rdpq_sprite_blit(s_bossHealthBarEndSprite, layout->frameX, endY, &(rdpq_blitparms_t){
            .scale_x = BOSS_UI_HEALTH_FRAME_SCALE,
            .scale_y = BOSS_UI_HEALTH_FRAME_SCALE,
        });
    }

    if (hasMid && midDrawW > 0) {
        float midScaleX = (float)midDrawW / (float)midSrcW;

        rdpq_sprite_blit(s_bossHealthBarMidSprite, midLeft, midY, &(rdpq_blitparms_t){
            .scale_x = midScaleX,
            .scale_y = BOSS_UI_HEALTH_FRAME_SCALE,
        });
    }

    if (hasEnd) {
        // Reuse the same end-cap sprite, mirrored horizontally for the right side.
        rdpq_sprite_blit(s_bossHealthBarEndSprite, frameRight - endW, endY, &(rdpq_blitparms_t){
            .scale_x = BOSS_UI_HEALTH_FRAME_SCALE,
            .scale_y = BOSS_UI_HEALTH_FRAME_SCALE,
            .flip_x = true,
        });
    }
}

static void boss_ui_draw_health_fill(
    const BossUiHealthLayout *layout,
    float ratio,
    float flash,
    float introProgress
) {
    if (!layout) return;
    if (layout->fillW <= 0 || layout->fillH <= 0) return;

    boss_ui_prepare_flat_ui_pipe();

    int barLeft = layout->fillX;
    int barRight = layout->fillX + layout->fillW;
    int top = layout->fillY;
    int bottom = layout->fillY + layout->fillH;

    int center = (barLeft + barRight) / 2;
    int halfWidth = (barRight - barLeft) / 2;

    int revealLeft = center - (int)((float)halfWidth * introProgress);
    int revealRight = center + (int)((float)halfWidth * introProgress);

    if (revealRight > revealLeft) {
        rdpq_set_prim_color(RGBA32(50, 50, 50, 130));
        rdpq_fill_rectangle(revealLeft, top, revealRight, bottom);
    }

    int fillEnd = barLeft + (int)((float)(barRight - barLeft) * ratio);
    int lossEnd = barLeft + (int)((float)(barRight - barLeft) * s_bossHealthBarState.lossTrail);
    int gainEnd = barLeft + (int)((float)(barRight - barLeft) * s_bossHealthBarState.gainTrail);

    int clipLeft = (revealLeft > barLeft) ? revealLeft : barLeft;
    int clipRight = (revealRight < barRight) ? revealRight : barRight;

    int alpha = 170;

    // Recent-damage segment.
    int lossClipLeft = (fillEnd > clipLeft) ? fillEnd : clipLeft;
    int lossClipRight = (lossEnd < clipRight) ? lossEnd : clipRight;

    if (lossClipRight > lossClipLeft) {
        rdpq_set_prim_color(RGBA32(230, 200, 60, alpha));
        rdpq_fill_rectangle(lossClipLeft, top, lossClipRight, bottom);
    }

    // Health fill.
    int red = 200 + (int)(55.0f * flash);
    int green = 30 + (int)(20.0f * flash);
    int blue = 30 + (int)(20.0f * flash);

    rdpq_set_prim_color(RGBA32(red, green, blue, alpha));

    int fillClipRight = (fillEnd < clipRight) ? fillEnd : clipRight;

    if (fillClipRight > clipLeft) {
        rdpq_fill_rectangle(clipLeft, top, fillClipRight, bottom);
    }

    // Recent-heal segment.
    int gainClipLeft = (gainEnd > clipLeft) ? gainEnd : clipLeft;
    int gainClipRight = (fillEnd < clipRight) ? fillEnd : clipRight;

    if (gainClipRight > gainClipLeft) {
        rdpq_set_prim_color(RGBA32(240, 240, 240, alpha));
        rdpq_fill_rectangle(gainClipLeft, top, gainClipRight, bottom);
    }
}

static void boss_ui_draw_health_name(const BossUiHealthLayout *layout)
{
    if (!layout) return;

    if (!s_bossHealthBarNameSprite || s_bossHealthBarNameSurf.width <= 0) {
        return;
    }

    boss_ui_prepare_sprite_ui_pipe(true);

    rdpq_sprite_blit(s_bossHealthBarNameSprite, layout->nameX, layout->nameY, &(rdpq_blitparms_t){
        .scale_x = BOSS_UI_NAME_SCALE,
        .scale_y = BOSS_UI_NAME_SCALE,
    });
}

void boss_ui_init(void)
{
    s_bossUiIntro = 1.0f;
    boss_ui_reset_bar_trail(&s_bossHealthBarState, 1.0f);

    // IA8 lock-on icon so the alpha gradient is preserved.
    boss_ui_load_sprite(
        &s_zTargetIconSprite,
        &s_zTargetIconSurf,
        "rom:/ztargetIcon.ia8.sprite"
    );

    // Segmented decorative boss health bar frame.
    boss_ui_load_sprite(
        &s_bossHealthBarEndSprite,
        &s_bossHealthBarEndSurf,
        "rom:/ui/healthbars/boss/boss_background_healthbar_end.ia8.sprite"
    );

    boss_ui_load_sprite(
        &s_bossHealthBarMidSprite,
        &s_bossHealthBarMidSurf,
        "rom:/ui/healthbars/boss/boss_background_healthbar_mid.ia8.sprite"
    );

    // Boss name plate.
    boss_ui_load_sprite(
        &s_bossHealthBarNameSprite,
        &s_bossHealthBarNameSurf,
        "rom:/ui/healthbars/boss/guardian_of_the_shackled_sun.ia4.sprite"
    );
}

void boss_ui_cleanup(void)
{
    boss_ui_free_sprite(&s_zTargetIconSprite, &s_zTargetIconSurf);
    boss_ui_free_sprite(&s_bossHealthBarEndSprite, &s_bossHealthBarEndSurf);
    boss_ui_free_sprite(&s_bossHealthBarMidSprite, &s_bossHealthBarMidSurf);
    boss_ui_free_sprite(&s_bossHealthBarNameSprite, &s_bossHealthBarNameSurf);

    s_bossUiIntro = 1.0f;
    boss_ui_reset_bar_trail(&s_bossHealthBarState, 1.0f);
}

void boss_ui_reset(void)
{
    s_bossUiIntro = 1.0f;
    boss_ui_reset_bar_trail(&s_bossHealthBarState, 1.0f);
}

void boss_ui_set_intro(float progress)
{
    s_bossUiIntro = boss_ui_clampf(progress, 0.0f, 1.0f);
}

void boss_ui_snap_health_trail(float ratio)
{
    boss_ui_reset_bar_trail(&s_bossHealthBarState, ratio);
}

void boss_ui_draw_health_bar(const char *name, float ratio, float flash)
{
    (void)name;

    ratio = boss_ui_clampf(ratio, 0.0f, 1.0f);
    flash = boss_ui_clampf(flash, 0.0f, 1.0f);

    BossUiHealthLayout layout = boss_ui_make_health_layout();

    boss_ui_update_bar_trails(ratio, &s_bossHealthBarState);

    // Draw order:
    // 1. Health fill/trails.
    // 2. Decorative frame overlay.
    // 3. Boss name above the frame.
    boss_ui_draw_health_fill(&layout, ratio, flash, s_bossUiIntro);
    boss_ui_draw_health_frame(&layout);
    boss_ui_draw_health_name(&layout);
}

void boss_ui_draw_lockon_marker(
    T3DViewport *viewport,
    const T3DVec3 *worldPos,
    bool visible
) {
    if (!visible) return;
    if (!viewport || !worldPos) return;

    T3DVec3 screenPos;
    if (!boss_ui_world_to_screen(viewport, worldPos, &screenPos, 8)) {
        return;
    }

    int px = (int)screenPos.v[0];
    int py = (int)screenPos.v[1];

    rdpq_set_mode_standard();

    if (s_zTargetIconSprite && s_zTargetIconSurf.width > 0 && s_zTargetIconSurf.height > 0) {
        // Avoid alpha-compare clipping; rely on IA8 sprite alpha.
        rdpq_mode_alphacompare(0);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

        // Scale with depth so the marker behaves like a world-space attachment.
        float z01 = screenPos.v[2];

        if (z01 < 0.0f) {
            z01 = 0.0f;
        }

        if (z01 > 0.9999f) {
            z01 = 0.9999f;
        }

        // Keep these in sync with camera projection.
        const float nearClip = 4.0f;
        const float farClip  = 2000.0f;

        float z = nearClip + z01 * (farClip - nearClip);

        if (z < nearClip) {
            z = nearClip;
        }

        // Reference distance where the icon is about 8x8 on screen.
        const float zRef = 300.0f;

        float scale = 0.125f * (zRef / z);

        if (scale < 0.05f) {
            scale = 0.05f;
        }

        if (scale > 0.275f) {
            scale = 0.275f;
        }

        rdpq_sprite_blit(s_zTargetIconSprite, px, py, &(rdpq_blitparms_t){
            .scale_x = scale,
            .scale_y = scale,
            .cx = s_zTargetIconSurf.width / 2,
            .cy = s_zTargetIconSurf.height / 2,
        });
    } else {
        // Fallback marker if the sprite failed to load.
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));

        const int halfSize = 3;

        rdpq_fill_rectangle(
            px - halfSize,
            py - halfSize,
            px + halfSize + 1,
            py + halfSize + 1
        );
    }
}

void boss_ui_draw_post_boss_a_prompt(
    T3DViewport *viewport,
    const T3DVec3 *worldPos,
    bool visible
) {
    if (!visible) return;
    if (!viewport || !worldPos) return;
    if (!button_prompt_has_a_button()) return;

    T3DVec3 promptWorldPos = *worldPos;

    // Small lift so the prompt does not intersect the head/target point.
    promptWorldPos.v[1] += 12.0f;

    T3DVec3 screenPos;
    if (!boss_ui_world_to_screen(viewport, &promptWorldPos, &screenPos, 16)) {
        return;
    }

    int px = (int)screenPos.v[0];
    int py = (int)screenPos.v[1];

    button_prompt_draw_a_icon_centered(px, py, 20.0f);
}