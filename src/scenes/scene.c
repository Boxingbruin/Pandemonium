#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include <math.h>
#include <string.h>
#include <stdio.h>

#include "scene.h"
#include "scene_sfx.h"
#include "scene_controller.h"

#include "audio_controller.h"

#include "camera_controller.h"

#include "joypad_utility.h"
#include "general_utility.h"
#include "game_lighting.h"
#include "game_time.h"

#include "globals.h"
#include "../utilities/button_prompt_utility.h"

#include "../managers/cutscene_manager.h"
#include "../managers/cutscene_manager_internal.h"
#include "scene_context.h"

#include "character.h"
#include "character_ui.h"
#include "../game/bosses/boss.h"
#include "../game/bosses/boss_ai.h"
#include "../game/bosses/boss_render.h"
#include "../game/bosses/boss_ui.h"
#include "../game/bosses/environmental_effects/boss_ground_crush.h"
#include "../controllers/dialog_controller.h"
#include "display_utility.h"
#include "menu_controller.h"
#include "save_controller.h"
#include "collision_system.h"
#include "letterbox_utility.h"
#include "utilities/sword_trail.h"
#include "utilities/animation_utility.h"

// TODO: This should not be declared in the header file, as it is only used externally (temp)
#include "dev.h"
#include "debug_draw.h"
#include "utilities/simple_collision_utility.h"

#include "video_player_utility.h"

#include "multi_sword_attacks.h" // TODO: call only from boss
#include "fx/lightning_fx.h"
#include "systems/particle_system.h"
#include "fx/dust_particles_fx.h"
#include "fx/blood_particles_fx.h"
//#include "boulder_hazard.h" // close-range ground-boulder hazard

// Forward declaration: scene_init() and scene_restart() start the Guardian intro.
void scene_init_cutscene(void);

T3DModel* mapModel;
rspq_block_t* mapDpl;
T3DMat4FP* mapMatrix;

T3DModel* sunshaftsModel;
rspq_block_t* sunshaftsDpl;
T3DMat4FP* sunshaftsMatrix;

T3DModel* pillarsModel;
rspq_block_t* pillarsDpl;
T3DMat4FP* pillarsMatrix;

T3DModel* pillarsFrontModel;
rspq_block_t* pillarsFrontDpl;
T3DMat4FP* pillarsFrontMatrix;

T3DModel* chainsModel;
rspq_block_t* chainsDpl;
T3DMat4FP* chainsMatrix;

T3DModel* fogDoorModel;
rspq_block_t* fogDoorDpl;
T3DMat4FP* fogDoorMatrix;
ScrollParams fogScrollParams = {
    .xSpeed = 0.0f,
    .ySpeed = 10.0f,
    .scale  = 64
};

T3DModel* windowsModel;
rspq_block_t* windowsDpl;
T3DMat4FP* windowsMatrix;

T3DModel* roomLedgeModel;
rspq_block_t* roomLedgeDpl;
T3DMat4FP* roomLedgeMatrix;

T3DModel* roomFloorModel;
rspq_block_t* roomFloorDpl;
T3DMat4FP* roomFloorMatrix;

T3DModel* floorGlowModel;
rspq_block_t* floorGlowDpl;
T3DMat4FP* floorGlowMatrix;
ScrollParams floorGlowScrollParams = {
    .xSpeed = 0.0f,
    .ySpeed = 10.0f,
    .scale  = 64
};

//======== PHASE 2 ========

// static T3DModel* bossChainsModel;
// static rspq_block_t* bossChainsDpl;
// static T3DMat4FP* bossChainsMatrix;
// static T3DSkeleton* bossChainsSkeleton;
// static T3DAnim** bossChainsAnimations = NULL;
// static int currentBossChainsAnimation = 0;
// static bool bossChainsVisible = true;

T3DModel* bossChainsGlowModel;
rspq_block_t* bossChainsGlowDpl;
T3DMat4FP* bossChainsGlowMatrix;
ScrollParams bossChainsGlowScrollParams = {
    .xSpeed = 0.0f,
    .ySpeed = 20.0f,
    .scale  = 64
};

//==========================

// Guardian room vertical offset
static float roomY = -1.0f;

// Cutscene/screen transition state shared with Guardian cutscene context.
static bool screenTransition = false;

// Character yaw used when gameplay begins.
// The old title-facing yaw was +90 degrees; gameplay faces the opposite direction.
static const float GUARDIAN_CHARACTER_YAW = T3D_PI * 1.5f;


// ------------------------------------------------------------
// Video trigger AABB at world origin
// ------------------------------------------------------------
static float videoTrigMin[3] = { 502.7f, 0.0f,  -25.0f };
static float videoTrigMax[3] = { 552.7f, 120.0f, 25.0f };

static bool videoTrigFired = false;
static bool videoTrigHitThisFrame = false;
static bool videoPendingPlay = false;

static VideoPrerollState videoPreroll = VIDEO_PREROLL_NONE;
static float videoPrerollTimer = 0.0f;

// tweakable
static const float VIDEO_BLACK_HOLD_S = 0.5f;
static const float VIDEO_FADE_SPEED   = 200.0f; // same scale you already use
static bool bossDeathMusicFadeStarted = false;

// Post-boss interaction ("restored") state
static bool bossPostDefeatTalkDone = false;
static bool bossWasDead = false; // Tracks death transition for one-time post-death cleanup

// Per-slot save stats: track one "run" (boss attempt) start time, and record clear time at death transition.
static bool s_bossRunActive = false;
static double s_bossRunStartS = 0.0;

// Post-boss interaction distances (XZ)
static const float POST_BOSS_PROMPT_DIST  = 140.0f;   // show A prompt and allow talk when inside this range

// ------------------------------------------------------------
// Cutscene music
// ------------------------------------------------------------
static bool s_pendingBossLoopMusic = false;
static const char *s_bossLoopMusicPath = "rom:/audio/music/boss_phase1-looping-22k.wav64";

// Require the character to be facing the boss for post-boss interaction.
// The third-person camera orbits independently of the character's body yaw,
// so checking the character's facing direction matches what the player sees:
// turning away with the left stick correctly hides the prompt and disables A.
static bool scene_character_facing_boss_xz(const Boss *boss, float minDot)
{
    if (!boss) return false;

    // Character forward (XZ) — matches character.c convention: (-sin(yaw), cos(yaw))
    float yaw = character.rot[1];
    float fwdX = -sinf(yaw);
    float fwdZ =  cosf(yaw);

    // Direction from character to boss (XZ)
    float toX = boss->pos[0] - character.pos[0];
    float toZ = boss->pos[2] - character.pos[2];
    float toLen = sqrtf(toX*toX + toZ*toZ);
    if (toLen < 0.001f) return true; // standing on top of it
    toX /= toLen;
    toZ /= toLen;

    float dot = fwdX * toX + fwdZ * toZ;
    return dot >= minDot;
}

// Within prompt distance (XZ). Controls visibility of the "A" prompt above the boss.
static bool scene_post_boss_in_range(const Boss *boss)
{
    if (!boss) return false;
    float dx = boss->pos[0] - character.pos[0];
    float dz = boss->pos[2] - character.pos[2];
    float d = sqrtf(dx*dx + dz*dz);
    return d <= POST_BOSS_PROMPT_DIST;
}

// In range AND facing the boss. Required for the A press to actually start the dialog.
static bool scene_post_boss_interact_allowed(const Boss *boss)
{
    if (!scene_post_boss_in_range(boss)) return false;
    return scene_character_facing_boss_xz(boss, 0.5f); // ~60° cone in front
}

// Boss title fade control (shown during intro, fades out when fight starts)
static float bossTitleFade = 0.0f;
static float bossTitleFadeSpeed = 1.8f;

// Victory title card ("Enemy restored") timing/state
static float victoryTitleTimer = 0.0f;
static bool victoryTitleDone = false;
static const float VICTORY_TITLE_FADEIN_S  = 0.75f;
static const float VICTORY_TITLE_HOLD_S    = 2.00f;
static const float VICTORY_TITLE_FADEOUT_S = 0.90f;

// progress for boss/player UIs
static float bossUiIntro = 1.0f;
static float playerUiIntro = 1.0f;
static float uiIntroSpeed = 1.5f;

// (cutsceneState, cutsceneTimer, cutsceneCameraTimer, cutsceneCamPosStart/End
// live in cutscene_manager.c — accessed via the internal header above.)
static Boss* g_boss = NULL;  // Boss instance pointer

// Game state management
static GameState gameState = GAME_STATE_PLAYING;
static bool lastMenuActive = false;

// Death screen restart lockout (prevents rapid A-mash from instantly restarting)
static float deathRestartLockoutTimer = 0.0f;
static const float DEATH_RESTART_LOCKOUT_S = 2.0f;

// Input state tracking
static bool lastAPressed = false;
static bool lastStartPressed = false;
static bool lastZPressed = false;
static bool lastLPressed = false;
static bool lastInteractAHeld = false;
static bool lastCLeftHeld = false;
static bool lastCRightHeld = false;

// Z-target cycling / tap-toggle behavior:
// - Tap Z toggles lock-on on/off
// - Hold Z keeps lock-on active and allows cycling targets with C-left/C-right
static float s_zHoldTimer = 0.0f;
static bool  s_zHoldConsumed = false;     // true if we used Z-hold for cycling this hold
static bool  s_zActivatedOnPress = false; // true if we turned lock-on ON on Z press
static const float Z_TAP_TOGGLE_MAX_S = 0.22f;

