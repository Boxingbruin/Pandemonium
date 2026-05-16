#include "cutscene_manager.h"
#include "cutscene_manager_internal.h"

#include <libdragon.h>
#include <rdpq.h>
#include <rdpq_mode.h>
#include <rdpq_sprite.h>
#include <rdpq_text.h>
#include <t3d/t3d.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>
#include <t3d/t3dmodel.h>
#include <t3d/t3dmath.h>

#include "globals.h"
#include "video_layout.h"
#include "scene.h"

// ----------------------------------------------------------------------------
// State (owned by the manager; scene.c still mutates these via the internal
// header during the active cutscene state machine).
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

T3DModel*     cutsceneChainBreakModel      = NULL;
rspq_block_t* cutsceneChainBreakDpl        = NULL;
T3DMat4FP*    cutsceneChainBreakMatrix     = NULL;
T3DSkeleton*  cutsceneChainBreakSkeleton   = NULL;
T3DAnim**     cutsceneChainBreakAnimations = NULL;

// ----------------------------------------------------------------------------
// Dialog strings (cutscene-only, so they live here).
// ----------------------------------------------------------------------------
static const char *s_phase1Dialogs[] = {
    "^Those who approach the\nthrone of gold~ ^fall at my\nblade.",
    "^A Knight?~ >Where is your\n^loyalty...",
    "^Where is your...~ <Fear.",
};

static const char *s_phase2Dialogs[] = {
    "^Valiant effort...~ ^Knight.",
    "^Now leave these sundered\nhalls.",
    "^Before the endless night",
    "^Through shackled sun\ndeliverance...~ ^Forces my\nhand once more.",
    "~ <Burn.",
    "<BURN MY MORTAL FLESH!!",
};

const char *cutscene_manager_get_phase1_dialog(int idx) {
    int count = (int)(sizeof(s_phase1Dialogs) / sizeof(s_phase1Dialogs[0]));
    if (idx < 0 || idx >= count) return "";
    return s_phase1Dialogs[idx];
}

const char *cutscene_manager_get_phase2_dialog(int idx) {
    int count = (int)(sizeof(s_phase2Dialogs) / sizeof(s_phase2Dialogs[0]));
    if (idx < 0 || idx >= count) return "";
    return s_phase2Dialogs[idx];
}

// ----------------------------------------------------------------------------
// Lifecycle
// ----------------------------------------------------------------------------
void cutscene_manager_init(void)
{
    // Load the chain-break model used by PHASE1_BREAK_CHAINS.
    cutsceneChainBreakModel    = t3d_model_load("rom:/cutscene/shatter_chain.t3dm");
    cutsceneChainBreakSkeleton = malloc_uncached(sizeof(T3DSkeleton));
    *cutsceneChainBreakSkeleton = t3d_skeleton_create(cutsceneChainBreakModel);

    const char *names[] = {"ChainBreak"};
    const int   count   = 1;

    cutsceneChainBreakAnimations = malloc_uncached(count * sizeof(T3DAnim*));
    for (int i = 0; i < count; i++) {
        cutsceneChainBreakAnimations[i]  = malloc_uncached(sizeof(T3DAnim));
        *cutsceneChainBreakAnimations[i] = t3d_anim_create(cutsceneChainBreakModel, names[i]);
        t3d_anim_set_looping(cutsceneChainBreakAnimations[i], false);
        t3d_anim_set_playing(cutsceneChainBreakAnimations[i], true);
        t3d_anim_attach(cutsceneChainBreakAnimations[i], cutsceneChainBreakSkeleton);
    }

    rspq_block_begin();
    t3d_model_draw_skinned(cutsceneChainBreakModel, cutsceneChainBreakSkeleton);
    cutsceneChainBreakDpl = rspq_block_end();

    cutsceneChainBreakMatrix = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_from_srt_euler(
        cutsceneChainBreakMatrix,
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, 0.0f, 0.0f}
    );
}

void cutscene_manager_cleanup(void)
{
    if (cutsceneChainBreakDpl) {
        rspq_block_free(cutsceneChainBreakDpl);
        cutsceneChainBreakDpl = NULL;
    }
    if (cutsceneChainBreakModel) {
        t3d_model_free(cutsceneChainBreakModel);
        cutsceneChainBreakModel = NULL;
    }
    if (cutsceneChainBreakMatrix) {
        free_uncached(cutsceneChainBreakMatrix);
        cutsceneChainBreakMatrix = NULL;
    }
    if (cutsceneChainBreakSkeleton) {
        t3d_skeleton_destroy(cutsceneChainBreakSkeleton);
        free_uncached(cutsceneChainBreakSkeleton);
        cutsceneChainBreakSkeleton = NULL;
    }
    if (cutsceneChainBreakAnimations) {
        if (cutsceneChainBreakAnimations[0]) {
            t3d_anim_destroy(cutsceneChainBreakAnimations[0]);
            free_uncached(cutsceneChainBreakAnimations[0]);
        }
        free_uncached(cutsceneChainBreakAnimations);
        cutsceneChainBreakAnimations = NULL;
    }
}

