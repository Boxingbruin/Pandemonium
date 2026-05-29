#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "scene.h"
#include "scene_sfx.h"

#include "audio_controller.h"

#include "camera_controller.h"

#include "joypad_utility.h"
#include "general_utility.h"
#include "game_lighting.h"
#include "game_time.h"
#include "game_math.h"

#include "globals.h"
#include "../utilities/video_layout.h"
#include "../utilities/button_prompt_utility.h"

#include "../managers/cutscene_manager.h"
#include "../managers/cutscene_manager_internal.h"
#include "scene_context.h"

#include "character.h"
#include "../game/bosses/boss.h"
#include "../game/bosses/boss_ai.h"
#include "../game/bosses/boss_render.h"
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
//#include "boulder_hazard.h" // close-range ground-boulder hazard

// Dust (implemented later near lock-on indicator)
static void dust_reset(void);
static void dust_update(float dt);
static void dust_draw(T3DViewport *viewport);

// Blood (implemented after dust)
static void blood_reset(void);
static void blood_update(float dt);
static void blood_draw(T3DViewport *viewport);

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


// (phase1Dialogs / phase2Dialogs and cutsceneDialogActive, phase2CutsceneTriggered,
// bossPostDefeatDialogStep all live in cutscene_manager.c — accessed via the
// internal header above.)

// Post-boss interaction ("restored") state
static bool bossPostDefeatTalkDone = false;
static bool bossWasDead = false; // Tracks death transition for one-time post-death cleanup

// Per-slot save stats: track one "run" (boss attempt) start time, and record clear time at death transition.
static bool s_bossRunActive = false;
static double s_bossRunStartS = 0.0;

// Post-boss interaction distances (XZ)
static const float POST_BOSS_PROMPT_DIST  = 140.0f;   // show A prompt and allow talk when inside this range

// ------------------------------------------------------------
// Cutscene music -> looping boss music handoff
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
static bool bossActivated = false;
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

// Dust particle sprite (simple puffs)
static sprite_t* dustParticleSprite = NULL;
static surface_t dustParticleSurf = {0};

// Blood splatter sprites (large + medium variants + tiny variants).
// Loaded as IA8 so they can be tinted red via prim color and stay TMEM-cheap.
enum {
    BLOOD_SPRITE_LARGE = 0,
    BLOOD_SPRITE_MEDIUM_A,
    BLOOD_SPRITE_MEDIUM_B,
    BLOOD_SPRITE_MEDIUM_C,
    BLOOD_SPRITE_TINY_A,
    BLOOD_SPRITE_TINY_B,
    BLOOD_SPRITE_TINY_C,
    BLOOD_SPRITE_COUNT
};
static sprite_t* bloodSprites[BLOOD_SPRITE_COUNT] = {0};
static surface_t bloodSurfs[BLOOD_SPRITE_COUNT] = {0};

// Z-target lock-on icon sprite
static sprite_t* zTargetIconSprite = NULL;
static surface_t zTargetIconSurf = {0};

// HUD: C-button item assignment (health potion on C-left)
static sprite_t* cUpSprite = NULL;
static sprite_t* cDownSprite = NULL;
static sprite_t* cLeftSprite = NULL;
static sprite_t* cRightSprite = NULL;
static surface_t cUpSurf = {0};
static surface_t cDownSurf = {0};
static surface_t cLeftSurf = {0};
static surface_t cRightSurf = {0};

static sprite_t* healthBottleSprite = NULL;
static surface_t healthBottleSurf = {0};

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
    //lightning_fx_system_init("rom:/boss/boss_back_sword_lightning2.t3dm");
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

    // Load dust particle sprite
    dustParticleSprite = sprite_load("rom:/dustParticle.ia8.sprite");
    if (dustParticleSprite) {
        dustParticleSurf = sprite_get_pixels(dustParticleSprite);
    }

    // Load blood splatter sprites (IA8 - tinted red at draw time)
    static const char* bloodPaths[BLOOD_SPRITE_COUNT] = {
        "rom:/blood/blood_large.ia8.sprite",
        "rom:/blood/blood_medium_a.ia8.sprite",
        "rom:/blood/blood_medium_b.ia8.sprite",
        "rom:/blood/blood_medium_c.ia8.sprite",
        "rom:/blood/blood_tiny_a.ia8.sprite",
        "rom:/blood/blood_tiny_b.ia8.sprite",
        "rom:/blood/blood_tiny_c.ia8.sprite",
    };
    for (int i = 0; i < BLOOD_SPRITE_COUNT; i++) {
        bloodSprites[i] = sprite_load(bloodPaths[i]);
        if (bloodSprites[i]) {
            bloodSurfs[i] = sprite_get_pixels(bloodSprites[i]);
        }
    }

    // Load Z-target lock-on icon (IA8 so the alpha gradient is preserved)
    zTargetIconSprite = sprite_load("rom:/ztargetIcon.ia8.sprite");
    if (zTargetIconSprite) {
        zTargetIconSurf = sprite_get_pixels(zTargetIconSprite);
    }

    // Load C-button HUD icons.
    // C-left uses the empty variant because the health potion graphic is drawn over it.
    // The other three show the arrow icon (unassigned slots), so use the regular variant.
    cUpSprite = sprite_load("rom:/buttons/CUp.sprite");
    if (cUpSprite) cUpSurf = sprite_get_pixels(cUpSprite);
    cDownSprite = sprite_load("rom:/buttons/CDown.sprite");
    if (cDownSprite) cDownSurf = sprite_get_pixels(cDownSprite);
    cLeftSprite = sprite_load("rom:/buttons/CButton_empty.sprite");
    if (cLeftSprite) cLeftSurf = sprite_get_pixels(cLeftSprite);
    cRightSprite = sprite_load("rom:/buttons/CRight.sprite");
    if (cRightSprite) cRightSurf = sprite_get_pixels(cRightSprite);

    // Load health potion bottle sprite (IA8)
    healthBottleSprite = sprite_load("rom:/healthBottle.ia8.sprite");
    if (healthBottleSprite) {
        healthBottleSurf = sprite_get_pixels(healthBottleSprite);
    }

    // Start boss music
    // TODO: Its turned off for now as it gets annoying to listen to and it crackles
    // audio_play_music("rom:/audio/music/boss_final_phase.wav64", true);

    // Start boss intro cutscene after character and boss are loaded and positioned
    //dialog_controller_speak("^A powerful enemy approaches...~\n<Prepare for battle!", 0, 3.0f, false, true);

    // Initialize and show letterbox bars for intro
    letterbox_init();
    letterbox_show(false);  // Show immediately without animation

    collision_init();


    dust_reset();
    blood_reset();

    msa_init();

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

