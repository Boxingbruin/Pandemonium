#include "cutscene_guardian_phase1.h"

#include <stdbool.h>
#include <math.h>
#include <string.h>

#include <libdragon.h>
#include <rdpq.h>
#include <rdpq_mode.h>
#include <rdpq_text.h>

#include <t3d/t3d.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>
#include <t3d/t3dmodel.h>

#include "scene_context.h"
#include "../controllers/audio_controller.h"
#include "../controllers/camera_controller.h"
#include "../objects/character.h"
#include "../managers/cutscene_manager.h"
#include "../managers/cutscene_manager_internal.h"
#include "../game/bosses/boss_anim.h"
#include "../game/bosses/boss_render.h"
#include "../utilities/display_utility.h"
#include "../utilities/globals.h"
#include "../utilities/joypad_utility.h"
#include "../utilities/letterbox_utility.h"

#define CINEMATIC_CHAINS_ANIM_COUNT 2
#define CHAIN_BREAK_ANIM_COUNT 1

static bool s_loaded = false;

// ------------------------------------------------------------
// Dialog
// ------------------------------------------------------------

static const char *s_phase1Dialogs[] = {
    "^Those who approach the\nthrone of gold~ ^fall at my\nblade.",
    "^A Knight?~ >Where is your\n^loyalty...",
    "^Where is your...~ <Fear.",
};

static const char *cutscene_guardian_phase1_get_dialog(int idx)
{
    int count = (int)(sizeof(s_phase1Dialogs) / sizeof(s_phase1Dialogs[0]));
    if (idx < 0 || idx >= count) return "";
    return s_phase1Dialogs[idx];
}

// ------------------------------------------------------------
// Phase 1 assets
// ------------------------------------------------------------

static T3DModel *s_cinematicChainsModel = NULL;
static rspq_block_t *s_cinematicChainsDpl = NULL;
static T3DMat4FP *s_cinematicChainsMatrix = NULL;
static T3DSkeleton *s_cinematicChainsSkeleton = NULL;
static T3DAnim **s_cinematicChainsAnimations = NULL;
static int s_currentCinematicChainsAnimation = 0;
static bool s_cinematicChainsVisible = true;

static T3DModel *s_chainBreakModel = NULL;
static rspq_block_t *s_chainBreakDpl = NULL;
static T3DMat4FP *s_chainBreakMatrix = NULL;
static T3DSkeleton *s_chainBreakSkeleton = NULL;
static T3DAnim **s_chainBreakAnimations = NULL;

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

static T3DMat4FP *cutscene_guardian_phase1_alloc_matrix(void)
{
    return malloc_uncached(sizeof(T3DMat4FP));
}

static void cutscene_guardian_phase1_free_matrix(T3DMat4FP **matrix)
{
    if (!matrix || !*matrix) return;

    free_uncached(*matrix);
    *matrix = NULL;
}

static void cutscene_guardian_phase1_free_dpl(rspq_block_t **dpl)
{
    if (!dpl || !*dpl) return;

    rspq_block_free(*dpl);
    *dpl = NULL;
}

static void cutscene_guardian_phase1_free_model(T3DModel **model)
{
    if (!model || !*model) return;

    t3d_model_free(*model);
    *model = NULL;
}

static void cutscene_guardian_phase1_set_origin_matrix(T3DMat4FP *matrix)
{
    if (!matrix) return;

    t3d_mat4fp_from_srt_euler(
        matrix,
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, 0.0f, 0.0f}
    );
}

static void cutscene_guardian_phase1_free_cinematic_chains_anim(void)
{
    if (s_cinematicChainsAnimations) {
        for (int i = 0; i < CINEMATIC_CHAINS_ANIM_COUNT; i++) {
            if (s_cinematicChainsAnimations[i]) {
                t3d_anim_destroy(s_cinematicChainsAnimations[i]);
                free_uncached(s_cinematicChainsAnimations[i]);
                s_cinematicChainsAnimations[i] = NULL;
            }
        }

        free_uncached(s_cinematicChainsAnimations);
        s_cinematicChainsAnimations = NULL;
    }

    if (s_cinematicChainsSkeleton) {
        t3d_skeleton_destroy(s_cinematicChainsSkeleton);
        free_uncached(s_cinematicChainsSkeleton);
        s_cinematicChainsSkeleton = NULL;
    }
}

