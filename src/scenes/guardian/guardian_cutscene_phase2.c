#include "guardian_cutscene_phase2.h"

#include <stdbool.h>
#include <math.h>
#include <string.h>

#include <libdragon.h>
#include <rdpq.h>
#include <rdpq_mode.h>

#include <t3d/t3d.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>
#include <t3d/t3dmodel.h>

#include "guardian_scene_context.h"
#include "../../controllers/audio_controller.h"
#include "../../controllers/camera_controller.h"
#include "../../managers/cutscene_manager.h"
#include "../../managers/cutscene_manager_internal.h"
#include "../../utilities/display_utility.h"
#include "../../game/boss/boss_render.h"
#include "../../utilities/general_utility.h"
#include "../../fx/screen_shake.h"
#include "../../utilities/globals.h"
#include "../../utilities/joypad_utility.h"
#include "../../fx/lightning_fx.h"

#define BNW_CHAINS_ANIM_COUNT 1

static bool s_loaded = false;

static float s_floorSpinBnwYaw = 0.0f;

// ------------------------------------------------------------
// Dialog
// ------------------------------------------------------------

static const char *s_phase2Dialogs[] = {
    "^Valiant effort...~ ^Knight.",
    "^Now leave these sundered\nhalls.",
    "^Before the endless night",
    "^Through shackled sun\ndeliverance...~ ^Forces my\nhand once more.",
    "~ <Burn.",
    "<BURN MY MORTAL FLESH!!",
};

static const char *cutscene_guardian_phase2_get_dialog(int idx)
{
    int count = (int)(sizeof(s_phase2Dialogs) / sizeof(s_phase2Dialogs[0]));
    if (idx < 0 || idx >= count) return "";
    return s_phase2Dialogs[idx];
}

// ------------------------------------------------------------
// Phase-2 assets
// ------------------------------------------------------------

static T3DModel *s_bnwBossModel = NULL;
static rspq_block_t *s_bnwBossDpl = NULL;
static T3DMat4FP *s_bnwBossMatrix = NULL;

static T3DModel *s_bnwChainsModel = NULL;
static rspq_block_t *s_bnwChainsDpl = NULL;
static T3DMat4FP *s_bnwChainsMatrix = NULL;
static T3DSkeleton *s_bnwChainsSkeleton = NULL;
static T3DAnim **s_bnwChainsAnimations = NULL;
static int s_currentBnwChainsAnimation = 0;

static T3DModel *s_floorGlowBnwModel = NULL;
static rspq_block_t *s_floorGlowBnwDpl = NULL;
static T3DMat4FP *s_floorGlowBnwMatrix = NULL;
static ScrollParams s_floorGlowBnwScrollParams = {
    .xSpeed = -300.0f,
    .ySpeed = 0.0f,
    .scale  = 64
};

static T3DModel *s_floorSpinBnwModel = NULL;
static rspq_block_t *s_floorSpinBnwDpl = NULL;
static T3DMat4FP *s_floorSpinBnwMatrix = NULL;

static T3DModel *s_shackledSunModel = NULL;
static rspq_block_t *s_shackledSunDpl = NULL;
static T3DMat4FP *s_shackledSunMatrix = NULL;

static T3DModel *s_shackledSunGlowModel = NULL;
static rspq_block_t *s_shackledSunGlowDpl = NULL;
static T3DMat4FP *s_shackledSunGlowMatrix = NULL;
static ScrollParams s_shackledSunGlowScrollParams = {
    .xSpeed = 30.0f,
    .ySpeed = 0.0f,
    .scale  = 64
};

static T3DModel *s_shacklesModel = NULL;
static rspq_block_t *s_shacklesDpl = NULL;
static T3DMat4FP *s_shacklesMatrix = NULL;
static T3DMat4FP *s_shackles2Matrix = NULL;
static ScrollParams s_shacklesScrollParams = {
    .xSpeed = 50.0f,
    .ySpeed = 0.0f,
    .scale  = 64
};
static ScrollParams s_shackles2ScrollParams = {
    .xSpeed = -50.0f,
    .ySpeed = 0.0f,
    .scale  = 64
};

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

static T3DMat4FP *cutscene_guardian_phase2_alloc_matrix(void)
{
    return malloc_uncached(sizeof(T3DMat4FP));
}