typedef enum {
    LOCK_TARGET_LOWERLEG_LEFT = 0,
    LOCK_TARGET_LOWERLEG_RIGHT,
    LOCK_TARGET_WAIST,
    LOCK_TARGET_COUNT
} LockTargetId;

// Current lock target selection (cycled via held-Z + C-left/C-right)
static int s_lockTargetIndex = LOCK_TARGET_WAIST;

// Victory title card background ("Enemy restored")
static sprite_t* victoryTitleBgSprite = NULL;
static surface_t victoryTitleBgSurf = {0};


static const char *SCENE1_SFX_PATHS[SCENE1_SFX_COUNT] = {
    [SCENE1_SFX_TITLE_WALK]  = "rom:/audio/sfx/title_screen_walk_effect-22k.wav64",

    [SCENE1_SFX_BOSS_SWING1] = "rom:/audio/sfx/boss/boss_swing1_22k.wav64",
    [SCENE1_SFX_BOSS_SWING2] = "rom:/audio/sfx/boss/boss_swing2_22k.wav64",
    [SCENE1_SFX_BOSS_SWING3] = "rom:/audio/sfx/boss/boss_swing3_22k.wav64",
    [SCENE1_SFX_BOSS_SWING4] = "rom:/audio/sfx/boss/boss_swing4_22k.wav64",

    [SCENE1_SFX_BOSS_SMASH1] = "rom:/audio/sfx/boss/boss_smash1_22k.wav64",
    [SCENE1_SFX_BOSS_SMASH2] = "rom:/audio/sfx/boss/boss_smash2_22k.wav64",
    [SCENE1_SFX_BOSS_SMASH3] = "rom:/audio/sfx/boss/boss_smash3_22k.wav64",

    [SCENE1_SFX_BOSS_LUNGE]  = "rom:/audio/sfx/boss/boss_lunge_attack_22k.wav64",
    [SCENE1_SFX_BOSS_LAND1]  = "rom:/audio/sfx/boss/boss_land1_22k.wav64",
    [SCENE1_SFX_BOSS_LAND2]  = "rom:/audio/sfx/boss/boss_land2_22k.wav64",

    [SCENE1_SFX_BOSS_STEP1]  = "rom:/audio/sfx/boss/boss_step1_22k.wav64",

    // Character SFX
    [SCENE1_SFX_CHAR_SWING1]         = "rom:/audio/sfx/character/char_swing1_22k.wav64",
    [SCENE1_SFX_CHAR_ATTACK_HIT1]    = "rom:/audio/sfx/character/char_attack_hit1_22k.wav64",
    [SCENE1_SFX_CHAR_ATTACK_HIT2]    = "rom:/audio/sfx/character/char_attack_hit2_22k.wav64",
    [SCENE1_SFX_CHAR_ATTACK_HIT3]    = "rom:/audio/sfx/character/char_attack_hit3_22k.wav64",
    [SCENE1_SFX_CHAR_ATTACK_HIT4]    = "rom:/audio/sfx/character/char_attack_hit4_22k.wav64",
    [SCENE1_SFX_CHAR_ATTACK_HIT5]    = "rom:/audio/sfx/character/char_attack_hit5_22k.wav64",
    [SCENE1_SFX_CHAR_ATTACK_HIT6]    = "rom:/audio/sfx/character/char_attack_hit6_22k.wav64",
    [SCENE1_SFX_CHAR_FOOTSTEP_RUN1]  = "rom:/audio/sfx/character/char_footstep_run1_22k.wav64",
    [SCENE1_SFX_CHAR_FOOTSTEP_RUN2]  = "rom:/audio/sfx/character/char_footstep_run2_22k.wav64",
    [SCENE1_SFX_CHAR_FOOTSTEP_RUN3]  = "rom:/audio/sfx/character/char_footstep_run3_22k.wav64",
    [SCENE1_SFX_CHAR_FOOTSTEP_RUN4]  = "rom:/audio/sfx/character/char_footstep_run4_22k.wav64",
    [SCENE1_SFX_CHAR_FOOTSTEP_WALK1] = "rom:/audio/sfx/character/char_footstep_walk1_22k.wav64",
    [SCENE1_SFX_CHAR_FOOTSTEP_WALK2] = "rom:/audio/sfx/character/char_footstep_walk2_22k.wav64",
    [SCENE1_SFX_CHAR_FOOTSTEP_WALK3] = "rom:/audio/sfx/character/char_footstep_walk3_22k.wav64",
    [SCENE1_SFX_CHAR_FOOTSTEP_WALK4] = "rom:/audio/sfx/character/char_footstep_walk4_22k.wav64",

    [SCENE1_SFX_CHAR_UMPH] = "rom:/audio/sfx/character/umph_22k.wav64",
};

static void scene_begin_video_preroll(void)
{
    if (videoTrigFired) return;

    videoTrigFired = true;
    videoTrigHitThisFrame = true;

    // start fade-to-black using your existing priming mechanism
    startScreenFade = true;              // primes fadeBlackAlpha inside display_utility
    videoPreroll = VIDEO_PREROLL_FADING_TO_BLACK;
    videoPrerollTimer = 0.0f;
}

static void scene_update_video_trigger(void)
{
    videoTrigHitThisFrame = false;
    if (videoTrigFired) return;

    float capA[3], capB[3], r;
    collision_get_character_capsule_world(capA, capB, &r);

    if (scu_capsule_vs_rect_f(capA, capB, r, videoTrigMin, videoTrigMax)) {
        scene_begin_video_preroll();
    }
}

static void scene_update_video_preroll(void)
{
    if (videoPreroll == VIDEO_PREROLL_NONE) return;

    videoPrerollTimer += deltaTime;

    if (videoPreroll == VIDEO_PREROLL_FADING_TO_BLACK) {
        if (videoPrerollTimer >= 3.0f) {
            videoPreroll = VIDEO_PREROLL_BLACK_HOLD;
            videoPrerollTimer = 0.0f;
        }
        return;
    }

    // VIDEO_PREROLL_BLACK_HOLD
    if (videoPrerollTimer >= VIDEO_BLACK_HOLD_S) {

        // IMPORTANT: ensure audio controller stops touching CHANNEL_MUSIC
        // (and doesn't later mixer_ch_stop it mid-video)
        gameState = GAME_STATE_VIDEO;
        video_player_request("rom:/video.h264");

        audio_set_music_volume(10);

        videoPreroll = VIDEO_PREROLL_NONE;
    }
}

void scene_load_environment(void)
{
    // ===== LOAD MAP =====
    mapModel = t3d_model_load("rom:/boss_room/room.t3dm");
    rspq_block_begin();
        t3d_model_draw(mapModel);
    mapDpl = rspq_block_end();

    mapMatrix = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_from_srt_euler(
        mapMatrix,
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, roomY, 0.0f}
    );

    // ===== LOAD PILLARS =====
    pillarsModel = t3d_model_load("rom:/boss_room/pillars.t3dm");
    rspq_block_begin();
        t3d_model_draw(pillarsModel);
    pillarsDpl = rspq_block_end();

    pillarsMatrix = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_from_srt_euler(
        pillarsMatrix,
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, roomY, 0.0f}
    );

    pillarsFrontModel = t3d_model_load("rom:/boss_room/pillars_front.t3dm");
    rspq_block_begin();
        t3d_model_draw(pillarsFrontModel);
    pillarsFrontDpl = rspq_block_end();

    pillarsFrontMatrix = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_from_srt_euler(
        pillarsFrontMatrix,
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, roomY, 0.0f}
    );

    // ===== LOAD LEDGE =====
    roomLedgeModel = t3d_model_load("rom:/boss_room/room_ledge_walls.t3dm");
    rspq_block_begin();
        t3d_model_draw(roomLedgeModel);
    roomLedgeDpl = rspq_block_end();

    roomLedgeMatrix = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_from_srt_euler(
        roomLedgeMatrix,
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, roomY, 0.0f}
    );

    // ===== LOAD WINDOWS =====
    windowsModel = t3d_model_load("rom:/boss_room/windows.t3dm");
    rspq_block_begin();
        t3d_model_draw(windowsModel);
    windowsDpl = rspq_block_end();

    windowsMatrix = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_from_srt_euler(
        windowsMatrix,
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, roomY, 0.0f}
    );

    // ===== LOAD PERSISTENT CEILING CHAINS =====
    chainsModel = t3d_model_load("rom:/boss_room/ceiling_chains.t3dm");
    rspq_block_begin();
        t3d_model_draw(chainsModel);
    chainsDpl = rspq_block_end();

    chainsMatrix = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_from_srt_euler(
        chainsMatrix,
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, roomY, 0.0f}
    );

    // ===== LOAD SUN SHAFTS =====
    sunshaftsModel = t3d_model_load("rom:/boss_room/sunshafts.t3dm");
    rspq_block_begin();
        t3d_model_draw(sunshaftsModel);
    sunshaftsDpl = rspq_block_end();

    sunshaftsMatrix = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_from_srt_euler(
        sunshaftsMatrix,
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, roomY, 0.0f}
    );

    // ===== LOAD FOG DOOR =====
    fogDoorModel = t3d_model_load("rom:/boss_room/fog.t3dm");
    rspq_block_begin();
        t3d_model_draw(fogDoorModel);
    fogDoorDpl = rspq_block_end();

    fogDoorMatrix = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_from_srt_euler(
        fogDoorMatrix,
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, roomY, 0.0f}
    );

    // ===== LOAD FLOOR GLOW =====
    floorGlowModel = t3d_model_load("rom:/boss_room/floor_glow.t3dm");
    rspq_block_begin();
        t3d_model_draw(floorGlowModel);
    floorGlowDpl = rspq_block_end();

    floorGlowMatrix = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_from_srt_euler(
        floorGlowMatrix,
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, roomY, 0.0f}
    );

    // ===== LOAD FLOOR =====
    roomFloorModel = t3d_model_load("rom:/boss_room/floor.t3dm");
    rspq_block_begin();
        t3d_model_draw(roomFloorModel);
    roomFloorDpl = rspq_block_end();

    roomFloorMatrix = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_from_srt_euler(
        roomFloorMatrix,
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, roomY, 0.0f}
    );

    // ===== LOAD BOSS CHAINS GLOW =====
    // Keep this here only if it is visible after the phase-2 cutscene / during gameplay.
    // If it is phase-2-cutscene-only, move it into cutscene_guardian_phase2.c instead.
    bossChainsGlowModel = t3d_model_load("rom:/boss/boss_chain_glow.t3dm");
    rspq_block_begin();
        t3d_model_draw(bossChainsGlowModel);
    bossChainsGlowDpl = rspq_block_end();

    bossChainsGlowMatrix = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_from_srt_euler(
        bossChainsGlowMatrix,
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, roomY, 0.0f}
    );

    // Global/system-level effect. Keep loaded if it is used outside one cutscene,
    // or move it later if it is phase-2-only.
    // lightning_fx_system_init("rom:/boss/boss_back_sword_lightning2.t3dm");
}

