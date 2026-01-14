#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include <t3d/t3ddebug.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "audio_controller.h"

#include "camera_controller.h"

#include "joypad_utility.h"
#include "general_utility.h"
#include "game_lighting.h"
#include "game_time.h"

#include "globals.h"

#include "scene.h"
#include "character.h"
#include "game/bosses/boss.h"
#include "game/bosses/boss_anim.h"
#include "dialog_controller.h"
#include "display_utility.h"
//#include "collision_mesh.h"
#include "collision_system.h"
#include "letterbox_utility.h"

// TODO: This should not be declared in the header file, as it is only used externally (temp)
#include "dev.h"
#include "debug_draw.h"

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

// Dynamic Banner (Title Screen)
static T3DModel* dynamicBannerModel; 
static rspq_block_t* dynamicBannerDpl; 
static T3DMat4FP* dynamicBannerMatrix; 
static T3DSkeleton* dynamicBannerSkeleton; 
static T3DAnim** dynamicBannerAnimations = NULL;

// Cinematic Chains
static T3DModel* cinematicChainsModel; 
static rspq_block_t* cinematicChainsDpl; 
static T3DMat4FP* cinematicChainsMatrix; 
static T3DSkeleton* cinematicChainsSkeleton; 
static T3DAnim** cinematicChainsAnimations = NULL;
static int currentCinematicChainsAnimation = 0;
// Cutscene Chain Break
static T3DModel* cutsceneChainBreakModel; 
static rspq_block_t* cutsceneChainBreakDpl; 
static T3DMat4FP* cutsceneChainBreakMatrix; 
static T3DSkeleton* cutsceneChainBreakSkeleton; 
static T3DAnim** cutsceneChainBreakAnimations = NULL;

static int currentTitleDialog = 0;
static float titleTextActivationTimer = 0.0f;
static float titleTextActivationTime = 50.0f;

static float titleStartGameTimer = 0.0f;
static float titleStartGameTime = 10.0f;
static float titleFadeTime = 7.0f;

static float roomY = -1.0f;

static bool screenTransition = false;
static bool screenBreath = false;

static const char *titleDialogs[] = {
    ">The Demon\nking has\nforced\nthe land\ninto a\ncentury long\ndarkness.",
    ">The King\nhas trained\na legion\nof powerful\nknights\nsworn to\nprotect the\nthrone.",
    ">These\nbattle born\nknights are\ntaken from\ntheir\nfamilies and\ncast into\nservitude.",
    ">Enduring\nblade and\ntorment\nuntil nothing\nremains but\nhollow armor."
};
#define TITLE_DIALOG_COUNT (sizeof(titleDialogs) / sizeof(titleDialogs[0]))

static const char *phase1Dialogs[] = {
    "^Those who approach the\nthrone of gold~ ^fall at my\nblade.",
    "^A Knight?~ >Where is your\n^loyalty...",
    "^Where is your...~ <Fear.",
};
bool cutsceneDialogActive = false;

static CutsceneState cutsceneState = CUTSCENE_PHASE1_INTRO;
static float cutsceneTimer = 0.0f;
static float cutsceneCameraTimer = 0.0f;  // Separate timer for camera movement (doesn't reset)
static bool bossActivated = false;
static Boss* g_boss = NULL;  // Boss instance pointer
static T3DVec3 cutsceneCamPosStart;  // Initial camera position (further back)
static T3DVec3 cutsceneCamPosEnd;    // Final camera position (closer to boss)

// Game state management
static GameState gameState = GAME_STATE_TITLE;
static bool lastMenuActive = false;

// Input state tracking
static bool lastAPressed = false;
static bool lastZPressed = false;

// All SFX:
static wav64_t characterTitleSfx;