static void cutscene_guardian_phase2_make_model_block(
    T3DModel *model,
    rspq_block_t **outDpl
)
{
    if (!model || !outDpl) return;

    rspq_block_begin();
        t3d_model_draw(model);
    *outDpl = rspq_block_end();
}

static void cutscene_guardian_phase2_set_matrix_default(
    T3DMat4FP *matrix,
    float roomY
)
{
    if (!matrix) return;

    t3d_mat4fp_from_srt_euler(
        matrix,
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, roomY, 0.0f}
    );
}

static void cutscene_guardian_phase2_set_matrix_origin(T3DMat4FP *matrix)
{
    if (!matrix) return;

    t3d_mat4fp_from_srt_euler(
        matrix,
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, 0.0f, 0.0f}
    );
}

static void cutscene_guardian_phase2_set_matrix_origin_inverted(T3DMat4FP *matrix)
{
    if (!matrix) return;

    t3d_mat4fp_from_srt_euler(
        matrix,
        (float[3]){-MODEL_SCALE, -MODEL_SCALE, -MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, 0.0f, 0.0f}
    );
}

static void cutscene_guardian_phase2_free_matrix(T3DMat4FP **matrix)
{
    if (!matrix || !*matrix) return;

    free_uncached(*matrix);
    *matrix = NULL;
}

static void cutscene_guardian_phase2_free_dpl(rspq_block_t **dpl)
{
    if (!dpl || !*dpl) return;

    rspq_block_free(*dpl);
    *dpl = NULL;
}

static void cutscene_guardian_phase2_free_model(T3DModel **model)
{
    if (!model || !*model) return;

    t3d_model_free(*model);
    *model = NULL;
}

static void cutscene_guardian_phase2_free_bnw_chains_anim(void)
{
    if (s_bnwChainsAnimations) {
        for (int i = 0; i < BNW_CHAINS_ANIM_COUNT; i++) {
            if (s_bnwChainsAnimations[i]) {
                t3d_anim_destroy(s_bnwChainsAnimations[i]);
                free_uncached(s_bnwChainsAnimations[i]);
                s_bnwChainsAnimations[i] = NULL;
            }
        }

        free_uncached(s_bnwChainsAnimations);
        s_bnwChainsAnimations = NULL;
    }

    if (s_bnwChainsSkeleton) {
        t3d_skeleton_destroy(s_bnwChainsSkeleton);
        free_uncached(s_bnwChainsSkeleton);
        s_bnwChainsSkeleton = NULL;
    }
}

bool cutscene_guardian_phase2_handles(CutsceneState state)
{
    return state == CUTSCENE_PHASE2_INTRO ||
           state == CUTSCENE_PHASE2_KNEEL ||
           state == CUTSCENE_PHASE2_BLURB ||
           state == CUTSCENE_PHASE2_MIND ||
           state == CUTSCENE_PHASE2_SHACKLED_SUN ||
           state == CUTSCENE_PHASE2_BURN ||
           state == CUTSCENE_PHASE2_BNW ||
           state == CUTSCENE_PHASE2_END;
}

static void cutscene_guardian_phase2_next_state(SceneContext *ctx, CutsceneState nextState)
{
    cutsceneTimer = 0.0f;
    cutsceneCameraTimer = 0.0f;
    cutsceneState = nextState;

    cutscene_guardian_phase2_enter(ctx, nextState);
}




static void cutscene_guardian_phase2_draw_standard_room(
    SceneContext *ctx,
    bool drawTransitionAfterTwoSeconds
)
{
    if (!ctx) return;

    rdpq_sync_pipe();
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

        if (ctx->pillarsMatrix && ctx->pillarsDpl) {
            t3d_matrix_set(ctx->pillarsMatrix, true);
            rspq_block_run(ctx->pillarsDpl);
        }
    t3d_matrix_pop(1);

    rdpq_sync_pipe();
    rdpq_mode_zbuf(true, true);

    t3d_matrix_push_pos(1);
        if (ctx->boss) {
            boss_draw(ctx->boss);
        }
    t3d_matrix_pop(1);

    rdpq_sync_pipe();
    rdpq_mode_zbuf(false, false);

    t3d_matrix_push_pos(1);
        if (ctx->sunshaftsMatrix && ctx->sunshaftsDpl) {
            t3d_matrix_set(ctx->sunshaftsMatrix, true);
            rspq_block_run(ctx->sunshaftsDpl);
        }

        if (ctx->pillarsFrontMatrix && ctx->pillarsFrontDpl) {
            t3d_matrix_set(ctx->pillarsFrontMatrix, true);
            rspq_block_run(ctx->pillarsFrontDpl);
        }
    t3d_matrix_pop(1);

    if (drawTransitionAfterTwoSeconds && cutsceneTimer >= 2.0f) {
        if (ctx->screenTransition && *ctx->screenTransition) {
            display_utility_solid_black_transition(false, 200.0f);
        }
    }

    cutscene_manager_draw_dialog();
}

