#include "cutscene_manager.h"
#include "cutscene_manager_internal.h"

#include <libdragon.h>

#include <t3d/t3d.h>
#include <t3d/t3danim.h>

#include "globals.h"
#include "joypad_utility.h"

#include "../controllers/camera_controller.h"
#include "../controllers/dialog_controller.h"

#include "../scenes/guardian/guardian_cutscene_phase1.h"
#include "guardian_cutscene_phase2.h"

#include "../scenes/guardian/guardian_scene_context.h"

#include "../utilities/button_prompt_utility.h"

#include "../game/boss/boss.h"

// TODO: These should be removed once cutscene animations are handled directly
// by the boss scripts.
#include "../game/boss/boss_anim.h"

// ----------------------------------------------------------------------------
// Constants
// ----------------------------------------------------------------------------

#define CUTSCENE_CAMERA_FOV      1.544792654048f
#define CUTSCENE_CAMERA_DISTANCE 4.05f

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

    cutsceneCamPosStart       = (T3DVec3){{0.0f, 0.0f, 0.0f}};
    cutsceneCamPosEnd         = (T3DVec3){{0.0f, 0.0f, 0.0f}};

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
// Camera helpers
// ----------------------------------------------------------------------------

void cutscene_manager_set_camera_shot(
    SceneContext *ctx,
    T3DVec3 start,
    T3DVec3 end,
    T3DVec3 target
)
{
    (void)ctx;

    cutsceneCameraTimer = 0.0f;
    cutsceneCamPosStart = start;
    cutsceneCamPosEnd = end;

    camera_mode(CAMERA_CUSTOM);

    camera_initialize(
        &cutsceneCamPosStart,
        &(T3DVec3){{0, 0, 1}},
        CUTSCENE_CAMERA_FOV,
        CUTSCENE_CAMERA_DISTANCE
    );

    customCamTarget = target;
}

void cutscene_manager_update_camera(float duration)
{
    if (duration <= 0.0f) {
        customCamPos = cutsceneCamPosEnd;
        return;
    }

    float t = cutsceneCameraTimer / duration;
    if (t > 1.0f) t = 1.0f;

    float easeT = t * t * (3.0f - 2.0f * t);

    customCamPos.v[0] = cutsceneCamPosStart.v[0] + (cutsceneCamPosEnd.v[0] - cutsceneCamPosStart.v[0]) * easeT;
    customCamPos.v[1] = cutsceneCamPosStart.v[1] + (cutsceneCamPosEnd.v[1] - cutsceneCamPosStart.v[1]) * easeT;
    customCamPos.v[2] = cutsceneCamPosStart.v[2] + (cutsceneCamPosEnd.v[2] - cutsceneCamPosStart.v[2]) * easeT;
}

// ----------------------------------------------------------------------------
// Dialog helpers
// ----------------------------------------------------------------------------

void cutscene_manager_begin_dialog(const char *text, float holdSec)
{
    if (!text) {
        cutsceneDialogActive = false;
        return;
    }

    cutsceneDialogActive = true;

    dialog_controller_speak(
        text,
        0,
        holdSec,
        false,
        false
    );
}

void cutscene_manager_update_dialog(void)
{
    if (!cutsceneDialogActive) return;

    dialog_controller_update();
}

void cutscene_manager_draw_dialog(void)
{
    if (!cutsceneDialogActive) return;

    int height = 70;
    int width = 220;
    int x = (SCREEN_WIDTH - width) / 2;
    int y = 240 - height - 10;

    dialog_controller_draw(false, x, y, width, height);
}

void cutscene_manager_clear_dialog(void)
{
    cutsceneDialogActive = false;
    dialog_controller_stop_speaking();
}

// ----------------------------------------------------------------------------
// Boss helpers
// ----------------------------------------------------------------------------

void cutscene_manager_update_boss_transform(SceneContext *ctx)
{
    if (!ctx || !ctx->boss) return;

    boss_anim_update(ctx->boss);

    T3DMat4FP *mat = (T3DMat4FP*)ctx->boss->modelMat;
    if (mat) {
        t3d_mat4fp_from_srt_euler(
            mat,
            ctx->boss->scale,
            ctx->boss->rot,
            ctx->boss->pos
        );
    }
}