static bool scene_get_boss_bone_world_pos(int boneIndex, T3DVec3 *outWorld);

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
    if (scene_get_boss_bone_world_pos(bone, &worldPos)) {
        return worldPos;
    }

    // 3) Ultimate fallback: boss position with a small lift so the marker isn't at the feet.
    return (T3DVec3){{ g_boss->pos[0], g_boss->pos[1] + 40.0f, g_boss->pos[2] }};
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
    bossActivated = false;
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
    display_utility_set_boss_ui_intro(bossUiIntro);
    display_utility_set_player_ui_intro(playerUiIntro);

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
    msa_init();
    lightning_fx_system_ring_enable(false);
    animation_utility_set_screen_shake_mag(0.0f);

    // Reset letterbox to show state for intro
    letterbox_show(false);

    // Reset run timer state (a new run will be started when we enter gameplay again)
    s_bossRunActive = false;
    s_bossRunStartS = 0.0;

    dust_reset();
    boss_ground_crush_reset();
    blood_reset();
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

static bool scene_get_boss_bone_world_pos(int boneIndex, T3DVec3 *outWorld)
{
    if (!outWorld) return false;
    if (!g_boss || !g_boss->skeleton || !g_boss->modelMat) return false;
    if (boneIndex < 0) return false;

    T3DSkeleton* skel = (T3DSkeleton*)g_boss->skeleton;
    const T3DMat4FP* boneMat = &skel->boneMatricesFP[boneIndex];
    const T3DMat4FP* modelMat = (const T3DMat4FP*)g_boss->modelMat;

    const float boneLocal[3] = { 0.0f, 0.0f, 0.0f };
    float boneModel[3];
    mat4fp_mul_point_f32_row3_colbasis(boneMat, boneLocal, boneModel);

    float boneWorld[3];
    mat4fp_mul_point_f32_row3_colbasis(modelMat, boneModel, boneWorld);

    *outWorld = (T3DVec3){{ boneWorld[0], boneWorld[1], boneWorld[2] }};
    return true;
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

static void draw_post_boss_a_prompt(T3DViewport *viewport)
{
    // Show "A" above the boss only after defeat, when close enough to interact.
    if (!viewport || !button_prompt_has_a_button()) return;
    if (scene_is_cutscene_active() || !scene_is_boss_active() || !g_boss) return;
    if (g_boss->state != BOSS_STATE_DEAD) return;

    // Show whenever we're near the boss, regardless of facing. The A press itself
    // still requires the character to be facing the boss (see interact gate).
    if (!scene_post_boss_in_range(g_boss)) return;

    // Anchor to the boss head bone (true attachment). Fall back to lock-focus if head bone isn't available.
    T3DVec3 worldPos;
    if (!scene_get_boss_bone_world_pos(g_boss->headBoneIndex, &worldPos)) {
        worldPos = get_boss_lock_focus_point();
    }
    // Small lift so the prompt doesn't intersect the head.
    worldPos.v[1] += 12.0f;

    T3DVec3 screenPos;
    t3d_viewport_calc_viewspace_pos(viewport, &screenPos, &worldPos);

    // Skip if behind camera
    if (screenPos.v[2] >= 1.0f) return;

    int px = (int)screenPos.v[0];
    int py = (int)screenPos.v[1];

    const int margin = 16;
    if (px < -margin || px > SCREEN_WIDTH + margin || py < -margin || py > SCREEN_HEIGHT + margin) {
        return;
    }

    button_prompt_draw_a_icon_centered(px, py, 20.0f);
}

static void draw_cbutton_hud(void)
{
    // Bottom-left C-button diamond. C-left holds the health potion (empty button
    // sprite with the bottle drawn on top); the other three show their arrow icons.
    if (!cLeftSprite) return;

    int w = (cLeftSurf.width > 0) ? cLeftSurf.width : 24;
    int h = (cLeftSurf.height > 0) ? cLeftSurf.height : 24;
    // Target on-screen button size — source sprites are 64×64.
    const float kTargetButtonPx = 20.0f;
    int srcMax = (w > h) ? w : h;
    const float cScale = (srcMax > 0) ? (kTargetButtonPx / (float)srcMax) : 1.0f;
    int drawW = (int)((float)w * cScale);
    int drawH = (int)((float)h * cScale);

    const int marginX = ui_safe_margin_x();
    const int marginY = ui_safe_margin_y();

    // Diamond spacing: the source sprites have transparent padding, so tighten
    // the centre-to-centre offset to ~half a button so visible edges touch.
    const float kSpacingFrac = 0.7f;
    int spacingX = (int)((float)drawW * kSpacingFrac);
    int spacingY = (int)((float)drawH * kSpacingFrac);

    // Diamond centre: enough room from the safe bounds that the outer C-left
    // and C-down icons sit flush against the bottom-left corner.
    int centerX = marginX + spacingX + (drawW / 2);
    int centerY = SCREEN_HEIGHT - marginY - spacingY - (drawH / 2);

    int leftX  = centerX - spacingX; int leftY  = centerY;
    int rightX = centerX + spacingX; int rightY = centerY;
    int upX    = centerX;            int upY    = centerY - spacingY;
    int downX  = centerX;            int downY  = centerY + spacingY;

    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(0);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_filter(FILTER_BILINEAR);

    if (cUpSprite && cUpSurf.width > 0 && cUpSurf.height > 0) {
        rdpq_sprite_blit(cUpSprite, upX, upY, &(rdpq_blitparms_t){
            .scale_x = cScale, .scale_y = cScale,
            .cx = cUpSurf.width / 2, .cy = cUpSurf.height / 2,
        });
    }
    if (cDownSprite && cDownSurf.width > 0 && cDownSurf.height > 0) {
        rdpq_sprite_blit(cDownSprite, downX, downY, &(rdpq_blitparms_t){
            .scale_x = cScale, .scale_y = cScale,
            .cx = cDownSurf.width / 2, .cy = cDownSurf.height / 2,
        });
    }
    if (cRightSprite && cRightSurf.width > 0 && cRightSurf.height > 0) {
        rdpq_sprite_blit(cRightSprite, rightX, rightY, &(rdpq_blitparms_t){
            .scale_x = cScale, .scale_y = cScale,
            .cx = cRightSurf.width / 2, .cy = cRightSurf.height / 2,
        });
    }
    if (cLeftSurf.width > 0 && cLeftSurf.height > 0) {
        rdpq_sprite_blit(cLeftSprite, leftX, leftY, &(rdpq_blitparms_t){
            .scale_x = cScale, .scale_y = cScale,
            .cx = cLeftSurf.width / 2, .cy = cLeftSurf.height / 2,
        });
    }

    // Only show the potion bottle + count when the player still has potions.
    if (character_get_health_potion_count() > 0) {
        if (healthBottleSprite && healthBottleSurf.width > 0 && healthBottleSurf.height > 0) {
            float target = (float)((drawW < drawH) ? drawW : drawH) * 0.80f;
            float denom = (float)((healthBottleSurf.width > healthBottleSurf.height) ? healthBottleSurf.width : healthBottleSurf.height);
            float s = (denom > 0.0f) ? (target / denom) : 1.0f;
            if (s < 0.05f) s = 0.05f;
            if (s > 4.0f)  s = 4.0f;

            // Tint the IA8 bottle sprite dark red so it contrasts against the yellow button.
            rdpq_sync_pipe();
            rdpq_set_mode_standard();
            rdpq_mode_alphacompare(1);
            rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_mode_filter(FILTER_BILINEAR);
            rdpq_set_prim_color(RGBA32(180, 30, 30, 255));

            rdpq_sprite_blit(healthBottleSprite, leftX, leftY, &(rdpq_blitparms_t){
                .scale_x = s, .scale_y = s,
                .cx = healthBottleSurf.width / 2, .cy = healthBottleSurf.height / 2,
            });
        }

        {
            int count = character_get_health_potion_count();
            // Place the count immediately to the right of the potion graphic,
            // vertically aligned to the button centre.
            const int textX = leftX + (drawW / 2) + 2;
            const int textY = leftY + 4;

            rdpq_sync_pipe();
            rdpq_set_mode_standard();
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_set_prim_color(RGBA32(0, 0, 0, 255));
            rdpq_text_printf(NULL, FONT_UNBALANCED, textX, textY, "%d", count);
        }
    }
}

bool scene_is_cutscene_active(void) {
    return cutsceneState != CUTSCENE_NONE;
}

bool scene_is_boss_active(void) {
    return bossActivated;
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

// Check if character would collide with room boundaries at the given position
// Returns true if character would be outside room bounds (collision detected)
// bool scene_check_room_bounds(float posX, float posY, float posZ)
// {
//     return collision_mesh_check_bounds(posX, posY, posZ);
// }

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

    bossActivated = true;

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
    display_utility_set_boss_ui_intro(bossUiIntro);
    display_utility_set_player_ui_intro(playerUiIntro);

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

// End-of-phase-2-cutscene teardown: drop cutscene-only effects and hand control
// back to the fight at phase 2. Used both when the cutscene completes naturally
// and when the player skips it via the A-button overlay.
static void scene_finish_phase2_cutscene(void)
{
    lightning_fx_system_ring_enable(false);
    animation_utility_set_screen_shake_mag(0.0f);
    joypad_rumble_stop();
    dialog_controller_stop_speaking();
    cutsceneDialogActive = false;

    if (g_boss) {
        g_boss->phaseIndex = 2;
        g_boss->handAttackColliderActive = false;
        g_boss->sphereAttackColliderActive = false;
        g_boss->velX = 0.0f;
        g_boss->velZ = 0.0f;

        // Phase 2 opens with the aerial sword barrage. The handler
        // lifts the boss to hoverCenterPos.y + hoverHeight and brings
        // him back down at the end (no gravity in this game).
        boss_ai_start_aerial_sword_barrage(g_boss);
    }

    // Phase 2 cutscene music was started non-looping; arm the boss loop to take over.
    s_pendingBossLoopMusic = true;

    // Long camera blend so the cinematic angle holds while the boss
    // lifts ~80 units for the aerial sword barrage. With a short
    // (1s) blend the player-follow camera snaps to the player
    // while the boss is mid-lift and he ends up off-screen — the
    // visible result is "swords fall, boss never moved."
    camera_mode_smooth(CAMERA_CHARACTER, 5.0f);
    cameraLockOnActive = true;

    cutsceneTimer = 0.0f;
    cutsceneCameraTimer = 0.0f;
    cutsceneState = CUTSCENE_NONE;
    skipButtonVisible = false;

    // HUD was hidden the whole cutscene — snap intro + trail so it
    // reappears at the current value instead of animating back in.
    bossUiIntro = 1.0f;
    display_utility_set_boss_ui_intro(1.0f);
    if (g_boss && g_boss->maxHealth > 0.0f) {
        display_utility_snap_boss_health_trail(g_boss->health / g_boss->maxHealth);
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

            if (bossActivated && g_boss) {
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

void scene_update(void)
{
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
        if (bossActivated && g_boss) {
            boss_update(g_boss);
        }

        letterbox_update();

        if (deathRestartLockoutTimer >= DEATH_RESTART_LOCKOUT_S && btn.a) {
            scene_restart();
        }
        return;
    }

    // Don't update game logic when pause menu is active (including victory overlay case)
    if (pauseMenuBlocking) {
        return;
    }

    // Debug hotkey: L-trigger skips to boss defeated (dead + fully stopped)
    // NOTE: This is intentionally not gated by DEV_MODE because DEV_MODE is currently
    // compiled as false in `globals.h`, which would otherwise compile this out.
    // bool lHeld = joypad.btn.l;
    // bool lJustPressed = lHeld && !lastLPressed;
    // lastLPressed = lHeld;
    // if (lJustPressed && bossActivated && g_boss) {
    //     scene_debug_force_boss_defeated();
    // }

    if(cutsceneState == CUTSCENE_NONE) // Normal gameplay
    {
        // Post-boss interaction trigger: after defeat, when Z-targeted and close enough,
        // start the dialog/camera only when player presses A (held-edge).
        // IMPORTANT: check this BEFORE character_update so A doesn't also trigger a roll.
        if (bossActivated && g_boss && g_boss->state == BOSS_STATE_DEAD) {
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

        if (bossActivated && g_boss) {
            boss_update(g_boss);
            // Boss death no longer forces GAME_STATE_VICTORY.
            // The boss will play its collapse and remain still; the player can keep moving.

            // Phase 2 transition: trigger cutscene when boss health drops to 40%.
#if PHASE_2_ENABLED
            if (!phase2CutsceneTriggered && g_boss->phaseIndex == 1
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
            display_utility_set_boss_ui_intro(bossUiIntro);
        }
        if (playerUiIntro < 1.0f) {
            playerUiIntro += deltaTime / uiIntroSpeed;
            if (playerUiIntro > 1.0f) playerUiIntro = 1.0f;
            display_utility_set_player_ui_intro(playerUiIntro);
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
    if (bossActivated && g_boss) {
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
    bool lockonAllowed = cutsceneState == CUTSCENE_NONE && scene_is_boss_active() && g_boss;
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

    if (cutsceneState == CUTSCENE_NONE) {
        msa_update(deltaTime); // multi sword attack
    }
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
    // Show during gameplay when Z-targeting is active.
    // Allow the defeated boss to still be targetable (useful for post-fight dialog).
    if (!cameraLockOnActive || scene_is_cutscene_active() || !scene_is_boss_active() || !g_boss) {
        return;
    }

    // Anchor the marker to the boss' mid-body point so it aligns with lock-on aim.
    T3DVec3 worldPos = get_boss_lock_focus_point();

    // Project to screen space
    T3DVec3 screenPos;
    t3d_viewport_calc_viewspace_pos(viewport, &screenPos, &worldPos);

    // Skip if behind the camera or outside a small margin
    if (screenPos.v[2] >= 1.0f) {
        return;
    }
    const int margin = 8;
    int px = (int)screenPos.v[0];
    int py = (int)screenPos.v[1];
    if (px < -margin || px > SCREEN_WIDTH + margin || py < -margin || py > SCREEN_HEIGHT + margin) {
        return;
    }

    // Draw lock-on icon sprite (fallback to white dot if missing)
    rdpq_sync_pipe();
    rdpq_set_mode_standard();

    if (zTargetIconSprite) {
        // Avoid alpha-compare clipping; rely on the sprite's alpha (IA8).
        rdpq_mode_alphacompare(0);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

        // Scale with distance so the marker behaves like a world-space attachment
        // (close = larger, far = smaller) instead of constant screen-space UI size.
        //
        // Tiny3D provides screenPos.v[2] as normalized depth (near=0 .. far=1).
        float z01 = screenPos.v[2];
        if (z01 < 0.0f) z01 = 0.0f;
        if (z01 > 0.9999f) z01 = 0.9999f;

        // Keep these in sync with the camera projection (see camera_controller.c).
        const float nearClip = 4.0f;
        const float farClip  = 2000.0f;
        float z = nearClip + z01 * (farClip - nearClip);
        if (z < nearClip) z = nearClip;

        // Reference distance where we want the icon to be ~8x8 on-screen.
        const float zRef = 300.0f;
        float s = 0.125f * (zRef / z);
        // Clamp for readability (and to avoid enormous icons up close).
        if (s < 0.05f) s = 0.05f;
        if (s > 0.275f) s = 0.275f;

        rdpq_sprite_blit(zTargetIconSprite, px, py, &(rdpq_blitparms_t){
            .scale_x = s, .scale_y = s,
            .cx = 32, .cy = 32, // center of the 64x64 sprite
        });
    } else {
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
        const int halfSize = 3;
        rdpq_fill_rectangle(px - halfSize, py - halfSize, px + halfSize + 1, py + halfSize + 1);
    }
}

/* -----------------------------------------------------------------------------
 * Dust particles (simple world->screen puffs)
 * -------------------------------------------------------------------------- */

typedef struct {
    bool  active;
    float pos[3];   // world
    float vel[3];   // world units/sec
    float age;      // sec
    float life;     // sec
    float size_px;  // base pixel size
} DustParticle;

enum { DUST_MAX = 64 };
static DustParticle s_dust[DUST_MAX];

static inline float dust_clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline float dust_alpha01(const DustParticle *p) {
    if (!p || p->life <= 0.0f) return 0.0f;
    float t = dust_clampf(p->age / p->life, 0.0f, 1.0f);
    // Nice falloff (not linear).
    float a = 1.0f - t;
    return a * a;
}

static void dust_reset(void) {
    memset(s_dust, 0, sizeof(s_dust));
}

static int dust_alloc_slot(void) {
    for (int i = 0; i < DUST_MAX; i++) {
        if (!s_dust[i].active) return i;
    }
    // No free slot; evict the oldest.
    int oldest = 0;
    float bestAge = s_dust[0].age;
    for (int i = 1; i < DUST_MAX; i++) {
        if (s_dust[i].age > bestAge) {
            bestAge = s_dust[i].age;
            oldest = i;
        }
    }
    return oldest;
}

void scene_spawn_dust_burst(float x, float y, float z, float strength) {
    // Safe to call even before init/reset.
    if (strength < 0.05f) return;
    if (strength > 3.0f) strength = 3.0f;

    // Prefer readable puffs, but spawn enough to feel like "dust".
    int count = 6 + (int)(strength * 3.0f);
    if (count < 6) count = 6;
    if (count > 18) count = 18;

    // Track spawned positions for this burst so we can avoid stacking.
    float spawnX[18];
    float spawnZ[18];
    int spawned = 0;

    for (int i = 0; i < count; i++) {
        int idx = dust_alloc_slot();
        DustParticle *p = &s_dust[idx];

        // Randomized spawn around the impact point.
        // Use polar sampling so particles don't "stack" near the center.
        float dirX = 1.0f, dirZ = 0.0f;
        float radius = 0.0f;
        float px = x, pz = z;

        // Ensure puffs don't overlap too tightly (helps readability).
        // Use a small number of retries; if we fail, accept the last sample.
        //
        // NOTE: Use evenly spaced angles with jitter so the burst is *visibly* spread
        // around the boss base (avoids the "only 2 visible" look when several land in
        // similar screen-space).
        const float minSep = 18.0f; // world units
        for (int attempt = 0; attempt < 10; attempt++) {
            float jitter = (rand_custom_float() - 0.5f) * 0.35f; // +/- ~0.175 of a slot
            float t = ((float)i + 0.5f + jitter) / (float)count;
            float ang = t * (2.0f * T3D_PI);

            // Sample radius with sqrt for a more even distribution over area.
            float r01 = sqrtf(rand_custom_float());

            // Keep puffs near the boss' feet so they stay on-screen and read as "base dust".
            float rMin = 10.0f;
            float rMax = 42.0f + (18.0f * strength);
            radius = rMin + r01 * (rMax - rMin);

            dirX = cosf(ang);
            dirZ = sinf(ang);

            // Candidate position
            px = x + dirX * radius;
            pz = z + dirZ * radius;

            bool ok = true;
            for (int j = 0; j < spawned; j++) {
                float dx = px - spawnX[j];
                float dz = pz - spawnZ[j];
                if ((dx*dx + dz*dz) < (minSep * minSep)) {
                    ok = false;
                    break;
                }
            }
            if (ok) break;
        }

        p->active = true;
        p->age = 0.0f;
        // Last a bit longer so the burst reads.
        p->life = 0.65f + rand_custom_float() * 0.45f;

        // Large puffs: sizes are in pixels (screen-space) and later scaled from the sprite.
        // Bias toward bigger particles on the first couple slots, with smaller "filler" puffs after.
        float bigBias = (i < 2) ? 1.0f : 0.0f;
        // Tiny bump so they're a touch bigger overall.
        float base = 16.0f + (bigBias * 11.0f);     // make even the "small" ones readable
        float var  = 12.0f + (10.0f * strength);
        p->size_px = base + rand_custom_float() * var;

        p->pos[0] = px;
        // Use the provided impact Y so this works for any room/floor height.
        // Slightly above the floor to avoid any numerical weirdness.
        p->pos[1] = y + 0.5f + rand_custom_float() * 1.0f;
        p->pos[2] = pz;

        // Radial outward puff + slight upward drift.
        p->vel[0] = dirX * (35.0f + 35.0f * strength);
        p->vel[1] = (10.0f + 14.0f * rand_custom_float()) * strength;
        p->vel[2] = dirZ * (35.0f + 35.0f * strength);

        if (spawned < 18) {
            spawnX[spawned] = px;
            spawnZ[spawned] = pz;
            spawned++;
        }
    }
}

static void dust_update(float dt) {
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.25f) dt = 0.25f;

    for (int i = 0; i < DUST_MAX; i++) {
        DustParticle *p = &s_dust[i];
        if (!p->active) continue;

        p->age += dt;
        if (p->age >= p->life) {
            p->active = false;
            continue;
        }

        // Simple damped motion: expand quickly then slow, and drift up a bit.
        float dampXZ = expf(-5.0f * dt);
        p->vel[0] *= dampXZ;
        p->vel[2] *= dampXZ;
        p->vel[1] *= expf(-2.5f * dt);

        p->pos[0] += p->vel[0] * dt;
        p->pos[1] += p->vel[1] * dt;
        p->pos[2] += p->vel[2] * dt;
    }
}

static void dust_draw(T3DViewport *viewport) {
    if (!viewport) return;

    const bool useSprite = (dustParticleSprite && dustParticleSurf.width > 0 && dustParticleSurf.height > 0);

    // 2D render state.
    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    // Depth test against the 3D pass so dust doesn't always draw "on top".
    // We don't write depth (so UI stays unaffected).
    rdpq_mode_zbuf(true, false);
    if (useSprite) {
        // Standard alpha blending so prim alpha can fade particles.
        rdpq_mode_alphacompare(1);
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    } else {
        // Fallback if sprite missing.
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    }

    for (int i = 0; i < DUST_MAX; i++) {
        const DustParticle *p = &s_dust[i];
        if (!p->active) continue;

        T3DVec3 worldPos = {{ p->pos[0], p->pos[1], p->pos[2] }};
        T3DVec3 screenPos;
        t3d_viewport_calc_viewspace_pos(viewport, &screenPos, &worldPos);

        if (screenPos.v[2] >= 1.0f) continue;

        // Feed a per-primitive Z into RDPQ so depth testing can work for screen-space blits.
        // Tiny3D provides screenPos.v[2] as a normalized depth (near=0 .. far=1).
        float z01 = dust_clampf(screenPos.v[2], 0.0f, 0.9999f);
        rdpq_mode_zoverride(true, z01, 0);

        float a01 = dust_alpha01(p);
        // Slightly stronger alpha when using the sprite so it reads.
        uint8_t a = (uint8_t)dust_clampf(a01 * (useSprite ? 180.0f : 170.0f), 0.0f, 255.0f);
        if (a == 0) continue;

        // Light warm grey "dust".
        rdpq_set_prim_color(RGBA32(215, 210, 200, a));

        int px = (int)screenPos.v[0];
        int py = (int)screenPos.v[1];

        // Slight grow then fade.
        float grow = 1.0f + 0.7f * (p->age / p->life);
        int half = (int)(p->size_px * grow);
        if (half < 4) half = 4;
        if (half > 26) half = 26;

        if (useSprite) {
            const int src_w = dustParticleSurf.width;
            const int src_h = dustParticleSurf.height;
            const int w = half * 2;
            const int h = half * 2;
            const float sx = (src_w > 0) ? ((float)w / (float)src_w) : 1.0f;
            const float sy = (src_h > 0) ? ((float)h / (float)src_h) : 1.0f;
            rdpq_tex_blit(&dustParticleSurf, px - (w / 2), py - (h / 2), &(rdpq_blitparms_t){
                .scale_x = sx,
                .scale_y = sy,
            });
        } else {
            rdpq_fill_rectangle(px - half, py - half, px + half + 1, py + half + 1);
        }
    }

    // Restore to non-depth 2D for subsequent overlays.
    rdpq_mode_zoverride(false, 0.0f, 0);
    rdpq_mode_zbuf(false, false);
}

/* -----------------------------------------------------------------------------
 * Blood splatters (sword-on-boss impact)
 * -------------------------------------------------------------------------- */

typedef struct {
    bool  active;
    float pos[3];     // world
    float vel[3];     // world units/sec
    float age;        // sec
    float life;       // sec
    float size_px;    // base pixel size (height of sprite drawn)
    uint8_t spriteIdx;
    uint8_t r, g, b;  // tint (per-particle variation)
} BloodParticle;

enum { BLOOD_MAX = 48 };
static BloodParticle s_blood[BLOOD_MAX];

static inline float blood_clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline float blood_alpha01(const BloodParticle *p) {
    if (!p || p->life <= 0.0f) return 0.0f;
    float t = blood_clampf(p->age / p->life, 0.0f, 1.0f);
    // Mostly opaque for the first half, then fade out (quadratic).
    if (t < 0.55f) return 1.0f;
    float u = (t - 0.55f) / 0.45f;
    return (1.0f - u) * (1.0f - u);
}

static void blood_reset(void) {
    memset(s_blood, 0, sizeof(s_blood));
}

static int blood_alloc_slot(void) {
    for (int i = 0; i < BLOOD_MAX; i++) {
        if (!s_blood[i].active) return i;
    }
    int oldest = 0;
    float bestAge = s_blood[0].age;
    for (int i = 1; i < BLOOD_MAX; i++) {
        if (s_blood[i].age > bestAge) {
            bestAge = s_blood[i].age;
            oldest = i;
        }
    }
    return oldest;
}

void scene_spawn_blood_burst(float x, float y, float z, float strength) {
    if (strength < 0.05f) return;
    if (strength > 3.0f) strength = 3.0f;

    // One "large" anchor splatter, several mediums, several tinies.
    int largeCount  = 1;
    int mediumCount = 3 + (int)(strength * 1.5f);
    int tinyCount   = 6 + (int)(strength * 3.0f);
    if (mediumCount > 6) mediumCount = 6;
    if (tinyCount   > 12) tinyCount  = 12;

    int total = largeCount + mediumCount + tinyCount;

    for (int i = 0; i < total; i++) {
        int idx = blood_alloc_slot();
        BloodParticle *p = &s_blood[idx];

        // Pick which sprite tier this particle belongs to and randomize within tier.
        uint8_t spriteIdx;
        float baseSize;
        if (i < largeCount) {
            spriteIdx = BLOOD_SPRITE_LARGE;
            baseSize  = 22.0f + 4.0f * strength;
        } else if (i < largeCount + mediumCount) {
            int variant = (int)(rand_custom_float() * 3.0f);
            if (variant > 2) variant = 2;
            spriteIdx = (uint8_t)(BLOOD_SPRITE_MEDIUM_A + variant);
            baseSize  = 14.0f + 4.0f * strength;
        } else {
            int variant = (int)(rand_custom_float() * 3.0f);
            if (variant > 2) variant = 2;
            spriteIdx = (uint8_t)(BLOOD_SPRITE_TINY_A + variant);
            baseSize  = 7.0f + 3.0f * strength;
        }

        p->active = true;
        p->age    = 0.0f;
        // Mediums/tinies fly farther; the large anchor lingers a hair longer to read.
        p->life   = (i < largeCount) ? 0.55f : (0.35f + rand_custom_float() * 0.30f);
        p->size_px  = baseSize + rand_custom_float() * 3.0f;
        p->spriteIdx = spriteIdx;

        // Random red tint (darker arterial through brighter splatter).
        int rJitter = (int)((rand_custom_float() - 0.5f) * 30.0f);
        int gJitter = (int)(rand_custom_float() * 14.0f);
        int rr = 165 + rJitter;
        int gg = 12  + gJitter;
        int bb = 18;
        if (rr < 110) rr = 110;
        if (rr > 210) rr = 210;
        if (gg <   0) gg = 0;
        if (gg >  40) gg = 40;
        p->r = (uint8_t)rr;
        p->g = (uint8_t)gg;
        p->b = (uint8_t)bb;

        // Spawn directly at the contact point with a small jitter so all the sprites
        // don't overlap into one blob on screen.
        float jitterR = (rand_custom_float() * 4.0f) - 2.0f;
        float jitterU = (rand_custom_float() * 4.0f) - 2.0f;
        p->pos[0] = x + jitterR;
        p->pos[1] = y + jitterU;
        p->pos[2] = z + jitterR * 0.5f;

        // Pick an outward direction in a full 3D cone biased upward + outward.
        // Yaw is uniformly distributed; pitch leans up (toward the player's swing).
        float yaw   = rand_custom_float() * (2.0f * T3D_PI);
        float pitch = (T3D_PI * 0.20f) + rand_custom_float() * (T3D_PI * 0.30f); // 36° to 90°
        float c = cosf(pitch);
        float dirX = c * cosf(yaw);
        float dirY = sinf(pitch);
        float dirZ = c * sinf(yaw);

        // Speeds: big anchor moves slow (looks like a heavy gout), mediums fast, tinies fastest spray.
        float baseSpeed;
        if (i < largeCount)                      baseSpeed = 30.0f + 20.0f * strength;
        else if (i < largeCount + mediumCount)   baseSpeed = 110.0f + 60.0f * strength;
        else                                     baseSpeed = 170.0f + 90.0f * strength;
        float speed = baseSpeed * (0.75f + rand_custom_float() * 0.5f);

        p->vel[0] = dirX * speed;
        p->vel[1] = dirY * speed;
        p->vel[2] = dirZ * speed;
    }
}

static void blood_update(float dt) {
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.25f) dt = 0.25f;

    // Gravity in world units / s^2. Tuned to feel snappy at the small scale of a burst
    // (particles only live ~0.4-0.6s, so heavy gravity makes them clearly arc).
    const float GRAVITY = 900.0f;

    for (int i = 0; i < BLOOD_MAX; i++) {
        BloodParticle *p = &s_blood[i];
        if (!p->active) continue;

        p->age += dt;
        if (p->age >= p->life) {
            p->active = false;
            continue;
        }

        // Light air drag on the lateral axes only - keeps the arc readable.
        float dampXZ = expf(-1.5f * dt);
        p->vel[0] *= dampXZ;
        p->vel[2] *= dampXZ;

        // Gravity.
        p->vel[1] -= GRAVITY * dt;

        p->pos[0] += p->vel[0] * dt;
        p->pos[1] += p->vel[1] * dt;
        p->pos[2] += p->vel[2] * dt;
    }
}

static void blood_draw(T3DViewport *viewport) {
    if (!viewport) return;

    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_zbuf(true, false);
    rdpq_mode_alphacompare(1);
    rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    for (int i = 0; i < BLOOD_MAX; i++) {
        const BloodParticle *p = &s_blood[i];
        if (!p->active) continue;

        sprite_t *sp = bloodSprites[p->spriteIdx];
        surface_t *sf = &bloodSurfs[p->spriteIdx];
        if (!sp || sf->width <= 0 || sf->height <= 0) continue;

        T3DVec3 worldPos = {{ p->pos[0], p->pos[1], p->pos[2] }};
        T3DVec3 screenPos;
        t3d_viewport_calc_viewspace_pos(viewport, &screenPos, &worldPos);
        if (screenPos.v[2] >= 1.0f) continue;

        float z01 = blood_clampf(screenPos.v[2], 0.0f, 0.9999f);
        rdpq_mode_zoverride(true, z01, 0);

        float a01 = blood_alpha01(p);
        uint8_t a = (uint8_t)blood_clampf(a01 * 230.0f, 0.0f, 255.0f);
        if (a == 0) continue;

        rdpq_set_prim_color(RGBA32(p->r, p->g, p->b, a));

        // Slight shrink as the particle ages (droplets thin out).
        float shrink = 1.0f - 0.25f * (p->age / p->life);
        if (shrink < 0.5f) shrink = 0.5f;

        int h = (int)(p->size_px * shrink);
        if (h < 3) h = 3;
        if (h > 40) h = 40;

        // Preserve sprite aspect ratio.
        float aspect = (sf->height > 0) ? ((float)sf->width / (float)sf->height) : 1.0f;
        int w = (int)(h * aspect);
        if (w < 3) w = 3;

        int px = (int)screenPos.v[0];
        int py = (int)screenPos.v[1];

        float sx = (sf->width  > 0) ? ((float)w / (float)sf->width)  : 1.0f;
        float sy = (sf->height > 0) ? ((float)h / (float)sf->height) : 1.0f;
        rdpq_tex_blit(sf, px - (w / 2), py - (h / 2), &(rdpq_blitparms_t){
            .scale_x = sx,
            .scale_y = sy,
        });
    }

    rdpq_mode_zoverride(false, 0.0f, 0);
    rdpq_mode_zbuf(false, false);
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
    //rdpq_set_prim_color((color_t){0xFF, 0xFF, 0xFF, 0xFF});
    rdpq_mode_fog(RDPQ_FOG_STANDARD);
    rdpq_set_fog_color(fogColor);

    t3d_screen_clear_color(RGBA32(0, 0, 0, 0xFF));
    t3d_screen_clear_depth();

    if(cutsceneState != CUTSCENE_NONE){
        cutscene_manager_draw_fog();
    }else{
        t3d_fog_set_range(450.0f, 800.0f);
    }
    t3d_fog_set_enabled(true);

    // Lighting
    t3d_light_set_ambient(colorAmbient);
    // T3DVec3 negCamDir = {{-camDir.x, -camDir.y, -camDir.z}};
    // t3d_light_set_directional(0, (uint8_t[4]){0x00, 0x00, 0x00, 0xFF}, &negCamDir);
    // t3d_light_set_count(1);
    if(cutsceneState != CUTSCENE_NONE)
    {
        scene_draw_cutscene(viewport);
        // Draw letterbox bars during cutscenes
        letterbox_draw();
        // Draw skip indicator on top of letterbox bars
        cutscene_manager_draw_skip_overlay(skipButtonVisible);
        return;
    }
    // ===== DRAW 3D =====

    rdpq_sync_pipe();
    rdpq_mode_zbuf(false, false);

    // Draw no depth environment first
    t3d_matrix_push_pos(1);

        t3d_matrix_set(windowsMatrix, true);
        rspq_block_run(windowsDpl);

        t3d_matrix_set(mapMatrix, true);
        rspq_block_run(mapDpl);
    t3d_matrix_pop(1);


    // if(g_boss->isAttacking || g_boss->health <= 0 || g_boss->state == BOSS_STATE_COMBO_ATTACK || g_boss->state == BOSS_STATE_STOMP) // TODO: Hacky fix but something weird is going on with comnbo1 and we dont have time
    // {
        //Draw depth environment
        rdpq_sync_pipe();
        rdpq_mode_zbuf(true, true);

        t3d_matrix_push_pos(1);
            t3d_matrix_set(roomFloorMatrix, true);
            rspq_block_run(roomFloorDpl);
        t3d_matrix_pop(1);
    // }
    // else
    // {
    //     //Draw depth environment
    //     rdpq_sync_pipe();
    //     rdpq_mode_zbuf(false, false);

    //     t3d_matrix_push_pos(1);
    //         t3d_matrix_set(roomFloorMatrix, true);
    //         rspq_block_run(roomFloorDpl);
    //     t3d_matrix_pop(1);
    // }

    rdpq_sync_pipe();
    rdpq_mode_zbuf(false, false);

    t3d_matrix_push_pos(1);
        // projection effects
        boss_ground_crush_draw();
        // blob shadows
        character_draw_shadow();
        if (g_boss) {
            boss_draw_shadow(g_boss);
        }

    t3d_matrix_pop(1);

    rdpq_sync_pipe();
    rdpq_mode_zbuf(true, true);

    t3d_matrix_push_pos(1);
        t3d_matrix_set(roomLedgeMatrix, true);
        rspq_block_run(roomLedgeDpl);

        t3d_matrix_set(pillarsMatrix, true);
        rspq_block_run(pillarsDpl);

        t3d_matrix_set(pillarsFrontMatrix, true);
        rspq_block_run(pillarsFrontDpl);

    t3d_matrix_pop(1);

    rdpq_sync_pipe();
    rdpq_mode_zbuf(false, false);

    t3d_matrix_push_pos(1);
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
    t3d_matrix_pop(1);

    rdpq_sync_pipe();
    rdpq_mode_zbuf(true, true);

    // Draw characters

    t3d_matrix_push_pos(1);
    // if(cutsceneState == CUTSCENE_PHASE1_SWORDS_CLOSEUP)
    // {
    //     t3d_matrix_set(cinematicChainsMatrix, true);
    //     rspq_block_run(cinematicChainsDpl);
    // }

        character_draw();
        if (g_boss) {
            boss_draw(g_boss);
        }


    t3d_matrix_pop(1);

    msa_draw_visuals(viewport); // multi sword attack
    //boulder_hazard_draw(viewport); // close-range ground boulders

    //Draw transparencies last
    // t3d_matrix_push_pos(1);
    //     t3d_matrix_set(sunshaftsMatrix, true);
    //     rspq_block_run(sunshaftsDpl);
    // t3d_matrix_pop(1);

    // rdpq_sync_pipe();
    // rdpq_mode_zbuf(false, false);

    // Fog door (transparent): depth test ON, depth write OFF so it can be drawn late.
    rdpq_sync_pipe();
    rdpq_mode_zbuf(true, false);
    t3d_matrix_push_pos(1);
        if (fogDoorMatrix && fogDoorModel) {
            t3d_matrix_set(fogDoorMatrix, true);
            t3d_model_draw_custom(fogDoorModel, (T3DModelDrawConf){
                .userData = &fogScrollParams,
                .tileCb = tile_scroll,
            });
        }
    t3d_matrix_pop(1);
    rdpq_sync_pipe();
    rdpq_mode_zbuf(true, true);

    t3d_matrix_push_pos(1);
        t3d_matrix_set(chainsMatrix, true);
        rspq_block_run(chainsDpl);
    t3d_matrix_pop(1);
    // ===== DRAW 2D =====

    // Screen-space ribbon trails, drawn right after 3D so they feel "in world"
    sword_trail_draw_all(viewport);

    // Dust puffs (boss landings/impacts)
    dust_update(deltaTime);
    dust_draw(viewport);

    // Blood splatters from boss hits (drawn after dust so red reads on top of puffs)
    blood_update(deltaTime);
    blood_draw(viewport);

    // Post-boss interaction prompt ("A") above the defeated boss when close enough to interact
    draw_post_boss_a_prompt(viewport);

    // Overlay lock-on marker above the boss
    if(DEV_MODE)
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
        // Boss UI appears only during normal gameplay, not on victory screens.
        if (!isVictory && scene_is_boss_active() && g_boss && bossTitleFade <= 0.0f) {
            boss_draw_ui(g_boss, viewport);
        }
        character_draw_ui();
        draw_cbutton_hud();
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

    // video draw game over fade to black over everything (yes i know it's hacky, time crunch and sludge file)

    if (videoPreroll != VIDEO_PREROLL_NONE) {
        display_utility_solid_black_transition(false, VIDEO_FADE_SPEED);
    }


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
    //collision_mesh_cleanup();
    scene_delete_environment();
    boss_ground_crush_cleanup();
    camera_reset();

    character_delete();
    if (g_boss) {
        boss_free(g_boss);
        free(g_boss);
        g_boss = NULL;
    }

    dialog_controller_free();
    audio_scene_unload_sfx();
    cutscene_manager_cleanup();
    button_prompt_cleanup();

    if (victoryTitleBgSprite) {
        sprite_free(victoryTitleBgSprite);
        victoryTitleBgSprite = NULL;
        surface_free(&victoryTitleBgSurf);
    }

    if (dustParticleSprite) {
        sprite_free(dustParticleSprite);
        dustParticleSprite = NULL;
        surface_free(&dustParticleSurf);
    }

    for (int i = 0; i < BLOOD_SPRITE_COUNT; i++) {
        if (bloodSprites[i]) {
            sprite_free(bloodSprites[i]);
            bloodSprites[i] = NULL;
            surface_free(&bloodSurfs[i]);
        }
    }

    if (zTargetIconSprite) {
        sprite_free(zTargetIconSprite);
        zTargetIconSprite = NULL;
        surface_free(&zTargetIconSurf);
    }

    if (cUpSprite)    { sprite_free(cUpSprite);    cUpSprite = NULL;    surface_free(&cUpSurf); }
    if (cDownSprite)  { sprite_free(cDownSprite);  cDownSprite = NULL;  surface_free(&cDownSurf); }
    if (cLeftSprite)  { sprite_free(cLeftSprite);  cLeftSprite = NULL;  surface_free(&cLeftSurf); }
    if (cRightSprite) { sprite_free(cRightSprite); cRightSprite = NULL; surface_free(&cRightSurf); }

    if (healthBottleSprite) {
        sprite_free(healthBottleSprite);
        healthBottleSprite = NULL;
        surface_free(&healthBottleSurf);
    }
}