void scene_init(void)
{
    joypad_rumble_stop();

    // ==== Sounds ====

    audio_scene_load_paths(SCENE1_SFX_PATHS, SCENE1_SFX_COUNT);

    // ==== Camera ====

    cameraState = CAMERA_CUSTOM;
    lastCameraState = CAMERA_CUSTOM;

    // ==== Lighting ====
    game_lighting_initialize();
    colorAmbient[2] = 0xFF;
    colorAmbient[1] = 0xFF;
    colorAmbient[0] = 0xFF;
    colorAmbient[3] = 255;

    // Currently not using dir lights so ignore
    // colorDir[2] = 0xFF;
    // colorDir[1] = 0xFF;
    // colorDir[0] = 0xFF;
    // colorDir[3] = 0xFF;
    // lightDirVec = (T3DVec3){{-0.9833f, 0.1790f, -0.0318f}};
    // t3d_vec3_norm(&lightDirVec);

    scene_load_environment();

    g_boss = boss_spawn();
    if (!g_boss) {
        // Handle error
        return;
    }

    // The scene owns when the boss is commanded into/out of gameplay,
    // but the boss owns the actual active/cinematic/combat mode internally.
    boss_deactivate(g_boss);

    boss_ground_crush_init();

    // reset character
    character_reset();
    character_reset_button_state();

    // Initialize dialog controller
    dialog_controller_init();

    button_prompt_init();

    // Load victory title background (used for "Enemy restored")
    victoryTitleBgSprite = sprite_load("rom:/dialog-gradient.ia8.sprite");
    if (victoryTitleBgSprite) {
        victoryTitleBgSurf = sprite_get_pixels(victoryTitleBgSprite);
    }

    particle_system_init();
    dust_particles_fx_init();
    blood_particles_fx_init();

    character_ui_init();
    boss_ui_init();

    // Initialize and show letterbox bars for intro
    letterbox_init();
    letterbox_show(false);  // Show immediately without animation

    collision_init();


    dust_particles_fx_reset();
    blood_particles_fx_reset();

    //msa_init();

    // Guardian scene normal startup: entering the scene starts the phase-1 intro.
    gameState = GAME_STATE_PLAYING;
    cutsceneState = CUTSCENE_PHASE1_INTRO;
    cutsceneTimer = 0.0f;
    cutsceneCameraTimer = 0.0f;
    scene_init_cutscene();
    audio_stop_all_sfx();

    // DEBUG: uncomment to start the fight in phase 2
    // if (g_boss) g_boss->phaseIndex = 2;
    // phase2CutsceneTriggered = true;
}


static inline int scene_lockon_bone_index_for_target(LockTargetId target)
{
    if (!g_boss) return -1;
    switch (target) {
        case LOCK_TARGET_LOWERLEG_LEFT:  return g_boss->lowerLegLeftBoneIndex;
        case LOCK_TARGET_LOWERLEG_RIGHT: return g_boss->lowerLegRightBoneIndex;
        case LOCK_TARGET_WAIST:          return g_boss->waistBoneIndex;
        default:                         return -1;
    }
}

static inline bool scene_lockon_target_available(LockTargetId target)
{
    return scene_lockon_bone_index_for_target(target) >= 0;
}

static void scene_cycle_lock_target(int dir)
{
    if (LOCK_TARGET_COUNT <= 0) return;
    if (dir == 0) return;

    // Try to skip unavailable bones, but always terminate (max LOCK_TARGET_COUNT iterations).
    int idx = s_lockTargetIndex;
    for (int i = 0; i < LOCK_TARGET_COUNT; i++) {
        idx = (idx + dir) % LOCK_TARGET_COUNT;
        if (idx < 0) idx += LOCK_TARGET_COUNT;
        if (scene_lockon_target_available((LockTargetId)idx)) {
            s_lockTargetIndex = idx;
            return;
        }
    }
    // If nothing is available, keep current selection.
}

// Lock-on focus point for targeting.
static T3DVec3 get_boss_lock_focus_point(void)
{
    if (!g_boss) {
        return (T3DVec3){{0.0f, 0.0f, 0.0f}};
    }

    // 1) Selected target (if available)
    T3DVec3 worldPos;
    int bone = scene_lockon_bone_index_for_target((LockTargetId)s_lockTargetIndex);
    if (boss_get_bone_world_pos(g_boss, bone, &worldPos)) {
        return worldPos;
    }

    // Ultimate fallback: boss position with a small lift so the marker is not at the feet.
    return boss_get_fallback_lock_focus_point(g_boss);
}

void scene_reset(void)
{
    // Runtime state reset (no allocations / no frees)
    cutsceneState = CUTSCENE_NONE;
    cutsceneTimer = 0.0f;
    cutsceneCameraTimer = 0.0f;
    skipButtonVisible = false;
    lastCutsceneAPressed = false;
    // Note: skipButtonVisible is also used for title transition, so we reset it here
    if (g_boss) {
        boss_deactivate(g_boss);
    }
    phase2CutsceneTriggered = false;
    gameState = GAME_STATE_PLAYING;
    lastMenuActive = false;
    lastAPressed = false;
    lastStartPressed = false;
    lastZPressed = false;
    cameraLockOnActive = false;
    lastCLeftHeld = false;
    lastCRightHeld = false;
    s_zHoldTimer = 0.0f;
    s_zHoldConsumed = false;
    s_zActivatedOnPress = false;
    s_lockTargetIndex = LOCK_TARGET_WAIST;
    videoTrigFired = false;
    videoPendingPlay = false;

    videoPreroll = VIDEO_PREROLL_NONE;
    videoPrerollTimer = 0.0f;
    bossDeathMusicFadeStarted = false;
    videoTrigFired = false;
    videoPendingPlay = false;
    // Cutscene fade/transition state
    screenTransition = false;
    // UI state
    bossTitleFade = 0.0f;
    bossUiIntro = 1.0f;
    playerUiIntro = 1.0f;
    boss_ui_reset();
    character_ui_reset();
    boss_ui_set_intro(bossUiIntro);
    character_ui_set_intro(playerUiIntro);

    // Victory end-card state
    victoryTitleTimer = 0.0f;
    victoryTitleDone = false;

    // Death end-card state
    deathRestartLockoutTimer = 0.0f;

    // Post-boss interaction state
    bossPostDefeatTalkDone = false;
    bossPostDefeatDialogStep = 0;

    s_pendingBossLoopMusic = false;

    // Clear cutscene/attack effects that latch globally. Without these, a
    // mid-phase-2 death leaks state into the new run: leftover MSA swords keep
    // falling during phase 1, the BNW lightning ring stays armed, and the
    // shackled-sun screen shake never clears.
    //msa_init();
    lightning_fx_system_ring_enable(false);
    animation_utility_set_screen_shake_mag(0.0f);

    // Reset letterbox to show state for intro
    letterbox_show(false);

    // Reset run timer state (a new run will be started when we enter gameplay again)
    s_bossRunActive = false;
    s_bossRunStartS = 0.0;

    dust_particles_fx_reset();
    blood_particles_fx_reset();
    boss_ground_crush_reset();
}

static void scene_sync_input_edge_state(void)
{
    // Sync last-pressed to the current button state so held buttons don't cause "just pressed"
    // events immediately after restart.
    lastAPressed = btn.a;
    lastStartPressed = btn.start;
    // Use held state for Z so our lock-on toggle edge detection is reliable.
    lastZPressed = joypad.btn.z;
    // Use held-state for L since btn.* is "pressed this frame" in libdragon.
    lastLPressed = joypad.btn.l;
    // Use held state for interact-A edge detection (prevents missing presses).
    lastInteractAHeld = joypad.btn.a;
    lastCLeftHeld = joypad.btn.c_left;
    lastCRightHeld = joypad.btn.c_right;
    s_zHoldTimer = 0.0f;
    s_zHoldConsumed = false;
    s_zActivatedOnPress = false;
    lastCutsceneAPressed = btn.a;
}

