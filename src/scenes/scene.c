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

#include "character.h"
#include "game/bosses/boss.h"
#include "game/bosses/boss_anim.h"
#include "game/bosses/boss_render.h"
#include "dialog_controller.h"
#include "display_utility.h"
//#include "collision_mesh.h"
#include "collision_system.h"
#include "letterbox_utility.h"

// TODO: This should not be declared in the header file, as it is only used externally (temp)
#include "dev.h"
#include "debug_draw.h"
#include "utilities/simple_collision_utility.h"

// Four AABB walls: left, right, back, front (Y spans from floor to a fixed height)
static AABB g_roomWalls[4];
static const int g_roomWallCount = 4;

// Diagonal corner walls (octagon feel) using box/AABB colliders at corners
// Diagonal corner boxes approximating angled walls
static AABB g_diagBoxes[16];           // up to 4 corners * several steps
static int  g_diagBoxCount = 0;        // filled at init
static int  g_diagSteps = 5;           // number of small boxes per diagonal
static float g_diagBoxHalf = 8.0f;     // half-size of each small diagonal box
static float g_diagOffset = 80.0f;     // how deep the corner cut is from each axis


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
static bool cinematicChainsVisible = true;
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

// Title scene character facing: rotate to face down the hall
static const float TITLE_CHARACTER_YAW = T3D_PI * 0.5f; // +90° around Y

static bool screenTransition = false;
static bool screenBreath = false;

#define WALL_THICKNESS 20.0f
#define WALL_HEIGHT   200.0f

static SCU_OBB g_roomOBBs[] = {

    // -------------------------------------------------
    // right wall
    // (345, 595) -> (-430, 595)
    // -------------------------------------------------
    {
        .center = { (-430.0f + 345.0f) * 0.5f, 0.0f, 595.0f },                 // x=-42.5, z=595
        .half   = { (345.0f - (-430.0f)) * 0.5f, WALL_HEIGHT * 0.5f, WALL_THICKNESS * 0.5f }, // hx=387.5
        .yaw    = 3.1415926f
    },

    // -------------------------------------------------
    // front wall
    // (-430, 595) -> (-430, -595)
    // -------------------------------------------------
    {
        .center = { -430.0f, 0.0f, (595.0f + -595.0f) * 0.5f },                // x=-430, z=0
        .half   = { (595.0f - (-595.0f)) * 0.5f, WALL_HEIGHT * 0.5f, WALL_THICKNESS * 0.5f }, // hx=595
        .yaw    = -1.5707963f
    },

    // -------------------------------------------------
    // left wall
    // (-458, -595) -> (345, -595)
    // -------------------------------------------------
    {
        .center = { (-458.0f + 345.0f) * 0.5f, 0.0f, -595.0f },                // x=-56.5, z=-595
        .half   = { (345.0f - (-458.0f)) * 0.5f, WALL_HEIGHT * 0.5f, WALL_THICKNESS * 0.5f }, // hx=401.5
        .yaw    = 0.0f
    },

    // -------------------------------------------------
    // left wall bend in
    // (345, -595) -> (420, -420)
    // -------------------------------------------------
    {
        .center = { (345.0f + 420.0f) * 0.5f, 0.0f, (-595.0f + -420.0f) * 0.5f }, // x=382.5, z=-507.5
        .half   = { 95.52f, WALL_HEIGHT * 0.5f, WALL_THICKNESS * 0.5f },          // half length ≈ sqrt(75^2+175^2)/2
        .yaw    = 1.1659045f
    },

    // -------------------------------------------------
    // right wall bend in
    // (345, 595) -> (420, 420)
    // -------------------------------------------------
    {
        .center = { (345.0f + 420.0f) * 0.5f, 0.0f, (595.0f + 420.0f) * 0.5f },  // x=382.5, z=507.5
        .half   = { 95.52f, WALL_HEIGHT * 0.5f, WALL_THICKNESS * 0.5f },
        .yaw    = -1.1659045f
    },

    // -------------------------------------------------
    // left wall continued
    // (420, -415) -> (600, -415)
    // -------------------------------------------------
    {
        .center = { (420.0f + 600.0f) * 0.5f, 0.0f, -415.0f },                 // x=510, z=-415
        .half   = { (600.0f - 420.0f) * 0.5f, WALL_HEIGHT * 0.5f, WALL_THICKNESS * 0.5f }, // hx=90
        .yaw    = 0.0f
    },

    // -------------------------------------------------
    // right wall continued
    // (420, 415) -> (600, 415)
    // -------------------------------------------------
    {
        .center = { (420.0f + 600.0f) * 0.5f, 0.0f, 415.0f },                  // x=510, z=415
        .half   = { (600.0f - 420.0f) * 0.5f, WALL_HEIGHT * 0.5f, WALL_THICKNESS * 0.5f }, // hx=90
        .yaw    = 0.0f
    },

    // -------------------------------------------------
    // back wall
    // (600, 420) -> (600, -420)
    // -------------------------------------------------
    {
        .center = { 600.0f, 0.0f, (420.0f + -420.0f) * 0.5f },                 // x=600, z=0
        .half   = { (420.0f - (-420.0f)) * 0.5f, WALL_HEIGHT * 0.5f, WALL_THICKNESS * 0.5f }, // hx=420
        .yaw    = -1.5707963f
    },
};