// ------------------------------------------------------------
// Load / unload
// ------------------------------------------------------------

void cutscene_guardian_phase2_load(void)
{
    if (s_loaded) return;

    s_floorSpinBnwYaw = 0.0f;

    // BNW chains

    s_bnwChainsModel = t3d_model_load("rom:/boss/boss_bnw_chains2.t3dm");

    if (s_bnwChainsModel) {
        s_bnwChainsSkeleton = malloc_uncached(sizeof(T3DSkeleton));
        *s_bnwChainsSkeleton = t3d_skeleton_create(s_bnwChainsModel);

        const char *animationNames[BNW_CHAINS_ANIM_COUNT] = {
            "Molting"
        };

        s_bnwChainsAnimations = malloc_uncached(BNW_CHAINS_ANIM_COUNT * sizeof(T3DAnim*));

        for (int i = 0; i < BNW_CHAINS_ANIM_COUNT; i++) {
            s_bnwChainsAnimations[i] = malloc_uncached(sizeof(T3DAnim));
            *s_bnwChainsAnimations[i] = t3d_anim_create(s_bnwChainsModel, animationNames[i]);
            t3d_anim_attach(s_bnwChainsAnimations[i], s_bnwChainsSkeleton);
        }

        s_currentBnwChainsAnimation = 0;
        t3d_anim_set_looping(s_bnwChainsAnimations[s_currentBnwChainsAnimation], true);
        t3d_anim_set_playing(s_bnwChainsAnimations[s_currentBnwChainsAnimation], true);

        rspq_block_begin();
            t3d_model_draw_skinned(s_bnwChainsModel, s_bnwChainsSkeleton);
        s_bnwChainsDpl = rspq_block_end();

        s_bnwChainsMatrix = cutscene_guardian_phase2_alloc_matrix();
        cutscene_guardian_phase2_set_matrix_origin(s_bnwChainsMatrix);
    }

    // BNW boss
    s_bnwBossModel = t3d_model_load("rom:/boss/boss_bnw.t3dm");
    cutscene_guardian_phase2_make_model_block(s_bnwBossModel, &s_bnwBossDpl);

    s_bnwBossMatrix = cutscene_guardian_phase2_alloc_matrix();
    cutscene_guardian_phase2_set_matrix_default(s_bnwBossMatrix, -1.0f);

    // BNW floor glow
    s_floorGlowBnwModel = t3d_model_load("rom:/boss/boss_bnw_floor_glow1.t3dm");
    cutscene_guardian_phase2_make_model_block(s_floorGlowBnwModel, &s_floorGlowBnwDpl);

    s_floorGlowBnwMatrix = cutscene_guardian_phase2_alloc_matrix();
    cutscene_guardian_phase2_set_matrix_default(s_floorGlowBnwMatrix, -1.0f);

    // BNW floor spin
    s_floorSpinBnwModel = t3d_model_load("rom:/boss/boss_bnw_floor_spin_glow.t3dm");
    cutscene_guardian_phase2_make_model_block(s_floorSpinBnwModel, &s_floorSpinBnwDpl);

    s_floorSpinBnwMatrix = cutscene_guardian_phase2_alloc_matrix();
    cutscene_guardian_phase2_set_matrix_default(s_floorSpinBnwMatrix, -1.0f);

    //Shackled sun
    s_shackledSunModel = t3d_model_load("rom:/shackled_sun/shackled_sun.t3dm");
    cutscene_guardian_phase2_make_model_block(s_shackledSunModel, &s_shackledSunDpl);

    s_shackledSunMatrix = cutscene_guardian_phase2_alloc_matrix();
    cutscene_guardian_phase2_set_matrix_origin(s_shackledSunMatrix);

    // Shackled sun glow
    s_shackledSunGlowModel = t3d_model_load("rom:/shackled_sun/shackled_sun_rays.t3dm");
    cutscene_guardian_phase2_make_model_block(s_shackledSunGlowModel, &s_shackledSunGlowDpl);

    s_shackledSunGlowMatrix = cutscene_guardian_phase2_alloc_matrix();
    cutscene_guardian_phase2_set_matrix_origin(s_shackledSunGlowMatrix);

    // Shackles
    s_shacklesModel = t3d_model_load("rom:/shackled_sun/shackles.t3dm");
    cutscene_guardian_phase2_make_model_block(s_shacklesModel, &s_shacklesDpl);

    s_shacklesMatrix = cutscene_guardian_phase2_alloc_matrix();
    cutscene_guardian_phase2_set_matrix_origin(s_shacklesMatrix);

    s_shackles2Matrix = cutscene_guardian_phase2_alloc_matrix();
    cutscene_guardian_phase2_set_matrix_origin_inverted(s_shackles2Matrix);

    s_loaded = true;
}