static inline float scene_dist_xz(float ax, float az, float bx, float bz) {
    float dx = ax - bx;
    float dz = az - bz;
    return sqrtf(dx*dx + dz*dz);
}


static void scene_debug_force_boss_defeated(void)
{
    if (!g_boss) return;

    // Force boss into a fully stopped "dead" configuration.
    g_boss->health = 0.0f;
    g_boss->state = BOSS_STATE_DEAD;
    g_boss->stateTimer = 0.0f;
    g_boss->isAttacking = false;
    g_boss->attackAnimTimer = 0.0f;
    g_boss->handAttackColliderActive = false;
    g_boss->sphereAttackColliderActive = false;
    g_boss->velX = 0.0f;
    g_boss->velZ = 0.0f;
    g_boss->currentSpeed = 0.0f;

    // If the boss had any pending external AI requests, clear them too.
    g_boss->pendingRequests = 0;
    boss_set_mode(g_boss, BOSS_MODE_POST_DEFEAT);

    // Enter victory state so the "Enemy restored" end-card renders/advances.
    // This is primarily for debugging (L-trigger skip).
    if (scene_get_game_state() != GAME_STATE_VICTORY) {
        scene_set_game_state(GAME_STATE_VICTORY);
    } else {
        // If already in victory, restart the title animation.
        victoryTitleTimer = 0.0f;
        victoryTitleDone = false;
    }

    // Ensure we return to gameplay (no cutscene controlling camera/logic).
    cutsceneState = CUTSCENE_NONE;
    cutsceneTimer = 0.0f;
    cutsceneCameraTimer = 0.0f;
    cutsceneDialogActive = false;
    skipButtonVisible = false;

    // Keep post-defeat interaction available after skipping.
    bossPostDefeatTalkDone = false;
    bossPostDefeatDialogStep = 0;
}

bool scene_is_cutscene_active(void) {
    return cutsceneState != CUTSCENE_NONE;
}

bool scene_is_boss_active(void) {
    return boss_is_active(g_boss);
}

GameState scene_get_game_state(void) {
    return gameState;
}

void scene_set_game_state(GameState state) {
    if (state == gameState) return;

    GameState prev = gameState;
    gameState = state;

    // Death: lock out restart input for a short duration so late gameplay inputs don't
    // instantly skip the death screen straight back to title.
    if (state == GAME_STATE_DEAD && prev != GAME_STATE_DEAD) {
        deathRestartLockoutTimer = 0.0f;
    } else if (prev == GAME_STATE_DEAD && state != GAME_STATE_DEAD) {
        deathRestartLockoutTimer = 0.0f;
    }

    // Reset the victory title card whenever we enter or leave victory
    if (state == GAME_STATE_VICTORY && prev != GAME_STATE_VICTORY) {
        victoryTitleTimer = 0.0f;
        victoryTitleDone = false;
    } else if (prev == GAME_STATE_VICTORY && state != GAME_STATE_VICTORY) {
        victoryTitleTimer = 0.0f;
        victoryTitleDone = false;
    }
}

bool scene_is_menu_active(void) {
    return gameState == GAME_STATE_MENU;
}

void scene_restart(void)
{
    debugf("RESTART: Starting Guardian scene restart sequence\n");

    // 1) Stop running systems first (prevents update-on-freed state)
    audio_stop_all_sfx();
    audio_stop_music();
    dialog_controller_reset();

    // 2) Reset input edge-tracking (prevents phantom presses)
    character_reset_button_state();

    // 3) Reset gameplay entities (logic state)
    if (g_boss) boss_reset(g_boss);
    character_reset();

    // 4) Reset camera / lock-on and Guardian runtime flags
    camera_reset();
    camera_mode(CAMERA_CUSTOM);
    scene_reset();

    // 5) Re-enter Guardian intro runtime state (NO allocations / NO frees)
    gameState = GAME_STATE_PLAYING;
    cutsceneState = CUTSCENE_PHASE1_INTRO;
    cutsceneTimer = 0.0f;
    cutsceneCameraTimer = 0.0f;
    scene_init_cutscene();
    audio_stop_all_sfx();

    scene_sync_input_edge_state();

    debugf("RESTART: Guardian soft reset complete. cameraState=%d speaking=%s\n",
           cameraState, dialog_controller_speaking() ? "true" : "false");
}

void scene_init_playing(bool skippedCutscene)
{
    character.pos[0] = -320.43f;
    character.pos[1] = 4.0f;
    character.pos[2] = 0.0f;

    character.scale[0] = MODEL_SCALE * 0.5f;
    character.scale[1] = MODEL_SCALE * 0.5f;
    character.scale[2] = MODEL_SCALE * 0.5f;

    // Face towards boss
    character.rot[1] = GUARDIAN_CHARACTER_YAW;

    character_update_position();
    character_set_state(CHAR_STATE_NORMAL);
    character_set_velocity_xz(0.0f, 0.0f);

    // Skip dialog and cutscene
    dialog_controller_stop_speaking();

    cutsceneState = CUTSCENE_NONE;
    cutsceneTimer = 0.0f;
    cutsceneCameraTimer = 0.0f;
    cutsceneDialogActive = false;
    skipButtonVisible = false;
    lastCutsceneAPressed = false;

    if (g_boss) {
        boss_activate_combat(g_boss);
    }

    // Starting a new run (boss attempt).
    // Default save slot is 0 ("Save 1") until we have a UI to pick slots.
    (void)save_controller_increment_run_count();
    s_bossRunActive = true;
    s_bossRunStartS = nowS;

    // ---- Music handoff behavior ----
    // If the cutscene was skipped, slam immediately into looping music.
    // If not skipped, let the current (non-looping) cutscene track finish naturally,
    // then start the loop once it's done.
    if (skippedCutscene) {
        s_pendingBossLoopMusic = false;  // we are starting it now
        audio_stop_music();
        audio_play_music(s_bossLoopMusicPath, true);
    } else {
        // Do NOT stop the current music. Just arm the handoff.
        s_pendingBossLoopMusic = true;
    }

    // Hide letterbox bars with animation
    letterbox_hide();

    // Return camera control to the player
    camera_mode_smooth(CAMERA_CHARACTER, 1.0f);
    cameraLockOnActive = true;

    // Start boss title fully visible so it slides up on gameplay start
    bossTitleFade = 1.0f;

    // Reset UI intro animations (they will slide/fade into view)
    bossUiIntro = 0.0f;
    playerUiIntro = 0.0f;
    boss_ui_set_intro(bossUiIntro);
    character_ui_set_intro(playerUiIntro);

#if DEBUG_BOULDER_ATTACK_FIRST
    // Debug: immediately open phase 1 with the boulder attack so the hazard
    // can be observed without waiting for the AI to naturally select it.
    if (g_boss) {
        g_boss->state                    = BOSS_STATE_ATTACK1;
        g_boss->stateTimer               = 0.0f;
        g_boss->attack1Cooldown          = 6.0f;
        g_boss->attackCooldown           = 1.0f;
        g_boss->isAttacking              = true;
        g_boss->attackAnimTimer          = 0.0f;
        g_boss->animationTransitionTimer = 0.0f;
        g_boss->currentAttackHasHit      = false;
        g_boss->velX                     = 0.0f;
        g_boss->velZ                     = 0.0f;
        g_boss->currentAttackName        = "Attack1";
        g_boss->attackNameDisplayTimer   = 2.0f;
        g_boss->currentAttackId          = BOSS_ATTACK_ATTACK1;
    }
#endif
}

void scene_dev_warp_to_fight(void)
{
    gameState = GAME_STATE_PLAYING;
    cutscene_manager_reset();
    scene_init_playing(true);
}

void scene_dev_warp_to_pre_phase2(void)
{
    scene_dev_warp_to_fight();
    // Drop boss to just above the phase-2 trigger (40% of maxHealth) so the next
    // hit kicks off the phase 2 cutscene chain.
    if (g_boss) {
        g_boss->health = g_boss->maxHealth * 0.42f;
    }
}

// End of phase2 cutscene teardown
static void scene_finish_phase2_cutscene(void)
{
    lightning_fx_system_ring_enable(false);
    animation_utility_set_screen_shake_mag(0.0f);
    joypad_rumble_stop();
    dialog_controller_stop_speaking();
    cutsceneDialogActive = false;

    if (g_boss) {
        boss_activate_combat(g_boss);

        g_boss->phaseIndex = 2;
        g_boss->handAttackColliderActive = false;
        g_boss->sphereAttackColliderActive = false;
        g_boss->velX = 0.0f;
        g_boss->velZ = 0.0f;

        // Phase 2 opens with the aerial sword barrage.
        boss_ai_start_aerial_sword_barrage(g_boss);
    }

    // Phase 2 cutscene music was started non-looping; arm the boss loop to take over.
    s_pendingBossLoopMusic = true;

    // Long camera blend so the cinematic angle hold
    camera_mode_smooth(CAMERA_CHARACTER, 5.0f);
    cameraLockOnActive = true;

    cutsceneTimer = 0.0f;
    cutsceneCameraTimer = 0.0f;
    cutsceneState = CUTSCENE_NONE;
    skipButtonVisible = false;

    bossUiIntro = 1.0f;
    boss_ui_set_intro(1.0f);
    if (g_boss && g_boss->maxHealth > 0.0f) {
        boss_ui_snap_health_trail(g_boss->health / g_boss->maxHealth);
    }

    character_reset_button_state();
    scene_sync_input_edge_state();
}