static void cutscene_guardian_phase1_free_chain_break_anim(void)
{
    if (s_chainBreakAnimations) {
        for (int i = 0; i < CHAIN_BREAK_ANIM_COUNT; i++) {
            if (s_chainBreakAnimations[i]) {
                t3d_anim_destroy(s_chainBreakAnimations[i]);
                free_uncached(s_chainBreakAnimations[i]);
                s_chainBreakAnimations[i] = NULL;
            }
        }

        free_uncached(s_chainBreakAnimations);
        s_chainBreakAnimations = NULL;
    }

    if (s_chainBreakSkeleton) {
        t3d_skeleton_destroy(s_chainBreakSkeleton);
        free_uncached(s_chainBreakSkeleton);
        s_chainBreakSkeleton = NULL;
    }
}

bool cutscene_guardian_phase1_handles(CutsceneState state)
{
    return state == CUTSCENE_PHASE1_INTRO ||
           state == CUTSCENE_PHASE1_CHAIN_CLOSEUP ||
           state == CUTSCENE_PHASE1_SWORDS_CLOSEUP ||
           state == CUTSCENE_PHASE1_FILLER ||
           state == CUTSCENE_PHASE1_LOYALTY ||
           state == CUTSCENE_PHASE1_FEAR ||
           state == CUTSCENE_PHASE1_BREAK_CHAINS ||
           state == CUTSCENE_PHASE1_INTRO_END;
}

static void cutscene_guardian_phase1_next_state(SceneContext *ctx, CutsceneState nextState)
{
    cutsceneTimer = 0.0f;
    cutsceneCameraTimer = 0.0f;
    cutsceneState = nextState;

    cutscene_guardian_phase1_enter(ctx, nextState);
}

static void cutscene_guardian_phase1_draw_boss_title(SceneContext *ctx)
{
    if (!ctx || !ctx->boss) return;

    rdpq_sync_pipe();

    rdpq_set_mode_standard();
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_set_prim_color(RGBA32(0, 0, 0, 120));
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);

    int barWidth = 195;
    int barHeight = 23;
    int barTop = 35;
    int barLeft = (SCREEN_WIDTH - barWidth) / 2;

    rdpq_fill_rectangle(barLeft, barTop, barLeft + barWidth, barTop + barHeight);

    rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    rdpq_text_printf(&(rdpq_textparms_t){
        .align = ALIGN_CENTER,
        .width = SCREEN_WIDTH,
    }, FONT_UNBALANCED, 0, 50, "%s", ctx->boss->name);
}

static void cutscene_guardian_phase1_draw_chain_break(void)
{
    if (!s_chainBreakDpl || !s_chainBreakMatrix) return;

    t3d_matrix_set(s_chainBreakMatrix, true);
    rspq_block_run(s_chainBreakDpl);
}

// ------------------------------------------------------------
// Load / unload
// ------------------------------------------------------------

