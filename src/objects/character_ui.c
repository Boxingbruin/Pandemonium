#include "character_ui.h"

#include <libdragon.h>

#include "character.h"
#include "../utilities/game_time.h"
#include "../utilities/globals.h"

#include "../utilities/video_layout.h"

#define CHARACTER_UI_TRAIL_HOLD_TIME 0.35f
#define CHARACTER_UI_TRAIL_DECAY_RATE 0.6f

typedef struct {
    float lossTrail;
    float gainTrail;
    float lastRatio;
    float lossHold;
    float gainHold;
} CharacterUiBarTrailState;

static sprite_t* s_cUpSprite = NULL;
static sprite_t* s_cDownSprite = NULL;
static sprite_t* s_cLeftSprite = NULL;
static sprite_t* s_cRightSprite = NULL;

static surface_t s_cUpSurf = {0};
static surface_t s_cDownSurf = {0};
static surface_t s_cLeftSurf = {0};
static surface_t s_cRightSurf = {0};

static sprite_t* s_healthBottleSprite = NULL;
static surface_t s_healthBottleSurf = {0};

static float s_playerUiIntro = 1.0f;

static CharacterUiBarTrailState s_playerHealthBarState  = {1.0f, 1.0f, 1.0f, 0.0f, 0.0f};
static CharacterUiBarTrailState s_playerStaminaBarState = {1.0f, 1.0f, 1.0f, 0.0f, 0.0f};