// Temporary scene context until memory management is added and cutscenes become their own scenes.
static SceneContext sceneContext;
static void scene_update_context(void)
{
    sceneContext.boss = g_boss;

    sceneContext.roomY = roomY;

    sceneContext.gameState = &gameState;
    sceneContext.screenTransition = &screenTransition;

    sceneContext.windowsModel = windowsModel;
    sceneContext.windowsDpl = windowsDpl;
    sceneContext.windowsMatrix = windowsMatrix;

    sceneContext.mapModel = mapModel;
    sceneContext.mapDpl = mapDpl;
    sceneContext.mapMatrix = mapMatrix;

    sceneContext.roomFloorModel = roomFloorModel;
    sceneContext.roomFloorDpl = roomFloorDpl;
    sceneContext.roomFloorMatrix = roomFloorMatrix;

    sceneContext.roomLedgeModel = roomLedgeModel;
    sceneContext.roomLedgeDpl = roomLedgeDpl;
    sceneContext.roomLedgeMatrix = roomLedgeMatrix;

    sceneContext.pillarsModel = pillarsModel;
    sceneContext.pillarsDpl = pillarsDpl;
    sceneContext.pillarsMatrix = pillarsMatrix;

    sceneContext.pillarsFrontModel = pillarsFrontModel;
    sceneContext.pillarsFrontDpl = pillarsFrontDpl;
    sceneContext.pillarsFrontMatrix = pillarsFrontMatrix;

    sceneContext.sunshaftsModel = sunshaftsModel;
    sceneContext.sunshaftsDpl = sunshaftsDpl;
    sceneContext.sunshaftsMatrix = sunshaftsMatrix;

    sceneContext.chainsModel = chainsModel;
    sceneContext.chainsDpl = chainsDpl;
    sceneContext.chainsMatrix = chainsMatrix;

    sceneContext.bossChainsGlowModel = bossChainsGlowModel;
    sceneContext.bossChainsGlowDpl = bossChainsGlowDpl;
    sceneContext.bossChainsGlowMatrix = bossChainsGlowMatrix;
    sceneContext.bossChainsGlowScrollParams = &bossChainsGlowScrollParams;

    sceneContext.init_playing = scene_init_playing;
    sceneContext.finish_phase2_cutscene = scene_finish_phase2_cutscene;
}

void scene_init_cutscene(void)
{
    scene_update_context();

    skipButtonVisible = false;

    if (cutscene_manager_handles_guardian_cutscene(cutsceneState)) {
        cutscene_manager_enter(&sceneContext, cutsceneState);
        return;
    }

    switch (cutsceneState) {
        case CUTSCENE_POST_BOSS_RESTORED: {
            // Post-boss dialog: keep the current gameplay camera position,
            // but retarget it to face the boss.
            cutsceneDialogActive = true;
            bossPostDefeatDialogStep = 0;
            skipButtonVisible = false;
            lastCutsceneAPressed = false;

            // Freeze character motion for the duration of this dialog so we don't coast afterward.
            character_set_velocity_xz(0.0f, 0.0f);

            // Keep current camera position and smoothly rotate to face the boss.
            customCamPos = camPos;
            customCamTarget = get_boss_lock_focus_point();
            camera_mode_smooth(CAMERA_CUSTOM, 0.25f);

            // Start the current chat's first line.
            // bossPostDefeatChatIndex persists across cutscene entries.
            const PostBossChat *chat = cutscene_manager_get_post_boss_chat(bossPostDefeatChatIndex);
            dialog_controller_speak(chat->line1, 0, chat->holdSec, false, true);
        } break;

        default:
            break;
    }
}

static void scene_update_post_boss_cutscene_input(void)
{
    /*
     * Guardian phase cutscene skip is owned by cutscene_manager_skip().
     * This remains only for the scene-owned post-boss restored cutscene.
     */
    if (cutsceneState == CUTSCENE_POST_BOSS_RESTORED) {
        skipButtonVisible = false;
        lastCutsceneAPressed = btn.a;
        return;
    }

    lastCutsceneAPressed = btn.a;
}

void scene_cutscene_update(void)
{
    scene_update_context();

    if (cutscene_manager_handles_guardian_cutscene(cutsceneState)) {
        cutscene_manager_update(&sceneContext, deltaTime);
        return;
    }

    switch (cutsceneState) {
        case CUTSCENE_POST_BOSS_RESTORED: {
            // Post-boss dialog runs while gameplay continues to animate without player input.
            character_update_cinematic();

            if (boss_is_active(g_boss)) {
                boss_update(g_boss);
            }

            collision_update();

            // Allow A/Start to advance immediately to the next line or end.
            if (dialog_controller_speaking() && (btn.a || btn.start)) {
                dialog_controller_skip();
            }

            dialog_controller_update();

            if (!dialog_controller_speaking()) {
                const PostBossChat *chat = cutscene_manager_get_post_boss_chat(bossPostDefeatChatIndex);

                if (bossPostDefeatDialogStep == 0 && chat->line2) {
                    bossPostDefeatDialogStep = 1;

                    dialog_controller_speak(
                        chat->line2,
                        0,
                        chat->holdSec,
                        false,
                        true
                    );
                } else {
                    int count = cutscene_manager_post_boss_chat_count();

                    if (bossPostDefeatChatIndex < count - 1) {
                        bossPostDefeatChatIndex++;
                    }

                    cutsceneDialogActive = false;
                    cutsceneState = CUTSCENE_NONE;
                    cutsceneTimer = 0.0f;
                    cutsceneCameraTimer = 0.0f;
                    skipButtonVisible = false;

                    camera_mode_smooth(CAMERA_CHARACTER, 0.8f);

                    character_set_velocity_xz(0.0f, 0.0f);

                    character_reset_button_state();
                    scene_sync_input_edge_state();
                    return;
                }
            }
        } break;

        default:
            break;
    }

    scene_update_post_boss_cutscene_input();
}