static const int g_roomOBBCount = sizeof(g_roomOBBs) / sizeof(g_roomOBBs[0]);

#define TITLE_DIALOG_COUNT (sizeof(titleDialogs) / sizeof(titleDialogs[0]))

static const char *titleDialogs[] = {
    ">The Demon\nking has\nforced\nthe land\ninto a\ncentury long\ndarkness.",
    ">The King\nhas trained\na legion\nof powerful\nknights\nsworn to\nprotect the\nthrone.",
    ">These\nbattle born\nknights are\ntaken from\ntheir\nfamilies and\ncast into\nservitude.",
    ">Enduring\nblade and\ntorment\nuntil nothing\nremains but\nhollow armor."
};


static const char *phase1Dialogs[] = {
    "^Those who approach the\nthrone of gold~ ^fall at my\nblade.",
    "^A Knight?~ >Where is your\n^loyalty...",
    "^Where is your...~ <Fear.",
};
bool cutsceneDialogActive = false;

// Boss title fade control (shown during intro, fades out when fight starts)
static float bossTitleFade = 0.0f;
static float bossTitleFadeSpeed = 1.8f;

// progress for boss/player UIs
static float bossUiIntro = 1.0f;
static float playerUiIntro = 1.0f;
static float uiIntroSpeed = 1.5f;

// Title should not be treated as an active cutscene; we explicitly enter cutscenes when needed.
static CutsceneState cutsceneState = CUTSCENE_NONE;
static float cutsceneTimer = 0.0f;
static float cutsceneCameraTimer = 0.0f;  // Separate timer for camera movement (doesn't reset)
static bool bossActivated = false;
static Boss* g_boss = NULL;  // Boss instance pointer
static T3DVec3 cutsceneCamPosStart;  // Initial camera position (further back)
static T3DVec3 cutsceneCamPosEnd;    // Final camera position (closer to boss)

// Game state management
static GameState gameState = GAME_STATE_TITLE;
static bool lastMenuActive = false;

// Local-only restart counter (persists in RAM for the lifetime of the program)
static uint32_t restartCounter = 0;

// Input state tracking
static bool lastAPressed = false;
static bool lastStartPressed = false;
static bool lastZPressed = false;

// Cutscene skip - toggle button visibility
static sprite_t* aButtonSprite = NULL;
static surface_t aButtonSurf;
static bool skipButtonVisible = false; // Whether the skip button is currently visible
static bool lastCutsceneAPressed = false; // Track A button state for edge detection

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
};

