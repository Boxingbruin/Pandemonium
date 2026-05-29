#include "button_prompt_utility.h"

#include <libdragon.h>
#include <rdpq.h>
#include <rdpq_mode.h>
#include <rdpq_sprite.h>
#include <rdpq_text.h>

#include "globals.h"
#include "video_layout.h"

// ----------------------------------------------------------------------------
// State
// ----------------------------------------------------------------------------

static sprite_t *s_aButtonSprite = NULL;
static surface_t s_aButtonSurf = {0};

// ----------------------------------------------------------------------------
// Lifecycle
// ----------------------------------------------------------------------------

void button_prompt_init(void)
{
    if (s_aButtonSprite) return;

    s_aButtonSprite = sprite_load("rom:/buttons/A.sprite");
    if (s_aButtonSprite) {
        s_aButtonSurf = sprite_get_pixels(s_aButtonSprite);
    }
}

void button_prompt_cleanup(void)
{
    if (!s_aButtonSprite) return;

    sprite_free(s_aButtonSprite);
    s_aButtonSprite = NULL;

    surface_free(&s_aButtonSurf);
    s_aButtonSurf = (surface_t){0};
}

// ----------------------------------------------------------------------------
// Queries / sizing
// ----------------------------------------------------------------------------

bool button_prompt_has_a_button(void)
{
    return s_aButtonSprite &&
           s_aButtonSurf.width > 0 &&
           s_aButtonSurf.height > 0;
}

static float button_prompt_a_icon_scale(float targetPx)
{
    if (!button_prompt_has_a_button()) return 1.0f;

    int srcMax = (s_aButtonSurf.width > s_aButtonSurf.height)
        ? s_aButtonSurf.width
        : s_aButtonSurf.height;

    if (srcMax <= 0) return 1.0f;
    if (targetPx <= 0.0f) targetPx = 20.0f;

    return targetPx / (float)srcMax;
}

int button_prompt_a_icon_width(float targetPx)
{
    if (!button_prompt_has_a_button()) return 0;

    float s = button_prompt_a_icon_scale(targetPx);
    return (int)((float)s_aButtonSurf.width * s);
}

int button_prompt_a_icon_height(float targetPx)
{
    if (!button_prompt_has_a_button()) return 0;

    float s = button_prompt_a_icon_scale(targetPx);
    return (int)((float)s_aButtonSurf.height * s);
}

// ----------------------------------------------------------------------------
// Drawing
// ----------------------------------------------------------------------------

void button_prompt_draw_a_icon(int x, int y, float targetPx)
{
    if (!button_prompt_has_a_button()) return;

    float s = button_prompt_a_icon_scale(targetPx);

    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(0);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_filter(FILTER_BILINEAR);

    rdpq_sprite_blit(s_aButtonSprite, x, y, &(rdpq_blitparms_t){
        .scale_x = s,
        .scale_y = s,
    });
}

void button_prompt_draw_a_icon_centered(int centerX, int centerY, float targetPx)
{
    if (!button_prompt_has_a_button()) return;

    int w = button_prompt_a_icon_width(targetPx);
    int h = button_prompt_a_icon_height(targetPx);

    int x = centerX - (w / 2);
    int y = centerY - (h / 2);

    button_prompt_draw_a_icon(x, y, targetPx);
}

void button_prompt_draw_skip_bottom_right(void)
{
    if (!button_prompt_has_a_button()) return;

    const float targetPx = 20.0f;

    int buttonWidth = button_prompt_a_icon_width(targetPx);
    int buttonHeight = button_prompt_a_icon_height(targetPx);

    const int marginX = ui_safe_margin_x();
    const int marginY = ui_safe_margin_y();

    int buttonX = SCREEN_WIDTH  - buttonWidth  - marginX;
    int buttonY = SCREEN_HEIGHT - buttonHeight - marginY;

    button_prompt_draw_a_icon(buttonX, buttonY, targetPx);

    const int gap = 6;
    const int textRight = buttonX - gap;

    if (textRight > 0) {
        const int textY = buttonY + (buttonHeight / 2) + 6;

        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));

        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_RIGHT,
            .width = textRight,
            .wrap = WRAP_WORD,
        }, FONT_UNBALANCED, 0, textY, "%s", "skip");
    }
}