void scene_update(void) {

    if (gameState == GAME_STATE_VIDEO) {
        return;
    }

    audio_update_fade(deltaTime);

    if (s_pendingBossLoopMusic) {
        if (!audio_is_music_playing() && g_boss->health > 0) {
            s_pendingBossLoopMusic = false;
            audio_play_music(s_bossLoopMusicPath, true);
        }
    }

    if (g_boss && g_boss->health <= 0.0f && gameState != GAME_STATE_VIDEO) {
        if(!bossDeathMusicFadeStarted){
            bossDeathMusicFadeStarted = true;
            audio_stop_music_fade(2);
        }
        scene_update_video_trigger();
    }

    scene_update_video_preroll();  // always safe; it early-outs

    // Update all scrolling textures
    scroll_update();
    // Check if pause menu was just closed - if so, reset character button state
    // NOTE: during victory, the pause menu overlays without switching GAME_STATE to MENU.
    bool pauseMenuBlocking = scene_is_menu_active() || menu_controller_is_pause_menu_active();
    if (lastMenuActive && !pauseMenuBlocking) {
        // Menu was just closed - reset character button state to prevent false "just pressed"
        character_reset_button_state();
    }
    lastMenuActive = pauseMenuBlocking;

    // If player is dead, disable player control but keep boss/UI moving.
    // Death is owned by the character now; the scene only reads the character state.
    if (character_get_state() == CHAR_STATE_DEAD) {
        deathRestartLockoutTimer += deltaTime;

        // Still update the character so end-state animations, like Death, can play.
        character_update_cinematic();

        // Keep boss AI updating so it continues moving during end screen.
        if (boss_is_active(g_boss)) {
            boss_update(g_boss);
        }

        letterbox_update();

        if (deathRestartLockoutTimer >= DEATH_RESTART_LOCKOUT_S && btn.a) {
            scene_controller_switch_to_title();
        }
        return;
    }

    // Don't update game logic when pause menu is active (including victory overlay case)
    if (pauseMenuBlocking) {
        return;
    }

    // Debug hotkey: L-trigger skips to boss defeated (dead + fully stopped)
    // if (DEV_MODE)
    // {
    //     bool lHeld = joypad.btn.l;
    //     bool lJustPressed = lHeld && !lastLPressed;
    //     lastLPressed = lHeld;
    //     if (lJustPressed && boss_is_active(g_boss)) {
    //         scene_debug_force_boss_defeated();
    //     }
    // }

    if(cutsceneState == CUTSCENE_NONE) // Normal gameplay
    {
        // Post-boss interaction trigger: after defeat, when Z-targeted and close enough,
        // start the dialog/camera only when player presses A (held-edge).
        // IMPORTANT: check this BEFORE character_update so A doesn't also trigger a roll.
        if (boss_is_interactable(g_boss) && g_boss->state == BOSS_STATE_DEAD) {
            bool aHeld = joypad.btn.a;
            bool aJustPressed = aHeld && !lastInteractAHeld;
            lastInteractAHeld = aHeld;

            // Match the on-screen prompt behavior: if the "A" prompt is visible, A should talk.
            // Do NOT require Z-targeting; post-fight we untarget by default, but still allow re-targeting.
            if (scene_post_boss_interact_allowed(g_boss) && aJustPressed) {
                cutsceneState = CUTSCENE_POST_BOSS_RESTORED;
                cutsceneTimer = 0.0f;
                cutsceneCameraTimer = 0.0f;
                scene_init_cutscene();
                return;
            }
        } else {
            // Keep interact edge-tracker in sync when not eligible
            lastInteractAHeld = joypad.btn.a;
        }

        character_update();
        // Update character transform after constraint
        character_update_position();

        boss_ground_crush_update(deltaTime);

        if (boss_is_active(g_boss)) {
            boss_update(g_boss);
            // Boss death no longer forces GAME_STATE_VICTORY.
            // The boss will play its collapse and remain still; the player can keep moving.

            // Phase 2 transition: trigger cutscene when boss health drops to 40%.
#if PHASE_2_ENABLED
            if (boss_is_combat_active(g_boss)
                && !phase2CutsceneTriggered && g_boss->phaseIndex == 1
                && g_boss->health <= g_boss->maxHealth * 0.4f
                && g_boss->health > 0.0f) {
                phase2CutsceneTriggered = true;
                cutsceneTimer = 0.0f;
                cutsceneCameraTimer = 0.0f;
                cutsceneState = CUTSCENE_PHASE2_INTRO;
                g_boss->damageFlashTimer = 0.0f;
                scene_init_cutscene();
                return;
            }
#endif
        }

        collision_update();


        //dialog_controller_update();

        // Update boss title fade-out when fight has started
        if (bossTitleFade > 0.0f) {
            bossTitleFade -= deltaTime / bossTitleFadeSpeed;
            if (bossTitleFade < 0.0f) bossTitleFade = 0.0f;
        }

        // Progress UI intro animations
        // Boss health bar should start growing only after boss title is gone
        if (bossTitleFade <= 0.0f && bossUiIntro < 1.0f) {
            bossUiIntro += deltaTime / uiIntroSpeed;
            if (bossUiIntro > 1.0f) bossUiIntro = 1.0f;
            boss_ui_set_intro(bossUiIntro);
        }
        if (playerUiIntro < 1.0f) {
            playerUiIntro += deltaTime / uiIntroSpeed;
            if (playerUiIntro > 1.0f) playerUiIntro = 1.0f;
            character_ui_set_intro(playerUiIntro);
        }
    }
    else // Cutscene
    {
        scene_cutscene_update();
    }

    // Update letterbox animation
    letterbox_update();

    // Advance victory title animation timer (but still allow full gameplay update above)
    if (gameState == GAME_STATE_VICTORY && !victoryTitleDone) {
        const float total = VICTORY_TITLE_FADEIN_S + VICTORY_TITLE_HOLD_S + VICTORY_TITLE_FADEOUT_S;
        victoryTitleTimer += deltaTime;
        if (victoryTitleTimer >= total) {
            victoryTitleTimer = total;
            victoryTitleDone = true;
        }
    }

    // Post-boss cleanup: once the boss becomes dead, clear Z-targeting so the player is untargeted
    // after the fight. The player can still re-target by pressing Z as normal.
    if (boss_is_active(g_boss)) {
        bool bossDeadNow = (g_boss->state == BOSS_STATE_DEAD);
        if (bossDeadNow && !bossWasDead) {
            // Record fastest clear time per save slot (once, on death edge).
            if (s_bossRunActive && s_bossRunStartS > 0.0) {
                double dt = nowS - s_bossRunStartS;
                if (dt > 0.0) {
                    uint32_t ms = (uint32_t)(dt * 1000.0);
                    (void)save_controller_record_boss_clear_time_ms(ms);
                }
            }
            s_bossRunActive = false;

            cameraLockOnActive = false;
            cameraLockBlend = 0.0f;
            // Sync edge detectors so we don't immediately re-toggle due to a held button
            lastZPressed = joypad.btn.z;
            lastInteractAHeld = joypad.btn.a;
            lastCLeftHeld = joypad.btn.c_left;
            lastCRightHeld = joypad.btn.c_right;
            s_zHoldTimer = 0.0f;
            s_zHoldConsumed = false;
            s_zActivatedOnPress = false;
        }
        bossWasDead = bossDeadNow;
    } else {
        bossWasDead = false;
    }

    // Z-target:
    // - Tap Z toggles lock-on on/off
    // - Hold Z keeps lock-on active and allows cycling targets with C-left/C-right
    bool lockonAllowed = cutsceneState == CUTSCENE_NONE && boss_is_active(g_boss);
    bool zHeld = joypad.btn.z;
    bool zJustPressed = zHeld && !lastZPressed;
    bool zJustReleased = !zHeld && lastZPressed;
    bool cLeftHeld = joypad.btn.c_left;
    bool cRightHeld = joypad.btn.c_right;
    bool cLeftJustPressed = cLeftHeld && !lastCLeftHeld;
    bool cRightJustPressed = cRightHeld && !lastCRightHeld;

    // C-left is reserved for the health potion (when not holding Z for lock-on cycling).
    // Avoid consuming during cutscenes / dialog sequences.
    if (cutsceneState == CUTSCENE_NONE && cLeftJustPressed && !zHeld) {
        (void)character_try_use_health_potion();
    }

    // If lock-on is not allowed, force it off.
    if (!lockonAllowed) {
        cameraLockOnActive = false;
    }

    if (zJustPressed) {
        s_zHoldTimer = 0.0f;
        s_zHoldConsumed = false;
        s_zActivatedOnPress = false;

        // If we're currently NOT locked-on, pressing Z should immediately lock on.
        if (lockonAllowed && !cameraLockOnActive) {
            cameraLockOnActive = true;
            s_zActivatedOnPress = true;
        }
    }

    if (zHeld) {
        s_zHoldTimer += deltaTime;
    }

    if (lockonAllowed) {
        // While holding Z, allow cycling targets without changing lock-on active state.
        if (zHeld) {
            if (cLeftJustPressed) {
                scene_cycle_lock_target(-1);
                s_zHoldConsumed = true;
            } else if (cRightJustPressed) {
                scene_cycle_lock_target(1);
                s_zHoldConsumed = true;
            }
        }

        // Short tap-release toggles OFF when already locked-on.
        // Holding Z (or cycling) will not untarget.
        if (zJustReleased) {
            if (cameraLockOnActive && !s_zActivatedOnPress && !s_zHoldConsumed && s_zHoldTimer <= Z_TAP_TOGGLE_MAX_S) {
                cameraLockOnActive = false;
            }
            s_zActivatedOnPress = false;
        }

        // Update target position when lock-on is active
        if (cameraLockOnActive) {
            cameraLockOnTarget = get_boss_lock_focus_point();
        }
    }

    // if (cutsceneState == CUTSCENE_NONE) {
    //    msa_update(deltaTime); // multi sword attack
    // }
    //boulder_hazard_update(deltaTime); // close-range ground boulders

    lastZPressed = zHeld;
    lastCLeftHeld = cLeftHeld;
    lastCRightHeld = cRightHeld;
}

void scene_fixed_update(void)
{
}

// Draws a small lock-on marker over the boss when Z-targeting is active.
static void draw_lockon_indicator(T3DViewport *viewport)
{
    bool visible = cameraLockOnActive && !scene_is_cutscene_active() && boss_is_active(g_boss);
    T3DVec3 worldPos = get_boss_lock_focus_point();

    boss_ui_draw_lockon_marker(viewport, &worldPos, visible);
}

void scene_draw_cutscene(T3DViewport *viewport)
{
    scene_update_context();

    if (cutscene_manager_handles_guardian_cutscene(cutsceneState)) {
        cutscene_manager_draw(&sceneContext, viewport);
        return;
    }

    switch (cutsceneState) {
        case CUTSCENE_POST_BOSS_RESTORED: {
            /*
             * Render the normal scene while the post-boss dialog is active.
             * This avoids a black screen because this cutscene is camera/dialog only.
             */

            rdpq_sync_pipe();
            rdpq_mode_zbuf(false, false);

            // No-depth environment first.
            t3d_matrix_push_pos(1);
                if (windowsMatrix && windowsDpl) {
                    t3d_matrix_set(windowsMatrix, true);
                    rspq_block_run(windowsDpl);
                }

                if (mapMatrix && mapDpl) {
                    t3d_matrix_set(mapMatrix, true);
                    rspq_block_run(mapDpl);
                }
            t3d_matrix_pop(1);

            // Floor.
            rdpq_sync_pipe();
            rdpq_mode_zbuf(true, true);

            t3d_matrix_push_pos(1);
                if (roomFloorMatrix && roomFloorDpl) {
                    t3d_matrix_set(roomFloorMatrix, true);
                    rspq_block_run(roomFloorDpl);
                }
            t3d_matrix_pop(1);

            // Shadows.
            rdpq_sync_pipe();
            rdpq_mode_zbuf(false, false);

            t3d_matrix_push_pos(1);
                character_draw_shadow();

                if (g_boss) {
                    boss_draw_shadow(g_boss);
                }
            t3d_matrix_pop(1);

            // Room pieces.
            rdpq_sync_pipe();
            rdpq_mode_zbuf(true, true);

            t3d_matrix_push_pos(1);
                if (roomLedgeMatrix && roomLedgeDpl) {
                    t3d_matrix_set(roomLedgeMatrix, true);
                    rspq_block_run(roomLedgeDpl);
                }

                if (pillarsMatrix && pillarsDpl) {
                    t3d_matrix_set(pillarsMatrix, true);
                    rspq_block_run(pillarsDpl);
                }

                if (pillarsFrontMatrix && pillarsFrontDpl) {
                    t3d_matrix_set(pillarsFrontMatrix, true);
                    rspq_block_run(pillarsFrontDpl);
                }
            t3d_matrix_pop(1);

            // Characters.
            t3d_matrix_push_pos(1);
                character_draw();

                if (g_boss) {
                    boss_draw(g_boss);
                }
            t3d_matrix_pop(1);

            // Persistent gameplay chains only.
            // cinematicChains moved into cutscene_guardian_phase1.c and should not be referenced here.
            t3d_matrix_push_pos(1);
                if (chainsMatrix && chainsDpl) {
                    t3d_matrix_set(chainsMatrix, true);
                    rspq_block_run(chainsDpl);
                }
            t3d_matrix_pop(1);

            // 2D dialog overlay.
            if (cutsceneDialogActive) {
                int height = 70;
                int width = 220;
                int x = (SCREEN_WIDTH - width) / 2;
                int y = 240 - height - 10;

                dialog_controller_draw(false, x, y, width, height);
            }
        } break;

        default:
            break;
    }
}