void cutscene_guardian_phase1_load(void)
{
    if (s_loaded) return;

    // Cinematic chains
    s_cinematicChainsModel = t3d_model_load("rom:/boss_room/chains.t3dm");

    if (s_cinematicChainsModel) {
        s_cinematicChainsSkeleton = malloc_uncached(sizeof(T3DSkeleton));
        *s_cinematicChainsSkeleton = t3d_skeleton_create(s_cinematicChainsModel);

        const char *names[CINEMATIC_CHAINS_ANIM_COUNT] = {
            "ChainsInitial",
            "ChainsSeparate"
        };

        s_cinematicChainsAnimations = malloc_uncached(CINEMATIC_CHAINS_ANIM_COUNT * sizeof(T3DAnim*));

        for (int i = 0; i < CINEMATIC_CHAINS_ANIM_COUNT; i++) {
            s_cinematicChainsAnimations[i] = malloc_uncached(sizeof(T3DAnim));
            *s_cinematicChainsAnimations[i] = t3d_anim_create(s_cinematicChainsModel, names[i]);
            t3d_anim_attach(s_cinematicChainsAnimations[i], s_cinematicChainsSkeleton);
        }

        s_currentCinematicChainsAnimation = 0;
        t3d_anim_set_looping(s_cinematicChainsAnimations[s_currentCinematicChainsAnimation], true);
        t3d_anim_set_playing(s_cinematicChainsAnimations[s_currentCinematicChainsAnimation], true);

        rspq_block_begin();
            t3d_model_draw_skinned(s_cinematicChainsModel, s_cinematicChainsSkeleton);
        s_cinematicChainsDpl = rspq_block_end();

        s_cinematicChainsMatrix = cutscene_guardian_phase1_alloc_matrix();
        cutscene_guardian_phase1_set_origin_matrix(s_cinematicChainsMatrix);
    }

    // Chain break
    s_chainBreakModel = t3d_model_load("rom:/cutscene/shatter_chain.t3dm");

    if (s_chainBreakModel) {
        s_chainBreakSkeleton = malloc_uncached(sizeof(T3DSkeleton));
        *s_chainBreakSkeleton = t3d_skeleton_create(s_chainBreakModel);

        const char *names[CHAIN_BREAK_ANIM_COUNT] = {
            "ChainBreak"
        };

        s_chainBreakAnimations = malloc_uncached(CHAIN_BREAK_ANIM_COUNT * sizeof(T3DAnim*));

        for (int i = 0; i < CHAIN_BREAK_ANIM_COUNT; i++) {
            s_chainBreakAnimations[i] = malloc_uncached(sizeof(T3DAnim));
            *s_chainBreakAnimations[i] = t3d_anim_create(s_chainBreakModel, names[i]);

            t3d_anim_set_looping(s_chainBreakAnimations[i], false);
            t3d_anim_set_playing(s_chainBreakAnimations[i], true);
            t3d_anim_attach(s_chainBreakAnimations[i], s_chainBreakSkeleton);
        }

        rspq_block_begin();
            t3d_model_draw_skinned(s_chainBreakModel, s_chainBreakSkeleton);
        s_chainBreakDpl = rspq_block_end();

        s_chainBreakMatrix = cutscene_guardian_phase1_alloc_matrix();
        cutscene_guardian_phase1_set_origin_matrix(s_chainBreakMatrix);
    }

    s_cinematicChainsVisible = true;
    s_loaded = true;
}

void cutscene_guardian_phase1_unload(void)
{
    if (!s_loaded) return;

    rspq_wait();

    cutscene_guardian_phase1_free_dpl(&s_cinematicChainsDpl);
    cutscene_guardian_phase1_free_dpl(&s_chainBreakDpl);

    cutscene_guardian_phase1_free_cinematic_chains_anim();
    cutscene_guardian_phase1_free_chain_break_anim();

    cutscene_guardian_phase1_free_matrix(&s_cinematicChainsMatrix);
    cutscene_guardian_phase1_free_matrix(&s_chainBreakMatrix);

    cutscene_guardian_phase1_free_model(&s_cinematicChainsModel);
    cutscene_guardian_phase1_free_model(&s_chainBreakModel);

    s_currentCinematicChainsAnimation = 0;
    s_cinematicChainsVisible = true;
    s_loaded = false;
}

// ------------------------------------------------------------
// Enter
// ------------------------------------------------------------