void cutscene_guardian_phase2_unload(void)
{
    if (!s_loaded) return;

    rspq_wait();

    cutscene_guardian_phase2_free_dpl(&s_bnwBossDpl);
    cutscene_guardian_phase2_free_dpl(&s_bnwChainsDpl);
    cutscene_guardian_phase2_free_dpl(&s_floorGlowBnwDpl);
    cutscene_guardian_phase2_free_dpl(&s_floorSpinBnwDpl);
    cutscene_guardian_phase2_free_dpl(&s_shackledSunDpl);
    cutscene_guardian_phase2_free_dpl(&s_shackledSunGlowDpl);
    cutscene_guardian_phase2_free_dpl(&s_shacklesDpl);

    cutscene_guardian_phase2_free_bnw_chains_anim();

    cutscene_guardian_phase2_free_matrix(&s_bnwBossMatrix);
    cutscene_guardian_phase2_free_matrix(&s_bnwChainsMatrix);
    cutscene_guardian_phase2_free_matrix(&s_floorGlowBnwMatrix);
    cutscene_guardian_phase2_free_matrix(&s_floorSpinBnwMatrix);
    cutscene_guardian_phase2_free_matrix(&s_shackledSunMatrix);
    cutscene_guardian_phase2_free_matrix(&s_shackledSunGlowMatrix);
    cutscene_guardian_phase2_free_matrix(&s_shacklesMatrix);
    cutscene_guardian_phase2_free_matrix(&s_shackles2Matrix);

    cutscene_guardian_phase2_free_model(&s_bnwBossModel);
    cutscene_guardian_phase2_free_model(&s_bnwChainsModel);
    cutscene_guardian_phase2_free_model(&s_floorGlowBnwModel);
    cutscene_guardian_phase2_free_model(&s_floorSpinBnwModel);
    cutscene_guardian_phase2_free_model(&s_shackledSunModel);
    cutscene_guardian_phase2_free_model(&s_shackledSunGlowModel);
    cutscene_guardian_phase2_free_model(&s_shacklesModel);

    s_currentBnwChainsAnimation = 0;
    s_floorSpinBnwYaw = 0.0f;

    s_loaded = false;
}

// ------------------------------------------------------------
// Enter
// ------------------------------------------------------------