static void scene_draw_video_preroll_fade(void)
{
    if (videoPreroll != VIDEO_PREROLL_NONE) {
        display_utility_solid_black_transition(false, VIDEO_FADE_SPEED);
    }
}

static void scene_draw_video_trigger(T3DViewport *vp)
{
    if (!debugDraw || !vp) return;

    T3DVec3 mn = {{ videoTrigMin[0], videoTrigMin[1], videoTrigMin[2] }};
    T3DVec3 mx = {{ videoTrigMax[0], videoTrigMax[1], videoTrigMax[2] }};

    uint16_t color = DEBUG_COLORS[2];               // normal (green-ish)
    if (videoTrigHitThisFrame) color = DEBUG_COLORS[0]; // hit (red)
    if (videoTrigFired)        color = DEBUG_COLORS[5]; // fired (cyan)

    debug_draw_aabb(vp, &mn, &mx, color);
}

void scene_draw(T3DViewport *viewport)
{

    if(gameState == GAME_STATE_VIDEO)
        return;

    t3d_frame_start();

    if(!DITHER_ENABLED && !debugDraw)
    {
        rdpq_mode_dithering(DITHER_NONE_BAYER);
    }

    t3d_viewport_attach(viewport);

    // Fog
    color_t fogColor = (color_t){0, 0, 0, 0xFF};

    rdpq_mode_fog(RDPQ_FOG_STANDARD);
    rdpq_set_fog_color(fogColor);

    //t3d_screen_clear_color(RGBA32(0, 0, 0, 0xFF));
    t3d_screen_clear_depth();

    if(cutsceneState != CUTSCENE_NONE){
        cutscene_manager_draw_fog();
    }else{
        t3d_fog_set_range(450.0f, 800.0f);
    }
    t3d_fog_set_enabled(true);

    // Lighting
    t3d_light_set_ambient(colorAmbient);

    if(cutsceneState != CUTSCENE_NONE)
    {
        t3d_screen_clear_color(RGBA32(0, 0, 0, 0xFF));
        scene_draw_cutscene(viewport);
        // Draw letterbox bars during cutscenes
        letterbox_draw();
        // Draw skip indicator on top of letterbox bars
        cutscene_manager_draw_skip_overlay(skipButtonVisible);
        return;
    }
    // ===== DRAW 3D =====

    rdpq_mode_zbuf(false, false);

    // Draw no depth environment first
    t3d_matrix_push_pos(1);

    t3d_matrix_set(windowsMatrix, true);
    rspq_block_run(windowsDpl);

    t3d_matrix_set(mapMatrix, true);
    rspq_block_run(mapDpl);

    //Draw depth environment
    rdpq_mode_zbuf(true, true);

    t3d_matrix_set(roomFloorMatrix, true);
    rspq_block_run(roomFloorDpl);

    rdpq_mode_zbuf(false, false);

    // projection effects
    boss_ground_crush_draw();
    // blob shadows
    character_draw_shadow();
    if (g_boss) {
        boss_draw_shadow(g_boss);
    }

    rdpq_mode_zbuf(true, true);


    t3d_matrix_set(roomLedgeMatrix, true);
    rspq_block_run(roomLedgeDpl);

    t3d_matrix_set(pillarsMatrix, true);
    rspq_block_run(pillarsDpl);

    t3d_matrix_set(pillarsFrontMatrix, true);
    rspq_block_run(pillarsFrontDpl);

    rdpq_mode_zbuf(false, false);

    // floor glow
    if(g_boss->health <= 0)
    {
        t3d_matrix_set(floorGlowMatrix, true);
        // Create a struct to pass the scrolling parameters to the tile callback
        t3d_model_draw_custom(floorGlowModel, (T3DModelDrawConf){
            .userData = &floorGlowScrollParams,
            .tileCb = tile_scroll,
        });
    }


    rdpq_mode_zbuf(true, true);

    // Draw characters
    character_draw();
    if (g_boss) {
        boss_draw(g_boss);
    }

    //msa_draw_visuals(viewport); // multi sword attack
    //boulder_hazard_draw(viewport); // close-range ground boulders

    // Fog door (transparent): depth test ON, depth write OFF so it can be drawn late.
    rdpq_mode_zbuf(true, false);

    if (fogDoorMatrix && fogDoorModel) {
        t3d_matrix_set(fogDoorMatrix, true);
        t3d_model_draw_custom(fogDoorModel, (T3DModelDrawConf){
            .userData = &fogScrollParams,
            .tileCb = tile_scroll,
        });
    }

    rdpq_mode_zbuf(true, true);

    t3d_matrix_set(chainsMatrix, true);
    rspq_block_run(chainsDpl);

    t3d_matrix_pop(1);

    // Screen-space ribbon trails, drawn right after 3D so they feel "in world"
    sword_trail_draw_all(viewport);

    // Dust puffs (boss landings/impacts)
    dust_particles_fx_update(deltaTime);
    dust_particles_fx_draw(viewport);

    // Blood splatters from boss hits (drawn after dust so red reads on top of puffs)
    blood_particles_fx_update(deltaTime);
    blood_particles_fx_draw(viewport);

    // ===== DRAW 2D =====
    // Post-boss interaction prompt ("A") above the defeated boss when close enough to interact
    {
        T3DVec3 postBossPromptPos;
        bool postBossPromptVisible =
            !scene_is_cutscene_active()
            && boss_is_interactable(g_boss)
            && g_boss
            && g_boss->state == BOSS_STATE_DEAD
            && scene_post_boss_in_range(g_boss);

        if (!boss_get_head_world_pos(g_boss, &postBossPromptPos)) {
            postBossPromptPos = get_boss_lock_focus_point();
        }

        boss_ui_draw_post_boss_a_prompt(viewport, &postBossPromptPos, postBossPromptVisible);
    }

    // Overlay lock-on marker above the boss.
    draw_lockon_indicator(viewport);

    scene_draw_video_trigger(viewport);

    bool cutsceneActive = scene_is_cutscene_active();
    bool isDead = character_get_state() == CHAR_STATE_DEAD;
    bool isVictory = gameState == GAME_STATE_VICTORY;
    bool isEndScreen = isDead || isVictory;

    // Draw letterbox bars (they handle their own visibility and animation)
    letterbox_draw();

    // Draw UI elements after 3D rendering is complete.
    // Keep player UI visible during victory; hide it during death and cutscenes.
    if (!cutsceneActive && !isDead) {
        // Boss UI/debug is owned by boss_render/boss_ui now.
        // Keep scene responsible only for draw order and visibility gating.
        if (!isVictory && boss_is_active(g_boss) && bossTitleFade <= 0.0f && g_boss->maxHealth > 0.0f) {
            boss_draw_ui(g_boss, viewport);
        }

        character_ui_draw();
    }

    // Slide-up overlay for boss title after fight starts (no fading)
    if (!cutsceneActive && !isEndScreen && bossTitleFade > 0.0f && g_boss) {
        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);

        int barWidth = 195;
        int barHeight = 23;
        int barTop = 35;
        int barLeft = (SCREEN_WIDTH - barWidth) / 2;
        int slideDistance = 120; // pixels to move upward until off-screen
        float t = 1.0f - bossTitleFade; // 0 -> 1 as we slide up

        // Compute current Y positions while sliding upward
        int currentBarTop = barTop - (int)(slideDistance * t);
        int currentTextY = 50 - (int)(slideDistance * t);

        // Draw centered black bar behind the title (constant alpha)
        int barAlpha = 120;
        rdpq_set_prim_color(RGBA32(0, 0, 0, barAlpha));
        rdpq_fill_rectangle(barLeft, currentBarTop, barLeft + barWidth, currentBarTop + barHeight);

        // Draw centered title text (constant intensity while sliding up)
        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_CENTER,
            .width = SCREEN_WIDTH,
        }, FONT_UNBALANCED, 0, currentTextY, "%s", g_boss->name);
    }

    if (isEndScreen) {
        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);

        if (isDead) {
            // Full-screen overlay with prompt to restart
            rdpq_set_prim_color(RGBA32(0, 0, 0, 140));
            rdpq_fill_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
            rdpq_set_prim_color(RGBA32(255, 255, 255, 255));

            rdpq_text_printf(&(rdpq_textparms_t){
                .align = ALIGN_CENTER,
                .width = SCREEN_WIDTH,
            }, FONT_UNBALANCED, 0, SCREEN_HEIGHT / 2 - 12, "%s", "You Died");

            // Delay restart prompt so rapid gameplay A-mashing doesn't instantly restart.
            if (deathRestartLockoutTimer >= DEATH_RESTART_LOCKOUT_S) {
                const char *label = "Restart";
                const int gap = 6;
                const int y = (SCREEN_HEIGHT / 2) + 20;

                // Source sprite is 64×64; scale down to match other UI button sizes.
                const float kTargetPx = 20.0f;
                int buttonW = button_prompt_a_icon_width(kTargetPx);
                int buttonH = button_prompt_a_icon_height(kTargetPx);
                int x0 = (SCREEN_WIDTH / 2) - 44;

                if (button_prompt_has_a_button()) {
                    int buttonX = x0;
                    int buttonY = y - (buttonH / 2) - 6;
                    button_prompt_draw_a_icon(buttonX, buttonY, kTargetPx);
                }

                // Text baseline aligned to match other UI codepaths (roughly icon vertical center)
                int textX = x0 + buttonW + gap;
                rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
                rdpq_text_printf(&(rdpq_textparms_t){
                    .align = ALIGN_LEFT,
                    .width = SCREEN_WIDTH - textX,
                }, FONT_UNBALANCED, textX, y, "%s", label);
            }

            scene_draw_video_preroll_fade();
            return;
        }

        // Victory: Dark Souls–style title fade ("Enemy restored")
        {
            const float fadeIn = VICTORY_TITLE_FADEIN_S;
            const float hold = VICTORY_TITLE_HOLD_S;
            const float fadeOut = VICTORY_TITLE_FADEOUT_S;
            const float total = fadeIn + hold + fadeOut;

            float t = victoryTitleTimer;
            if (t < 0.0f) t = 0.0f;
            if (t > total) t = total;

            float a01 = 0.0f;
            if (t < fadeIn) {
                a01 = (fadeIn > 0.0f) ? (t / fadeIn) : 1.0f;
            } else if (t < fadeIn + hold) {
                a01 = 1.0f;
            } else {
                float u = (fadeOut > 0.0f) ? ((t - (fadeIn + hold)) / fadeOut) : 1.0f;
                if (u < 0.0f) u = 0.0f;
                if (u > 1.0f) u = 1.0f;
                a01 = 1.0f - u;
            }

            uint8_t textA = (uint8_t)fmaxf(0.0f, fminf(255.0f, a01 * 255.0f));
            uint8_t barA  = (uint8_t)fmaxf(0.0f, fminf(200.0f, a01 * 140.0f));

            if (!victoryTitleDone) {
                if (textA > 0) {
                    int barWidth = 250;
                    int barHeight = 28;
                    int barLeft = (SCREEN_WIDTH - barWidth) / 2;
                    int barTop = (SCREEN_HEIGHT / 2) - (barHeight / 2) - 2;

                    // Background: use dialog gradient sprite (fallback to solid bar if missing)
                    if (victoryTitleBgSprite) {
                        const int src_w = victoryTitleBgSurf.width;
                        const int src_h = victoryTitleBgSurf.height;
                        const float sx = (src_w > 0) ? ((float)barWidth  / (float)src_w) : 1.0f;
                        const float sy = (src_h > 0) ? ((float)barHeight / (float)src_h) : 1.0f;

                        rdpq_sync_pipe();
                        rdpq_set_mode_standard();
                        rdpq_mode_alphacompare(1);
                        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
                        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
                        rdpq_set_prim_color(RGBA32(255, 255, 255, barA));
                        rdpq_tex_blit(&victoryTitleBgSurf, barLeft, barTop, &(rdpq_blitparms_t){
                            .scale_x = sx,
                            .scale_y = sy,
                        });
                    } else {
                        rdpq_set_prim_color(RGBA32(0, 0, 0, barA));
                        rdpq_fill_rectangle(barLeft, barTop, barLeft + barWidth, barTop + barHeight);
                    }

                    // Ensure a simple combiner for text after the sprite blit path.
                    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
                    rdpq_set_prim_color(RGBA32(255, 255, 255, textA));
                    rdpq_text_printf(&(rdpq_textparms_t){
                        .align = ALIGN_CENTER,
                        .width = SCREEN_WIDTH,
                    }, FONT_UNBALANCED, 0, SCREEN_HEIGHT / 2, "%s", "Enemy restored");
                }
            }
        }

        scene_draw_video_preroll_fade();
        return;
    }

    // Draw dialog on top of everything
    int height = 70;
    int width = 220;
    int x = (SCREEN_WIDTH - width) / 2;
    // bottom positioning
    if(cutsceneDialogActive)
    {
        int y = 240 - height - 10;
        dialog_controller_draw(false, x, y, width, height);
    }


    if(screenTransition)
    {
        if(cutsceneState == CUTSCENE_PHASE1_INTRO){
            display_utility_solid_black_transition(true, 100.0f);
        }
        else if(cutsceneState == CUTSCENE_PHASE1_FEAR){
            display_utility_solid_black_transition(false, 200.0f);
        }
    }

    // Video preroll fade-to-black must draw over every non-video path.
    scene_draw_video_preroll_fade();


    //msa_draw_debug(viewport);

}