void cutscene_guardian_phase1_enter(SceneContext *ctx, CutsceneState state)
{
    if (!ctx) return;

    cutscene_guardian_phase1_load();

    skipButtonVisible = false;

    switch (state) {
        case CUTSCENE_PHASE1_INTRO: {
            if (ctx->boss) {
                cutscene_manager_set_camera_shot(
                    ctx,
                    (T3DVec3){{-700.0f, 120.4f, 0.0f}},
                    (T3DVec3){{-600.0f, 120.4f, 0.0f}},
                    (T3DVec3){{ctx->boss->pos[0], ctx->boss->pos[1] + 100.0f, ctx->boss->pos[2]}}
                );
            }

            camera_mode(CAMERA_CUSTOM);

            if (ctx->screenTransition) {
                *ctx->screenTransition = true;
            }

            startScreenFade = true;

            if (ctx->gameState) {
                *ctx->gameState = GAME_STATE_PLAYING;
            }

            character_reset();

            audio_stop_music();
            audio_play_music("rom:/audio/music/boss_phase1_cutscene1-22k.wav64", false);
        } break;

        case CUTSCENE_PHASE1_CHAIN_CLOSEUP: {
            if (ctx->screenTransition) {
                *ctx->screenTransition = false;
            }

            cutscene_manager_set_camera_shot(
                ctx,
                    (T3DVec3){{-239.0f, 239.4f, -133.7f}},
                    (T3DVec3){{-239.0f, 239.4f, -133.7f}},
                    (T3DVec3){{-151.9f, 208.0f, -96.0f}}
            );
        } break;

        case CUTSCENE_PHASE1_SWORDS_CLOSEUP: {
            cutscene_manager_set_camera_shot(
                ctx,
                    (T3DVec3){{-197.86f, 20.0f, 191.45f}},
                    (T3DVec3){{-220.97f, 20.0f, 190.0f}},
                    (T3DVec3){{-142.32f, 55.14f, 114.76f}}
            );
        } break;

        case CUTSCENE_PHASE1_FILLER: {

            cutscene_manager_set_camera_shot(
                ctx,
                    (T3DVec3){{-0.47f, 6.89f, 70.0f}},
                    (T3DVec3){{-0.15f, 22.99f, 32.26f}},
                    (T3DVec3){{0.476f, 55.54f, -71.29f}}
            );

            cutscene_manager_begin_dialog(
                cutscene_guardian_phase1_get_dialog(0),
                0.0f
            );
        } break;

        case CUTSCENE_PHASE1_LOYALTY: {

            cutscene_manager_set_camera_shot(
                ctx,
                    (T3DVec3){{-18.28f, 11.45f, 2.0f}},
                    (T3DVec3){{-18.28f, 11.45f, -2.0f}},
                    (T3DVec3){{80.4f, -1.0f, -11.0f}}
            );

            cutscene_manager_begin_dialog(
                cutscene_guardian_phase1_get_dialog(1),
                0.0f
            );
        } break;

        case CUTSCENE_PHASE1_FEAR: {

            cutscene_manager_set_camera_shot(
                ctx,
                    (T3DVec3){{-13.454f, 13.41f, -24.27f}},
                    (T3DVec3){{-13.454f, 25.41f, -24.27f}},
                    (T3DVec3){{400.0f, 43.41f, -29.67f}}
            );

            cutscene_manager_begin_dialog(
                cutscene_guardian_phase1_get_dialog(2),
                0.0f
            );
        } break;

        case CUTSCENE_PHASE1_BREAK_CHAINS: {
            if (ctx->screenTransition) {
                *ctx->screenTransition = false;
            }

            cutscene_manager_clear_dialog();

            if (s_chainBreakAnimations && s_chainBreakAnimations[0]) {
                t3d_anim_set_time(s_chainBreakAnimations[0], 0.0f);
                t3d_anim_set_playing(s_chainBreakAnimations[0], true);
            }

            cutscene_manager_set_camera_shot(
                ctx,
                    (T3DVec3){{-22.31f, 1.7f, 0.65f}},
                    (T3DVec3){{-42.31f, 1.7f, 0.65f}},
                    (T3DVec3){{-12.31f, 1.7f, 0.65f}}
            );

            joypad_rumble_pulse_seconds(0.5f);
        } break;

        case CUTSCENE_PHASE1_INTRO_END: {
            camera_mode(CAMERA_CUSTOM);

            cutscene_manager_set_boss_anim(ctx, BOSS_ANIM_KNEEL);

            s_currentCinematicChainsAnimation = 1;

            if (s_cinematicChainsAnimations && s_cinematicChainsAnimations[s_currentCinematicChainsAnimation]) {
                t3d_anim_set_looping(s_cinematicChainsAnimations[s_currentCinematicChainsAnimation], false);
                t3d_anim_set_time(s_cinematicChainsAnimations[s_currentCinematicChainsAnimation], 0.0f);
                t3d_anim_set_playing(s_cinematicChainsAnimations[s_currentCinematicChainsAnimation], true);
            }

            if (ctx->screenTransition) {
                *ctx->screenTransition = true;
            }

            startScreenFade = true;

            letterbox_hide();

            cutscene_manager_clear_dialog();

            cutscene_manager_set_camera_shot(
                ctx,
                    (T3DVec3){{-22.0f, 29.0f, -10.0f}},
                    (T3DVec3){{-150.0f, 29.0f, -10.0f}},
                    (T3DVec3){{100.0f, 29.0f, 0.0f}}
            );
        } break;

        default:
            break;
    }
}