static void debug_draw_obb_xz(
    T3DViewport *vp,
    const SCU_OBB *o,
    float y,
    uint16_t color)
{
    float c = cosf(o->yaw);
    float s = sinf(o->yaw);

    float hx = o->half[0];
    float hz = o->half[2];

    // 4 corners in local space (XZ)
    float lx[4] = { -hx,  hx,  hx, -hx };
    float lz[4] = { -hz, -hz,  hz,  hz };

    T3DVec3 p[4];

    for (int i = 0; i < 4; i++) {
        // local -> world (rotate + translate)
        float wx = o->center[0] + (c * lx[i] - s * lz[i]);
        float wz = o->center[2] + (s * lx[i] + c * lz[i]);

        p[i] = (T3DVec3){{ wx, y, wz }};
    }

    // Draw rectangle as two wire triangles
    debug_draw_tri_wire(vp, &p[0], &p[1], &p[2], color);
    debug_draw_tri_wire(vp, &p[0], &p[2], &p[3], color);
}

static void scene_get_character_world_capsule(float capA[3], float capB[3], float *radius)
{
    capA[0] = character.pos[0] + character.capsuleCollider.localCapA.v[0] * character.scale[0];
    capA[1] = character.pos[1] + character.capsuleCollider.localCapA.v[1] * character.scale[1];
    capA[2] = character.pos[2] + character.capsuleCollider.localCapA.v[2] * character.scale[2];

    capB[0] = character.pos[0] + character.capsuleCollider.localCapB.v[0] * character.scale[0];
    capB[1] = character.pos[1] + character.capsuleCollider.localCapB.v[1] * character.scale[1];
    capB[2] = character.pos[2] + character.capsuleCollider.localCapB.v[2] * character.scale[2];

    *radius = character.capsuleCollider.radius * character.scale[0];
}

