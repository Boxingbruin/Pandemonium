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
//#include "collision_mesh.h"
#include "collision_system.h"
#include "letterbox_utility.h"

// TODO: This should not be declared in the header file, as it is only used externally (temp)
#include "dev.h"
#include "debug_draw.h"

// Dynamic chains
#define NUM_DYNAMIC_CHAINS 2
#define DYNAMIC_CHAIN_ANIM_COUNT 1

T3DModel* mapModel;
rspq_block_t* mapDpl;
T3DMat4FP* mapMatrix;

T3DModel* sunshaftsModel;
rspq_block_t* sunshaftsDpl;
T3DMat4FP* sunshaftsMatrix;

T3DModel* pillarsModel;
rspq_block_t* pillarsDpl;
T3DMat4FP* pillarsMatrix;

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

static int currentTitleDialog = 0;
static float titleTextActivationTimer = 0.0f;
static float titleTextActivationTime = 50.0f;
static const char *titleDialogs[] = {
    ">The Demon\nking cast\nthe land\ninto a\ncentury long\ndarkness.",
    ">He's trained a legion\nof powerful\nknights\nsworn to\nprotect the\nthrone.",
    ">These\nbattle born\nknights are\ntaken from\ntheir\nfamilies and\ncast into\nservitude.",
    ">Enduring\nblade and\ntorment\nuntil nothing\nremains but\nhollow armor."
};
#define TITLE_DIALOG_COUNT (sizeof(titleDialogs) / sizeof(titleDialogs[0]))

const char* DYNAMIC_CHAIN_ANIMS[NUM_DYNAMIC_CHAINS][DYNAMIC_CHAIN_ANIM_COUNT] = {
    { "Chain1Initial" },  // chain 0, anim 0
    { "Chain2Initial" },  // chain 1, anim 0
};

T3DModel* dynamicChainModel; 
typedef struct {
    T3DModel*      model;
    rspq_block_t*  dpl;
    T3DMat4FP*     matrix;
    T3DSkeleton*   skeleton;
    T3DAnim**      anims;
} DynamicChain;
DynamicChain gDynamicChains[NUM_DYNAMIC_CHAINS];

// Cutscene state management
typedef enum {
    CUTSCENE_NONE,
    CUTSCENE_BOSS_INTRO,
    CUTSCENE_BOSS_INTRO_WAIT
} CutsceneState;

static CutsceneState cutsceneState = CUTSCENE_BOSS_INTRO;
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

void scene_load_dynamic_chains(void)
{
    dynamicChainModel = t3d_model_load("rom:/boss_room/dynamic_chain.t3dm");
    for (int i = 0; i < NUM_DYNAMIC_CHAINS; i++) {
        DynamicChain* chain = &gDynamicChains[i];

        chain->skeleton = malloc_uncached(sizeof(T3DSkeleton));
        *chain->skeleton = t3d_skeleton_create(dynamicChainModel);

        chain->anims = malloc_uncached(DYNAMIC_CHAIN_ANIM_COUNT * sizeof(T3DAnim*));

        for (int a = 0; a < DYNAMIC_CHAIN_ANIM_COUNT; a++) {
            const char* animName = DYNAMIC_CHAIN_ANIMS[i][a];

            chain->anims[a] = malloc_uncached(sizeof(T3DAnim));
            *chain->anims[a] = t3d_anim_create(dynamicChainModel, animName);

            t3d_anim_set_looping(chain->anims[a], true);
            t3d_anim_set_playing(chain->anims[a], true);
            t3d_anim_attach(chain->anims[a], chain->skeleton);
        }

        rspq_block_begin();
            t3d_model_draw_skinned(dynamicChainModel, chain->skeleton);
        chain->dpl = rspq_block_end();

        chain->matrix = malloc_uncached(sizeof(T3DMat4FP));

        float scale[3] = { MODEL_SCALE, MODEL_SCALE, MODEL_SCALE };
        float rot[3]   = { 0.0f, 0.0f, 0.0f };
        float pos[3]   = { 0.0f, -5.0f, 0.0f };

        t3d_mat4fp_from_srt_euler(chain->matrix, scale, rot, pos);
    }
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
        (float[3]){0.0f, -5.0f, 0.0f}    // ground level position
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
        (float[3]){0.0f, -5.0f, 0.0f}
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
        (float[3]){0.0f, -5.0f, 0.0f}
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
        (float[3]){0.0f, -5.0f, 0.0f}
    );

    // ===== LOAD CHAINS =====
    chainsModel = t3d_model_load("rom:/boss_room/chains.t3dm");
    rspq_block_begin();
    t3d_model_draw(chainsModel);
    chainsDpl = rspq_block_end();
    
    chainsMatrix = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_from_srt_euler(chainsMatrix, 
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, -5.0f, 0.0f}
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
        (float[3]){0.0f, -5.0f, 0.0f}
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
        (float[3]){0.0f, -5.0f, 0.0f}
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
        (float[3]){0.0f, -5.0f, 0.0f}
    );

    // ===== LOAD DYNAMIC CHAINS =====
    //scene_load_dynamic_chains();
    
}