// ------------------------------------------------------------
// Update
// ------------------------------------------------------------

void cutscene_guardian_phase1_update(SceneContext *ctx, float dt)
{
    if (!ctx) return;

    cutsceneTimer += dt;

    if (cutsceneState != CUTSCENE_NONE) {
        cutsceneCameraTimer += dt;
    }

    if (s_cinematicChainsAnimations && s_cinematicChainsAnimations[s_currentCinematicChainsAnimation]) {
        t3d_anim_update(s_cinematicChainsAnimations[s_currentCinematicChainsAnimation], dt);
    }

    if (s_cinematicChainsSkeleton) {
        t3d_skeleton_update(s_cinematicChainsSkeleton);
    }

    if (cutsceneState == CUTSCENE_PHASE1_BREAK_CHAINS) {
        if (s_chainBreakAnimations && s_chainBreakAnimations[0]) {
            t3d_anim_update(s_chainBreakAnimations[0], dt);
        }

        if (s_chainBreakSkeleton) {
            t3d_skeleton_update(s_chainBreakSkeleton);
        }
    }

    cutscene_manager_update_boss_transform(ctx);

    switch (cutsceneState) {
        case CUTSCENE_PHASE1_INTRO: {
            cutscene_manager_update_camera(9.0f);

            if (cutsceneTimer >= 9.0f) {
                cutscene_guardian_phase1_next_state(ctx, CUTSCENE_PHASE1_CHAIN_CLOSEUP);
                return;
            }
        } break;

        case CUTSCENE_PHASE1_CHAIN_CLOSEUP: {
            cutscene_manager_update_camera(6.0f);

            if (cutsceneTimer >= 6.0f) {
                cutscene_guardian_phase1_next_state(ctx, CUTSCENE_PHASE1_SWORDS_CLOSEUP);
                return;
            }
        } break;

        case CUTSCENE_PHASE1_SWORDS_CLOSEUP: {
            cutscene_manager_update_camera(4.0f);

            if (cutsceneTimer >= 5.0f) {
                cutscene_guardian_phase1_next_state(ctx, CUTSCENE_PHASE1_FILLER);
                return;
            }
        } break;

        case CUTSCENE_PHASE1_FILLER: {
            cutscene_manager_update_camera(13.0f);
            cutscene_manager_update_dialog();

            if (cutsceneTimer >= 10.0f) {
                cutscene_guardian_phase1_next_state(ctx, CUTSCENE_PHASE1_LOYALTY);
                return;
            }
        } break;

        case CUTSCENE_PHASE1_LOYALTY: {
            cutscene_manager_update_camera(5.0f);
            cutscene_manager_update_dialog();

            if (cutsceneTimer >= 5.0f) {
                cutscene_guardian_phase1_next_state(ctx, CUTSCENE_PHASE1_FEAR);
                return;
            }
        } break;

        case CUTSCENE_PHASE1_FEAR: {
            cutscene_manager_update_camera(7.5f);
            cutscene_manager_update_dialog();

            if (cutsceneTimer >= 6.5f &&
                ctx->boss &&
                ctx->boss->currentAnimation != BOSS_ANIM_KNEEL_CUTSCENE)
            {
                cutscene_manager_set_boss_anim(ctx, BOSS_ANIM_KNEEL_CUTSCENE);
            }

            if (cutsceneTimer >= 7.5f) {
                if (ctx->screenTransition && !*ctx->screenTransition) {
                    *ctx->screenTransition = true;
                    startScreenFade = true;
                }
            }

            if (cutsceneTimer >= 10.5f) {
                cutscene_guardian_phase1_next_state(ctx, CUTSCENE_PHASE1_BREAK_CHAINS);
                return;
            }
        } break;

        case CUTSCENE_PHASE1_BREAK_CHAINS: {
            cutscene_manager_update_camera(5.0f);

            if (cutsceneTimer >= 3.0f) {
                if (ctx->screenTransition && !*ctx->screenTransition) {
                    *ctx->screenTransition = true;
                    startScreenFade = true;
                }
            }

            if (cutsceneTimer >= 5.0f) {
                cutscene_guardian_phase1_next_state(ctx, CUTSCENE_PHASE1_INTRO_END);
                return;
            }
        } break;

        case CUTSCENE_PHASE1_INTRO_END: {
            cutscene_manager_update_camera(10.0f);

            if (cutsceneTimer >= 10.0f) {
                cutsceneTimer = 0.0f;
                cutsceneCameraTimer = 0.0f;

                if (ctx->init_playing) {
                    ctx->init_playing(false);
                }

                cutscene_guardian_phase1_unload();
                return;
            }
        } break;

        default:
            break;
    }
}