void cutscene_guardian_phase2_enter(SceneContext *ctx, CutsceneState state)
{
    if (!ctx) return;

    cutscene_guardian_phase2_load();

    skipButtonVisible = false;

    switch (state) {
        case CUTSCENE_PHASE2_INTRO: {
            if (ctx->boss) {
                ctx->boss->pos[0] = 0.0f;
                ctx->boss->pos[1] = 1.0f;
                ctx->boss->pos[2] = 0.0f;

                ctx->boss->rot[0] = 0.0f;
                ctx->boss->rot[1] = 0.0f;
                ctx->boss->rot[2] = 0.0f;

                ctx->boss->state = BOSS_STATE_INTRO;
                ctx->boss->stateTimer = 0.0f;
                ctx->boss->isAttacking = false;
                ctx->boss->handAttackColliderActive = false;
                ctx->boss->sphereAttackColliderActive = false;
                ctx->boss->velX = 0.0f;
                ctx->boss->velZ = 0.0f;

                cutscene_manager_set_boss_anim(ctx, BOSS_ANIM_PHASE2_COLLAPSE);
            }

            camera_mode(CAMERA_CUSTOM);

            if (ctx->screenTransition) {
                *ctx->screenTransition = true;
            }

            startScreenFade = true;

            if (ctx->gameState) {
                *ctx->gameState = GAME_STATE_PLAYING;
            }

            audio_stop_music();
            audio_play_music("rom:/audio/music/boss_phase2_cutscene2-22k.wav64", false);
            audio_set_music_volume(10);

            cutscene_manager_set_camera_shot(
                ctx,
                    (T3DVec3){{-20.87f, 6.37f, -39.34f}},
                    (T3DVec3){{-15.44f, 23.37f, -23.00f}},
                    (T3DVec3){{1.91f, 50.14f, 71.61f}}
            );
        } break;

        case CUTSCENE_PHASE2_KNEEL: {
            cutscene_manager_set_boss_anim(ctx, BOSS_ANIM_PHASE2_COLLAPSE_IDLE);

            cutscene_manager_begin_dialog(
                cutscene_guardian_phase2_get_dialog(0),
                0.0f
            );
        } break;

        case CUTSCENE_PHASE2_BLURB: {
            cutscene_manager_set_boss_anim(ctx, BOSS_ANIM_PHASE2_COLLAPSE_IDLE);

            cutscene_manager_begin_dialog(
                cutscene_guardian_phase2_get_dialog(1),
                0.0f
            );

            if (ctx->boss) {
                cutscene_manager_set_camera_shot(
                    ctx,
                    (T3DVec3){{-15.44f, 23.37f, -23.00f}},
                    (T3DVec3){{-36.95f, 18.85f, -1.9f}},
                    (T3DVec3){{ctx->boss->pos[0] - 10.0f, ctx->boss->pos[1] + 30.0f, ctx->boss->pos[2]}}
                );
            }
        } break;

        case CUTSCENE_PHASE2_MIND: {

            if (ctx->screenTransition) {
                *ctx->screenTransition = true;
            }

            startScreenFade = true;

            cutscene_manager_begin_dialog(
                cutscene_guardian_phase2_get_dialog(2),
                0.0f
            );

            cutscene_manager_set_camera_shot(
                ctx,
                    (T3DVec3){{-36.95f, 18.85f, -1.9f}},
                    (T3DVec3){{-16.97f, 28.0f, -1.6f}},
                    (T3DVec3){{53.87f, 60.67f, 0.0f}}
            );
        } break;

        case CUTSCENE_PHASE2_SHACKLED_SUN: {
            if (ctx->screenTransition) {
                *ctx->screenTransition = true;
            }

            startScreenFade = true;

            cutscene_manager_begin_dialog(
                cutscene_guardian_phase2_get_dialog(3),
                0.0f
            );

            cutscene_manager_set_camera_shot(
                ctx,
                    (T3DVec3){{-35.0f, 0.0f, 0.0f}},
                    (T3DVec3){{-25.0f, 0.0f, 0.0f}},
                    (T3DVec3){{100.0f, 0.0f, 0.0f}}
            );
        } break;

        case CUTSCENE_PHASE2_BURN: {
            cutscene_manager_set_boss_anim(ctx, BOSS_ANIM_PHASE2_WIN_KNEEL);

            cutscene_manager_begin_dialog(
                cutscene_guardian_phase2_get_dialog(4),
                0.0f
            );

            cutscene_manager_set_camera_shot(
                ctx,
                    (T3DVec3){{-50.0f, 20.0f, -10.0f}},
                    (T3DVec3){{-40.0f, 20.0f, -10.0f}},
                    (T3DVec3){{100.0f, 50.0f, 0.0f}}
            );
        } break;

        case CUTSCENE_PHASE2_BNW: {
            s_currentBnwChainsAnimation = 0;

            if (s_bnwChainsAnimations && s_bnwChainsAnimations[s_currentBnwChainsAnimation]) {
                t3d_anim_set_looping(s_bnwChainsAnimations[s_currentBnwChainsAnimation], true);
                t3d_anim_set_playing(s_bnwChainsAnimations[s_currentBnwChainsAnimation], true);
            }

            cutscene_manager_set_camera_shot(
                ctx,
                    (T3DVec3){{-40.0f, 20.0f, -10.0f}},
                    (T3DVec3){{-90.0f, 10.0f, -10.0f}},
                    (T3DVec3){{100.0f, 50.0f, 0.0f}}
            );

            lightning_fx_system_ring_config(
                40.0f,
                100.0f,
                0.0f,
                0.15f,
                0.60f
            );

            lightning_fx_system_ring_enable(true);

            cutscene_manager_begin_dialog(
                cutscene_guardian_phase2_get_dialog(5),
                0.0f
            );

            joypad_rumble_pulse_seconds(15.0f);

            if (ctx->screenTransition) {
                *ctx->screenTransition = true;
            }

            startScreenFade = true;
        } break;

        case CUTSCENE_PHASE2_END: {
            if (ctx->screenTransition) {
                *ctx->screenTransition = true;
            }

            startScreenFade = true;

            cutscene_manager_set_boss_anim(ctx, BOSS_ANIM_PHASE2_REVEAL);

            cutscene_manager_clear_dialog();

            if (ctx->boss) {
                cutscene_manager_set_camera_shot(
                    ctx,
                    (T3DVec3){{-92.38f, 32.0f, 4.65f}},
                    (T3DVec3){{-149.0f, 28.58f, 3.7f}},
                    (T3DVec3){{ctx->boss->pos[0], ctx->boss->pos[1] + 21.35f, ctx->boss->pos[2]}}
                );
            }
        } break;

        default:
            break;
    }
}