void scene_init_cinematic_camera()
{
    // Set up camera to focus on boss for intro cutscene
    // Start position: further back for dramatic reveal
    if (g_boss) {
        cutsceneCamPosStart = (T3DVec3){{g_boss->pos[0]+0.0f, g_boss->pos[1] + 250.0f, g_boss->pos[2] + 250.0f}};
        // End position: closer to boss (slowly move towards during cutscene)
        cutsceneCamPosEnd = (T3DVec3){{g_boss->pos[0]+700.0f, g_boss->pos[1] + 50.0f, g_boss->pos[2] + 0.0f}};
        camera_initialize(
            &cutsceneCamPosStart, 
            &(T3DVec3){{0,0,1}}, 
            1.544792654048f, 
            4.05f
        );
        customCamTarget = (T3DVec3){{g_boss->pos[0], g_boss->pos[1] + 15.0f, g_boss->pos[2]}};  // Look at boss center/chest area
    }
    camera_mode(CAMERA_CUSTOM);

    character.pos[0] = 0.0f;
    character.pos[1] = 0.0f;
    character.pos[2] = 0.0f;

    character.scale[0] = MODEL_SCALE * 0.5f;
    character.scale[1] = MODEL_SCALE * 0.5f;
    character.scale[2] = MODEL_SCALE * 0.5f;

    character.rot[1] = 0.0f;

    character_update_position();
}

void scene_title_init()
{
    audio_play_music("rom:/audio/music/_DEMONOUS.wav64", true);

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
    t3d_mat4fp_from_srt_euler(dynamicBannerMatrix, (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE}, (float[3]){0.0f, 0.0f, 0.0f}, (float[3]){0.0f, -5.0f, 0.0f} );

    dialog_controller_speak(titleDialogs[0], 0, 10.0f, false, true);
}