static float character_ui_clampf(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static void character_ui_reset_bar_trail(CharacterUiBarTrailState *s, float ratio)
{
    if (!s) return;

    ratio = character_ui_clampf(ratio, 0.0f, 1.0f);

    s->lossTrail = ratio;
    s->gainTrail = ratio;
    s->lastRatio = ratio;
    s->lossHold = 0.0f;
    s->gainHold = 0.0f;
}

static void character_ui_update_bar_trails(float ratio, CharacterUiBarTrailState *s)
{
    if (!s) return;

    ratio = character_ui_clampf(ratio, 0.0f, 1.0f);

    if (ratio < s->lastRatio) {
        // Just lost health/stamina.
        // Refresh loss hold and snap gain trail forward so stale healing
        // highlight does not reappear behind new damage/use.
        s->lossHold = CHARACTER_UI_TRAIL_HOLD_TIME;

        if (s->gainTrail < ratio) {
            s->gainTrail = ratio;
        }
    } else if (ratio > s->lastRatio) {
        // Just gained health/stamina.
        // Refresh gain hold and snap loss trail up to current.
        s->gainHold = CHARACTER_UI_TRAIL_HOLD_TIME;

        if (s->lossTrail < ratio) {
            s->lossTrail = ratio;
        }
    }

    s->lastRatio = ratio;

    // Clamp trails to valid sides of the current ratio.
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
        s->lossTrail -= CHARACTER_UI_TRAIL_DECAY_RATE * deltaTime;

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
        s->gainTrail += CHARACTER_UI_TRAIL_DECAY_RATE * deltaTime;

        if (s->gainTrail > ratio) {
            s->gainTrail = ratio;
        }
    }
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

void character_ui_init(void)
{
    s_playerUiIntro = 1.0f;

    character_ui_reset_bar_trail(&s_playerHealthBarState, 1.0f);
    character_ui_reset_bar_trail(&s_playerStaminaBarState, 1.0f);

    // C-left uses the empty variant because the health potion graphic is drawn
    // over it. The other three show their arrow icon/unassigned slot.
    character_ui_load_sprite(&s_cUpSprite,    &s_cUpSurf,    "rom:/buttons/CUp.sprite");
    character_ui_load_sprite(&s_cDownSprite,  &s_cDownSurf,  "rom:/buttons/CDown.sprite");
    character_ui_load_sprite(&s_cLeftSprite,  &s_cLeftSurf,  "rom:/buttons/CButton_empty.sprite");
    character_ui_load_sprite(&s_cRightSprite, &s_cRightSurf, "rom:/buttons/CRight.sprite");

    // Health potion bottle icon.
    character_ui_load_sprite(&s_healthBottleSprite, &s_healthBottleSurf, "rom:/healthBottle.ia8.sprite");
}

void character_ui_cleanup(void)
{
    character_ui_free_sprite(&s_cUpSprite,    &s_cUpSurf);
    character_ui_free_sprite(&s_cDownSprite,  &s_cDownSurf);
    character_ui_free_sprite(&s_cLeftSprite,  &s_cLeftSurf);
    character_ui_free_sprite(&s_cRightSprite, &s_cRightSurf);

    character_ui_free_sprite(&s_healthBottleSprite, &s_healthBottleSurf);

    s_playerUiIntro = 1.0f;

    character_ui_reset_bar_trail(&s_playerHealthBarState, 1.0f);
    character_ui_reset_bar_trail(&s_playerStaminaBarState, 1.0f);
}

void character_ui_reset(void)
{
    s_playerUiIntro = 1.0f;

    character_ui_reset_bar_trail(&s_playerHealthBarState, 1.0f);
    character_ui_reset_bar_trail(&s_playerStaminaBarState, 1.0f);
}

void character_ui_set_intro(float progress)
{
    s_playerUiIntro = character_ui_clampf(progress, 0.0f, 1.0f);
}

void character_ui_draw_health_bar(const char *name, float ratio, float flash)
{
    (void)name;

    ratio = character_ui_clampf(ratio, 0.0f, 1.0f);
    flash = character_ui_clampf(flash, 0.0f, 1.0f);

    rdpq_sync_pipe();
    rdpq_set_mode_standard();

#ifdef RDPQ_FOG_DISABLED
    rdpq_mode_fog(RDPQ_FOG_DISABLED);
#else
    rdpq_mode_fog(0);
#endif

    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    const int marginX = ui_safe_margin_x();
    const int marginY = ui_safe_margin_y();

    const int barWidth = 120;
    const int barHeight = 10;

    float p = s_playerUiIntro;
    int slideDist = 40;
    int yOffset = (int)((1.0f - p) * (float)slideDist);

    int left = marginX;
    int top = marginY + 4 - yOffset;
    int bottom = top + barHeight;
    int right = left + barWidth;

    // Background.
    rdpq_set_prim_color(RGBA32(35, 35, 35, 160));
    rdpq_fill_rectangle(left, top, right, bottom);

    // Inner empty region.
    rdpq_set_prim_color(RGBA32(10, 10, 10, 190));
    rdpq_fill_rectangle(left + 1, top + 1, right - 1, bottom - 1);

    character_ui_update_bar_trails(ratio, &s_playerHealthBarState);

    int red = 200 + (int)(55.0f * flash);
    int green = 35 + (int)(45.0f * flash);
    int blue = 35 + (int)(45.0f * flash);

    int fillRight = left + 2 + (int)((float)(barWidth - 4) * ratio);
    int lossRight = left + 2 + (int)((float)(barWidth - 4) * s_playerHealthBarState.lossTrail);
    int gainLeft = left + 2 + (int)((float)(barWidth - 4) * s_playerHealthBarState.gainTrail);

    // Recent-damage segment.
    if (lossRight > fillRight) {
        rdpq_set_prim_color(RGBA32(230, 200, 60, 220));
        rdpq_fill_rectangle(fillRight, top + 2, lossRight, bottom - 2);
    }

    // Health fill.
    if (fillRight > left + 2) {
        rdpq_set_prim_color(RGBA32(red, green, blue, 220));
        rdpq_fill_rectangle(left + 2, top + 2, fillRight, bottom - 2);
    }

    // Recent-heal highlight.
    if (gainLeft < fillRight) {
        rdpq_set_prim_color(RGBA32(240, 240, 240, 220));
        rdpq_fill_rectangle(gainLeft, top + 2, fillRight, bottom - 2);
    }

    // Light frame.
    rdpq_set_prim_color(RGBA32(210, 210, 210, 180));
    rdpq_fill_rectangle(left, top, right, top + 1);
    rdpq_fill_rectangle(left, bottom - 1, right, bottom);
    rdpq_fill_rectangle(left, top, left + 1, bottom);
    rdpq_fill_rectangle(right - 1, top, right, bottom);
}

void character_ui_draw_stamina_bar(float ratio)
{
    ratio = character_ui_clampf(ratio, 0.0f, 1.0f);

    rdpq_sync_pipe();
    rdpq_set_mode_standard();

#ifdef RDPQ_FOG_DISABLED
    rdpq_mode_fog(RDPQ_FOG_DISABLED);
#else
    rdpq_mode_fog(0);
#endif

    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    const int marginX = ui_safe_margin_x();
    const int marginY = ui_safe_margin_y();

    const int healthBarWidth = 120;
    const int healthBarHeight = 10;

    const int barWidth = (healthBarWidth * 3) / 4;
    const int barHeight = 6;

    float p = s_playerUiIntro;
    int slideDist = 40;
    int yOffset = (int)((1.0f - p) * (float)slideDist);

    // Sit flush against the bottom of the health bar so the borders share a line.
    int healthBarBottom = marginY + 4 + healthBarHeight - yOffset;

    int left = marginX;
    int top = healthBarBottom;
    int bottom = top + barHeight;
    int right = left + barWidth;

    // Background.
    rdpq_set_prim_color(RGBA32(35, 35, 35, 160));
    rdpq_fill_rectangle(left, top, right, bottom);

    // Inner empty region.
    rdpq_set_prim_color(RGBA32(10, 10, 10, 190));
    rdpq_fill_rectangle(left + 1, top + 1, right - 1, bottom - 1);

    character_ui_update_bar_trails(ratio, &s_playerStaminaBarState);

    int fillWidth = (int)((float)(barWidth - 4) * ratio);
    int lossWidth = (int)((float)(barWidth - 4) * s_playerStaminaBarState.lossTrail);
    int gainWidth = (int)((float)(barWidth - 4) * s_playerStaminaBarState.gainTrail);

    // Recently-used segment.
    if (lossWidth > fillWidth) {
        rdpq_set_prim_color(RGBA32(230, 200, 60, 220));
        rdpq_fill_rectangle(left + 2 + fillWidth, top + 2, left + 2 + lossWidth, bottom - 2);
    }

    // Stamina fill.
    // Require at least 2px before rendering so sub-pixel residue does not leave
    // a sliver when the bar is empty.
    if (fillWidth >= 2) {
        rdpq_set_prim_color(RGBA32(60, 200, 80, 220));
        rdpq_fill_rectangle(left + 2, top + 2, left + 2 + fillWidth, bottom - 2);
    }

    // Recently-regenerated highlight.
    if (gainWidth < fillWidth) {
        rdpq_set_prim_color(RGBA32(240, 240, 240, 220));
        rdpq_fill_rectangle(left + 2 + gainWidth, top + 2, left + 2 + fillWidth, bottom - 2);
    }

    // Light frame.
    rdpq_set_prim_color(RGBA32(210, 210, 210, 180));
    rdpq_fill_rectangle(left, top, right, top + 1);
    rdpq_fill_rectangle(left, bottom - 1, right, bottom);
    rdpq_fill_rectangle(left, top, left + 1, bottom);
    rdpq_fill_rectangle(right - 1, top, right, bottom);
}

void character_ui_draw_c_buttons(void)
{
    // Bottom-left C-button diamond.
    // This should be called by scene after the 3D/world pass, with the rest of
    // the 2D UI.
    if (!s_cLeftSprite) return;

    int w = (s_cLeftSurf.width > 0) ? s_cLeftSurf.width : 24;
    int h = (s_cLeftSurf.height > 0) ? s_cLeftSurf.height : 24;

    // Target on-screen button size. Source sprites are currently 64x64.
    const float targetButtonPx = 20.0f;
    int srcMax = (w > h) ? w : h;

    float cScale = (srcMax > 0)
        ? (targetButtonPx / (float)srcMax)
        : 1.0f;

    int drawW = (int)((float)w * cScale);
    int drawH = (int)((float)h * cScale);

    const int marginX = ui_safe_margin_x();
    const int marginY = ui_safe_margin_y();

    // Diamond spacing. The source sprites have transparent padding, so this
    // keeps the visible button edges closer together.
    const float spacingFrac = 0.7f;
    int spacingX = (int)((float)drawW * spacingFrac);
    int spacingY = (int)((float)drawH * spacingFrac);

    // Diamond center. Enough room from safe bounds that C-left and C-down sit
    // near the bottom-left corner.
    int centerX = marginX + spacingX + (drawW / 2);
    int centerY = SCREEN_HEIGHT - marginY - spacingY - (drawH / 2);

    int leftX  = centerX - spacingX;
    int leftY  = centerY;

    int rightX = centerX + spacingX;
    int rightY = centerY;

    int upX    = centerX;
    int upY    = centerY - spacingY;

    int downX  = centerX;
    int downY  = centerY + spacingY;

    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(0);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_filter(FILTER_BILINEAR);

    if (s_cUpSprite && s_cUpSurf.width > 0 && s_cUpSurf.height > 0) {
        rdpq_sprite_blit(s_cUpSprite, upX, upY, &(rdpq_blitparms_t){
            .scale_x = cScale,
            .scale_y = cScale,
            .cx = s_cUpSurf.width / 2,
            .cy = s_cUpSurf.height / 2,
        });
    }

    if (s_cDownSprite && s_cDownSurf.width > 0 && s_cDownSurf.height > 0) {
        rdpq_sprite_blit(s_cDownSprite, downX, downY, &(rdpq_blitparms_t){
            .scale_x = cScale,
            .scale_y = cScale,
            .cx = s_cDownSurf.width / 2,
            .cy = s_cDownSurf.height / 2,
        });
    }

    if (s_cRightSprite && s_cRightSurf.width > 0 && s_cRightSurf.height > 0) {
        rdpq_sprite_blit(s_cRightSprite, rightX, rightY, &(rdpq_blitparms_t){
            .scale_x = cScale,
            .scale_y = cScale,
            .cx = s_cRightSurf.width / 2,
            .cy = s_cRightSurf.height / 2,
        });
    }

    if (s_cLeftSprite && s_cLeftSurf.width > 0 && s_cLeftSurf.height > 0) {
        rdpq_sprite_blit(s_cLeftSprite, leftX, leftY, &(rdpq_blitparms_t){
            .scale_x = cScale,
            .scale_y = cScale,
            .cx = s_cLeftSurf.width / 2,
            .cy = s_cLeftSurf.height / 2,
        });
    }

    // Only show the potion bottle + count while the player has potions.
    int potionCount = character_get_health_potion_count();

    if (potionCount <= 0) {
        return;
    }

    if (s_healthBottleSprite && s_healthBottleSurf.width > 0 && s_healthBottleSurf.height > 0) {
        float target = (float)((drawW < drawH) ? drawW : drawH) * 0.80f;
        float denom = (float)(
            (s_healthBottleSurf.width > s_healthBottleSurf.height)
                ? s_healthBottleSurf.width
                : s_healthBottleSurf.height
        );

        float bottleScale = (denom > 0.0f)
            ? (target / denom)
            : 1.0f;

        if (bottleScale < 0.05f) bottleScale = 0.05f;
        if (bottleScale > 4.0f)  bottleScale = 4.0f;

        // Tint the IA8 bottle sprite dark red so it contrasts against the
        // yellow button.
        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        rdpq_mode_alphacompare(1);
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_set_prim_color(RGBA32(180, 30, 30, 255));

        rdpq_sprite_blit(s_healthBottleSprite, leftX, leftY, &(rdpq_blitparms_t){
            .scale_x = bottleScale,
            .scale_y = bottleScale,
            .cx = s_healthBottleSurf.width / 2,
            .cy = s_healthBottleSurf.height / 2,
        });
    }

    // Potion count.
    {
        const int textX = leftX + (drawW / 2) + 2;
        const int textY = leftY + 4;

        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(RGBA32(0, 0, 0, 255));

        rdpq_text_printf(NULL, FONT_UNBALANCED, textX, textY, "%d", potionCount);
    }
}

void character_ui_draw(void)
{
    float hp = character_get_health();
    float maxHp = character_get_max_health();
    float stamina = character_get_stamina();
    float maxStamina = character_get_max_stamina();

    float hpRatio = (maxHp > 0.0f) ? (hp / maxHp) : 0.0f;
    float staminaRatio = (maxStamina > 0.0f) ? (stamina / maxStamina) : 0.0f;

    // If your character module does not expose a damage flash value yet,
    // keep this at 0.0f. Later this should come from character visual feedback.
    float damageFlash = 0.0f;

    character_ui_draw_health_bar("Player", hpRatio, damageFlash);
    character_ui_draw_stamina_bar(staminaRatio);
    character_ui_draw_c_buttons();
}