// ------------------------------------------------------------
// Update
// ------------------------------------------------------------

void cutscene_guardian_phase2_update(SceneContext *ctx, float dt)
{
    if (!ctx) return;

    cutsceneTimer += dt;

    if (cutsceneState != CUTSCENE_NONE) {
        cutsceneCameraTimer += dt;
    }

    cutscene_manager_update_boss_transform(ctx);

    switch (cutsceneState) {
        case CUTSCENE_PHASE2_INTRO: {
            cutscene_manager_update_camera(4.0f);

            if (cutsceneTimer >= 7.0f) {
                cutscene_guardian_phase2_next_state(ctx, CUTSCENE_PHASE2_KNEEL);
                return;
            }
        } break;

        case CUTSCENE_PHASE2_KNEEL: {
            cutscene_manager_update_dialog();

            if (cutsceneTimer >= 7.0f) {
                cutscene_guardian_phase2_next_state(ctx, CUTSCENE_PHASE2_BLURB);
                return;
            }
        } break;

        case CUTSCENE_PHASE2_BLURB: {
            cutscene_manager_update_camera(5.0f);

            cutscene_manager_update_dialog();

            if (cutsceneTimer >= 5.0f) {
                cutscene_guardian_phase2_next_state(ctx, CUTSCENE_PHASE2_MIND);
                return;
            }
        } break;

        case CUTSCENE_PHASE2_MIND: {
            cutscene_manager_update_camera(4.0f);

            cutscene_manager_update_dialog();

            if (cutsceneTimer >= 4.0f) {
                cutscene_guardian_phase2_next_state(ctx, CUTSCENE_PHASE2_SHACKLED_SUN);
                return;
            }
        } break;

        case CUTSCENE_PHASE2_SHACKLED_SUN: {
            cutscene_manager_update_camera(8.0f);

            cutscene_manager_update_dialog();
            screen_shake_set_shake_mag(0.1f);

            if (cutsceneTimer >= 10.0f) {
                cutscene_guardian_phase2_next_state(ctx, CUTSCENE_PHASE2_BURN);
                return;
            }
        } break;

        case CUTSCENE_PHASE2_BURN: {
            cutscene_manager_update_camera(2.8f);

            cutscene_manager_update_dialog();

            if (cutsceneTimer >= 2.8f) {
                cutscene_guardian_phase2_next_state(ctx, CUTSCENE_PHASE2_BNW);
                return;
            }
        } break;

        case CUTSCENE_PHASE2_BNW: {
            if (s_bnwChainsAnimations && s_bnwChainsAnimations[s_currentBnwChainsAnimation]) {
                t3d_anim_update(s_bnwChainsAnimations[s_currentBnwChainsAnimation], dt);
            }

            if (s_bnwChainsSkeleton) {
                t3d_skeleton_update(s_bnwChainsSkeleton);
            }

            cutscene_manager_update_camera(15.0f);

            const float spinSpeed = -20.0f;
            s_floorSpinBnwYaw += dt * spinSpeed;

            if (s_floorSpinBnwYaw > T3D_PI * 2.0f) {
                s_floorSpinBnwYaw -= T3D_PI * 2.0f;
            }

            if (s_floorSpinBnwMatrix) {
                t3d_mat4fp_from_srt_euler(
                    s_floorSpinBnwMatrix,
                    (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
                    (float[3]){0.0f, s_floorSpinBnwYaw, 0.0f},
                    (float[3]){0.0f, ctx->roomY, 0.0f}
                );
            }

            lightning_fx_system_update(dt);
            screen_shake_set_shake_mag(0.2f);
            cutscene_manager_update_dialog();

            if (cutsceneTimer >= 15.0f) {
                cutscene_guardian_phase2_next_state(ctx, CUTSCENE_PHASE2_END);
                return;
            }
        } break;

        case CUTSCENE_PHASE2_END: {
            cutscene_manager_update_camera(10.0f);

            if (cutsceneTimer >= 5.0f && ctx->boss) {
                float t = cutsceneCameraTimer / 10.0f;
                if (t > 1.0f) t = 1.0f;

                float easeT = t * t * (3.0f - 2.0f * t);

                customCamTarget.v[0] = ctx->boss->pos[0];
                customCamTarget.v[2] = ctx->boss->pos[2];

                float y0 = ctx->boss->pos[1];
                float y1 = ctx->boss->pos[1] + 37.9f;

                customCamTarget.v[1] = y0 + (y1 - y0) * easeT;
            }

            if (cutsceneTimer >= 10.0f) {
                if (ctx->finish_phase2_cutscene) {
                    ctx->finish_phase2_cutscene();
                }

                cutscene_guardian_phase2_unload();
                return;
            }
        } break;

        default:
            break;
    }
}

void cutscene_guardian_phase2_skip(SceneContext *ctx)
{
    if (!ctx) return;

    cutscene_manager_clear_dialog();

    if (ctx->finish_phase2_cutscene) {
        ctx->finish_phase2_cutscene();
    }

    cutscene_guardian_phase2_unload();
}

// ------------------------------------------------------------
// Draw
// ------------------------------------------------------------

void cutscene_guardian_phase2_draw(SceneContext *ctx, T3DViewport *viewport)
{
    (void)viewport;

    if (!ctx) return;

    switch (cutsceneState) {
        case CUTSCENE_PHASE2_MIND: {
            cutscene_guardian_phase2_draw_standard_room(ctx, true);
        } break;

        case CUTSCENE_PHASE2_KNEEL:
        case CUTSCENE_PHASE2_BLURB:
        case CUTSCENE_PHASE2_BURN:
        case CUTSCENE_PHASE2_INTRO: {
            cutscene_guardian_phase2_draw_standard_room(ctx, false);
        } break;

        case CUTSCENE_PHASE2_SHACKLED_SUN: {
            rdpq_sync_pipe();
            rdpq_mode_zbuf(false, false);

            t3d_matrix_push_pos(1);
                if (s_shackledSunGlowMatrix && s_shackledSunGlowModel) {
                    t3d_matrix_set(s_shackledSunGlowMatrix, true);
                    t3d_model_draw_custom(s_shackledSunGlowModel, (T3DModelDrawConf){
                        .userData = &s_shackledSunGlowScrollParams,
                        .tileCb = tile_scroll,
                    });
                }
            t3d_matrix_pop(1);

            t3d_matrix_push_pos(1);
                if (s_shackles2Matrix && s_shacklesModel) {
                    t3d_matrix_set(s_shackles2Matrix, true);
                    t3d_model_draw_custom(s_shacklesModel, (T3DModelDrawConf){
                        .userData = &s_shackles2ScrollParams,
                        .tileCb = tile_scroll,
                    });
                }

                if (s_shackledSunMatrix && s_shackledSunDpl) {
                    t3d_matrix_set(s_shackledSunMatrix, true);
                    rspq_block_run(s_shackledSunDpl);
                }

                if (s_shacklesMatrix && s_shacklesModel) {
                    t3d_matrix_set(s_shacklesMatrix, true);
                    t3d_model_draw_custom(s_shacklesModel, (T3DModelDrawConf){
                        .userData = &s_shacklesScrollParams,
                        .tileCb = tile_scroll,
                    });
                }
            t3d_matrix_pop(1);

            if (ctx->screenTransition && *ctx->screenTransition) {
                display_utility_solid_black_transition(true, 100.0f);
            }

            cutscene_manager_draw_dialog();
        } break;

        case CUTSCENE_PHASE2_BNW: {
            rdpq_sync_pipe();
            rdpq_mode_zbuf(true, true);

            rdpq_set_prim_color(RGBA32(0, 0, 0, 255));

            t3d_matrix_push_pos(1);
                if (s_bnwBossMatrix && s_bnwBossDpl) {
                    t3d_matrix_set(s_bnwBossMatrix, true);
                    rspq_block_run(s_bnwBossDpl);
                }
            t3d_matrix_pop(1);

            rdpq_set_prim_color(RGBA32(0, 0, 0, 255));

            t3d_matrix_push_pos(1);
                if (s_bnwChainsMatrix && s_bnwChainsDpl) {
                    t3d_matrix_set(s_bnwChainsMatrix, true);
                    rspq_block_run(s_bnwChainsDpl);
                }
            t3d_matrix_pop(1);

            rdpq_set_prim_color(RGBA32(0, 0, 0, 255));

            t3d_matrix_push_pos(1);
                if (s_floorSpinBnwMatrix && s_floorSpinBnwDpl) {
                    t3d_matrix_set(s_floorSpinBnwMatrix, true);
                    rspq_block_run(s_floorSpinBnwDpl);
                }

                rdpq_set_prim_color(RGBA32(0, 0, 0, 255));

                if (s_floorGlowBnwMatrix && s_floorGlowBnwModel) {
                    t3d_matrix_set(s_floorGlowBnwMatrix, true);
                    t3d_model_draw_custom(s_floorGlowBnwModel, (T3DModelDrawConf){
                        .userData = &s_floorGlowBnwScrollParams,
                        .tileCb = tile_scroll,
                    });
                }
            t3d_matrix_pop(1);

            lightning_fx_system_draw();

            rdpq_sync_pipe();

            if (cutsceneTimer >= 12.0f) {
                if (ctx->screenTransition && *ctx->screenTransition) {
                    display_utility_solid_black_transition(false, 150.0f);
                }
            }

            if (cutsceneTimer <= 9.0f) {
                cutscene_manager_draw_dialog();
            }
        } break;

        case CUTSCENE_PHASE2_END: {
            rdpq_sync_pipe();
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

                if (ctx->pillarsMatrix && ctx->pillarsDpl) {
                    t3d_matrix_set(ctx->pillarsMatrix, true);
                    rspq_block_run(ctx->pillarsDpl);
                }

                if (ctx->bossChainsGlowMatrix && ctx->bossChainsGlowModel) {
                    t3d_matrix_set(ctx->bossChainsGlowMatrix, true);
                    t3d_model_draw_custom(ctx->bossChainsGlowModel, (T3DModelDrawConf){
                        .userData = ctx->bossChainsGlowScrollParams,
                        .tileCb = tile_scroll,
                    });
                }
            t3d_matrix_pop(1);

            rdpq_sync_pipe();
            rdpq_mode_zbuf(true, true);

            t3d_matrix_push_pos(1);
                if (ctx->boss) {
                    boss_draw(ctx->boss);
                }
            t3d_matrix_pop(1);

            rdpq_sync_pipe();
            rdpq_mode_zbuf(false, false);

            t3d_matrix_push_pos(1);
                if (ctx->boss) {
                    boss_draw_shadow(ctx->boss);
                }

                if (ctx->sunshaftsMatrix && ctx->sunshaftsDpl) {
                    t3d_matrix_set(ctx->sunshaftsMatrix, true);
                    rspq_block_run(ctx->sunshaftsDpl);
                }
            t3d_matrix_pop(1);

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

void cutscene_guardian_phase2_draw_fog(void)
{
    switch (cutsceneState) {
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