void scene_resolve_character_room_obbs(void)
{
    // more iterations => less corner tunneling / less “elastic”
    for (int iter = 0; iter < 8; iter++) {
        float capA[3], capB[3], r;
        scene_get_character_world_capsule(capA, capB, &r);

        float vx, vz;
        character_get_velocity(&vx, &vz);

        bool any = false;

        for (int i = 0; i < g_roomOBBCount; i++) {
            float push[3];
            float n[3];

            if (scu_capsule_vs_obb_push_xz_f(capA, capB, r, &g_roomOBBs[i], push, n)) {

                // push out (world)
                character.pos[0] += push[0];
                character.pos[2] += push[2];

                // IMPORTANT: keep capsule in sync for subsequent OBB checks THIS iter
                capA[0] += push[0]; capA[2] += push[2];
                capB[0] += push[0]; capB[2] += push[2];

                // slide: remove inward velocity component (vn < 0 means into the surface)
                float vn = vx * n[0] + vz * n[2];
                if (vn < 0.0f) {
                    vx -= vn * n[0];
                    vz -= vn * n[2];
                }

                any = true;
            }
        }

        character_set_velocity_xz(vx, vz);

        if (!any) break;
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



static void scene_title_init_dynamic_banner_assets(void)
{
    if (dynamicBannerModel) return;

    // ===== LOAD Dynamic Banner (Title Screen) =====
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
}

static void scene_title_init(void)
{
    // Ensure title-only assets are loaded once.
    scene_title_init_dynamic_banner_assets();

    audio_play_music("rom:/audio/music/demonous-22k.wav64", true);

    // Init to title screen position
    camera_mode(CAMERA_CUSTOM);
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

    character.rot[1] = TITLE_CHARACTER_YAW;

    character_update_position();

    // Reset/prime banner animation so restarts start from a consistent pose.
    if (dynamicBannerAnimations && dynamicBannerAnimations[0]) {
        t3d_anim_set_time(dynamicBannerAnimations[0], 0.0f);
        t3d_anim_set_playing(dynamicBannerAnimations[0], true);
    }

    // Start Dialog
    currentTitleDialog = 0;
    titleTextActivationTimer = 0.0f;
    dialog_controller_speak(titleDialogs[0], 0, 9.0f, false, true);

    startScreenFade = true;
}

void scene_init(void) 
{
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
    // Note: dx and dz were calculated but not used - keeping for potential future use
    // float dx = g_boss->pos[0] - character.pos[0];
    // float dz = g_boss->pos[2] - character.pos[2];

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

    // Load A button sprite for cutscene skip indicator
    aButtonSprite = sprite_load("rom:/buttons/WhiteOutlineButtons/A.sprite");
    if (aButtonSprite) {
        aButtonSurf = sprite_get_pixels(aButtonSprite);
    }

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
// Uses the Spine1 bone position if available; otherwise falls back to capsule estimate.
static T3DVec3 get_boss_lock_focus_point(void)
{
    if (!g_boss) {
        return (T3DVec3){{0.0f, 0.0f, 0.0f}};
    }
    
    // Try to use Spine1 bone position if available
    if (g_boss->spine1BoneIndex >= 0 && g_boss->skeleton && g_boss->modelMat) {
        T3DSkeleton* skel = (T3DSkeleton*)g_boss->skeleton;
        if (skel && skel->skeletonRef) {
            // Get bone's transform matrix (in model space, updated by animation system)
            const T3DMat4FP* boneMat = &skel->boneMatricesFP[g_boss->spine1BoneIndex];
            const T3DMat4FP* modelMat = (const T3DMat4FP*)g_boss->modelMat;
            
            // Bone-local point (origin of the bone)
            const float boneLocal[3] = { 0.0f, 0.0f, 0.0f };
            
            // 1) Transform from bone-local space to model space (apply bone matrix)
            float boneModel[3];
            mat4fp_mul_point_f32_row3_colbasis(boneMat, boneLocal, boneModel);
            
            // 2) Transform from model space to world space (apply boss model matrix)
            float boneWorld[3];
            mat4fp_mul_point_f32_row3_colbasis(modelMat, boneModel, boneWorld);
            
            return (T3DVec3){{
                boneWorld[0],
                boneWorld[1],
                boneWorld[2]
            }};
        }
    }
    
    // Fallback to capsule-based estimate if bone is not available
    float focusOffset = g_boss->orbitRadius * 0.6f; // roughly chest height for current tuning

    float capA = g_boss->capsuleCollider.localCapA.v[1];
    float capB = g_boss->capsuleCollider.localCapB.v[1];

    // Use point halfway between midpoint and capB (i.e. 75% from A -> B)
    if (capA != 0.0f || capB != 0.0f) {
        focusOffset = (capA + capB + capB + capB) * 0.25f;
    }

    return (T3DVec3){{
        g_boss->pos[0],
        g_boss->pos[1] + focusOffset,
        g_boss->pos[2]
    }};
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
    gameState = GAME_STATE_TITLE;
    lastMenuActive = false;
    lastAPressed = false;
    lastStartPressed = false;
    lastZPressed = false;
    cameraLockOnActive = false;

    // Title state
    screenTransition = false;
    screenBreath = false;
    titleStartGameTimer = 0.0f;
    titleTextActivationTimer = 0.0f;
    currentTitleDialog = 0;

    // UI state
    bossTitleFade = 0.0f;
    bossUiIntro = 1.0f;
    playerUiIntro = 1.0f;
    display_utility_set_boss_ui_intro(bossUiIntro);
    display_utility_set_player_ui_intro(playerUiIntro);

    // Reset letterbox to show state for intro
    letterbox_show(false);
}

static void scene_sync_input_edge_state(void)
{
    // Sync last-pressed to the current button state so held buttons don't cause "just pressed"
    // events immediately after restart.
    lastAPressed = btn.a;
    lastStartPressed = btn.start;
    lastZPressed = btn.z;
    lastCutsceneAPressed = btn.a;
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

    // Count restarts (win/lose/manual restart all funnel through this soft reset).
    // Note: we intentionally do not persist this to any save file.
    restartCounter++;

    // 1) Stop running systems first (prevents update-on-freed state)
    audio_stop_all_sfx();
    audio_stop_music();
    dialog_controller_reset();

    // 2) Reset input edge-tracking (prevents phantom presses)
    character_reset_button_state();

    // 3) Reset gameplay entities (logic state)
    if (g_boss) boss_reset(g_boss);
    character_reset();

    // 4) Reset camera / lock-on and scene runtime flags
    camera_reset();
    camera_mode(CAMERA_CUSTOM);
    scene_reset();

    // 5) Enter title runtime state (NO re-init / NO allocations)
    scene_title_init();
    scene_sync_input_edge_state();

    debugf("RESTART: Soft reset complete. cameraState=%d speaking=%s\n",
           cameraState, dialog_controller_speaking() ? "true" : "false");
}

void scene_init_playing(){
    character.pos[0] = -361.43f;
    character.pos[1] = 4.0f;
    character.pos[2] = 0.0f;

    character.scale[0] = MODEL_SCALE * 0.5f;
    character.scale[1] = MODEL_SCALE * 0.5f;
    character.scale[2] = MODEL_SCALE * 0.5f;

    // Face towards boss
    character.rot[1] = TITLE_CHARACTER_YAW + T3D_PI;

    character_update_position();

    // Skip dialog and cutscene
    dialog_controller_stop_speaking();
    cutsceneState = CUTSCENE_NONE;
    cutsceneCameraTimer = 0.0f;
    bossActivated = true;
    // Switch music from intro cinematic to boss fight
    audio_stop_music();
    audio_play_music("rom:/audio/music/boss_phase1-looping-22k.wav64", true);
    // Hide letterbox bars with animation
    letterbox_hide();
    // Return camera control to the player
    camera_mode_smooth(CAMERA_CHARACTER, 1.0f);
    cameraLockOnActive = true;

    // Hide cinematic chains once gameplay starts
    cinematicChainsVisible = false;

    // Start boss title fully visible so it slides up on gameplay start
    bossTitleFade = 1.0f;

    // Reset UI intro animations (they will slide/fade into view)
    bossUiIntro = 0.0f;
    playerUiIntro = 0.0f;
    display_utility_set_boss_ui_intro(bossUiIntro);
    display_utility_set_player_ui_intro(playerUiIntro);

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
            // Start hiding letterbox bars before gameplay begins
            letterbox_hide();
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

    // Handle cutscene skip - toggle button on first A press, skip on second
    bool aCurrentlyPressed = btn.a;
    bool aJustPressed = aCurrentlyPressed && !lastCutsceneAPressed;
    
    if (aJustPressed)
    {
        if (!skipButtonVisible)
        {
            // First press - show the skip button
            skipButtonVisible = true;
        }
        else
        {
            // Second press - skip the cutscene
            skipButtonVisible = false;
            cutsceneTimer = 0.0f;
            cutsceneCameraTimer = 0.0f;
            scene_init_playing();
            return;
        }
    }
    
    // Update last state for next frame
    lastCutsceneAPressed = aCurrentlyPressed;
}

void scene_update_title(void)
{
    if(gameState == GAME_STATE_TITLE_TRANSITION)
    {
        if(titleStartGameTimer >= titleStartGameTime){
            titleStartGameTimer = 0.0f;
            // Enter the intro cutscene intentionally from title.
            cutsceneState = CUTSCENE_PHASE1_INTRO;
            cutsceneTimer = 0.0f;
            cutsceneCameraTimer = 0.0f;
            scene_init_cutscene();
            audio_stop_all_sfx(); // TODO: eventually we probably want a stop specific sound effect ID but that would add complexity at this point
        }
        else
        {
            // Handle skip button - toggle button on first A press, skip on second
            bool aCurrentlyPressed = btn.a;
            bool aJustPressed = aCurrentlyPressed && !lastCutsceneAPressed;
            
            if (aJustPressed)
            {
                if (!skipButtonVisible)
                {
                    // First press - show the skip button
                    skipButtonVisible = true;
                }
                else
                {
                    // Second press - skip to cutscene
                    skipButtonVisible = false;
                    titleStartGameTimer = 0.0f;
                    cutsceneState = CUTSCENE_PHASE1_INTRO;
                    cutsceneTimer = 0.0f;
                    cutsceneCameraTimer = 0.0f;
                    scene_init_cutscene();
                    audio_stop_all_sfx();
                    lastCutsceneAPressed = aCurrentlyPressed;
                    return;
                }
            }
            
            // Update last state for next frame
            lastCutsceneAPressed = aCurrentlyPressed;
            
            // Allow start button to skip immediately (original behavior)
            if(btn.start)
            {
                titleStartGameTimer = 0.0f;
                cutsceneState = CUTSCENE_PHASE1_INTRO;
                cutsceneTimer = 0.0f;
                cutsceneCameraTimer = 0.0f;
                scene_init_cutscene();
                audio_stop_all_sfx();
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

    bool startJustPressed = btn.start && !lastStartPressed;
    bool aJustPressed = btn.a && !lastAPressed;
    lastStartPressed = btn.start;
    lastAPressed = btn.a;

    if(startJustPressed || aJustPressed)
    {
        gameState = GAME_STATE_TITLE_TRANSITION;
        skipButtonVisible = false; // Reset skip button state when entering transition
        lastCutsceneAPressed = false;

        camera_breath_active(false); 
        screenBreath = false; 
        audio_stop_music_fade(6); // duration
        audio_play_scene_sfx_dist(
            SCENE1_SFX_TITLE_WALK, // sfx id
            1.0f,                  // base volume
            0.0f                   // distance
        );
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
        // Keep animation state updated (bars not drawn during title)
        letterbox_update();
        return;
    }

    // Check if menu was just closed - if so, reset character button state
    bool menuActive = scene_is_menu_active();
    if (lastMenuActive && !menuActive) {
        // Menu was just closed - reset character button state to prevent false "just pressed"
        character_reset_button_state();
    }
    lastMenuActive = menuActive;

    // If player is dead or victorious, disable player control but keep boss/UI moving
    if (gameState == GAME_STATE_DEAD || gameState == GAME_STATE_VICTORY) {
        // Keep boss AI updating so it continues moving during end screen
        if (bossActivated && g_boss) {
            boss_update(g_boss);
        }
        // Continue letterbox animation updates
        letterbox_update();

        // Allow restart prompt via A button
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
        // Constrain player inside obbs
        scene_resolve_character_room_obbs();
        // Update character transform after constraint
        character_update_position();
        if (bossActivated && g_boss) {
            boss_update(g_boss);
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

// Forward declaration
static void draw_cutscene_skip_indicator(void);

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

        // Restart counter (local-only). Don't show if there's 0 restarts.
        if (restartCounter > 0) {
            const int margin = 10;
            const int panelW = 60;
            const int panelH = 18;
            const int panelX0 = margin;
            const int panelY0 = SCREEN_HEIGHT - margin - panelH;

            rdpq_set_mode_standard();
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_set_prim_color(RGBA32(0, 0, 0, 120));
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
            rdpq_fill_rectangle(panelX0, panelY0, panelX0 + panelW, panelY0 + panelH);

            rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
            rdpq_text_printf(NULL, FONT_UNBALANCED, panelX0 + 6, panelY0 + 13, "Runs: %lu", (unsigned long)restartCounter);
        }

        // Draw A button graphic at bottom middle of play button, half off
        if (aButtonSprite) {
            int buttonWidth = aButtonSurf.width;
            int buttonHeight = aButtonSurf.height;
            // Play button rectangle: (15, 35) to (75, 58)
            // Center horizontally: (15 + 75) / 2 = 45
            // Bottom of button: y = 58
            // Position so half is below the button edge: y = 58 - buttonHeight/2
            int aButtonX = 45 - (buttonWidth / 2);
            int aButtonY = 62 - (buttonHeight / 2);
            
            // Draw the A button sprite on top
            rdpq_sync_pipe();
            rdpq_set_mode_standard();
            rdpq_mode_alphacompare(1);
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            
            rdpq_tex_blit(&aButtonSurf, aButtonX, aButtonY, NULL);
        }

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
        
        // Draw skip button on top during title transition (fog part)
        draw_cutscene_skip_indicator();
    }
}

// Draw the A button skip indicator (only when visible after first A press)
static void draw_cutscene_skip_indicator(void)
{
    // Only draw if button sprite is loaded, button is visible, and either:
    // - cutscene is active, OR
    // - we're in title transition (fog part)
    bool isTitleTransition = (gameState == GAME_STATE_TITLE_TRANSITION);
    bool isCutsceneActive = (cutsceneState != CUTSCENE_NONE);
    
    if (!aButtonSprite || !skipButtonVisible || (!isCutsceneActive && !isTitleTransition)) {
        return;
    }
    
    // Get actual sprite dimensions
    int buttonWidth = aButtonSurf.width;
    int buttonHeight = aButtonSurf.height;
    
    // Position at bottom right of screen, with margin from edges
    int margin = 10; // Margin from screen edges
    int buttonX = SCREEN_WIDTH - buttonWidth - margin;
    int buttonY = SCREEN_HEIGHT - buttonHeight - margin;
    
    // Draw the A button sprite
    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    
    rdpq_tex_blit(&aButtonSurf, buttonX, buttonY, NULL);
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
            
            int barWidth = 195;
            int barHeight = 23;
            int barTop = 35;
            int barLeft = (SCREEN_WIDTH - barWidth) / 2;
            rdpq_fill_rectangle(barLeft, barTop, barLeft + barWidth, barTop + barHeight);

            rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
            rdpq_text_printf(&(rdpq_textparms_t){
                .align = ALIGN_CENTER,
                .width = SCREEN_WIDTH,
            }, FONT_UNBALANCED, 0, 50, "%s", g_boss->name);

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
        // Draw letterbox bars during cutscenes
        letterbox_draw();
        // Draw skip indicator on top of letterbox bars
        draw_cutscene_skip_indicator();
        return;
    }
    // ===== DRAW 3D =====

    rdpq_sync_pipe();
    rdpq_mode_zbuf(false, false);

    // Draw no depth environment first
    t3d_matrix_push_pos(1);

        t3d_matrix_set(windowsMatrix, true);
        rspq_block_run(windowsDpl);

        t3d_matrix_set(roomFloorMatrix, true);
        rspq_block_run(roomFloorDpl);

        t3d_matrix_set(mapMatrix, true);
        rspq_block_run(mapDpl);
    t3d_matrix_pop(1);
    
    // Draw depth environment
    // rdpq_sync_pipe();
    // rdpq_mode_zbuf(true, true);

    // t3d_matrix_push_pos(1);   
    //     t3d_matrix_set(roomFloorMatrix, true);
    //     rspq_block_run(roomFloorDpl);
    // t3d_matrix_pop(1); 

    // rdpq_sync_pipe();
    // rdpq_mode_zbuf(false, false);

    t3d_matrix_push_pos(1);   
      // blob shadows here
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
        if (cinematicChainsVisible) {
            t3d_matrix_set(cinematicChainsMatrix, true);
            rspq_block_run(cinematicChainsDpl);
        }
        t3d_matrix_set(chainsMatrix, true);
        rspq_block_run(chainsDpl);
    t3d_matrix_pop(1); 
    // ===== DRAW 2D =====

    // Overlay lock-on marker above the boss
    if(DEV_MODE)
        draw_lockon_indicator(viewport);

    // Debug draw room colliders in gameplay
    if (cutsceneState == CUTSCENE_NONE && debugDraw) {
        float capA[3], capB[3], r;
        scene_get_character_world_capsule(capA, capB, &r);

        for (int i = 0; i < g_roomOBBCount; i++) {
            float push[3], n[3];
            bool hit = scu_capsule_vs_obb_push_xz_f(capA, capB, r, &g_roomOBBs[i], push, n);

            debug_draw_obb_xz(viewport, &g_roomOBBs[i], 0.0f, hit ? DEBUG_COLORS[0] : DEBUG_COLORS[2]);
        }
    }
    
    bool cutsceneActive = scene_is_cutscene_active();
    GameState state = scene_get_game_state();
    bool isDead = state == GAME_STATE_DEAD;
    bool isVictory = state == GAME_STATE_VICTORY;
    bool isEndScreen = isDead || isVictory;

    // Draw letterbox bars (they handle their own visibility and animation)
    letterbox_draw();

    // Draw UI elements after 3D rendering is complete (hide during cutscenes or death)
    if (!cutsceneActive && !isEndScreen) {
        // Boss UI appears only after the boss title has fully slid off-screen
        if (scene_is_boss_active() && g_boss && bossTitleFade <= 0.0f) {
            boss_draw_ui(g_boss, viewport);
        }
        character_draw_ui();
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
        // Full-screen overlay with prompt to restart
        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        // Use semi-transparent black to keep scene visible underneath
        rdpq_set_prim_color(RGBA32(0, 0, 0, 140));
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

void scene_cleanup(void) // Realistically we never want to call this for the jam.
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
    audio_scene_unload_sfx();

    // --- Title/Cutscene assets not covered by scene_delete_environment() ---
    if (dynamicBannerDpl) { rspq_block_free(dynamicBannerDpl); dynamicBannerDpl = NULL; }
    if (dynamicBannerModel) { t3d_model_free(dynamicBannerModel); dynamicBannerModel = NULL; }
    if (dynamicBannerMatrix) { free_uncached(dynamicBannerMatrix); dynamicBannerMatrix = NULL; }
    if (dynamicBannerSkeleton) { t3d_skeleton_destroy(dynamicBannerSkeleton); free_uncached(dynamicBannerSkeleton); dynamicBannerSkeleton = NULL; }
    if (dynamicBannerAnimations) {
        // only 1 anim currently
        if (dynamicBannerAnimations[0]) { t3d_anim_destroy(dynamicBannerAnimations[0]); free_uncached(dynamicBannerAnimations[0]); }
        free_uncached(dynamicBannerAnimations);
        dynamicBannerAnimations = NULL;
    }

    if (cinematicChainsDpl) { rspq_block_free(cinematicChainsDpl); cinematicChainsDpl = NULL; }
    if (cinematicChainsModel) { t3d_model_free(cinematicChainsModel); cinematicChainsModel = NULL; }
    if (cinematicChainsMatrix) { free_uncached(cinematicChainsMatrix); cinematicChainsMatrix = NULL; }
    if (cinematicChainsSkeleton) { t3d_skeleton_destroy(cinematicChainsSkeleton); free_uncached(cinematicChainsSkeleton); cinematicChainsSkeleton = NULL; }
    if (cinematicChainsAnimations) {
        for (int i = 0; i < 2; i++) {
            if (cinematicChainsAnimations[i]) { t3d_anim_destroy(cinematicChainsAnimations[i]); free_uncached(cinematicChainsAnimations[i]); }
        }
        free_uncached(cinematicChainsAnimations);
        cinematicChainsAnimations = NULL;
    }

    if (cutsceneChainBreakDpl) { rspq_block_free(cutsceneChainBreakDpl); cutsceneChainBreakDpl = NULL; }
    if (cutsceneChainBreakModel) { t3d_model_free(cutsceneChainBreakModel); cutsceneChainBreakModel = NULL; }
    if (cutsceneChainBreakMatrix) { free_uncached(cutsceneChainBreakMatrix); cutsceneChainBreakMatrix = NULL; }
    if (cutsceneChainBreakSkeleton) { t3d_skeleton_destroy(cutsceneChainBreakSkeleton); free_uncached(cutsceneChainBreakSkeleton); cutsceneChainBreakSkeleton = NULL; }
    if (cutsceneChainBreakAnimations) {
        // only 1 anim currently
        if (cutsceneChainBreakAnimations[0]) { t3d_anim_destroy(cutsceneChainBreakAnimations[0]); free_uncached(cutsceneChainBreakAnimations[0]); }
        free_uncached(cutsceneChainBreakAnimations);
        cutsceneChainBreakAnimations = NULL;
    }

    if (aButtonSprite) {
        sprite_free(aButtonSprite);
        aButtonSprite = NULL;
        surface_free(&aButtonSurf);
    }
}