void cutscene_manager_reset(void)
{
    cutsceneState           = CUTSCENE_NONE;
    cutsceneTimer           = 0.0f;
    cutsceneCameraTimer     = 0.0f;
    cutsceneDialogActive    = false;
    skipButtonVisible       = false;
    lastCutsceneAPressed    = false;
    bossPostDefeatDialogStep = 0;
    phase2CutsceneTriggered = false;
}

// ----------------------------------------------------------------------------
// Queries
// ----------------------------------------------------------------------------
CutsceneState cutscene_manager_get_state(void)        { return cutsceneState; }
bool          cutscene_manager_is_active(void)        { return cutsceneState != CUTSCENE_NONE; }
bool          cutscene_manager_is_dialog_active(void) { return cutsceneDialogActive; }
bool          cutscene_manager_is_skip_visible(void)  { return skipButtonVisible; }

bool cutscene_manager_phase2_triggered(void)       { return phase2CutsceneTriggered; }
void cutscene_manager_mark_phase2_triggered(void)  { phase2CutsceneTriggered = true; }

// ----------------------------------------------------------------------------
// Chain-break
// ----------------------------------------------------------------------------
void cutscene_manager_chain_break_tick(float dt)
{
    if (!cutsceneChainBreakAnimations || !cutsceneChainBreakAnimations[0]) return;
    t3d_anim_update(cutsceneChainBreakAnimations[0], dt);
    t3d_skeleton_update(cutsceneChainBreakSkeleton);
}

void cutscene_manager_chain_break_draw(void)
{
    if (!cutsceneChainBreakDpl || !cutsceneChainBreakMatrix) return;
    t3d_matrix_set(cutsceneChainBreakMatrix, true);
    rspq_block_run(cutsceneChainBreakDpl);
}

// ----------------------------------------------------------------------------
// Fog ranges per cutscene state.
// ----------------------------------------------------------------------------
void cutscene_manager_draw_fog(void)
{
    switch (cutsceneState) {
        case CUTSCENE_PHASE1_INTRO:
            t3d_fog_set_range(300.0f, 600.0f);
            break;
        case CUTSCENE_PHASE1_CHAIN_CLOSEUP:
            t3d_fog_set_range(300.0f, 500.0f);
            break;
        case CUTSCENE_PHASE1_SWORDS_CLOSEUP:
            t3d_fog_set_range(450.0f, 800.0f);
            break;
        case CUTSCENE_PHASE1_FILLER:
            t3d_fog_set_range(30.0f, 50.0f);
            break;
        case CUTSCENE_PHASE1_LOYALTY:
            t3d_fog_set_range(3.0f, 10.0f);
            break;
        case CUTSCENE_PHASE1_FEAR:
            t3d_fog_set_range(20.0f, 50.0f);
            break;
        case CUTSCENE_PHASE1_INTRO_END:
            t3d_fog_set_range(450.0f, 800.0f);
            break;
        case CUTSCENE_PHASE2_MIND:
        case CUTSCENE_PHASE2_KNEEL:
        case CUTSCENE_PHASE2_BURN:
        case CUTSCENE_PHASE2_INTRO:
            t3d_fog_set_range(300.0f, 600.0f);
            break;
        case CUTSCENE_PHASE2_SHACKLED_SUN:
            break;
        case CUTSCENE_PHASE2_BNW: {
            const color_t prim = RGBA32(255, 255, 255, 255);
            t3d_screen_clear_color(prim);
            t3d_screen_clear_depth();
            rdpq_mode_fog(RDPQ_FOG_STANDARD);
            rdpq_set_fog_color(prim);
            t3d_fog_set_range(300.0f, 500.0f);
        } break;
        case CUTSCENE_PHASE2_END:
            t3d_fog_set_range(30.0f, 400.0f);
            break;
        default:
            break;
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
    int buttonWidth  = surf.width;
    int buttonHeight = surf.height;

    const int marginX = ui_safe_margin_x();
    const int marginY = ui_safe_margin_y();
    int buttonX = SCREEN_WIDTH  - buttonWidth  - marginX;
    int buttonY = SCREEN_HEIGHT - buttonHeight - marginY;

    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(0);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_sprite_blit(sprite, buttonX, buttonY, NULL);

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