void cutscene_guardian_phase1_skip(SceneContext *ctx)
{
    cutscene_manager_clear_dialog();

    skipButtonVisible = false;
    cutsceneTimer = 0.0f;
    cutsceneCameraTimer = 0.0f;

    if (ctx && ctx->init_playing) {
        ctx->init_playing(true);
    }

    cutscene_guardian_phase1_unload();
}

// ------------------------------------------------------------
// Draw
// ------------------------------------------------------------

void cutscene_guardian_phase1_draw(SceneContext *ctx, T3DViewport *viewport)
{
    (void)viewport;

    if (!ctx) return;

    switch (cutsceneState) {
        case CUTSCENE_PHASE1_INTRO: {
            rdpq_mode_zbuf(false, false);

            t3d_matrix_push_pos(1);

            if (ctx->mapMatrix && ctx->mapDpl) {
                t3d_matrix_set(ctx->mapMatrix, true);
                rspq_block_run(ctx->mapDpl);
            }

            if (ctx->roomFloorMatrix && ctx->roomFloorDpl) {
                t3d_matrix_set(ctx->roomFloorMatrix, true);
                rspq_block_run(ctx->roomFloorDpl);
            }

            if (ctx->roomLedgeMatrix && ctx->roomLedgeDpl) {
                t3d_matrix_set(ctx->roomLedgeMatrix, true);
                rspq_block_run(ctx->roomLedgeDpl);
            }

            if (ctx->boss) {
                boss_draw(ctx->boss);
            }

            if (ctx->pillarsMatrix && ctx->pillarsDpl) {
                t3d_matrix_set(ctx->pillarsMatrix, true);
                rspq_block_run(ctx->pillarsDpl);
            }

            if (ctx->sunshaftsMatrix && ctx->sunshaftsDpl) {
                t3d_matrix_set(ctx->sunshaftsMatrix, true);
                rspq_block_run(ctx->sunshaftsDpl);
            }

            if (s_cinematicChainsVisible && s_cinematicChainsMatrix && s_cinematicChainsDpl) {
                t3d_matrix_set(s_cinematicChainsMatrix, true);
                rspq_block_run(s_cinematicChainsDpl);
            }

            if (ctx->chainsMatrix && ctx->chainsDpl) {
                t3d_matrix_set(ctx->chainsMatrix, true);
                rspq_block_run(ctx->chainsDpl);
            }

            if (ctx->pillarsFrontMatrix && ctx->pillarsFrontDpl) {
                t3d_matrix_set(ctx->pillarsFrontMatrix, true);
                rspq_block_run(ctx->pillarsFrontDpl);
            }

            t3d_matrix_pop(1);

            if (ctx->screenTransition && *ctx->screenTransition) {
                display_utility_solid_black_transition(true, 100.0f);
            }
        } break;

        case CUTSCENE_PHASE1_CHAIN_CLOSEUP: {
            rdpq_mode_zbuf(false, false);

            t3d_matrix_push_pos(1);

            if (ctx->mapMatrix && ctx->mapDpl) {
                t3d_matrix_set(ctx->mapMatrix, true);
                rspq_block_run(ctx->mapDpl);
            }

            if (ctx->roomFloorMatrix && ctx->roomFloorDpl) {
                t3d_matrix_set(ctx->roomFloorMatrix, true);
                rspq_block_run(ctx->roomFloorDpl);
            }

            if (ctx->pillarsMatrix && ctx->pillarsDpl) {
                t3d_matrix_set(ctx->pillarsMatrix, true);
                rspq_block_run(ctx->pillarsDpl);
            }

            if (ctx->boss) {
                boss_draw(ctx->boss);
            }

            if (ctx->sunshaftsMatrix && ctx->sunshaftsDpl) {
                t3d_matrix_set(ctx->sunshaftsMatrix, true);
                rspq_block_run(ctx->sunshaftsDpl);
            }

            if (s_cinematicChainsMatrix && s_cinematicChainsDpl) {
                t3d_matrix_set(s_cinematicChainsMatrix, true);
                rspq_block_run(s_cinematicChainsDpl);
            }

            t3d_matrix_pop(1);
        } break;

        case CUTSCENE_PHASE1_SWORDS_CLOSEUP: {
            rdpq_mode_zbuf(false, false);

            t3d_matrix_push_pos(1);

            if (ctx->windowsMatrix && ctx->windowsDpl) {
                t3d_matrix_set(ctx->windowsMatrix, true);
                rspq_block_run(ctx->windowsDpl);
            }

            if (ctx->mapMatrix && ctx->mapDpl) {
                t3d_matrix_set(ctx->mapMatrix, true);
                rspq_block_run(ctx->mapDpl);
            }

            if (ctx->pillarsMatrix && ctx->pillarsDpl) {
                t3d_matrix_set(ctx->pillarsMatrix, true);
                rspq_block_run(ctx->pillarsDpl);
            }

            rdpq_mode_zbuf(true, true);

            if (ctx->roomFloorMatrix && ctx->roomFloorDpl) {
                t3d_matrix_set(ctx->roomFloorMatrix, true);
                rspq_block_run(ctx->roomFloorDpl);
            }

            if (ctx->boss) {
                boss_draw(ctx->boss);
            }

            rdpq_mode_zbuf(false, false);

            if (ctx->chainsMatrix && ctx->chainsDpl) {
                t3d_matrix_set(ctx->chainsMatrix, true);
                rspq_block_run(ctx->chainsDpl);
            }

            if (ctx->sunshaftsMatrix && ctx->sunshaftsDpl) {
                t3d_matrix_set(ctx->sunshaftsMatrix, true);
                rspq_block_run(ctx->sunshaftsDpl);
            }

            if (s_cinematicChainsMatrix && s_cinematicChainsDpl) {
                t3d_matrix_set(s_cinematicChainsMatrix, true);
                rspq_block_run(s_cinematicChainsDpl);
            }
            t3d_matrix_pop(1);
        } break;

        case CUTSCENE_PHASE1_FILLER: {
            rdpq_mode_zbuf(false, false);

            t3d_matrix_push_pos(1);

            if (s_cinematicChainsMatrix && s_cinematicChainsDpl) {
                t3d_matrix_set(s_cinematicChainsMatrix, true);
                rspq_block_run(s_cinematicChainsDpl);
            }

            if (ctx->sunshaftsMatrix && ctx->sunshaftsDpl) {
                t3d_matrix_set(ctx->sunshaftsMatrix, true);
                rspq_block_run(ctx->sunshaftsDpl);
            }

            rdpq_mode_zbuf(true, true);

            if (ctx->roomFloorMatrix && ctx->roomFloorDpl) {
                t3d_matrix_set(ctx->roomFloorMatrix, true);
                rspq_block_run(ctx->roomFloorDpl);
            }

            if (ctx->boss) {
                boss_draw(ctx->boss);
            }

            t3d_matrix_pop(1);

            cutscene_manager_draw_dialog();
        } break;

        case CUTSCENE_PHASE1_LOYALTY: {
            rdpq_mode_zbuf(true, true);

            t3d_matrix_push_pos(1);

            if (ctx->roomFloorMatrix && ctx->roomFloorDpl) {
                t3d_matrix_set(ctx->roomFloorMatrix, true);
                rspq_block_run(ctx->roomFloorDpl);
            }

            if (ctx->boss) {
                boss_draw(ctx->boss);
            }

            t3d_matrix_pop(1);

            cutscene_manager_draw_dialog();
        } break;

        case CUTSCENE_PHASE1_FEAR: {
            rdpq_mode_zbuf(false, false);

            t3d_matrix_push_pos(1);

            if (ctx->roomFloorMatrix && ctx->roomFloorDpl) {
                t3d_matrix_set(ctx->roomFloorMatrix, true);
                rspq_block_run(ctx->roomFloorDpl);
            }

            rdpq_mode_zbuf(true, true);

            if (ctx->boss) {
                boss_draw(ctx->boss);
            }

            t3d_matrix_pop(1);

            cutscene_manager_draw_dialog();

            if (ctx->screenTransition && *ctx->screenTransition) {
                display_utility_solid_black_transition(false, 200.0f);
            }
        } break;

        case CUTSCENE_PHASE1_BREAK_CHAINS: {
            rdpq_mode_zbuf(false, false);

            t3d_matrix_push_pos(1);
                cutscene_guardian_phase1_draw_chain_break();
            t3d_matrix_pop(1);

            if (ctx->screenTransition && *ctx->screenTransition) {
                display_utility_solid_black_transition(false, 200.0f);
            }
        } break;

        case CUTSCENE_PHASE1_INTRO_END: {
            rdpq_mode_zbuf(false, false);

            t3d_matrix_push_pos(1);

            if (ctx->mapMatrix && ctx->mapDpl) {
                t3d_matrix_set(ctx->mapMatrix, true);
                rspq_block_run(ctx->mapDpl);
            }

            if (ctx->roomLedgeMatrix && ctx->roomLedgeDpl) {
                t3d_matrix_set(ctx->roomLedgeMatrix, true);
                rspq_block_run(ctx->roomLedgeDpl);
            }

            if (ctx->pillarsMatrix && ctx->pillarsDpl) {
                t3d_matrix_set(ctx->pillarsMatrix, true);
                rspq_block_run(ctx->pillarsDpl);
            }

            rdpq_mode_zbuf(true, true);

            if (ctx->roomFloorMatrix && ctx->roomFloorDpl) {
                t3d_matrix_set(ctx->roomFloorMatrix, true);
                rspq_block_run(ctx->roomFloorDpl);
            }

            if (ctx->boss) {
                boss_draw(ctx->boss);
            }

            if (s_cinematicChainsMatrix && s_cinematicChainsDpl) {
                t3d_matrix_set(s_cinematicChainsMatrix, true);
                rspq_block_run(s_cinematicChainsDpl);
            }

            rdpq_mode_zbuf(false, false);

            if (ctx->chainsMatrix && ctx->chainsDpl) {
                t3d_matrix_set(ctx->chainsMatrix, true);
                rspq_block_run(ctx->chainsDpl);
            }

            if (ctx->boss) {
                boss_draw_shadow(ctx->boss);
            }

            t3d_matrix_pop(1);

            cutscene_guardian_phase1_draw_boss_title(ctx);

            if (ctx->screenTransition && *ctx->screenTransition) {
                display_utility_solid_black_transition(true, 100.0f);
            }
        } break;

        default:
            break;
    }
}

// ------------------------------------------------------------
// Fog
// ------------------------------------------------------------

void cutscene_guardian_phase1_draw_fog(void)
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

        default:
            break;
    }
}