void scene_load_sfx(){
    wav64_open(&characterTitleSfx, "rom:/audio/sfx/title_screen_walk_effect-22k.wav64");
    wav64_set_loop(&characterTitleSfx, false);
}
void scene_load_environment(){

    // ===== LOAD MAP =====
    mapModel = t3d_model_load("rom:/boss_room/room.t3dm");
    rspq_block_begin();
    t3d_model_draw(mapModel);
    mapDpl = rspq_block_end();
    
    // Create map matrix
    mapMatrix = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_from_srt_euler(mapMatrix, 
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},    // scale to match character
        (float[3]){0.0f, 0.0f, 0.0f},    // rotation
        (float[3]){0.0f, roomY, 0.0f}    // ground level position
    );

    // ===== LOAD PILLARS =====
    pillarsModel = t3d_model_load("rom:/boss_room/pillars.t3dm");
    rspq_block_begin();
    t3d_model_draw(pillarsModel);
    pillarsDpl = rspq_block_end();
    
    pillarsMatrix = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_from_srt_euler(pillarsMatrix, 
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, roomY, 0.0f}
    );

    pillarsFrontModel = t3d_model_load("rom:/boss_room/pillars_front.t3dm");
    rspq_block_begin();
    t3d_model_draw(pillarsFrontModel);
    pillarsFrontDpl = rspq_block_end();
    
    pillarsFrontMatrix = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_from_srt_euler(pillarsFrontMatrix, 
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
    t3d_mat4fp_from_srt_euler(roomLedgeMatrix, 
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
    t3d_mat4fp_from_srt_euler(windowsMatrix, 
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, roomY, 0.0f}
    );

    // ===== LOAD CHAINS =====
    chainsModel = t3d_model_load("rom:/boss_room/ceiling_chains.t3dm");
    rspq_block_begin();
    t3d_model_draw(chainsModel);
    chainsDpl = rspq_block_end();
    
    chainsMatrix = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_from_srt_euler(chainsMatrix, 
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
    t3d_mat4fp_from_srt_euler(sunshaftsMatrix, 
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
    t3d_mat4fp_from_srt_euler(fogDoorMatrix, 
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
    t3d_mat4fp_from_srt_euler(roomFloorMatrix, 
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, roomY, 0.0f}
    );
   
    // ===== LOAD Cinematic Chains =====
    cinematicChainsModel = t3d_model_load("rom:/boss_room/chains.t3dm"); 
    cinematicChainsSkeleton = malloc_uncached(sizeof(T3DSkeleton)); 
    *cinematicChainsSkeleton = t3d_skeleton_create(cinematicChainsModel); 
    const char* cinematicChainsAnimationNames[] = {"ChainsInitial", "ChainsSeparate"}; 
    const int cinematicChainsAnimationCount = 2;

    cinematicChainsAnimations = malloc_uncached(cinematicChainsAnimationCount * sizeof(T3DAnim*)); 
    for (int i = 0; i < cinematicChainsAnimationCount; i++) { 
        cinematicChainsAnimations[i] = malloc_uncached(sizeof(T3DAnim)); 
        *cinematicChainsAnimations[i] = t3d_anim_create(cinematicChainsModel, cinematicChainsAnimationNames[i]); 
        t3d_anim_attach(cinematicChainsAnimations[i], cinematicChainsSkeleton); 
    }

    t3d_anim_set_looping(cinematicChainsAnimations[currentCinematicChainsAnimation], true); 
    t3d_anim_set_playing(cinematicChainsAnimations[currentCinematicChainsAnimation], true); 

    rspq_block_begin(); 
    t3d_model_draw_skinned(cinematicChainsModel, cinematicChainsSkeleton); 
    cinematicChainsDpl = rspq_block_end(); 
    cinematicChainsMatrix = malloc_uncached(sizeof(T3DMat4FP)); 
    t3d_mat4fp_from_srt_euler(cinematicChainsMatrix, (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE}, (float[3]){0.0f, 0.0f, 0.0f}, (float[3]){0.0f, 0.0f, 0.0f} );

    // ===== LOAD Chain Break =====
    cutsceneChainBreakModel = t3d_model_load("rom:/cutscene/shatter_chain.t3dm"); 
    cutsceneChainBreakSkeleton = malloc_uncached(sizeof(T3DSkeleton)); 
    *cutsceneChainBreakSkeleton = t3d_skeleton_create(cutsceneChainBreakModel); 
    const char* cutsceneChainBreakAnimationNames[] = {"ChainBreak"}; 
    const int cutsceneChainBreakAnimationCount = 1;

    cutsceneChainBreakAnimations = malloc_uncached(cutsceneChainBreakAnimationCount * sizeof(T3DAnim*)); 
    for (int i = 0; i < cutsceneChainBreakAnimationCount; i++) { 
        cutsceneChainBreakAnimations[i] = malloc_uncached(sizeof(T3DAnim)); 
        *cutsceneChainBreakAnimations[i] = t3d_anim_create(cutsceneChainBreakModel, cutsceneChainBreakAnimationNames[i]); 
        t3d_anim_set_looping(cutsceneChainBreakAnimations[i], false); 
        t3d_anim_set_playing(cutsceneChainBreakAnimations[i], true); 
        t3d_anim_attach(cutsceneChainBreakAnimations[i], cutsceneChainBreakSkeleton); 
    }

    rspq_block_begin(); 
    t3d_model_draw_skinned(cutsceneChainBreakModel, cutsceneChainBreakSkeleton); 
    cutsceneChainBreakDpl = rspq_block_end(); 
    cutsceneChainBreakMatrix = malloc_uncached(sizeof(T3DMat4FP)); 
    t3d_mat4fp_from_srt_euler(cutsceneChainBreakMatrix, (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE}, (float[3]){0.0f, 0.0f, 0.0f}, (float[3]){0.0f, 0.0f, 0.0f} );

}



void scene_title_init()
{
    audio_play_music("rom:/audio/music/demonous-22k.wav64", true);

    // Init to title screen position
    camera_initialize(
        &(T3DVec3){{-580.6f, 75.0f, 0.0f}}, 
        &(T3DVec3){{-1,0,0}}, 
        1.544792654048f, 
        4.05f
    );

    customCamTarget.v[1] = 90.0f;

    character.pos[0] = -650.0f;
    character.pos[1] = 44.0f;
    character.pos[2] = 0.0f;

    character.scale[0] = MODEL_SCALE * 1.5f;
    character.scale[1] = MODEL_SCALE * 1.5f;
    character.scale[2] = MODEL_SCALE * 1.5f;

    character.rot[1] = 0.0f;

    character_update_position();


    // ===== LOAD Dynamic Banner =====
    dynamicBannerModel = t3d_model_load("rom:/title_screen/dynamic_banners.t3dm"); 
    dynamicBannerSkeleton = malloc_uncached(sizeof(T3DSkeleton)); 
    *dynamicBannerSkeleton = t3d_skeleton_create(dynamicBannerModel); 
    const char* dynamicBannerAnimationNames[] = {"Wind"}; 
    const int dynamicBannerAnimationCount = 1;

    dynamicBannerAnimations = malloc_uncached(dynamicBannerAnimationCount * sizeof(T3DAnim*)); 
    for (int i = 0; i < dynamicBannerAnimationCount; i++) { 
        dynamicBannerAnimations[i] = malloc_uncached(sizeof(T3DAnim)); 
        *dynamicBannerAnimations[i] = t3d_anim_create(dynamicBannerModel, dynamicBannerAnimationNames[i]); 
        t3d_anim_set_looping(dynamicBannerAnimations[i], true); 
        t3d_anim_set_playing(dynamicBannerAnimations[i], true); 
        t3d_anim_attach(dynamicBannerAnimations[i], dynamicBannerSkeleton); 
    }

    rspq_block_begin(); 
    t3d_model_draw_skinned(dynamicBannerModel, dynamicBannerSkeleton); 
    dynamicBannerDpl = rspq_block_end(); 
    dynamicBannerMatrix = malloc_uncached(sizeof(T3DMat4FP)); 
    t3d_mat4fp_from_srt_euler(dynamicBannerMatrix, (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE}, (float[3]){0.0f, 0.0f, 0.0f}, (float[3]){0.0f, roomY, 0.0f} );

    // Start Dialog
    dialog_controller_speak(titleDialogs[0], 0, 9.0f, false, true);

    startScreenFade = true;
}

void scene_init(void) 
{
    // ==== Sounds ====

    scene_load_sfx();

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

    // Load collision mesh
    // NOTE: If collision wireframe doesn't match the rendered room, adjust this scale.
    // The exported bossroom.collision is in glb units (~ +/- 100). Using 0.1 made the
    // collision volume a tiny square; start with 1.0 for now.
    // collision_mesh_set_transform(6.2f, 0.0f, roomY, 0.0f);
    // collision_mesh_init();

    scene_load_environment();
    
    g_boss = boss_spawn();
    if (!g_boss) {
        // Handle error
        return;
    }

    // Transform will be updated in boss_update()
    
    // Make character face the boss
    float dx = g_boss->pos[0] - character.pos[0];
    float dz = g_boss->pos[2] - character.pos[2];

    // Initialize character
    character_init();
    
    // // Set character initial position to be on the ground
    // character.pos[0] = 150.0f;
    // character.pos[1] = -4.8f;  // Position character feet on map surface
    // // Spawn inside the collision volume.
    // character.pos[2] = 0.0f;
    // character.rot[1] = -atan2f(dx, dz);
    // character_update_position();  // Update transform matrix with new rotation

    // Initialize dialog controller
    dialog_controller_init();

    //scene_init_cinematic_camera();
    // Start boss music
    // TODO: Its turned off for now as it gets annoying to listen to and it crackles
    // audio_play_music("rom:/audio/music/boss_final_phase.wav64", true);

    // Start boss intro cutscene after character and boss are loaded and positioned
    //dialog_controller_speak("^A powerful enemy approaches...~\n<Prepare for battle!", 0, 3.0f, false, true);

    // Initialize and show letterbox bars for intro
    letterbox_init();
    letterbox_show(false);  // Show immediately without animation

    collision_init();

    scene_title_init();
}

// Returns a consistent point around the boss' midsection for lock-on targeting.
// Prefers the midpoint of the boss capsule if it is configured; otherwise falls
// back to an estimate derived from the boss' orbit radius.
static T3DVec3 get_boss_lock_focus_point(void)
{
    if (!g_boss) {
        return (T3DVec3){{0.0f, 0.0f, 0.0f}};
    }
    
    // Default to a mid-body estimate even if the capsule data is uninitialized.
    float focusOffset = g_boss->orbitRadius * 0.6f; // roughly chest height for current tuning

    float capA = g_boss->capsuleCollider.localCapA.v[1];
    float capB = g_boss->capsuleCollider.localCapB.v[1];

    // // If a capsule is defined, use its midpoint (scaled to world space).
    // if (g_boss->scale[1] > 0.0f && (capA != 0.0f || capB != 0.0f)) {
    //     focusOffset = (capA + capB) * 0.5f;
    // }

    // Use point halfway between midpoint and capB (i.e. 75% from A -> B)
    if (capA != 0.0f || capB != 0.0f) {
        focusOffset = (capA + capB + capB + capB) * 0.25f;
    }

    //focusOffset = capB;

    return (T3DVec3){{
        g_boss->pos[0],
        g_boss->pos[1] + focusOffset,
        g_boss->pos[2]
    }};
}

void scene_reset(void)
{
    cutsceneState = CUTSCENE_PHASE1_INTRO;
    cutsceneTimer = 0.0f;
    cutsceneCameraTimer = 0.0f;
    bossActivated = false;
    gameState = GAME_STATE_TITLE;
    lastMenuActive = false;
    lastAPressed = false;
    lastZPressed = false;
    // Reset letterbox to show state for intro
    letterbox_show(false);
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
    gameState = state;
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
    debugf("RESTART: Starting restart sequence\n");
    
    // Reset ALL static variables first
    dialog_controller_reset();
    if (g_boss) {
        boss_reset(g_boss);
    }
    character_reset();
    scene_reset();
    
    // Clean up current scene objects
    character_delete();
    if (g_boss) {
        boss_free(g_boss);
        free(g_boss);
        g_boss = NULL;
    }
    
    // Reset camera 
    camera_reset();
    
    // Reinitialize everything by calling scene_init
    // This should set up everything correctly including camera and dialog
    scene_init();
    
    debugf("RESTART: After scene_init, cameraState = %d, speaking = %s\n", 
           cameraState, dialog_controller_speaking() ? "true" : "false");
}

void scene_init_playing(){
    character.pos[0] = -361.43f;
    character.pos[1] = 4.0f;
    character.pos[2] = 0.0f;

    character.scale[0] = MODEL_SCALE * 0.5f;
    character.scale[1] = MODEL_SCALE * 0.5f;
    character.scale[2] = MODEL_SCALE * 0.5f;

    character.rot[1] = 0.0f;

    character_update_position();

    // Skip dialog and cutscene
    dialog_controller_stop_speaking();
    cutsceneState = CUTSCENE_NONE;
    cutsceneCameraTimer = 0.0f;
    bossActivated = true;
    // Hide letterbox bars with animation
    letterbox_hide();
    // Return camera control to the player
    camera_mode_smooth(CAMERA_CHARACTER, 1.0f);
    cameraLockOnActive = true;

}

void scene_set_cinematic_camera(T3DVec3 posStart, T3DVec3 posEnd, T3DVec3 posTarget)
{
    cutsceneCamPosStart = posStart;
    cutsceneCamPosEnd = posEnd;

    camera_initialize(
        &cutsceneCamPosStart, 
        &(T3DVec3){{0,0,1}}, 
        1.544792654048f, 
        4.05f
    );

    customCamTarget = posTarget;  // Look at boss center/chest area
}

void scene_init_cutscene()
{
    switch (cutsceneState) 
    {
        case CUTSCENE_PHASE1_INTRO:
            scene_set_cinematic_camera((T3DVec3){{-700.0f, 120.4f, 0.0f}}, (T3DVec3){{-600.0f, 120.4f, 0.0f}}, (T3DVec3){{g_boss->pos[0], g_boss->pos[1] + 100.0f, g_boss->pos[2]}});

            camera_mode(CAMERA_CUSTOM);

            screenTransition = true;
            startScreenFade = true;
            gameState = GAME_STATE_PLAYING;
            character_reset();
            audio_stop_music();
            audio_play_music("rom:/audio/music/boss_phase1_cutscene1-22k.wav64", false);
            break;
        case CUTSCENE_PHASE1_CHAIN_CLOSEUP:
            screenTransition = false;
            scene_set_cinematic_camera((T3DVec3){{-239.0f, 239.4f, -133.7f}}, (T3DVec3){{-239.0f, 239.4f, -133.7f}}, (T3DVec3){{-151.9f, 208.0f, -96.0f}});
            break;
        case CUTSCENE_PHASE1_SWORDS_CLOSEUP:
            //scene_set_cinematic_camera((T3DVec3){{-197.867f, 20.0f, 191.45f}}, (T3DVec3){{-235.97f, 20.0f, 135.587f}}, (T3DVec3){{-161.14f, 43.29f, 84.56f}});
            scene_set_cinematic_camera((T3DVec3){{-197.86f, 20.0f, 191.45f}}, (T3DVec3){{-220.97f, 20.0f, 190.0f}}, (T3DVec3){{-142.32f, 55.14f, 114.76f}});
            break;
        case CUTSCENE_PHASE1_FILLER:
            cutsceneDialogActive = true;
            scene_set_cinematic_camera((T3DVec3){{-0.47f, 6.89f,  70.0f}}, (T3DVec3){{-0.15f, 22.99f, 32.26f}}, (T3DVec3){{0.476f, 55.54f, -71.29f}});
            dialog_controller_speak(phase1Dialogs[0], 0, 0.0f, false, false);
            break;
        case CUTSCENE_PHASE1_LOYALTY:
            cutsceneDialogActive = true;
            scene_set_cinematic_camera((T3DVec3){{-18.28f, 11.45f,  2.0f}}, (T3DVec3){{-18.28f, 11.45f,  -2.0f}}, (T3DVec3){{80.4f, -1.0f, -11.0f}});
            dialog_controller_speak(phase1Dialogs[1], 0, 0.0f, false, false);
            break;
        case CUTSCENE_PHASE1_FEAR:
            cutsceneDialogActive = true;
            scene_set_cinematic_camera((T3DVec3){{-13.454f, 13.41f,  -24.27f}}, (T3DVec3){{-13.454f, 25.41f,  -24.27f}}, (T3DVec3){{400.0f, 43.41f, -29.67f}});
            dialog_controller_speak(phase1Dialogs[2], 0, 0.0f, false, false);

            break;
        case CUTSCENE_PHASE1_BREAK_CHAINS:
            screenTransition = false;
            cutsceneDialogActive = false;
            scene_set_cinematic_camera((T3DVec3){{-22.31f, 1.7f, 0.65f}}, (T3DVec3){{-42.31f, 1.7f, 0.65f}}, (T3DVec3){{-12.31f, 1.7f, 0.65f}});
            break;
        case CUTSCENE_PHASE1_INTRO_END:
            T3DAnim** anims = (T3DAnim**)g_boss->animations;
            t3d_anim_set_playing(anims[BOSS_ANIM_KNEEL], true);
            g_boss->currentAnimation = BOSS_ANIM_KNEEL;
            g_boss->currentAnimState = BOSS_ANIM_KNEEL;

            currentCinematicChainsAnimation = 1;
            t3d_anim_set_looping(cinematicChainsAnimations[currentCinematicChainsAnimation], false); 
            t3d_anim_set_playing(cinematicChainsAnimations[currentCinematicChainsAnimation], true); 

            screenTransition = true;
            startScreenFade = true;
            cutsceneDialogActive = false;
            scene_set_cinematic_camera((T3DVec3){{-22.0f, 29.0f, -10.0f}}, (T3DVec3){{-150.0f, 29.0f, -10.0f}}, (T3DVec3){{100.0f, 29.0f, 0.0f}});
            break;
        default:
            break;
    }
}

static inline float ease_in_out(float x) {
    if (x < 0.0f) x = 0.0f;
    if (x > 1.0f) x = 1.0f;
    return x * x * (3.0f - 2.0f * x);
}

void scene_cutscene_update()
{
    // Update cutscene state
    cutsceneTimer += deltaTime;
    // Update camera timer separately (doesn't reset when transitioning states)
    if (cutsceneState != CUTSCENE_NONE) {
        cutsceneCameraTimer += deltaTime;
    }
    
    t3d_anim_update(cinematicChainsAnimations[currentCinematicChainsAnimation], deltaTime);
    t3d_skeleton_update(cinematicChainsSkeleton);

    if(cutsceneState == CUTSCENE_PHASE1_BREAK_CHAINS)
    {
        t3d_anim_update(cutsceneChainBreakAnimations[0], deltaTime);
        t3d_skeleton_update(cutsceneChainBreakSkeleton);
    }

    if (g_boss) {
        boss_anim_update(g_boss);
        // Update transforms for rendering
        T3DMat4FP* mat = (T3DMat4FP*)g_boss->modelMat;
        if (mat) {
            t3d_mat4fp_from_srt_euler(mat, g_boss->scale, g_boss->rot, g_boss->pos);
        }
    }

    switch (cutsceneState) {
        case CUTSCENE_PHASE1_INTRO: {

            //dialog_controller_update();
            
            // Slowly move camera towards boss during cutscene
            // Use a smooth interpolation over ~5 seconds

            float cameraMoveDuration = 9.0f;
            float t = cutsceneCameraTimer / cameraMoveDuration;
            if (t > 1.0f) t = 1.0f;  // Clamp to 1.0
            
            // Smooth interpolation (ease-in-out)
            float easeT = t * t * (3.0f - 2.0f * t);

            // Update camera position
            customCamPos.v[0] = cutsceneCamPosStart.v[0] + (cutsceneCamPosEnd.v[0] - cutsceneCamPosStart.v[0]) * easeT;
            customCamPos.v[1] = cutsceneCamPosStart.v[1] + (cutsceneCamPosEnd.v[1] - cutsceneCamPosStart.v[1]) * easeT;
            customCamPos.v[2] = cutsceneCamPosStart.v[2] + (cutsceneCamPosEnd.v[2] - cutsceneCamPosStart.v[2]) * easeT;
            
            // Wait for dialog to finish
            // if (!dialog_controller_speaking()) {
            //     cutsceneState = CUTSCENE_BOSS_INTRO_WAIT;
            //     cutsceneTimer = 0.0f;
            // }

            // End state of the segment
            if(cutsceneTimer >= 9.0f)
            {
                cutsceneTimer = 0.0f;
                cutsceneState = CUTSCENE_PHASE1_CHAIN_CLOSEUP;
                scene_init_cutscene();
                return;
            }

            // Allow skipping intro with A button
            if (btn.a) 
            {
                scene_init_playing();
                return;
            }
        } break;
        
        case CUTSCENE_PHASE1_CHAIN_CLOSEUP: {
            float cameraMoveDuration = 6.0f;
            float t = cutsceneCameraTimer / cameraMoveDuration;
            if (t > 1.0f) t = 1.0f;  // Clamp to 1.0
            
            // Smooth interpolation (ease-in-out)
            float easeT = t * t * (3.0f - 2.0f * t);

            // Update camera position
            customCamPos.v[0] = cutsceneCamPosStart.v[0] + (cutsceneCamPosEnd.v[0] - cutsceneCamPosStart.v[0]) * easeT;
            customCamPos.v[1] = cutsceneCamPosStart.v[1] + (cutsceneCamPosEnd.v[1] - cutsceneCamPosStart.v[1]) * easeT;
            customCamPos.v[2] = cutsceneCamPosStart.v[2] + (cutsceneCamPosEnd.v[2] - cutsceneCamPosStart.v[2]) * easeT;

            // End state of the segment
            if(cutsceneTimer >= 6.0f)
            {
                cutsceneTimer = 0.0f;
                cutsceneCameraTimer = 0.0f;
                cutsceneState = CUTSCENE_PHASE1_SWORDS_CLOSEUP;
                scene_init_cutscene();
                return;
            }

            if (btn.a) 
            {
                scene_init_playing();
                return;
            }
        } break;
        
        case CUTSCENE_PHASE1_SWORDS_CLOSEUP:  {

            float cameraMoveDuration = 4.0f;
            float t = cutsceneCameraTimer / cameraMoveDuration;
            if (t > 1.0f) t = 1.0f;  // Clamp to 1.0
            
            // Smooth interpolation (ease-in-out)
            float easeT = t * t * (3.0f - 2.0f * t);
            // Update camera position
            customCamPos.v[0] = cutsceneCamPosStart.v[0] + (cutsceneCamPosEnd.v[0] - cutsceneCamPosStart.v[0]) * easeT;
            customCamPos.v[1] = cutsceneCamPosStart.v[1] + (cutsceneCamPosEnd.v[1] - cutsceneCamPosStart.v[1]) * easeT;
            customCamPos.v[2] = cutsceneCamPosStart.v[2] + (cutsceneCamPosEnd.v[2] - cutsceneCamPosStart.v[2]) * easeT;

            // End state of the segment
            if(cutsceneTimer >= 5.0f)
            {
                cutsceneTimer = 0.0f;
                cutsceneCameraTimer = 0.0f;
                cutsceneState = CUTSCENE_PHASE1_FILLER;
                scene_init_cutscene();
                return;
            }

            if (btn.a) 
            {
                scene_init_playing();
                return;
            }
        } break;

        case CUTSCENE_PHASE1_FILLER:  {
            float cameraMoveDuration = 13.0f;
            float t = cutsceneCameraTimer / cameraMoveDuration;
            if (t > 1.0f) t = 1.0f;  // Clamp to 1.0
            
            // Smooth interpolation (ease-in-out)
            float easeT = t * t * (3.0f - 2.0f * t);
            // Update camera position
            customCamPos.v[0] = cutsceneCamPosStart.v[0] + (cutsceneCamPosEnd.v[0] - cutsceneCamPosStart.v[0]) * easeT;
            customCamPos.v[1] = cutsceneCamPosStart.v[1] + (cutsceneCamPosEnd.v[1] - cutsceneCamPosStart.v[1]) * easeT;
            customCamPos.v[2] = cutsceneCamPosStart.v[2] + (cutsceneCamPosEnd.v[2] - cutsceneCamPosStart.v[2]) * easeT;

            dialog_controller_update();

            // End state of the segment
            if(cutsceneTimer >= 10.0f)
            {
                cutsceneTimer = 0.0f;
                cutsceneCameraTimer = 0.0f;
                cutsceneState = CUTSCENE_PHASE1_LOYALTY;
                scene_init_cutscene();
                return;
            }

            if (btn.a) 
            {
                scene_init_playing();
                return;
            }
        } break;

        case CUTSCENE_PHASE1_LOYALTY:  {

            float cameraMoveDuration = 5.0f;
            float t = cutsceneCameraTimer / cameraMoveDuration;
            if (t > 1.0f) t = 1.0f;  // Clamp to 1.0
            
            // Smooth interpolation (ease-in-out)
            float easeT = t * t * (3.0f - 2.0f * t);
            // Update camera position
            customCamPos.v[0] = cutsceneCamPosStart.v[0] + (cutsceneCamPosEnd.v[0] - cutsceneCamPosStart.v[0]) * easeT;
            customCamPos.v[1] = cutsceneCamPosStart.v[1] + (cutsceneCamPosEnd.v[1] - cutsceneCamPosStart.v[1]) * easeT;
            customCamPos.v[2] = cutsceneCamPosStart.v[2] + (cutsceneCamPosEnd.v[2] - cutsceneCamPosStart.v[2]) * easeT;

            dialog_controller_update();

            // End state of the segment
            if(cutsceneTimer >= 5.0f)
            {
                cutsceneTimer = 0.0f;
                cutsceneCameraTimer = 0.0f;
                cutsceneState = CUTSCENE_PHASE1_FEAR;
                scene_init_cutscene();
                return;
            }

            if (btn.a) 
            {
                scene_init_playing();
                return;
            }
        } break;

        case CUTSCENE_PHASE1_FEAR:  {

            float cameraMoveDuration = 7.5f;
            float t = cutsceneCameraTimer / cameraMoveDuration;
            if (t > 1.0f) t = 1.0f;  // Clamp to 1.0
            
            // Smooth interpolation (ease-in-out)
            float easeT = t * t * (3.0f - 2.0f * t);
            // Update camera position
            customCamPos.v[0] = cutsceneCamPosStart.v[0] + (cutsceneCamPosEnd.v[0] - cutsceneCamPosStart.v[0]) * easeT;
            customCamPos.v[1] = cutsceneCamPosStart.v[1] + (cutsceneCamPosEnd.v[1] - cutsceneCamPosStart.v[1]) * easeT;
            customCamPos.v[2] = cutsceneCamPosStart.v[2] + (cutsceneCamPosEnd.v[2] - cutsceneCamPosStart.v[2]) * easeT;

            dialog_controller_update();

            // Play grab sword anim
            if(cutsceneTimer >= 6.5f && g_boss->currentAnimation != BOSS_ANIM_KNEEL_CUTSCENE)
            {
                T3DAnim** anims = (T3DAnim**)g_boss->animations;
                t3d_anim_set_playing(anims[BOSS_ANIM_KNEEL_CUTSCENE], true);
                g_boss->currentAnimation = BOSS_ANIM_KNEEL_CUTSCENE;
                g_boss->currentAnimState = BOSS_ANIM_KNEEL_CUTSCENE;
            }

            if(cutsceneTimer >= 7.5f)
            {
                if(!screenTransition)
                {
                    screenTransition = true;
                    startScreenFade = true;
                }
            }

            // End state of the segment
            if(cutsceneTimer >= 10.5f)
            {
                cutsceneTimer = 0.0f;
                cutsceneCameraTimer = 0.0f;
                cutsceneState = CUTSCENE_PHASE1_BREAK_CHAINS;
                scene_init_cutscene();
                return;
            }

            if (btn.a) 
            {
                scene_init_playing();
                return;
            }
        } break;
        case CUTSCENE_PHASE1_BREAK_CHAINS:  {

            float cameraMoveDuration = 5.0f;
            float t = cutsceneCameraTimer / cameraMoveDuration;
            if (t > 1.0f) t = 1.0f;  // Clamp to 1.0
            
            // Smooth interpolation (ease-in-out)
            float easeT = t * t * (3.0f - 2.0f * t);
            // Update camera position
            customCamPos.v[0] = cutsceneCamPosStart.v[0] + (cutsceneCamPosEnd.v[0] - cutsceneCamPosStart.v[0]) * easeT;
            customCamPos.v[1] = cutsceneCamPosStart.v[1] + (cutsceneCamPosEnd.v[1] - cutsceneCamPosStart.v[1]) * easeT;
            customCamPos.v[2] = cutsceneCamPosStart.v[2] + (cutsceneCamPosEnd.v[2] - cutsceneCamPosStart.v[2]) * easeT;

            //dialog_controller_update();


            if(cutsceneTimer >= 3.0f)
            {
                if(!screenTransition)
                {
                    screenTransition = true;
                    startScreenFade = true;
                }
            }

            // End state of the segment
            if(cutsceneTimer >= 5.0f)
            {
                cutsceneTimer = 0.0f;
                cutsceneCameraTimer = 0.0f;
                cutsceneState = CUTSCENE_PHASE1_INTRO_END;
                scene_init_cutscene();
                return;
            }

            if (btn.a) 
            {
                scene_init_playing();
                return;
            }
        } break;
        // Wait for boss to be activated before moving to the next state
        case CUTSCENE_PHASE1_INTRO_END:{
            float cameraMoveDuration = 10.0f;
            float t = cutsceneCameraTimer / cameraMoveDuration;
            if (t > 1.0f) t = 1.0f;  // Clamp to 1.0
            
            // Smooth interpolation (ease-in-out)
            float easeT = t * t * (3.0f - 2.0f * t);
            // Update camera position
            customCamPos.v[0] = cutsceneCamPosStart.v[0] + (cutsceneCamPosEnd.v[0] - cutsceneCamPosStart.v[0]) * easeT;
            customCamPos.v[1] = cutsceneCamPosStart.v[1] + (cutsceneCamPosEnd.v[1] - cutsceneCamPosStart.v[1]) * easeT;
            customCamPos.v[2] = cutsceneCamPosStart.v[2] + (cutsceneCamPosEnd.v[2] - cutsceneCamPosStart.v[2]) * easeT;

            //dialog_controller_update();

            // End state of the segment
            if(cutsceneTimer >= 10.0f)
            {
                cutsceneTimer = 0.0f;
                cutsceneCameraTimer = 0.0f;
                scene_init_playing();
                return;
            }


        } break;
        default:
            break;
    }

    if (btn.a) 
    {
        cutsceneTimer = 0.0f;
        cutsceneCameraTimer = 0.0f;
        scene_init_playing();
        return;
    }
}

void scene_update_title(void)
{
    if(gameState == GAME_STATE_TITLE_TRANSITION)
    {
        if(titleStartGameTimer >= titleStartGameTime){
            audio_stop_sfx(0);
            scene_init_cutscene();
            titleStartGameTimer = 0.0f;
        }
        else
        {
            if(btn.start || btn.b)
            {
                scene_init_cutscene();
                titleStartGameTimer = 0.0f;
                audio_stop_sfx(0);
                return;
            }

            audio_update_fade(deltaTime);
            titleStartGameTimer += deltaTime;

            float forwardSpeed = 15.0f;
            float targetDropSpeed = 1.0f;

            // compute forward dir
            customCamDir.v[0] = customCamTarget.v[0] - customCamPos.v[0];
            customCamDir.v[1] = customCamTarget.v[1] - customCamPos.v[1];
            customCamDir.v[2] = customCamTarget.v[2] - customCamPos.v[2];
            t3d_vec3_norm(&customCamDir);

            // move forward
            for (int i = 0; i < 3; i++) {
                customCamPos.v[i]    += customCamDir.v[i] * forwardSpeed * deltaTime;
                customCamTarget.v[i] += customCamDir.v[i] * forwardSpeed * deltaTime;
            }

            // gently lower target
            customCamTarget.v[1] -= targetDropSpeed * deltaTime;
        }
        return;
    }

    if(btn.start || btn.a)
    {
        gameState = GAME_STATE_TITLE_TRANSITION;

        camera_breath_active(false); 
        screenBreath = false; 
        audio_stop_music_fade(6); // duration
        audio_play_sfx(&characterTitleSfx, 0, 10);
        return;
    }

    if (!screenBreath) 
    { 
        camera_breath_active(true); 
        screenBreath = true; 
    }

    camera_breath_update(deltaTime);

    if(titleTextActivationTimer >= titleTextActivationTime){
        dialog_controller_update();
        if(!dialog_controller_speaking())
        {
            currentTitleDialog ++;
            if(currentTitleDialog >= TITLE_DIALOG_COUNT)
            {
                titleTextActivationTimer = 0;
                currentTitleDialog = -1;
            }
            else
            {
                dialog_controller_speak(titleDialogs[currentTitleDialog], 0, 9.0f, false, true);
            }
        }
    }
    else
    {
        titleTextActivationTimer += deltaTime;
    }
}

void scene_update(void) 
{
    // Update all scrolling textures
    scroll_update();

    if(gameState == GAME_STATE_TITLE || gameState == GAME_STATE_TITLE_TRANSITION)
    {
        scene_update_title();
        character_update();
        t3d_anim_update(dynamicBannerAnimations[0], deltaTime);
        t3d_skeleton_update(dynamicBannerSkeleton);
        return;
    }

    // Check if menu was just closed - if so, reset character button state
    bool menuActive = scene_is_menu_active();
    if (lastMenuActive && !menuActive) {
        // Menu was just closed - reset character button state to prevent false "just pressed"
        character_reset_button_state();
    }
    lastMenuActive = menuActive;

    // If player is dead or victorious, wait for restart input and halt gameplay updates
    if (gameState == GAME_STATE_DEAD || gameState == GAME_STATE_VICTORY) {

        if (btn.a) {
            scene_restart();
        }
        return;
    }
    
    // Don't update game logic when menu is active
    if (scene_is_menu_active()) {
        return;
    }

    if(cutsceneState == CUTSCENE_NONE) // Normal gameplay
    {
        
        character_update();
        if (bossActivated && g_boss) {
            boss_update(g_boss);
        }

        collision_update();


        //dialog_controller_update();
    }
    else // Cutscene
    {
        scene_cutscene_update();
    }
    
    // Update letterbox animation
    letterbox_update();

    // Z-target toggle: press Z to toggle lock-on, target updates with boss movement when active
    bool zPressed = btn.z;
    
    if (zPressed && !lastZPressed)  // Z button just pressed (rising edge)
    {
        if (!cameraLockOnActive)
        {
            cameraLockOnActive = true;
        }
        else
        {
            cameraLockOnActive = false;
        }
    }
    lastZPressed = zPressed;
    
    // Update target position when lock-on is active
    if (cameraLockOnActive)
    {
        cameraLockOnTarget = get_boss_lock_focus_point();
    }
}

void scene_fixed_update(void) 
{
}

// Draws a small lock-on marker over the boss when Z-targeting is active.
static void draw_lockon_indicator(T3DViewport *viewport)
{
    // Only show during gameplay when the boss is alive and actually targeted
    if (!cameraLockOnActive || scene_is_cutscene_active() || !scene_is_boss_active() || !g_boss || g_boss->health <= 0.0f) {
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

    // Simple white dot
    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_set_prim_color(RGBA32(255, 255, 255, 255));

    const int halfSize = 3;
    rdpq_fill_rectangle(px - halfSize, py - halfSize, px + halfSize + 1, py + halfSize + 1);
}

void scene_draw_title(T3DViewport *viewport)
{
    // ===== DRAW 3D =====
    rdpq_sync_pipe();
    rdpq_mode_zbuf(false, false);

    // Draw no depth environment first
    t3d_matrix_push_pos(1);
        t3d_matrix_set(mapMatrix, true);
        rspq_block_run(mapDpl);

        t3d_matrix_set(dynamicBannerMatrix, true);
        rspq_block_run(dynamicBannerDpl);
    t3d_matrix_pop(1);

        // Draw depth environment
    rdpq_sync_pipe();
    rdpq_mode_zbuf(true, true);

    t3d_matrix_push_pos(1);
        character_draw();

        t3d_matrix_set(fogDoorMatrix, true);
        // Create a struct to pass the scrolling parameters to the tile callback
        t3d_model_draw_custom(fogDoorModel, (T3DModelDrawConf){
            .userData = &fogScrollParams,
            .tileCb = tile_scroll,
        });
    t3d_matrix_pop(1);

    // ======== Draw 2D ======== //
    rdpq_sync_pipe();
    //Title text
    if(gameState != GAME_STATE_TITLE_TRANSITION)
    {
        rdpq_set_mode_standard();
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(RGBA32(0,0,0,120));
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_fill_rectangle(15, 35, 75, 58);

        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
        rdpq_text_printf(NULL, FONT_UNBALANCED, 35, 50, "Play");

        if(titleTextActivationTimer >= titleTextActivationTime)
        {
            dialog_controller_draw(true, 190, 20, 120, 180);
        }

        display_utility_solid_black_transition(true, 200.0f);

    }
    else
    {
        if (titleStartGameTimer >= titleFadeTime && !screenTransition)
        {
            startScreenFade = true; // this is set in the display utility. Must not update this value.
            screenTransition = true; // this is to toggle off setting the display utility.
        }

        if(screenTransition)
        {
            display_utility_solid_black_transition(false, 200.0f);
        }
    }
}

void scene_draw_cutscene_fog(){
    switch(cutsceneState){
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
            //t3d_fog_set_range(450.0f, 800.0f);
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

void scene_draw_cutscene(){

    switch(cutsceneState){
        case CUTSCENE_PHASE1_INTRO:
            rdpq_sync_pipe();
            rdpq_mode_zbuf(false, false);

            // Draw no depth environment first
            t3d_matrix_push_pos(1);
                // t3d_matrix_set(windowsMatrix, true);
                // rspq_block_run(windowsDpl);

                t3d_matrix_set(mapMatrix, true);
                rspq_block_run(mapDpl);


                t3d_matrix_set(roomFloorMatrix, true);
                rspq_block_run(roomFloorDpl);

                t3d_matrix_set(roomLedgeMatrix, true);
                rspq_block_run(roomLedgeDpl);

                if (g_boss) {
                    boss_draw(g_boss);
                }
                t3d_matrix_set(pillarsMatrix, true);
                rspq_block_run(pillarsDpl);

            t3d_matrix_pop(1);
    
            t3d_matrix_push_pos(1);   
                //Draw transparencies last
                t3d_matrix_set(sunshaftsMatrix, true);
                rspq_block_run(sunshaftsDpl);

                t3d_matrix_set(cinematicChainsMatrix, true);
                rspq_block_run(cinematicChainsDpl);

                t3d_matrix_set(chainsMatrix, true);
                rspq_block_run(chainsDpl);

                t3d_matrix_set(pillarsFrontMatrix, true);
                rspq_block_run(pillarsFrontDpl);

            t3d_matrix_pop(1); 

            //==== Draw 2D ====
            rdpq_sync_pipe();
            if(screenTransition)
            {
                display_utility_solid_black_transition(true, 100.0f);
            }
            break;
        case CUTSCENE_PHASE1_CHAIN_CLOSEUP:
            rdpq_sync_pipe();
            rdpq_mode_zbuf(false, false);

            // Draw no depth environment first
            t3d_matrix_push_pos(1);

                t3d_matrix_set(mapMatrix, true);
                rspq_block_run(mapDpl);


                t3d_matrix_set(roomFloorMatrix, true);
                rspq_block_run(roomFloorDpl);

                t3d_matrix_set(pillarsMatrix, true);
                rspq_block_run(pillarsDpl);

                if (g_boss) {
                    boss_draw(g_boss);
                }
            t3d_matrix_pop(1);

            t3d_matrix_push_pos(1);   
                //Draw transparencies last
                t3d_matrix_set(sunshaftsMatrix, true);
                rspq_block_run(sunshaftsDpl);

                t3d_matrix_set(cinematicChainsMatrix, true);
                rspq_block_run(cinematicChainsDpl);
            t3d_matrix_pop(1); 

            break;
        case CUTSCENE_PHASE1_SWORDS_CLOSEUP:
            rdpq_sync_pipe();
            rdpq_mode_zbuf(false, false);

            // Draw no depth environment first
            t3d_matrix_push_pos(1);
                t3d_matrix_set(windowsMatrix, true);
                rspq_block_run(windowsDpl);

                t3d_matrix_set(mapMatrix, true);
                rspq_block_run(mapDpl);

                t3d_matrix_set(pillarsMatrix, true);
                rspq_block_run(pillarsDpl);

            t3d_matrix_pop(1);

            rdpq_sync_pipe();
            rdpq_mode_zbuf(true, true);

            t3d_matrix_push_pos(1);
                t3d_matrix_set(roomFloorMatrix, true);
                rspq_block_run(roomFloorDpl);

                if (g_boss) {
                    boss_draw(g_boss);
                }
            t3d_matrix_pop(1);

            rdpq_sync_pipe();
            rdpq_mode_zbuf(false, false);

            t3d_matrix_push_pos(1);   

                t3d_matrix_set(chainsMatrix, true);
                rspq_block_run(chainsDpl);
                //Draw transparencies last
                t3d_matrix_set(sunshaftsMatrix, true);
                rspq_block_run(sunshaftsDpl);

                t3d_matrix_set(cinematicChainsMatrix, true);
                rspq_block_run(cinematicChainsDpl);
            t3d_matrix_pop(1); 
            break;
        case CUTSCENE_PHASE1_FILLER: {
            rdpq_sync_pipe();
            rdpq_mode_zbuf(false, false);

            // Draw no depth environment first
            t3d_matrix_push_pos(1);
                // t3d_fog_set_range(450.0f, 800.0f);
                // t3d_matrix_set(windowsMatrix, true);
                // rspq_block_run(windowsDpl);
                // //t3d_fog_set_range(30.0f, 50.0f);
                // t3d_matrix_set(mapMatrix, true);
                // rspq_block_run(mapDpl);

                t3d_matrix_set(cinematicChainsMatrix, true);
                rspq_block_run(cinematicChainsDpl);

                t3d_matrix_set(sunshaftsMatrix, true);
                rspq_block_run(sunshaftsDpl);
            t3d_matrix_pop(1);

            rdpq_sync_pipe();
            rdpq_mode_zbuf(true, true);

            // Draw no depth environment first
            t3d_matrix_push_pos(1);
                t3d_matrix_set(roomFloorMatrix, true);
                rspq_block_run(roomFloorDpl);

                if (g_boss) {
                    boss_draw(g_boss);
                }
            t3d_matrix_pop(1); 

            // 2D

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

        } break;
        case CUTSCENE_PHASE1_LOYALTY: {
            rdpq_sync_pipe();
            rdpq_mode_zbuf(true, true);

            t3d_matrix_push_pos(1);
                t3d_matrix_set(roomFloorMatrix, true);
                rspq_block_run(roomFloorDpl);
                if (g_boss) {
                    boss_draw(g_boss);
                }
            t3d_matrix_pop(1);

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
        } break;
        case CUTSCENE_PHASE1_FEAR:{
            rdpq_sync_pipe();
            rdpq_mode_zbuf(false, false);

            // Draw no depth environment first
            t3d_matrix_push_pos(1);
                t3d_matrix_set(roomFloorMatrix, true);
                rspq_block_run(roomFloorDpl);
            t3d_matrix_pop(1);

            rdpq_sync_pipe();
            rdpq_mode_zbuf(true, true);

            t3d_matrix_push_pos(1);
                if (g_boss) {
                    boss_draw(g_boss);
                }
            t3d_matrix_pop(1);

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

            rdpq_sync_pipe();
            if(screenTransition)
            {
                display_utility_solid_black_transition(false, 200.0f);
            }

        } break;

        case CUTSCENE_PHASE1_BREAK_CHAINS:{
            rdpq_sync_pipe();
            rdpq_mode_zbuf(false, false);
            
            // Draw no depth environment first
            t3d_matrix_push_pos(1);
                t3d_matrix_set(cutsceneChainBreakMatrix, true);
                rspq_block_run(cutsceneChainBreakDpl);
            t3d_matrix_pop(1);

            rdpq_sync_pipe(); // idk if it's needed but there was a crash here
            if(screenTransition)
            {
                display_utility_solid_black_transition(false, 200.0f);
            }

        } break;

        case CUTSCENE_PHASE1_INTRO_END:
            rdpq_sync_pipe();
            rdpq_mode_zbuf(false, false);

            // Draw no depth environment first
            t3d_matrix_push_pos(1);
                // t3d_matrix_set(windowsMatrix, true);
                // rspq_block_run(windowsDpl);

                t3d_matrix_set(mapMatrix, true);
                rspq_block_run(mapDpl);

                t3d_matrix_set(roomLedgeMatrix, true);
                rspq_block_run(roomLedgeDpl);

                t3d_matrix_set(pillarsMatrix, true);
                rspq_block_run(pillarsDpl);

            t3d_matrix_pop(1);
    
            rdpq_sync_pipe();
            rdpq_mode_zbuf(true, true);

            t3d_matrix_push_pos(1);
                t3d_matrix_set(roomFloorMatrix, true);
                rspq_block_run(roomFloorDpl);
                if (g_boss) {
                    boss_draw(g_boss);
                }

                t3d_matrix_set(cinematicChainsMatrix, true);
                rspq_block_run(cinematicChainsDpl);
            t3d_matrix_pop(1);

            rdpq_sync_pipe();
            rdpq_mode_zbuf(false, false);

            t3d_matrix_push_pos(1);   
                //Draw transparencies last
                // t3d_matrix_set(sunshaftsMatrix, true);
                // rspq_block_run(sunshaftsDpl);

                t3d_matrix_set(chainsMatrix, true);
                rspq_block_run(chainsDpl);
            t3d_matrix_pop(1); 

            //==== Draw 2D ====
            rdpq_sync_pipe();

            rdpq_set_mode_standard();
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_set_prim_color(RGBA32(0,0,0,120));
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            
            rdpq_fill_rectangle(25, 35, 220, 58);

            rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
            rdpq_text_printf(NULL, FONT_UNBALANCED, 35, 50, g_boss->name);

            if(screenTransition)
            {
                display_utility_solid_black_transition(true, 100.0f);
            }
            break;

        default:
            break;
    }
}

void scene_draw(T3DViewport *viewport) 
{

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

    if(cutsceneState != CUTSCENE_NONE){
        scene_draw_cutscene_fog();
    }else{
        t3d_fog_set_range(450.0f, 800.0f);
    }
    t3d_fog_set_enabled(true);

    t3d_screen_clear_color(RGBA32(0, 0, 0, 0xFF));
    t3d_screen_clear_depth();

    // Lighting
    t3d_light_set_ambient(colorAmbient);
    // T3DVec3 negCamDir = {{-camDir.x, -camDir.y, -camDir.z}};
    // t3d_light_set_directional(0, (uint8_t[4]){0x00, 0x00, 0x00, 0xFF}, &negCamDir);
    // t3d_light_set_count(1);

    if(gameState == GAME_STATE_TITLE || gameState == GAME_STATE_TITLE_TRANSITION)
    {
        scene_draw_title(viewport);
        return;
    }

    if(cutsceneState != CUTSCENE_NONE)
    {
        scene_draw_cutscene();
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
    
    // Draw depth environment
    rdpq_sync_pipe();
    rdpq_mode_zbuf(true, true);

    t3d_matrix_push_pos(1);   

        t3d_matrix_set(roomFloorMatrix, true);
        rspq_block_run(roomFloorDpl);

        t3d_matrix_set(roomLedgeMatrix, true);
        rspq_block_run(roomLedgeDpl);

        t3d_matrix_set(pillarsMatrix, true);
        rspq_block_run(pillarsDpl);

        t3d_matrix_set(pillarsFrontMatrix, true);
        rspq_block_run(pillarsFrontDpl);

    t3d_matrix_pop(1); 

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

    //Draw transparencies last
    // t3d_matrix_push_pos(1);    
    //     t3d_matrix_set(sunshaftsMatrix, true);
    //     rspq_block_run(sunshaftsDpl);
    // t3d_matrix_pop(1);

    // rdpq_sync_pipe();
    // rdpq_mode_zbuf(false, false);

    t3d_matrix_push_pos(1);   
        t3d_matrix_set(cinematicChainsMatrix, true);
        rspq_block_run(cinematicChainsDpl);
        t3d_matrix_set(chainsMatrix, true);
        rspq_block_run(chainsDpl);
    t3d_matrix_pop(1); 
    // ===== DRAW 2D =====

    // Overlay lock-on marker above the boss
    if(DEV_MODE)
        draw_lockon_indicator(viewport);
    
    bool cutsceneActive = scene_is_cutscene_active();
    GameState state = scene_get_game_state();
    bool isDead = state == GAME_STATE_DEAD;
    bool isVictory = state == GAME_STATE_VICTORY;
    bool isEndScreen = isDead || isVictory;

    // Draw letterbox bars (they handle their own visibility and animation)
    letterbox_draw();

    // Draw UI elements after 3D rendering is complete (hide during cutscenes or death)
    if (!cutsceneActive && !isEndScreen) {
        if (scene_is_boss_active() && g_boss) {
            boss_draw_ui(g_boss, viewport);
        }
        character_draw_ui();
    }
    
    if (isEndScreen) {
        // Full-screen overlay with prompt to restart
        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        rdpq_set_prim_color(RGBA32(0, 0, 0, 160));
        rdpq_fill_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
        
        const char* header = isVictory ? "Victory!" : "You Died";
        const char* prompt = "Press A to restart";
        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_CENTER,
            .width = SCREEN_WIDTH,
        }, FONT_UNBALANCED, 0, SCREEN_HEIGHT / 2 - 12, "%s", header);
        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_CENTER,
            .width = SCREEN_WIDTH,
        }, FONT_UNBALANCED, 0, SCREEN_HEIGHT / 2 + 4, "%s", prompt);
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

}

void scene_delete_environment(void)
{
    // --- DPLs ---
    if (mapDpl)        { rspq_block_free(mapDpl);        mapDpl = NULL; }
    if (pillarsDpl)    { rspq_block_free(pillarsDpl);    pillarsDpl = NULL; }
    if (roomLedgeDpl)  { rspq_block_free(roomLedgeDpl);  roomLedgeDpl = NULL; }
    if (windowsDpl)    { rspq_block_free(windowsDpl);    windowsDpl = NULL; }
    if (chainsDpl)     { rspq_block_free(chainsDpl);     chainsDpl = NULL; }
    if (sunshaftsDpl)  { rspq_block_free(sunshaftsDpl);  sunshaftsDpl = NULL; }
    if (fogDoorDpl)    { rspq_block_free(fogDoorDpl);    fogDoorDpl = NULL; }
    if (roomFloorDpl)  { rspq_block_free(roomFloorDpl);  roomFloorDpl = NULL; }

    // --- Models ---
    if (mapModel)       { t3d_model_free(mapModel);       mapModel = NULL; }
    if (pillarsModel)   { t3d_model_free(pillarsModel);   pillarsModel = NULL; }
    if (roomLedgeModel) { t3d_model_free(roomLedgeModel); roomLedgeModel = NULL; }
    if (windowsModel)   { t3d_model_free(windowsModel);   windowsModel = NULL; }
    if (chainsModel)    { t3d_model_free(chainsModel);    chainsModel = NULL; }
    if (sunshaftsModel) { t3d_model_free(sunshaftsModel); sunshaftsModel = NULL; }
    if (fogDoorModel)   { t3d_model_free(fogDoorModel);   fogDoorModel = NULL; }
    if (roomFloorModel) { t3d_model_free(roomFloorModel); roomFloorModel = NULL; }

    // --- Matrices (malloc_uncached) ---
    if (mapMatrix)       { free_uncached(mapMatrix);       mapMatrix = NULL; }
    if (pillarsMatrix)   { free_uncached(pillarsMatrix);   pillarsMatrix = NULL; }
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
    camera_reset();
    
    character_delete();
    if (g_boss) {
        boss_free(g_boss);
        free(g_boss);
        g_boss = NULL;
    }

    dialog_controller_free();
}