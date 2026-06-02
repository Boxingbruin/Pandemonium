#include "boss_ui.h"

#include <libdragon.h>

#include "../../utilities/globals.h"
#include "../../utilities/game_time.h"
#include "../../utilities/video_layout.h"
#include "../../utilities/button_prompt_utility.h"

#define BOSS_UI_TRAIL_HOLD_TIME 0.35f
#define BOSS_UI_TRAIL_DECAY_RATE 0.6f

typedef struct {
    float lossTrail;
    float gainTrail;
    float lastRatio;
    float lossHold;
    float gainHold;
} BossUiBarTrailState;

static sprite_t* s_zTargetIconSprite = NULL;
static surface_t s_zTargetIconSurf = {0};

static sprite_t* s_bossHealthBarBackgroundSprite = NULL;
static surface_t s_bossHealthBarBackgroundSurf = {0};

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

    // Decorative boss health bar frame.
    boss_ui_load_sprite(
        &s_bossHealthBarBackgroundSprite,
        &s_bossHealthBarBackgroundSurf,
        "rom:/ui/healthbars/boss/boss_background_healthbar.sprite"
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
    boss_ui_free_sprite(&s_bossHealthBarBackgroundSprite, &s_bossHealthBarBackgroundSurf);
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

    // Reset pipeline so UI colors are not affected by 3D fog/lighting state.
    rdpq_sync_pipe();
    rdpq_set_mode_standard();

#ifdef RDPQ_FOG_DISABLED
    rdpq_mode_fog(RDPQ_FOG_DISABLED);
#else
    rdpq_mode_fog(0);
#endif

    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    const int marginY = ui_safe_margin_y();
    const int bossBarWidth = 220;

    int left = (SCREEN_WIDTH - bossBarWidth) / 2;
    int right = left + bossBarWidth;

    const int fillInsetX = 18;

    int barLeft = left + fillInsetX;
    int barRight = right - fillInsetX;

    if (barRight <= barLeft) {
        barLeft = left;
        barRight = right;
    }

    const int fillHeight = 8;

    float frameScale = 1.0f;
    int frameY = SCREEN_HEIGHT - marginY - 24;
    int fillCenterY = frameY + 12;

    if (s_bossHealthBarBackgroundSprite && s_bossHealthBarBackgroundSurf.width > 0) {
        frameScale = (float)(right - left) / (float)s_bossHealthBarBackgroundSurf.width;

        int frameHeight = (int)((float)s_bossHealthBarBackgroundSurf.height * frameScale);

        frameY = SCREEN_HEIGHT - marginY - frameHeight;
        fillCenterY = frameY + frameHeight / 2;
    }

    int top = fillCenterY - fillHeight / 2 + 2;
    int bottom = top + fillHeight;

    float p = s_bossUiIntro;
    int alpha = 170;

    // Width reveal from center outward based on intro progress.
    int center = (barLeft + barRight) / 2;
    int halfWidth = (barRight - barLeft) / 2;

    int revealLeft = center - (int)((float)halfWidth * p);
    int revealRight = center + (int)((float)halfWidth * p);

    if (revealRight > revealLeft) {
        rdpq_set_prim_color(RGBA32(50, 50, 50, 130));
        rdpq_fill_rectangle(revealLeft, top, revealRight, bottom);
    }

    boss_ui_update_bar_trails(ratio, &s_bossHealthBarState);

    int fillEnd = barLeft + (int)((float)(barRight - barLeft) * ratio);
    int lossEnd = barLeft + (int)((float)(barRight - barLeft) * s_bossHealthBarState.lossTrail);
    int gainEnd = barLeft + (int)((float)(barRight - barLeft) * s_bossHealthBarState.gainTrail);

    int clipLeft = (revealLeft > barLeft) ? revealLeft : barLeft;
    int clipRight = (revealRight < barRight) ? revealRight : barRight;

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

    // Decorative frame overlay.
    if (s_bossHealthBarBackgroundSprite && s_bossHealthBarBackgroundSurf.width > 0) {
        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        rdpq_mode_alphacompare(1);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

        rdpq_sprite_blit(s_bossHealthBarBackgroundSprite, left, frameY, &(rdpq_blitparms_t){
            .scale_x = frameScale,
            .scale_y = frameScale,
        });
    }

    // Boss name plate.
    if (s_bossHealthBarNameSprite && s_bossHealthBarNameSurf.width > 0) {
        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        rdpq_mode_alphacompare(1);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

        const float nameScale = 0.5f;

        int nameHeight = (int)((float)s_bossHealthBarNameSurf.height * nameScale);
        int nameY = frameY - nameHeight;

        rdpq_sprite_blit(s_bossHealthBarNameSprite, left + 12, nameY + 12, &(rdpq_blitparms_t){
            .scale_x = nameScale,
            .scale_y = nameScale,
        });
    }
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

    rdpq_sync_pipe();
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