void scene_delete_environment(void)
{
    // --- DPLs ---
    if (mapDpl)        { rspq_block_free(mapDpl);        mapDpl = NULL; }
    if (pillarsDpl)    { rspq_block_free(pillarsDpl);    pillarsDpl = NULL; }
    if (pillarsFrontDpl) { rspq_block_free(pillarsFrontDpl); pillarsFrontDpl = NULL; }
    if (roomLedgeDpl)  { rspq_block_free(roomLedgeDpl);  roomLedgeDpl = NULL; }
    if (windowsDpl)    { rspq_block_free(windowsDpl);    windowsDpl = NULL; }
    if (chainsDpl)     { rspq_block_free(chainsDpl);     chainsDpl = NULL; }
    if (sunshaftsDpl)  { rspq_block_free(sunshaftsDpl);  sunshaftsDpl = NULL; }
    if (fogDoorDpl)    { rspq_block_free(fogDoorDpl);    fogDoorDpl = NULL; }
    if (roomFloorDpl)  { rspq_block_free(roomFloorDpl);  roomFloorDpl = NULL; }

    // --- Models ---
    if (mapModel)       { t3d_model_free(mapModel);       mapModel = NULL; }
    if (pillarsModel)   { t3d_model_free(pillarsModel);   pillarsModel = NULL; }
    if (pillarsFrontModel) { t3d_model_free(pillarsFrontModel); pillarsFrontModel = NULL; }
    if (roomLedgeModel) { t3d_model_free(roomLedgeModel); roomLedgeModel = NULL; }
    if (windowsModel)   { t3d_model_free(windowsModel);   windowsModel = NULL; }
    if (chainsModel)    { t3d_model_free(chainsModel);    chainsModel = NULL; }
    if (sunshaftsModel) { t3d_model_free(sunshaftsModel); sunshaftsModel = NULL; }
    if (fogDoorModel)   { t3d_model_free(fogDoorModel);   fogDoorModel = NULL; }
    if (roomFloorModel) { t3d_model_free(roomFloorModel); roomFloorModel = NULL; }

    // --- Matrices (malloc_uncached) ---
    if (mapMatrix)       { free_uncached(mapMatrix);       mapMatrix = NULL; }
    if (pillarsMatrix)   { free_uncached(pillarsMatrix);   pillarsMatrix = NULL; }
    if (pillarsFrontMatrix) { free_uncached(pillarsFrontMatrix); pillarsFrontMatrix = NULL; }
    if (roomLedgeMatrix) { free_uncached(roomLedgeMatrix); roomLedgeMatrix = NULL; }
    if (windowsMatrix)   { free_uncached(windowsMatrix);   windowsMatrix = NULL; }
    if (chainsMatrix)    { free_uncached(chainsMatrix);    chainsMatrix = NULL; }
    if (sunshaftsMatrix) { free_uncached(sunshaftsMatrix); sunshaftsMatrix = NULL; }
    if (fogDoorMatrix)   { free_uncached(fogDoorMatrix);   fogDoorMatrix = NULL; }
    if (roomFloorMatrix) { free_uncached(roomFloorMatrix); roomFloorMatrix = NULL; }
}

void scene_cleanup(void)
{
    rspq_wait();
    //collision_mesh_cleanup();
    scene_delete_environment();
    boss_ground_crush_cleanup();
    camera_reset();

    /*
     * Character lifetime is owned by main.c for this transitional pass.
     * Do not delete/free it during scene transitions.
     */
    if (g_boss) {
        boss_free(g_boss);
        g_boss = NULL;
    }

    blood_particles_fx_cleanup();
    dust_particles_fx_cleanup();
    particle_system_cleanup();

    dialog_controller_free();
    audio_scene_unload_sfx();
    cutscene_manager_cleanup();
    button_prompt_cleanup();

    if (victoryTitleBgSprite) {
        sprite_free(victoryTitleBgSprite);
        victoryTitleBgSprite = NULL;
        surface_free(&victoryTitleBgSurf);
    }

    character_ui_cleanup();
    boss_ui_cleanup();
}