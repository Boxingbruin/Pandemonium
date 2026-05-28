#include "cutscene_manager.h"
#include "cutscene_manager_internal.h"

#include <libdragon.h>
#include <rdpq.h>
#include <rdpq_mode.h>
#include <rdpq_sprite.h>
#include <rdpq_text.h>

#include <t3d/t3d.h>

#include "globals.h"
#include "video_layout.h"
#include "scene.h"

#include "cutscene_guardian_phase1.h"
#include "cutscene_guardian_phase2.h"

// ----------------------------------------------------------------------------
// State
// ----------------------------------------------------------------------------

CutsceneState cutsceneState        = CUTSCENE_NONE;
float         cutsceneTimer        = 0.0f;
float         cutsceneCameraTimer  = 0.0f;

T3DVec3 cutsceneCamPosStart = {{0}};
T3DVec3 cutsceneCamPosEnd   = {{0}};

bool cutsceneDialogActive    = false;
bool phase2CutsceneTriggered = false;

bool skipButtonVisible       = false;
bool lastCutsceneAPressed    = false;

int  bossPostDefeatDialogStep = 0;
int  bossPostDefeatChatIndex  = 0;

// ----------------------------------------------------------------------------
// Post-boss dialog strings
// ----------------------------------------------------------------------------

static const PostBossChat s_postBossChats[] = {
    { "<Released... ^I am no longer\nbound by the kings will.~ ",
      "^Restore these shattered\nlands... The king must fall...~ ", 4.0f },
    { "^Go now",                  NULL, 3.0f },
    { "^Do you ~mock^ me?",        NULL, 3.0f },
    { "<Why are you still here",   NULL, 3.5f },
    { ">You did not heed my call\nLet the king deal with you...~", NULL, 4.0f },
    { ">Leave~...",               NULL, 2.5f },
};

int cutscene_manager_post_boss_chat_count(void)
{
    return (int)(sizeof(s_postBossChats) / sizeof(s_postBossChats[0]));
}

const PostBossChat *cutscene_manager_get_post_boss_chat(int idx)
{
    int count = cutscene_manager_post_boss_chat_count();

    if (idx < 0) idx = 0;
    if (idx >= count) idx = count - 1;

    return &s_postBossChats[idx];
}

// ----------------------------------------------------------------------------
// Lifecycle
// ----------------------------------------------------------------------------

void cutscene_manager_init(void)
{
    /*
     * No heavy cutscene assets are loaded here anymore.
     *
     * Guardian phase 1 and phase 2 load/unload their own cinematic assets.
     */
}

void cutscene_manager_cleanup(void)
{
    cutscene_guardian_phase1_unload();
    cutscene_guardian_phase2_unload();
}

void cutscene_manager_reset(void)
{
    cutsceneState             = CUTSCENE_NONE;
    cutsceneTimer             = 0.0f;
    cutsceneCameraTimer       = 0.0f;

    cutsceneDialogActive      = false;
    skipButtonVisible         = false;
    lastCutsceneAPressed      = false;

    bossPostDefeatDialogStep  = 0;
    bossPostDefeatChatIndex   = 0;

    phase2CutsceneTriggered   = false;

    cutscene_guardian_phase1_unload();
    cutscene_guardian_phase2_unload();
}

// ----------------------------------------------------------------------------
// Queries
// ----------------------------------------------------------------------------

CutsceneState cutscene_manager_get_state(void)
{
    return cutsceneState;
}

bool cutscene_manager_is_active(void)
{
    return cutsceneState != CUTSCENE_NONE;
}

bool cutscene_manager_is_dialog_active(void)
{
    return cutsceneDialogActive;
}

bool cutscene_manager_is_skip_visible(void)
{
    return skipButtonVisible;
}

bool cutscene_manager_phase2_triggered(void)
{
    return phase2CutsceneTriggered;
}

void cutscene_manager_mark_phase2_triggered(void)
{
    phase2CutsceneTriggered = true;
}

// ----------------------------------------------------------------------------
// Fog ranges per cutscene state.
// ----------------------------------------------------------------------------

void cutscene_manager_draw_fog(void)
{
    if (cutscene_guardian_phase1_handles(cutsceneState)) {
        cutscene_guardian_phase1_draw_fog();
        return;
    }

    if (cutscene_guardian_phase2_handles(cutsceneState)) {
        cutscene_guardian_phase2_draw_fog();
        return;
    }
}

// ----------------------------------------------------------------------------
// A-button + "skip" overlay. Drawn during in-engine cutscenes AND during FMV.
// ----------------------------------------------------------------------------

void cutscene_manager_draw_skip_overlay(void)
{
    sprite_t *sprite = scene_get_a_button_sprite();
    if (!sprite) return;

    bool isTitleTransition = (scene_get_game_state() == GAME_STATE_TITLE_TRANSITION);
    bool isCutsceneActive  = cutscene_manager_is_active();

    if (!skipButtonVisible || (!isCutsceneActive && !isTitleTransition)) {
        return;
    }

    surface_t surf = sprite_get_pixels(sprite);

    const float kTargetPx = 20.0f;
    int srcMax = (surf.width > surf.height) ? surf.width : surf.height;
    float s = (srcMax > 0) ? (kTargetPx / (float)srcMax) : 1.0f;

    int buttonWidth  = (int)((float)surf.width  * s);
    int buttonHeight = (int)((float)surf.height * s);

    const int marginX = ui_safe_margin_x();
    const int marginY = ui_safe_margin_y();

    int buttonX = SCREEN_WIDTH  - buttonWidth  - marginX;
    int buttonY = SCREEN_HEIGHT - buttonHeight - marginY;

    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(0);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_filter(FILTER_BILINEAR);

    rdpq_sprite_blit(sprite, buttonX, buttonY, &(rdpq_blitparms_t){
        .scale_x = s,
        .scale_y = s,
    });

    const int gap = 6;
    const int textRight = buttonX - gap;

    if (textRight > 0) {
        const int textY = buttonY + (buttonHeight / 2) + 6;

        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));

        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_RIGHT,
            .width = textRight,
            .wrap  = WRAP_WORD,
        }, FONT_UNBALANCED, 0, textY, "%s", "skip");
    }
}