void scene_init(void) 
{
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
    // collision_mesh_set_transform(6.2f, 0.0f, -5.0f, 0.0f);
    // collision_mesh_init();

    scene_load_environment();
    
    // Initialize boss model (imported from boss.glb -> boss.t3dm)
    g_boss = boss_spawn();
    if (!g_boss) {
        // Handle error
        return;
    }

    // Place boss at a dramatic distance from the character for the intro
    g_boss->pos[0] = -452.0f;  // Dramatic but visible distance from character
    g_boss->pos[1] = 0.0f; // align to ground level similar to character
    g_boss->pos[2] = 0.0f;  // Back for dramatic reveal
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
    cutsceneState = CUTSCENE_BOSS_INTRO;
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

void update_dynamic_chains(int animIndex)
{
    for (int i = 0; i < NUM_DYNAMIC_CHAINS; i++) 
    {
        t3d_anim_update(gDynamicChains[i].anims[animIndex], deltaTime);
        t3d_skeleton_update(gDynamicChains[i].skeleton);

        t3d_mat4fp_from_srt_euler(gDynamicChains[i].matrix, 
            (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
            (float[3]){0.0f, 0.0f, 0.0f},
            (float[3]){0.0f, -5.0f, 0.0f}
        );
    }
}

void scene_cutscene_update()
{
    // Update cutscene state
    cutsceneTimer += deltaTime;
    // Update camera timer separately (doesn't reset when transitioning states)
    if (cutsceneState != CUTSCENE_NONE) {
        cutsceneCameraTimer += deltaTime;
    }
    
    // Check for A button press to skip intro cutscene
    bool aPressed = joypad.btn.a;
    bool aJustPressed = aPressed && !lastAPressed;
    lastAPressed = aPressed;
    
    switch (cutsceneState) {
        case CUTSCENE_BOSS_INTRO:
            // During intro cutscene, update character and boss for rendering but disable AI
            character_update();
            if (g_boss) {
                boss_anim_update(g_boss);
                // Update transforms for rendering
                T3DMat4FP* mat = (T3DMat4FP*)g_boss->modelMat;
                if (mat) {
                    t3d_mat4fp_from_srt_euler(mat, g_boss->scale, g_boss->rot, g_boss->pos);
                }
            }
            dialog_controller_update();
            
            // Slowly move camera towards boss during cutscene
            // Use a smooth interpolation over ~5 seconds
            const float cameraMoveDuration = 7.0f;
            float t = cutsceneCameraTimer / cameraMoveDuration;
            if (t > 1.0f) t = 1.0f;  // Clamp to 1.0
            
            // Smooth interpolation (ease-in-out)
            float easeT = t * t * (3.0f - 2.0f * t);
            
            // Update camera position
            customCamPos.v[0] = cutsceneCamPosStart.v[0] + (cutsceneCamPosEnd.v[0] - cutsceneCamPosStart.v[0]) * easeT;
            customCamPos.v[1] = cutsceneCamPosStart.v[1] + (cutsceneCamPosEnd.v[1] - cutsceneCamPosStart.v[1]) * easeT;
            customCamPos.v[2] = cutsceneCamPosStart.v[2] + (cutsceneCamPosEnd.v[2] - cutsceneCamPosStart.v[2]) * easeT;
            
            // Only print occasionally to avoid spam
            if (((int)(cutsceneTimer * 10)) % 10 == 0) {
                debugf("CUTSCENE_BOSS_INTRO: dialog_speaking = %s, cameraState = %d, cutsceneTimer = %.1f\n",
                       dialog_controller_speaking() ? "true" : "false", cameraState, cutsceneTimer);
            }
            
            // Allow skipping intro with A button
            if (aJustPressed) {
                // Skip dialog and cutscene
                dialog_controller_stop_speaking();
                cutsceneState = CUTSCENE_NONE;
                cutsceneCameraTimer = 0.0f;
                bossActivated = true;
                // Hide letterbox bars with animation
                letterbox_hide();
                // Return camera control to the player
                camera_mode_smooth(CAMERA_CHARACTER, 1.0f);
                break;
            }
            
            // Wait for dialog to finish
            if (!dialog_controller_speaking()) {
                debugf("CUTSCENE_BOSS_INTRO: Dialog finished, moving to WAIT state\n");
                cutsceneState = CUTSCENE_BOSS_INTRO_WAIT;
                cutsceneTimer = 0.0f;
            }
            break;
            
        // Wait for boss to be activated before moving to the next state
        case CUTSCENE_BOSS_INTRO_WAIT:
            if (g_boss) {
                boss_anim_update(g_boss);
                // Update transforms for rendering
                T3DMat4FP* mat = (T3DMat4FP*)g_boss->modelMat;
                if (mat) {
                    t3d_mat4fp_from_srt_euler(mat, g_boss->scale, g_boss->rot, g_boss->pos);
                }
            }
            // Continue camera movement during wait period (using same timer)
            const float cameraMoveDurationWait = 5.0f;
            float tWait = cutsceneCameraTimer / cameraMoveDurationWait;
            if (tWait > 1.0f) tWait = 1.0f;
            float easeTWait = tWait * tWait * (3.0f - 2.0f * tWait);
            customCamPos.v[0] = cutsceneCamPosStart.v[0] + (cutsceneCamPosEnd.v[0] - cutsceneCamPosStart.v[0]) * easeTWait;
            customCamPos.v[1] = cutsceneCamPosStart.v[1] + (cutsceneCamPosEnd.v[1] - cutsceneCamPosStart.v[1]) * easeTWait;
            customCamPos.v[2] = cutsceneCamPosStart.v[2] + (cutsceneCamPosEnd.v[2] - cutsceneCamPosStart.v[2]) * easeTWait;
            debugf("CUTSCENE_BOSS_INTRO_WAIT: cutsceneTimer = %.2f\n", cutsceneTimer);
            // Allow skipping wait period with A button
            if (aJustPressed || cutsceneTimer >= 1.0f) {
                debugf("CUTSCENE_BOSS_INTRO_WAIT: Ending cutscene, switching to CHARACTER camera\n");
                cutsceneState = CUTSCENE_NONE;
                cutsceneCameraTimer = 0.0f;
                bossActivated = true;
                // Hide letterbox bars with animation
                letterbox_hide();
                // Return camera control to the player
                camera_mode_smooth(CAMERA_CHARACTER, 1.0f);
            }
            break;
        default:
            break;
    }
}

void scene_update_title(void)
{
    if(btn.start)
    {
        scene_init_cinematic_camera();
        gameState = GAME_STATE_PLAYING;
        //gameState = GAME_STATE_TITLE_TRANSITION;

    }

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
                dialog_controller_speak(titleDialogs[currentTitleDialog], 0, 10.0f, false, true);
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
        bool aPressed = joypad.btn.a;
        bool aJustPressed = aPressed && !lastAPressed;
        lastAPressed = aPressed;

        if (aJustPressed) {
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
    //Title text

    rdpq_sync_pipe();
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

}

void scene_draw(T3DViewport *viewport) 
{

    t3d_frame_start();

    if(!HARDWARE_MODE && !debugDraw)
    {
        rdpq_mode_dithering(DITHER_NONE_BAYER);
    }

    t3d_viewport_attach(viewport);

    // Fog
    color_t fogColor = (color_t){0, 0, 0, 0xFF};
    //rdpq_set_prim_color((color_t){0xFF, 0xFF, 0xFF, 0xFF});
    rdpq_mode_fog(RDPQ_FOG_STANDARD);
    rdpq_set_fog_color(fogColor);

    t3d_fog_set_range(450.0f, 800.0f);
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
    // ===== DRAW 3D =====

    rdpq_sync_pipe();
    rdpq_mode_zbuf(false, false);

    // Draw no depth environment first
    t3d_matrix_push_pos(1);
        t3d_matrix_set(windowsMatrix, true);
        rspq_block_run(windowsDpl);

        t3d_matrix_set(mapMatrix, true);
        rspq_block_run(mapDpl);

        t3d_matrix_set(fogDoorMatrix, true);
        // Create a struct to pass the scrolling parameters to the tile callback
        t3d_model_draw_custom(fogDoorModel, (T3DModelDrawConf){
            .userData = &fogScrollParams,
            .tileCb = tile_scroll,
        });

        t3d_matrix_set(chainsMatrix, true);
        rspq_block_run(chainsDpl);
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
    t3d_matrix_pop(1); 

    // Draw characters
    // rdpq_sync_pipe();
    // rdpq_set_prim_color((color_t){0, 0, 0, 0x20});

    t3d_matrix_push_pos(1);
        character_draw();
        if (g_boss) {
            boss_draw(g_boss);
        }
    t3d_matrix_pop(1);

    //Draw transparencies last
    t3d_matrix_push_pos(1);    
        t3d_matrix_set(sunshaftsMatrix, true);
        rspq_block_run(sunshaftsDpl);
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
    int y = 240 - height - 10; 
    dialog_controller_draw(false, x, y, width, height);

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