void cutscene_manager_set_boss_anim(SceneContext *ctx, int animIndex)
{
    if (!ctx || !ctx->boss || !ctx->boss->animations) return;

    T3DAnim **anims = (T3DAnim**)ctx->boss->animations;

    t3d_anim_set_playing(anims[animIndex], true);

    ctx->boss->currentAnimation = animIndex;
    ctx->boss->currentAnimState = animIndex;
}

// ----------------------------------------------------------------------------
// Phase routing
// ----------------------------------------------------------------------------

bool cutscene_manager_handles_guardian_cutscene(CutsceneState state)
{
    return cutscene_guardian_phase1_handles(state) ||
           cutscene_guardian_phase2_handles(state);
}

void cutscene_manager_enter(SceneContext *ctx, CutsceneState state)
{
    if (!ctx) return;

    skipButtonVisible = false;
    lastCutsceneAPressed = false;

    if (cutscene_guardian_phase1_handles(state)) {
        if (ctx->boss) {
            boss_activate_cinematic(ctx->boss);
        }

        cutscene_guardian_phase1_enter(ctx, state);
        return;
    }

    if (cutscene_guardian_phase2_handles(state)) {
        if (ctx->boss) {
            boss_activate_cinematic(ctx->boss);
        }

        cutscene_guardian_phase2_enter(ctx, state);
        return;
    }
}

void cutscene_manager_update(SceneContext *ctx, float dt)
{
    if (!ctx) return;

    if (cutscene_guardian_phase1_handles(cutsceneState)) {
        cutscene_guardian_phase1_update(ctx, dt);
        cutscene_manager_skip(ctx);
        return;
    }

    if (cutscene_guardian_phase2_handles(cutsceneState)) {
        cutscene_guardian_phase2_update(ctx, dt);
        cutscene_manager_skip(ctx);
        return;
    }
}

void cutscene_manager_draw(SceneContext *ctx, T3DViewport *viewport)
{
    if (!ctx) return;

    if (cutscene_guardian_phase1_handles(cutsceneState)) {
        cutscene_guardian_phase1_draw(ctx, viewport);
        return;
    }

    if (cutscene_guardian_phase2_handles(cutsceneState)) {
        cutscene_guardian_phase2_draw(ctx, viewport);
        return;
    }
}

void cutscene_manager_skip(SceneContext *ctx)
{
    if (!ctx) return;

    /*
     * CUTSCENE_POST_BOSS_RESTORED is still scene-owned for now.
     * It has special gameplay/dialog behavior and should not be skipped by
     * the phase skip flow.
     */
    if (cutsceneState == CUTSCENE_POST_BOSS_RESTORED) {
        skipButtonVisible = false;
        lastCutsceneAPressed = btn.a;
        return;
    }

    bool aCurrentlyPressed = btn.a;
    bool aJustPressed = aCurrentlyPressed && !lastCutsceneAPressed;

    if (aJustPressed) {
        if (!skipButtonVisible) {
            skipButtonVisible = true;
        } else {
            if (cutscene_guardian_phase1_handles(cutsceneState)) {
                cutscene_guardian_phase1_skip(ctx);
                return;
            }

            if (cutscene_guardian_phase2_handles(cutsceneState)) {
                cutscene_guardian_phase2_skip(ctx);
                return;
            }
        }
    }

    lastCutsceneAPressed = aCurrentlyPressed;
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
// A-button + "skip" overlay.
// The button_prompt_utility owns the A-button sprite and actual prompt drawing.
// The caller owns visibility because this prompt is used by both actual cutscenes
// and the title-screen fog-door transition.
// ----------------------------------------------------------------------------

void cutscene_manager_draw_skip_overlay(bool visible)
{
    if (!visible) {
        return;
    }

    button_prompt_draw_skip_bottom_right();
}