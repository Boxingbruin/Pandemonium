#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "testing_scene.h"

#include "../../controllers/camera_controller.h"
#include "../../fx/dust_particles_fx.h"
#include "../../utilities/display_utility.h"
#include "../../utilities/game_lighting.h"
#include "../../utilities/game_time.h"
#include "../../utilities/general_utility.h"
#include "../../utilities/globals.h"
#include "../../utilities/joypad_utility.h"

// Declared here in case general_utility.h has not exposed the prototype yet.
void tile_double_scroll(void* userData, rdpq_texparms_t *tp, rdpq_tile_t tile);

/*
 * Comparison/testing scene.
 *
 * This scene owns only environment models and a scene-local camera-pan cutscene.
 * It does not spawn the Guardian boss, character, collision, UI, or dialog.
 * That keeps the memory/performance comparison focused on the loaded room set.
 *
 * Runtime behavior:
 *   - Defaults to the original environment.
 *   - Starts a looping camera-pan cutscene immediately.
 *   - Press A to cycle original -> test -> optimized -> optimized (frozen) ->
 *     optimized (no mipmaps) -> optimized (no mipmaps, frozen) ->
 *     no materials -> decimated frozen -> original, unloading and reloading only the
 *     environment while preserving cutscene position.
 *   - Press B to cycle in the opposite direction.
 *   - While the scene is running in freecam, A/B cycle environments without
 *     moving the camera vertically. The paused dev menu still owns A/B for movement.
 *   - Press Start to restart the camera-pan cutscene from the beginning.
 *   - Press Z to pause/resume cutscene time.
 *   - Extra FX remain disabled so they do not affect the environment comparison.
 *   - Only the selected environment is loaded at any time.
 *   - Test, optimized, optimized (no mipmaps), no-material, and decimated frozen have
 *     independent object storage, load/delete functions, and draw functions.
 *     The two frozen optimized modes reuse their corresponding optimized
 *     storage because only one environment is loaded at a time.
 *   - Each comparison state loads only the models in its active draw list.
 *     Models whose draws are disabled are not loaded and contribute no materials.
 *   - The original environment's sunshafts use double texture scrolling.
 *   - Optimized uses
 *     the combined test-floor-opt.t3dm, test-walls-opt.t3dm,
 *     test-walls_back-opt.t3dm, test-ceiling-opt.t3dm,
 *     and test-pillars-opt.t3dm.
 *   - The optimized floor materials named "floor", "floor_ornate",
 *     "floor_debris_pile2", and "carpet_border" each use their own scene-local
 *     three-texture custom mip chain.
 *   - The "baseboard" material in test-walls-opt and test-walls_back-opt uses
 *     the shared baseboard8 three-texture chain. The "walls" material uses
 *     Tiny3D's normal texture-loading path with no custom mipmapping. Each wall
 *     model gets its own recorded draw list; shared wall-model mip sprite data
 *     is loaded only once.
 *   - Both wall models give "door_pillar" a three-texture door_detail_pillar4
 *     chain and "door_detail_top" a two-texture door_detail_top3 chain.
 *     test-walls-opt additionally gives "door" a three-texture door6 chain;
 *     that chain is not bound to test-walls_back-opt.
 *   - Each source texture has an explicit first RDP level. Intermediate RDP
 *     levels reuse the most recently uploaded texture without using more TMEM.
 *   - CI mip chains keep source 0's palette active, so every manually authored
 *     CI source must use the same indexed palette and palette ordering.
 *   - Optimized (no mipmaps) loads the exact same combined -opt meshes as
 *     optimized, but with plain draw lists and no custom mip chains bound, so
 *     each material just uses Tiny3D's normal single-level texture loading.
 *   - Frozen optimized modes use the same meshes and material behavior as
 *     their regular counterparts, but static display lists are recorded as
 *     frozen blocks after the live draw-pass RDP state has been established.
 *   - No-materials uses the same substituted mesh set with the -nomat suffix.
 *   - Decimated frozen uses the same substituted mesh set with the -solid suffix,
 *     but records every static model as a frozen block.
 *   - Bloom and ambient dust particles are disabled for every comparison state.
 *   - Test only additionally loads a ceiling ring, wall niches/windows, and
 *     two decal layers. The ceiling ring draws with the rest of the ceiling
 *     pass. The niches/windows draw last among the depth-tested geometry with
 *     depth test+write enabled, then both decal layers draw on top with depth
 *     test+write disabled (decal layer 1 first, decal layer 2 last).
 */

#define TESTING_SCENE_PATH_PREFIX "rom:/boss_room/"

#define TESTING_SCENE_OPTIMIZED_MAX_MIP_TEXTURE_COUNT 3
#define TESTING_SCENE_OPTIMIZED_FLOOR_MIP_CHAIN_COUNT 4
#define TESTING_SCENE_OPTIMIZED_WALLS_MIP_CHAIN_COUNT 4
#define TESTING_SCENE_OPTIMIZED_WALLS_BACK_MIP_CHAIN_COUNT 3
#define TESTING_SCENE_OPTIMIZED_MAX_MIP_CHAIN_COUNT 4

_Static_assert(
    TESTING_SCENE_OPTIMIZED_FLOOR_MIP_CHAIN_COUNT
        <= TESTING_SCENE_OPTIMIZED_MAX_MIP_CHAIN_COUNT
        && TESTING_SCENE_OPTIMIZED_WALLS_MIP_CHAIN_COUNT
            <= TESTING_SCENE_OPTIMIZED_MAX_MIP_CHAIN_COUNT
        && TESTING_SCENE_OPTIMIZED_WALLS_BACK_MIP_CHAIN_COUNT
            <= TESTING_SCENE_OPTIMIZED_WALLS_MIP_CHAIN_COUNT,
    "optimized mip binding storage is too small"
);

/*
 * RDP level assignment:
 *   0, 1       -> full-resolution source
 *   2, 3       -> half-resolution source
 *   4, 5, 6, 7 -> quarter-resolution source
 *
 * This is the default assignment. Individual chains can override their source
 * start levels below. The carpet uses { 0, 2, 3 }, so its quarter-resolution
 * source begins immediately after its single half-resolution level.
 *
 * The total level count can be at most eight because the RDP exposes TILE0
 * through TILE7.
 */
#define TESTING_SCENE_OPTIMIZED_MIP1_START_RDP_LEVEL 2
#define TESTING_SCENE_OPTIMIZED_MIP2_START_RDP_LEVEL 4
#define TESTING_SCENE_OPTIMIZED_MIP_RDP_LEVEL_COUNT 8

_Static_assert(
    TESTING_SCENE_OPTIMIZED_MIP_RDP_LEVEL_COUNT >= 3
        && TESTING_SCENE_OPTIMIZED_MIP_RDP_LEVEL_COUNT <= 8,
    "optimized mipmapping requires between 3 and 8 RDP levels"
);

_Static_assert(
    TESTING_SCENE_OPTIMIZED_MIP1_START_RDP_LEVEL > 0
        && TESTING_SCENE_OPTIMIZED_MIP1_START_RDP_LEVEL
            < TESTING_SCENE_OPTIMIZED_MIP2_START_RDP_LEVEL
        && TESTING_SCENE_OPTIMIZED_MIP2_START_RDP_LEVEL
            < TESTING_SCENE_OPTIMIZED_MIP_RDP_LEVEL_COUNT,
    "optimized mip start levels must be ordered and inside the RDP chain"
);

typedef struct TestingSceneOptimizedMipChain {
    const char *chainName;
    const char *materialName;
    uint8_t sourceTextureCount;
    const char *texturePaths[
        TESTING_SCENE_OPTIMIZED_MAX_MIP_TEXTURE_COUNT
    ];
    uint8_t sourceStartRdpLevels[
        TESTING_SCENE_OPTIMIZED_MAX_MIP_TEXTURE_COUNT
    ];
    sprite_t *sprites[
        TESTING_SCENE_OPTIMIZED_MAX_MIP_TEXTURE_COUNT
    ];
    bool ready;
} TestingSceneOptimizedMipChain;

static TestingSceneOptimizedMipChain s_optimizedFloorMipChains[
    TESTING_SCENE_OPTIMIZED_FLOOR_MIP_CHAIN_COUNT
] = {
    {
        .chainName = "floor6",
        .materialName = "floor",
        .sourceTextureCount = 3,
        /* Source 0 uses the material's own textureA.texPath. */
        .texturePaths = {
            NULL,
            TESTING_SCENE_PATH_PREFIX "floor6-mip1.i4.sprite",
            TESTING_SCENE_PATH_PREFIX "floor6-mip2.i4.sprite",
        },
        .sourceStartRdpLevels = {
            0,
            TESTING_SCENE_OPTIMIZED_MIP1_START_RDP_LEVEL,
            TESTING_SCENE_OPTIMIZED_MIP2_START_RDP_LEVEL,
        },
    },
    {
        .chainName = "floor_ornate11",
        .materialName = "floor_ornate",
        .sourceTextureCount = 3,
        /* Source 0 uses floor_ornate11.i4.sprite from the material. */
        .texturePaths = {
            NULL,
            TESTING_SCENE_PATH_PREFIX "floor_ornate11-mip1.i4.sprite",
            TESTING_SCENE_PATH_PREFIX "floor_ornate11-mip2.i4.sprite",
        },
        .sourceStartRdpLevels = {
            0,
            TESTING_SCENE_OPTIMIZED_MIP1_START_RDP_LEVEL,
            TESTING_SCENE_OPTIMIZED_MIP2_START_RDP_LEVEL,
        },
    },
    {
        .chainName = "floor_debris_pile5",
        .materialName = "floor_debris_pile2",
        .sourceTextureCount = 3,
        /* Debris uses its explicitly renamed I4 source at all three stages. */
        .texturePaths = {
            TESTING_SCENE_PATH_PREFIX "floor_debris_pile5.i4.sprite",
            TESTING_SCENE_PATH_PREFIX "floor_debris_pile5-mip1.i4.sprite",
            TESTING_SCENE_PATH_PREFIX "floor_debris_pile5-mip2.i4.sprite",
        },
        .sourceStartRdpLevels = {
            0,
            TESTING_SCENE_OPTIMIZED_MIP1_START_RDP_LEVEL,
            TESTING_SCENE_OPTIMIZED_MIP2_START_RDP_LEVEL,
        },
    },
    {
        .chainName = "carpet_border8",
        .materialName = "carpet_border",
        .sourceTextureCount = 3,
        /* CI8 mip levels use source 0's palette. */
        .texturePaths = {
            TESTING_SCENE_PATH_PREFIX "carpet_border8.ci8.sprite",
            TESTING_SCENE_PATH_PREFIX "carpet_border8-mip1.ci8.sprite",
            TESTING_SCENE_PATH_PREFIX "carpet_border8-mip2.ci8.sprite",
        },
        /* Do not repeat mip1: mip2 starts on the immediately following level. */
        .sourceStartRdpLevels = {
            0,
            TESTING_SCENE_OPTIMIZED_MIP1_START_RDP_LEVEL,
            TESTING_SCENE_OPTIMIZED_MIP1_START_RDP_LEVEL + 1,
        },
    },
};

static TestingSceneOptimizedMipChain s_optimizedWallsMipChains[
    TESTING_SCENE_OPTIMIZED_WALLS_MIP_CHAIN_COUNT
] = {
    {
        .chainName = "baseboard8",
        .materialName = "baseboard",
        .sourceTextureCount = 3,
        .texturePaths = {
            TESTING_SCENE_PATH_PREFIX "baseboard8.i4.sprite",
            TESTING_SCENE_PATH_PREFIX "baseboard8-mip1.i4.sprite",
            TESTING_SCENE_PATH_PREFIX "baseboard8-mip2.i4.sprite",
        },
        /* Keep mip1 at level 2, but start mip2 on the following level. */
        .sourceStartRdpLevels = {
            0,
            TESTING_SCENE_OPTIMIZED_MIP1_START_RDP_LEVEL,
            TESTING_SCENE_OPTIMIZED_MIP1_START_RDP_LEVEL + 1,
        },
    },
    {
        .chainName = "door_detail_top3",
        /* This 80x48 source matches the door_detail_top material. */
        .materialName = "door_detail_top",
        .sourceTextureCount = 2,
        .texturePaths = {
            TESTING_SCENE_PATH_PREFIX "door_detail_top3.i4.sprite",
            TESTING_SCENE_PATH_PREFIX "door_detail_top3-mip1.i4.sprite",
        },
        /* Switch to the only authored mip at the earliest possible level. */
        .sourceStartRdpLevels = {
            0,
            2,
        },
    },
    {
        .chainName = "door_detail_pillar4",
        /* This 32x128 source matches the door_pillar material. */
        .materialName = "door_pillar",
        .sourceTextureCount = 3,
        .texturePaths = {
            TESTING_SCENE_PATH_PREFIX "door_detail_pillar4.i4.sprite",
            TESTING_SCENE_PATH_PREFIX "door_detail_pillar4-mip1.i4.sprite",
            TESTING_SCENE_PATH_PREFIX "door_detail_pillar4-mip2.i4.sprite",
        },
        /* Earliest possible transitions: base -> mip1 -> mip2. */
        .sourceStartRdpLevels = {
            0,
            1,
            3,
        },
    },
    {
        .chainName = "door6",
        .materialName = "door",
        .sourceTextureCount = 3,
        .texturePaths = {
            TESTING_SCENE_PATH_PREFIX "door6.i4.sprite",
            TESTING_SCENE_PATH_PREFIX "door6-mip1.i4.sprite",
            TESTING_SCENE_PATH_PREFIX "door6-mip2.i4.sprite",
        },
        /* Earliest possible transitions: base -> mip1 -> mip2. */
        .sourceStartRdpLevels = {
            0,
            2,
            3,
        },
    },
};

/*
 * Nearest-level mipmapping does not consume a combiner cycle, so it is safe
 * with the optimized materials' existing two-cycle combiners.
 * MIPMAP_INTERPOLATE would require each affected material to use one pass.
 */
static const rdpq_mipmap_t TESTING_SCENE_OPTIMIZED_MIP_MODE = MIPMAP_NEAREST;

typedef enum TestingSceneEnvironment {
    TESTING_SCENE_ENVIRONMENT_ORIGINAL = 0,
    TESTING_SCENE_ENVIRONMENT_TEST,
    TESTING_SCENE_ENVIRONMENT_OPTIMIZED,
    TESTING_SCENE_ENVIRONMENT_OPTIMIZED_FROZEN,
    TESTING_SCENE_ENVIRONMENT_OPTIMIZED_NO_MIPS,
    TESTING_SCENE_ENVIRONMENT_OPTIMIZED_NO_MIPS_FROZEN,
    TESTING_SCENE_ENVIRONMENT_NO_MATERIALS,
    TESTING_SCENE_ENVIRONMENT_DECIMATED_FROZEN,
    TESTING_SCENE_ENVIRONMENT_COUNT,
} TestingSceneEnvironment;

static const float TESTING_SCENE_ROOM_Y = -1.0f;
static const uint8_t TESTING_SCENE_BLOOM_ALPHA = 20;

/* Must match freecam's A/B vertical speed in camera_controller.c. */
static const float TESTING_SCENE_FREECAM_VERTICAL_SPEED = 60.0f;

// Keep the development label inside typical 320x240 CRT overscan.
static const float TESTING_SCENE_LABEL_X = 24.0f;
static const float TESTING_SCENE_LABEL_Y = 30.0f;

/*
 * Extra upward camera/target lift applied only during the last half
 * of TESTING_SCENE_CUTSCENE_DOOR_SHOT.
 */
static const float TESTING_SCENE_DOOR_SHOT_UPWARD_OFFSET = 80.0f;

static TestingSceneEnvironment s_requestedEnvironment = TESTING_SCENE_ENVIRONMENT_ORIGINAL;
static TestingSceneEnvironment s_loadedEnvironment = TESTING_SCENE_ENVIRONMENT_COUNT;

static ScrollParams s_fogScrollParams = {
    .xSpeed = 0.0f,
    .ySpeed = 10.0f,
    .scale  = 64,
};

static ScrollParams s_sunshaftsScrollParams = {
    // TILE0: main/streak texture.
    .xSpeed = -3.0f,
    .ySpeed = -3.0f,
    .scale  = 32,

    // TILE1: secondary/dust texture.
    .xSpeedTwo = -0.0f,
    .ySpeedTwo = -8.0f,
    .scaleTwo  = 32,
};

typedef struct TestingSceneObject {
    const char *path;
    T3DModel *model;
    rspq_block_t *dpl;
    T3DMat4FP *matrix;
    bool drawCustom;
    bool doubleScroll;
    bool frozen;
    bool optimizedMipDraw;
    ScrollParams *scrollParams;
    TestingSceneOptimizedMipChain *mipChains;
    T3DMaterial *boundMipMaterials[
        TESTING_SCENE_OPTIMIZED_MAX_MIP_CHAIN_COUNT
    ];
    uint8_t mipChainCount;
} TestingSceneObject;

typedef struct TestingSceneVec3 {
    float x;
    float y;
    float z;
} TestingSceneVec3;

typedef enum TestingSceneCutsceneState {
    TESTING_SCENE_CUTSCENE_STATUE_SHOT = 0,

    /*
     * First half of the old other-corner shot.
     * Timeline starts this at 8 seconds.
     */
    TESTING_SCENE_CUTSCENE_OTHER_CORNER,

    TESTING_SCENE_CUTSCENE_BROKEN_STATUE_SHOT,
    TESTING_SCENE_CUTSCENE_STATUE_FACE,
    TESTING_SCENE_CUTSCENE_DOOR_SHOT,

    /*
     * This was the old final shot and now happens after door shot.
     */
    TESTING_SCENE_CUTSCENE_WALLS_PAN,
    TESTING_SCENE_CUTSCENE_WIDE_INTRO,
    TESTING_SCENE_CUTSCENE_FLOOR_PASS,

    TESTING_SCENE_CUTSCENE_BACK_VIEW,

    /*
     * Second half of the old other-corner shot.
     */
    TESTING_SCENE_CUTSCENE_OTHER_CORNER_SECOND_HALF,

    /*
     * First shot moved to the end of the loop.
     */
    TESTING_SCENE_CUTSCENE_WINDOW_SHOT,

    TESTING_SCENE_CUTSCENE_COUNT,
} TestingSceneCutsceneState;

typedef enum TestingSceneCameraEaseMode {
    TESTING_SCENE_CAMERA_EASE_LINEAR = 0,
    TESTING_SCENE_CAMERA_EASE_IN_OUT,
    TESTING_SCENE_CAMERA_EASE_OUT,
} TestingSceneCameraEaseMode;

// ------------------------------------------------------------
// Scene-local cutscene state
// ------------------------------------------------------------

static TestingSceneCutsceneState s_cutsceneState = TESTING_SCENE_CUTSCENE_STATUE_SHOT;
static float s_cutsceneTimer = 0.0f;
static float s_cutsceneCameraTimer = 0.0f;
static bool s_cutscenePaused = false;
static bool s_lastAHeld = false;
static bool s_lastBHeld = false;
static bool s_lastStartHeld = false;
static bool s_lastZHeld = false;
static bool s_testingSceneBloomEnabled = true;
static bool s_freecamViewNeedsRefresh = false;

static TestingSceneVec3 s_cutsceneCamPosStart;
static TestingSceneVec3 s_cutsceneCamPosEnd;
static TestingSceneVec3 s_cutsceneCamTargetStart;
static TestingSceneVec3 s_cutsceneCamTargetEnd;

// ------------------------------------------------------------
// Test environment objects
// ------------------------------------------------------------

static TestingSceneObject s_testFloor;
static TestingSceneObject s_testFloorTiles;
static TestingSceneObject s_testWalls;
static TestingSceneObject s_testCeiling;
static TestingSceneObject s_testCeilingRing;
static TestingSceneObject s_testWallsBack;
static TestingSceneObject s_testPillars;
static TestingSceneObject s_testFogDoor;
static TestingSceneObject s_testNichesWindows;
static TestingSceneObject s_testDecalsLayer1;
static TestingSceneObject s_testDecalsLayer2;

// ------------------------------------------------------------
// Optimized environment objects
// ------------------------------------------------------------

static TestingSceneObject s_optimizedFloor;
static TestingSceneObject s_optimizedWalls;
static TestingSceneObject s_optimizedCeiling;
static TestingSceneObject s_optimizedWallsBack;
static TestingSceneObject s_optimizedPillars;
static TestingSceneObject s_optimizedFogDoor;

// ------------------------------------------------------------
// Optimized (no mipmaps) environment objects
// ------------------------------------------------------------

static TestingSceneObject s_optimizedNoMipsFloor;
static TestingSceneObject s_optimizedNoMipsWalls;
static TestingSceneObject s_optimizedNoMipsCeiling;
static TestingSceneObject s_optimizedNoMipsWallsBack;
static TestingSceneObject s_optimizedNoMipsPillars;
static TestingSceneObject s_optimizedNoMipsFogDoor;

// ------------------------------------------------------------
// No-material environment objects
// ------------------------------------------------------------

static TestingSceneObject s_noMaterialsFloor;
static TestingSceneObject s_noMaterialsFloorTiles;
static TestingSceneObject s_noMaterialsWalls;
static TestingSceneObject s_noMaterialsCeiling;
static TestingSceneObject s_noMaterialsWallsBack;
static TestingSceneObject s_noMaterialsPillars;
static TestingSceneObject s_noMaterialsFogDoor;

// ------------------------------------------------------------
// Decimated frozen environment objects
// ------------------------------------------------------------

static TestingSceneObject s_decimatedFrozenFloor;
static TestingSceneObject s_decimatedFrozenFloorTiles;
static TestingSceneObject s_decimatedFrozenWalls;
static TestingSceneObject s_decimatedFrozenCeiling;
static TestingSceneObject s_decimatedFrozenWallsBack;
static TestingSceneObject s_decimatedFrozenPillars;
static TestingSceneObject s_decimatedFrozenFogDoor;

// ------------------------------------------------------------
// Original environment objects
// ------------------------------------------------------------

static TestingSceneObject s_originalRoom;
static TestingSceneObject s_originalFloor;
static TestingSceneObject s_originalRoomLedgeWalls;
static TestingSceneObject s_originalPillars;
static TestingSceneObject s_originalPillarsFront;
static TestingSceneObject s_originalFog;
static TestingSceneObject s_originalSunshafts;
static TestingSceneObject s_originalWindows;

static void testing_scene_cycle_requested_environment_forward(void);
static void testing_scene_cycle_requested_environment_backward(void);
static void testing_scene_apply_extra_fx_for_loaded_environment(void);

// ------------------------------------------------------------
// Cutscene helpers
// ------------------------------------------------------------

static float testing_scene_clampf(float value, float min, float max)
{
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static float testing_scene_smoothstep(float t)
{
    t = testing_scene_clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static float testing_scene_ease_out(float t)
{
    t = testing_scene_clampf(t, 0.0f, 1.0f);

    /*
     * Quadratic ease-out:
     * starts immediately, slows into the end.
     */
    return 1.0f - ((1.0f - t) * (1.0f - t));
}

static TestingSceneVec3 testing_scene_vec3_lerp(TestingSceneVec3 a, TestingSceneVec3 b, float t)
{
    TestingSceneVec3 out = {
        .x = a.x + (b.x - a.x) * t,
        .y = a.y + (b.y - a.y) * t,
        .z = a.z + (b.z - a.z) * t,
    };

    return out;
}

static T3DVec3 testing_scene_to_t3d_vec3(TestingSceneVec3 v)
{
    return (T3DVec3){{ v.x, v.y, v.z }};
}

static TestingSceneCameraEaseMode testing_scene_get_camera_ease_mode(TestingSceneCutsceneState state)
{
    switch (state) {
        /*
         * Old shots keep the original smoothstep camera motion.
         */
        case TESTING_SCENE_CUTSCENE_FLOOR_PASS:
        case TESTING_SCENE_CUTSCENE_WIDE_INTRO:
        case TESTING_SCENE_CUTSCENE_WALLS_PAN:
            return TESTING_SCENE_CAMERA_EASE_IN_OUT;

        /*
         * Door shot is a new shot, but should ease in/out like before.
         */
        case TESTING_SCENE_CUTSCENE_DOOR_SHOT:
            return TESTING_SCENE_CAMERA_EASE_IN_OUT;

        /*
         * Statue face/head shot should ease out only.
         */
        case TESTING_SCENE_CUTSCENE_STATUE_FACE:
            return TESTING_SCENE_CAMERA_EASE_OUT;

        /*
         * Other new shots stay linear.
         */
        case TESTING_SCENE_CUTSCENE_WINDOW_SHOT:
        case TESTING_SCENE_CUTSCENE_STATUE_SHOT:
        case TESTING_SCENE_CUTSCENE_BROKEN_STATUE_SHOT:
        case TESTING_SCENE_CUTSCENE_OTHER_CORNER:
        case TESTING_SCENE_CUTSCENE_OTHER_CORNER_SECOND_HALF:
        case TESTING_SCENE_CUTSCENE_BACK_VIEW:
        default:
            return TESTING_SCENE_CAMERA_EASE_LINEAR;
    }
}

static float testing_scene_apply_camera_ease(TestingSceneCutsceneState state, float rawT)
{
    rawT = testing_scene_clampf(rawT, 0.0f, 1.0f);

    switch (testing_scene_get_camera_ease_mode(state)) {
        case TESTING_SCENE_CAMERA_EASE_IN_OUT:
            return testing_scene_smoothstep(rawT);

        case TESTING_SCENE_CAMERA_EASE_OUT:
            return testing_scene_ease_out(rawT);

        case TESTING_SCENE_CAMERA_EASE_LINEAR:
        default:
            return rawT;
    }
}

/*
 * The cinematic keeps advancing and updating customCamPos/customCamTarget
 * while freecam is active. It must not take ownership of the live camera
 * until the dev tools restore CAMERA_CUSTOM.
 */
static void testing_scene_activate_cinematic_camera_if_allowed(void)
{
    if (cameraState == CAMERA_FREECAM) {
        return;
    }

    camera_mode(CAMERA_CUSTOM);
}

static void testing_scene_set_camera_shot(
    TestingSceneVec3 posStart,
    TestingSceneVec3 posEnd,
    TestingSceneVec3 targetStart,
    TestingSceneVec3 targetEnd
) {
    s_cutsceneCamPosStart = posStart;
    s_cutsceneCamPosEnd = posEnd;
    s_cutsceneCamTargetStart = targetStart;
    s_cutsceneCamTargetEnd = targetEnd;

    customCamPos = testing_scene_to_t3d_vec3(posStart);
    customCamTarget = testing_scene_to_t3d_vec3(targetStart);
}

static void testing_scene_enter_cutscene_state(TestingSceneCutsceneState state)
{
    testing_scene_activate_cinematic_camera_if_allowed();

    switch (state) {
        case TESTING_SCENE_CUTSCENE_WINDOW_SHOT:
            testing_scene_set_camera_shot(
                (TestingSceneVec3){ 225.0f, 107.0f, -157.0f },
                (TestingSceneVec3){ 114.56f, 213.30f, -367.42f },
                (TestingSceneVec3){ 182.5f, 148.28f, -237.97f },
                (TestingSceneVec3){ 72.0f, 253.94f, -448.31f }
            );
            break;

        case TESTING_SCENE_CUTSCENE_STATUE_SHOT:
            testing_scene_set_camera_shot(
                (TestingSceneVec3){ 167.72f, 45.67f, -456.46f },
                (TestingSceneVec3){ 387.62f, 117.48f, -388.32f },
                (TestingSceneVec3){ 258.90f, 75.45f, -428.20f },
                (TestingSceneVec3){ 478.81f, 147.26f, -360.0f }
            );
            break;

        case TESTING_SCENE_CUTSCENE_BROKEN_STATUE_SHOT:
            testing_scene_set_camera_shot(
                (TestingSceneVec3){ 171.85f, 38.54f, -232.24f },
                (TestingSceneVec3){ 32.37f, 38.54f, -300.63f },
                (TestingSceneVec3){ 214.0f, 66.91f, -318.34f },
                (TestingSceneVec3){ 74.59f, 66.91f, -386.73f }
            );
            break;

        case TESTING_SCENE_CUTSCENE_STATUE_FACE:
            testing_scene_set_camera_shot(
                (TestingSceneVec3){ -215.85f, 18.97f, -502.74f },
                (TestingSceneVec3){ -202.71f, 18.98f, -538.25f },
                (TestingSceneVec3){ -122.71f, 30.66f, -468.27f },
                (TestingSceneVec3){ -109.56f, 30.66f, -503.78f }
            );
            break;

        case TESTING_SCENE_CUTSCENE_DOOR_SHOT:
            testing_scene_set_camera_shot(
                (TestingSceneVec3){ -551.84f, 51.99f, 1.84f },
                (TestingSceneVec3){ -117.58f, 76.7666f, -4.8f },
                (TestingSceneVec3){ -452.0f, 57.68f, 0.3f },
                (TestingSceneVec3){ -17.75f, 82.46f, -6.33f }
            );
            break;

        case TESTING_SCENE_CUTSCENE_FLOOR_PASS:
            testing_scene_set_camera_shot(
                (TestingSceneVec3){ -120.0f,  65.0f, 130.0f },
                (TestingSceneVec3){  120.0f,  65.0f, 130.0f },
                (TestingSceneVec3){    0.0f,  25.0f, -20.0f },
                (TestingSceneVec3){    0.0f,  25.0f, -20.0f }
            );
            break;

        case TESTING_SCENE_CUTSCENE_OTHER_CORNER:
            /*
             * First half of the other-corner shot.
             */
            testing_scene_set_camera_shot(
                (TestingSceneVec3){ -124.04f, 102.73f, 270.2f },
                (TestingSceneVec3){ -4.40f, 102.73f, 118.43f },
                (TestingSceneVec3){ -48.78f, 131.32f, 329.52f },
                (TestingSceneVec3){ 70.855f, 131.32f, 177.75f }
            );
            break;

        case TESTING_SCENE_CUTSCENE_OTHER_CORNER_SECOND_HALF:
            /*
             * Second half of the other-corner shot.
             */
            testing_scene_set_camera_shot(
                (TestingSceneVec3){ -4.40f, 102.73f, 118.43f },
                (TestingSceneVec3){ 115.24f, 102.73f, -33.34f },
                (TestingSceneVec3){ 70.855f, 131.32f, 177.75f },
                (TestingSceneVec3){ 190.49f, 131.32f, 25.98f }
            );
            break;

        case TESTING_SCENE_CUTSCENE_BACK_VIEW:
            /*
             * Start this shot from about halfway through its original camera move.
             * Original start was:
             *   camPos    {-64.54f, 33.24f,  11.56f}
             *   camTarget {-133.24f, 67.25f, -52.65f}
             */
            testing_scene_set_camera_shot(
                (TestingSceneVec3){ -153.265f, 33.24f, 106.485f },
                (TestingSceneVec3){ -241.99f, 33.24f, 201.41f },
                (TestingSceneVec3){ -221.965f, 67.25f, 42.275f },
                (TestingSceneVec3){ -310.69f, 67.25f, 137.2f }
            );
            break;

        case TESTING_SCENE_CUTSCENE_WIDE_INTRO:
            testing_scene_set_camera_shot(
                (TestingSceneVec3){ -420.0f, 115.0f, 310.0f },
                (TestingSceneVec3){ -180.0f, 125.0f, 250.0f },
                (TestingSceneVec3){    0.0f,  45.0f,   0.0f },
                (TestingSceneVec3){   30.0f,  55.0f, -35.0f }
            );
            break;

        case TESTING_SCENE_CUTSCENE_WALLS_PAN:
            testing_scene_set_camera_shot(
                (TestingSceneVec3){  230.0f, 175.0f, 220.0f },
                (TestingSceneVec3){  230.0f, 175.0f, -90.0f },
                (TestingSceneVec3){    0.0f,  55.0f,   0.0f },
                (TestingSceneVec3){  -60.0f,  45.0f,   0.0f }
            );
            break;

        default:
            break;
    }
}

static void testing_scene_next_cutscene_state(TestingSceneCutsceneState nextState)
{
    s_cutsceneTimer = 0.0f;
    s_cutsceneCameraTimer = 0.0f;
    s_cutsceneState = nextState;

    testing_scene_enter_cutscene_state(nextState);
}

static void testing_scene_start_autoplay_cutscene(void)
{
    s_cutsceneTimer = 0.0f;
    s_cutsceneCameraTimer = 0.0f;
    s_cutsceneState = TESTING_SCENE_CUTSCENE_STATUE_SHOT;
    s_cutscenePaused = false;
    s_lastZHeld = joypad.btn.z;

    testing_scene_enter_cutscene_state(s_cutsceneState);
}

static float testing_scene_get_cutscene_state_duration(TestingSceneCutsceneState state)
{
    switch (state) {
        /*
         * Timeline cut points:
         *
         * 0:00  STATUE_SHOT
         * 0:08  OTHER_CORNER first half
         * 0:15  BROKEN_STATUE_SHOT
         * 0:22  STATUE_FACE
         * 0:39  DOOR_SHOT
         * 0:52  WALLS_PAN
         * 0:59  WIDE_INTRO
         * 1:06  FLOOR_PASS
         * 1:11  BACK_VIEW
         * 1:18  OTHER_CORNER second half
         * 1:25  WINDOW_SHOT
         *
         * WINDOW_SHOT keeps its prior 12-second duration because no next cut
         * timestamp was provided after 1:25.
         */
        case TESTING_SCENE_CUTSCENE_STATUE_SHOT:               return 8.0f;
        case TESTING_SCENE_CUTSCENE_OTHER_CORNER:              return 7.0f;
        case TESTING_SCENE_CUTSCENE_BROKEN_STATUE_SHOT:        return 7.0f;
        case TESTING_SCENE_CUTSCENE_STATUE_FACE:               return 17.0f;
        case TESTING_SCENE_CUTSCENE_DOOR_SHOT:                 return 13.0f;
        case TESTING_SCENE_CUTSCENE_WALLS_PAN:                 return 7.0f;
        case TESTING_SCENE_CUTSCENE_WIDE_INTRO:                return 7.0f;
        case TESTING_SCENE_CUTSCENE_FLOOR_PASS:                return 5.0f;
        case TESTING_SCENE_CUTSCENE_BACK_VIEW:                 return 7.0f;
        case TESTING_SCENE_CUTSCENE_OTHER_CORNER_SECOND_HALF:  return 7.0f;
        case TESTING_SCENE_CUTSCENE_WINDOW_SHOT:               return 12.0f;

        default:                                               return 0.0f;
    }
}

static void testing_scene_update_cutscene_camera(float duration)
{
    float rawT = 1.0f;

    if (duration > 0.0f) {
        rawT = s_cutsceneCameraTimer / duration;
    }

    rawT = testing_scene_clampf(rawT, 0.0f, 1.0f);

    /*
     * Most new shots are linear.
     * Old shots ease in/out.
     * Door shot also eases in/out.
     * Statue face/head shot eases out only.
     */
    float cameraT = testing_scene_apply_camera_ease(s_cutsceneState, rawT);

    TestingSceneVec3 camPos = testing_scene_vec3_lerp(
        s_cutsceneCamPosStart,
        s_cutsceneCamPosEnd,
        cameraT
    );

    TestingSceneVec3 camTarget = testing_scene_vec3_lerp(
        s_cutsceneCamTargetStart,
        s_cutsceneCamTargetEnd,
        cameraT
    );

    if (s_cutsceneState == TESTING_SCENE_CUTSCENE_DOOR_SHOT) {
        /*
         * Door shot eases in/out for the base camera move.
         *
         * The upward crane starts in the last half and also eases in/out,
         * matching the previous behavior.
         */
        float upwardT = testing_scene_clampf((rawT - 0.5f) * 2.0f, 0.0f, 1.0f);
        upwardT = testing_scene_smoothstep(upwardT);

        float upwardOffset = upwardT * TESTING_SCENE_DOOR_SHOT_UPWARD_OFFSET;

        camPos.y += upwardOffset;
        camTarget.y += upwardOffset;
    }

    customCamPos = testing_scene_to_t3d_vec3(camPos);
    customCamTarget = testing_scene_to_t3d_vec3(camTarget);
}

static TestingSceneCutsceneState testing_scene_get_next_cutscene_state(TestingSceneCutsceneState state)
{
    switch (state) {
        case TESTING_SCENE_CUTSCENE_STATUE_SHOT:
            return TESTING_SCENE_CUTSCENE_OTHER_CORNER;

        case TESTING_SCENE_CUTSCENE_OTHER_CORNER:
            return TESTING_SCENE_CUTSCENE_BROKEN_STATUE_SHOT;

        case TESTING_SCENE_CUTSCENE_BROKEN_STATUE_SHOT:
            return TESTING_SCENE_CUTSCENE_STATUE_FACE;

        case TESTING_SCENE_CUTSCENE_STATUE_FACE:
            return TESTING_SCENE_CUTSCENE_DOOR_SHOT;

        case TESTING_SCENE_CUTSCENE_DOOR_SHOT:
            return TESTING_SCENE_CUTSCENE_WALLS_PAN;

        case TESTING_SCENE_CUTSCENE_WALLS_PAN:
            return TESTING_SCENE_CUTSCENE_WIDE_INTRO;

        case TESTING_SCENE_CUTSCENE_WIDE_INTRO:
            return TESTING_SCENE_CUTSCENE_FLOOR_PASS;

        case TESTING_SCENE_CUTSCENE_FLOOR_PASS:
            return TESTING_SCENE_CUTSCENE_BACK_VIEW;

        case TESTING_SCENE_CUTSCENE_BACK_VIEW:
            return TESTING_SCENE_CUTSCENE_OTHER_CORNER_SECOND_HALF;

        case TESTING_SCENE_CUTSCENE_OTHER_CORNER_SECOND_HALF:
            return TESTING_SCENE_CUTSCENE_WINDOW_SHOT;

        case TESTING_SCENE_CUTSCENE_WINDOW_SHOT:
        default:
            return TESTING_SCENE_CUTSCENE_STATUE_SHOT;
    }
}

static void testing_scene_update_cutscene(void)
{
    float duration = testing_scene_get_cutscene_state_duration(s_cutsceneState);

    if (s_cutscenePaused) {
        testing_scene_update_cutscene_camera(duration);
        return;
    }

    s_cutsceneTimer += deltaTime;
    s_cutsceneCameraTimer += deltaTime;

    testing_scene_update_cutscene_camera(duration);

    if (s_cutsceneTimer >= duration) {
        testing_scene_next_cutscene_state(
            testing_scene_get_next_cutscene_state(s_cutsceneState)
        );
    }
}

static void testing_scene_draw_cutscene_fog(void)
{
    switch (s_cutsceneState) {
        case TESTING_SCENE_CUTSCENE_WINDOW_SHOT:
        case TESTING_SCENE_CUTSCENE_STATUE_SHOT:
        case TESTING_SCENE_CUTSCENE_BROKEN_STATUE_SHOT:
        case TESTING_SCENE_CUTSCENE_STATUE_FACE:
        case TESTING_SCENE_CUTSCENE_DOOR_SHOT:
        case TESTING_SCENE_CUTSCENE_FLOOR_PASS:
        case TESTING_SCENE_CUTSCENE_OTHER_CORNER:
        case TESTING_SCENE_CUTSCENE_OTHER_CORNER_SECOND_HALF:
        case TESTING_SCENE_CUTSCENE_BACK_VIEW:
        case TESTING_SCENE_CUTSCENE_WIDE_INTRO:
        case TESTING_SCENE_CUTSCENE_WALLS_PAN:
        default:
            t3d_fog_set_range(300.0f, 700.0f);
            break;
    }
}

static bool testing_scene_should_draw_sunshafts(void)
{
    switch (s_cutsceneState) {
        case TESTING_SCENE_CUTSCENE_BROKEN_STATUE_SHOT:
        case TESTING_SCENE_CUTSCENE_STATUE_FACE:
            return false;

        default:
            return true;
    }
}

// ------------------------------------------------------------
// Environment object helpers
// ------------------------------------------------------------

static void testing_scene_object_clear(TestingSceneObject *object)
{
    if (!object) return;

    object->path = NULL;
    object->model = NULL;
    object->dpl = NULL;
    object->matrix = NULL;
    object->drawCustom = false;
    object->doubleScroll = false;
    object->frozen = false;
    object->optimizedMipDraw = false;
    object->scrollParams = NULL;
    object->mipChains = NULL;
    object->mipChainCount = 0;
    memset(
        object->boundMipMaterials,
        0,
        sizeof(object->boundMipMaterials)
    );
}

static void testing_scene_clear_all_object_handles(void)
{
    testing_scene_object_clear(&s_testFloor);
    testing_scene_object_clear(&s_testFloorTiles);
    testing_scene_object_clear(&s_testWalls);
    testing_scene_object_clear(&s_testCeiling);
    testing_scene_object_clear(&s_testCeilingRing);
    testing_scene_object_clear(&s_testWallsBack);
    testing_scene_object_clear(&s_testPillars);
    testing_scene_object_clear(&s_testFogDoor);
    testing_scene_object_clear(&s_testNichesWindows);
    testing_scene_object_clear(&s_testDecalsLayer1);
    testing_scene_object_clear(&s_testDecalsLayer2);

    testing_scene_object_clear(&s_optimizedFloor);
    testing_scene_object_clear(&s_optimizedWalls);
    testing_scene_object_clear(&s_optimizedCeiling);
    testing_scene_object_clear(&s_optimizedWallsBack);
    testing_scene_object_clear(&s_optimizedPillars);
    testing_scene_object_clear(&s_optimizedFogDoor);

    testing_scene_object_clear(&s_optimizedNoMipsFloor);
    testing_scene_object_clear(&s_optimizedNoMipsWalls);
    testing_scene_object_clear(&s_optimizedNoMipsCeiling);
    testing_scene_object_clear(&s_optimizedNoMipsWallsBack);
    testing_scene_object_clear(&s_optimizedNoMipsPillars);
    testing_scene_object_clear(&s_optimizedNoMipsFogDoor);

    testing_scene_object_clear(&s_noMaterialsFloor);
    testing_scene_object_clear(&s_noMaterialsFloorTiles);
    testing_scene_object_clear(&s_noMaterialsWalls);
    testing_scene_object_clear(&s_noMaterialsCeiling);
    testing_scene_object_clear(&s_noMaterialsWallsBack);
    testing_scene_object_clear(&s_noMaterialsPillars);
    testing_scene_object_clear(&s_noMaterialsFogDoor);

    testing_scene_object_clear(&s_decimatedFrozenFloor);
    testing_scene_object_clear(&s_decimatedFrozenFloorTiles);
    testing_scene_object_clear(&s_decimatedFrozenWalls);
    testing_scene_object_clear(&s_decimatedFrozenCeiling);
    testing_scene_object_clear(&s_decimatedFrozenWallsBack);
    testing_scene_object_clear(&s_decimatedFrozenPillars);
    testing_scene_object_clear(&s_decimatedFrozenFogDoor);

    testing_scene_object_clear(&s_originalRoom);
    testing_scene_object_clear(&s_originalFloor);
    testing_scene_object_clear(&s_originalRoomLedgeWalls);
    testing_scene_object_clear(&s_originalPillars);
    testing_scene_object_clear(&s_originalPillarsFront);
    testing_scene_object_clear(&s_originalFog);
    testing_scene_object_clear(&s_originalSunshafts);
    testing_scene_object_clear(&s_originalWindows);
}

static void testing_scene_object_free(TestingSceneObject *object)
{
    if (!object) return;

    if (object->dpl) {
        rspq_block_free(object->dpl);
        object->dpl = NULL;
    }

    if (object->model) {
        t3d_model_free(object->model);
        object->model = NULL;
    }

    if (object->matrix) {
        free_uncached(object->matrix);
        object->matrix = NULL;
    }

    object->path = NULL;
    object->drawCustom = false;
    object->doubleScroll = false;
    object->frozen = false;
    object->optimizedMipDraw = false;
    object->scrollParams = NULL;
    object->mipChains = NULL;
    object->mipChainCount = 0;
    memset(
        object->boundMipMaterials,
        0,
        sizeof(object->boundMipMaterials)
    );
}

static bool testing_scene_object_load_internal(
    TestingSceneObject *object,
    const char *path,
    bool drawCustom,
    ScrollParams *scrollParams,
    bool frozen
) {
    if (!object || !path) return false;

    object->path = path;
    object->drawCustom = drawCustom;
    object->doubleScroll = false;
    object->frozen = frozen && !drawCustom;
    object->optimizedMipDraw = false;
    object->scrollParams = scrollParams;
    object->mipChains = NULL;
    object->mipChainCount = 0;
    memset(
        object->boundMipMaterials,
        0,
        sizeof(object->boundMipMaterials)
    );

    object->model = t3d_model_load(path);
    if (!object->model) {
        debugf("testing_scene: failed to load model: %s\n", path);
        return false;
    }

    /*
     * Regular blocks can be recorded while loading because they resolve their
     * RDP commands at playback. Frozen blocks must wait until the draw pass has
     * established the exact live RDP state they will use.
     */
    if (!drawCustom && !object->frozen) {
        rspq_block_begin();
            t3d_model_draw(object->model);
        object->dpl = rspq_block_end();
    }

    object->matrix = malloc_uncached(sizeof(T3DMat4FP));
    if (!object->matrix) {
        debugf("testing_scene: failed to allocate matrix: %s\n", path);
        testing_scene_object_free(object);
        return false;
    }

    t3d_mat4fp_from_srt_euler(
        object->matrix,
        (float[3]){ MODEL_SCALE, MODEL_SCALE, MODEL_SCALE },
        (float[3]){ 0.0f, 0.0f, 0.0f },
        (float[3]){ 0.0f, TESTING_SCENE_ROOM_Y, 0.0f }
    );

    return true;
}

static bool testing_scene_object_load(
    TestingSceneObject *object,
    const char *path,
    bool drawCustom,
    ScrollParams *scrollParams
) {
    return testing_scene_object_load_internal(
        object,
        path,
        drawCustom,
        scrollParams,
        false
    );
}

static bool testing_scene_object_load_frozen(
    TestingSceneObject *object,
    const char *path
) {
    return testing_scene_object_load_internal(
        object,
        path,
        false,
        NULL,
        true
    );
}

static bool testing_scene_object_load_static(
    TestingSceneObject *object,
    const char *path,
    bool frozen
) {
    return frozen
        ? testing_scene_object_load_frozen(object, path)
        : testing_scene_object_load(object, path, false, NULL);
}

static bool testing_scene_object_load_double_scroll(
    TestingSceneObject *object,
    const char *path,
    ScrollParams *scrollParams
) {
    bool loaded = testing_scene_object_load(object, path, true, scrollParams);
    if (!loaded) return false;

    object->doubleScroll = true;
    return true;
}

static void testing_scene_free_optimized_mip_chain_sprites(
    TestingSceneOptimizedMipChain *chain
)
{
    if (!chain) return;

    for (int sourceIndex = 0;
         sourceIndex < TESTING_SCENE_OPTIMIZED_MAX_MIP_TEXTURE_COUNT;
         ++sourceIndex
    ) {
        if (chain->sprites[sourceIndex]) {
            sprite_free(chain->sprites[sourceIndex]);
            chain->sprites[sourceIndex] = NULL;
        }
    }

    chain->ready = false;
}

static void testing_scene_free_optimized_mip_chains(
    TestingSceneOptimizedMipChain *chains,
    int chainCount
)
{
    if (!chains || chainCount <= 0) return;

    for (int chainIndex = 0; chainIndex < chainCount; ++chainIndex) {
        testing_scene_free_optimized_mip_chain_sprites(&chains[chainIndex]);
    }
}

static bool testing_scene_optimized_mip_chain_uses_palette(
    const TestingSceneOptimizedMipChain *chain
) {
    if (!chain || !chain->sprites[0]) return false;

    tex_format_t format = sprite_get_format(chain->sprites[0]);
    return format == FMT_CI4 || format == FMT_CI8;
}

static bool testing_scene_is_power_of_two_u16(uint16_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

/* Match the tile settings Tiny3D would derive from the original material. */
static rdpq_texparms_t testing_scene_get_material_texture_params(
    const T3DMaterialTexture *texture
) {
    rdpq_texparms_t params = (rdpq_texparms_t){};

    params.s.translate = texture->s.low;
    params.s.mirror = texture->s.mirror;
    params.s.repeats = REPEAT_INFINITE;
    params.s.scale_log = (int)texture->s.shift;

    if (texture->s.clamp) {
        params.s.repeats = testing_scene_is_power_of_two_u16(texture->texWidth)
            ? (texture->s.height - texture->s.low + 1.0f) / (float)texture->texWidth
            : 1.0f;
    }

    params.t.translate = texture->t.low;
    params.t.mirror = texture->t.mirror;
    params.t.repeats = REPEAT_INFINITE;
    params.t.scale_log = (int)texture->t.shift;

    if (texture->t.clamp) {
        params.t.repeats = testing_scene_is_power_of_two_u16(texture->texHeight)
            ? (texture->t.height - texture->t.low + 1.0f) / (float)texture->texHeight
            : 1.0f;
    }

    return params;
}

static bool testing_scene_load_optimized_mip_chain(
    TestingSceneOptimizedMipChain *chain,
    const T3DMaterial *material
) {
    if (!chain || !material) return false;

    testing_scene_free_optimized_mip_chain_sprites(chain);

    if (chain->sourceTextureCount < 2
        || chain->sourceTextureCount
            > TESTING_SCENE_OPTIMIZED_MAX_MIP_TEXTURE_COUNT
    ) {
        debugf(
            "testing_scene: mip chain '%s' has invalid source texture count %d\n",
            chain->chainName,
            chain->sourceTextureCount
        );
        return false;
    }

    if (chain->sourceStartRdpLevels[0] != 0) {
        debugf(
            "testing_scene: mip chain '%s' source 0 must start at RDP level 0\n",
            chain->chainName
        );
        return false;
    }

    for (int sourceIndex = 1;
         sourceIndex < chain->sourceTextureCount;
         ++sourceIndex
    ) {
        uint8_t previousStart =
            chain->sourceStartRdpLevels[sourceIndex - 1];
        uint8_t currentStart =
            chain->sourceStartRdpLevels[sourceIndex];

        if (currentStart <= previousStart
            || currentStart >= TESTING_SCENE_OPTIMIZED_MIP_RDP_LEVEL_COUNT
        ) {
            debugf(
                "testing_scene: mip chain '%s' has invalid source %d "
                "start level %d after level %d\n",
                chain->chainName,
                sourceIndex,
                currentStart,
                previousStart
            );
            return false;
        }
    }

    if ((int)material->textureA.s.shift
            + chain->sourceTextureCount - 1 >= 11
        || (int)material->textureA.t.shift
            + chain->sourceTextureCount - 1 >= 11
    ) {
        debugf(
            "testing_scene: mip chain '%s' leaves no texture shift room for %d mip textures\n",
            chain->chainName,
            chain->sourceTextureCount
        );
        return false;
    }

    for (int sourceIndex = 0;
         sourceIndex < chain->sourceTextureCount;
         ++sourceIndex
    ) {
        const char *path = chain->texturePaths[sourceIndex]
            ? chain->texturePaths[sourceIndex]
            : material->textureA.texPath;

        if (!path) {
            debugf(
                "testing_scene: mip chain '%s' source %d has no texture path\n",
                chain->chainName,
                sourceIndex
            );
            testing_scene_free_optimized_mip_chain_sprites(chain);
            return false;
        }

        chain->sprites[sourceIndex] = sprite_load(path);

        if (!chain->sprites[sourceIndex]) {
            debugf(
                "testing_scene: failed to load mip chain '%s' source %d: %s\n",
                chain->chainName,
                sourceIndex,
                path
            );
            testing_scene_free_optimized_mip_chain_sprites(chain);
            return false;
        }
    }

    sprite_t *base = chain->sprites[0];
    tex_format_t baseFormat = sprite_get_format(base);

    if (base->width != material->textureA.texWidth
        || base->height != material->textureA.texHeight
    ) {
        debugf(
            "testing_scene: mip chain '%s' base is %dx%d; model expects %dx%d\n",
            chain->chainName,
            base->width,
            base->height,
            material->textureA.texWidth,
            material->textureA.texHeight
        );
        testing_scene_free_optimized_mip_chain_sprites(chain);
        return false;
    }

    if ((baseFormat == FMT_CI4 || baseFormat == FMT_CI8)
        && !sprite_get_palette(base)
    ) {
        debugf(
            "testing_scene: CI mip chain '%s' source 0 has no palette\n",
            chain->chainName
        );
        testing_scene_free_optimized_mip_chain_sprites(chain);
        return false;
    }

    for (int sourceIndex = 1;
         sourceIndex < chain->sourceTextureCount;
         ++sourceIndex
    ) {
        sprite_t *previous = chain->sprites[sourceIndex - 1];
        sprite_t *current = chain->sprites[sourceIndex];
        uint16_t expectedWidth = previous->width > 1
            ? previous->width / 2
            : 1;
        uint16_t expectedHeight = previous->height > 1
            ? previous->height / 2
            : 1;

        if (current->width != expectedWidth || current->height != expectedHeight) {
            debugf(
                "testing_scene: mip chain '%s' source %d is %dx%d; expected %dx%d\n",
                chain->chainName,
                sourceIndex,
                current->width,
                current->height,
                expectedWidth,
                expectedHeight
            );
            testing_scene_free_optimized_mip_chain_sprites(chain);
            return false;
        }

        if (sprite_get_format(current) != baseFormat) {
            debugf(
                "testing_scene: mip chain '%s' source %d format differs from source 0\n",
                chain->chainName,
                sourceIndex
            );
            testing_scene_free_optimized_mip_chain_sprites(chain);
            return false;
        }
    }

    chain->ready = true;
    return true;
}

/*
 * A loaded sprite chain may be shared by several models, but every model owns
 * different T3DMaterial instances. Verify that the shared base sprite still
 * matches the material metadata before recording that model's draw list.
 */
static bool testing_scene_optimized_mip_chain_matches_material(
    const TestingSceneOptimizedMipChain *chain,
    const T3DMaterial *material
) {
    if (!chain || !chain->ready || !chain->sprites[0] || !material) {
        return false;
    }

    const sprite_t *base = chain->sprites[0];

    if (base->width != material->textureA.texWidth
        || base->height != material->textureA.texHeight
    ) {
        debugf(
            "testing_scene: shared mip chain '%s' base is %dx%d; "
            "model material expects %dx%d\n",
            chain->chainName,
            base->width,
            base->height,
            material->textureA.texWidth,
            material->textureA.texHeight
        );
        return false;
    }

    if ((int)material->textureA.s.shift
            + chain->sourceTextureCount - 1 >= 11
        || (int)material->textureA.t.shift
            + chain->sourceTextureCount - 1 >= 11
    ) {
        debugf(
            "testing_scene: shared mip chain '%s' leaves no texture shift "
            "room for this material\n",
            chain->chainName
        );
        return false;
    }

    return true;
}

static void testing_scene_upload_optimized_mip_chain(
    const TestingSceneOptimizedMipChain *chain,
    const T3DMaterial *material
) {
    rdpq_texparms_t sourceParams =
        testing_scene_get_material_texture_params(&material->textureA);
    bool usesPalette =
        testing_scene_optimized_mip_chain_uses_palette(chain);
    int sourceIndex = 0;

    rdpq_tex_multi_begin();

    for (int rdpLevel = 0;
         rdpLevel < TESTING_SCENE_OPTIMIZED_MIP_RDP_LEVEL_COUNT;
         ++rdpLevel
    ) {
        bool startsNextSource =
            sourceIndex + 1 < chain->sourceTextureCount
            && rdpLevel
                == chain->sourceStartRdpLevels[sourceIndex + 1];

        if (startsNextSource) {
            ++sourceIndex;

            /* Each source texture is half the preceding source's dimensions. */
            ++sourceParams.s.scale_log;
            ++sourceParams.t.scale_log;
            sourceParams.s.translate *= 0.5f;
            sourceParams.t.translate *= 0.5f;
        }

        rdpq_tile_t tile = (rdpq_tile_t)(TILE0 + rdpLevel);

        if (rdpLevel == 0) {
            /* Upload source 0 normally so a CI chain installs its palette. */
            rdpq_sprite_upload(
                tile,
                chain->sprites[sourceIndex],
                &sourceParams
            );
        } else if (startsNextSource) {
            if (usesPalette) {
                /*
                 * Every CI mip level must use source 0's palette. Upload only
                 * these index surfaces so their private palettes cannot replace
                 * the base palette in the RDP's single CI8 TLUT.
                 */
                surface_t mipPixels =
                    sprite_get_pixels(chain->sprites[sourceIndex]);
                rdpq_tex_upload(tile, &mipPixels, &sourceParams);
            } else {
                rdpq_sprite_upload(
                    tile,
                    chain->sprites[sourceIndex],
                    &sourceParams
                );
            }
        } else {
            /* Point this level at the previous upload without using more TMEM. */
            rdpq_tex_reuse(tile, &sourceParams);
        }
    }

    rdpq_tex_multi_end();
}

static TestingSceneOptimizedMipChain *testing_scene_find_optimized_mip_chain(
    T3DMaterial *material,
    TestingSceneOptimizedMipChain *chains,
    T3DMaterial *const *boundMaterials,
    int chainCount
)
{
    if (!material || !chains || !boundMaterials || chainCount <= 0) {
        return NULL;
    }

    for (int chainIndex = 0; chainIndex < chainCount; ++chainIndex) {
        TestingSceneOptimizedMipChain *chain = &chains[chainIndex];

        if (chain->ready && boundMaterials[chainIndex] == material) {
            return chain;
        }
    }

    return NULL;
}

/*
 * Record one optimized model manually. Objects using a configured mip material
 * receive its texture chain; every other material keeps Tiny3D's normal state
 * minimization and automatic texture loading.
 */
static void testing_scene_record_optimized_model_with_mipmaps(
    const T3DModel *model,
    TestingSceneOptimizedMipChain *chains,
    T3DMaterial *const *boundMaterials,
    int chainCount
) {
    T3DModelState state = t3d_model_state_create();
    T3DModelIter iterator = t3d_model_iter_create(
        model,
        T3D_CHUNK_TYPE_OBJECT
    );
    TestingSceneOptimizedMipChain *activeMipChain = NULL;
    T3DMaterial *activeMipMaterial = NULL;

    while (t3d_model_iter_next(&iterator)) {
        T3DObject *modelObject = iterator.object;
        T3DMaterial *material = modelObject->material;

        if (material) {
            TestingSceneOptimizedMipChain *requestedMipChain =
                testing_scene_find_optimized_mip_chain(
                    material,
                    chains,
                    boundMaterials,
                    chainCount
                );

            if (requestedMipChain && requestedMipChain != activeMipChain) {
                testing_scene_upload_optimized_mip_chain(
                    requestedMipChain,
                    material
                );
                rdpq_mode_mipmap(
                    TESTING_SCENE_OPTIMIZED_MIP_MODE,
                    TESTING_SCENE_OPTIMIZED_MIP_RDP_LEVEL_COUNT
                );

                /*
                 * The custom chain is already in TMEM. Pretend this material's
                 * ordinary texture is current so t3d_model_draw_material()
                 * applies every other setting without overwriting the chain.
                 */
                state.lastTextureHashA = material->textureA.textureHash;
                state.lastTextureHashB = material->textureB.textureHash;

                /*
                 * Force Tiny3D to rebuild its triangle command after enabling
                 * mipmapping; the command captures the RDP LOD state.
                 */
                state.lastRenderFlags = ~material->renderFlags;
                activeMipChain = requestedMipChain;
                activeMipMaterial = material;
            } else if (!requestedMipChain && activeMipChain) {
                rdpq_mode_mipmap(MIPMAP_NONE, 0);

                if (testing_scene_optimized_mip_chain_uses_palette(
                        activeMipChain
                    )
                ) {
                    rdpq_mode_tlut(TLUT_NONE);
                }

                /* Rebuild Tiny3D's triangle command for ordinary materials. */
                state.lastRenderFlags = ~material->renderFlags;
                activeMipChain = NULL;
                activeMipMaterial = NULL;
            }

            t3d_model_draw_material(material, &state);
        }

        t3d_model_draw_object(modelObject, NULL);
    }

    if (activeMipChain) {
        /* Do not leak mipmap or palette state past the optimized model. */
        rdpq_mode_mipmap(MIPMAP_NONE, 0);

        if (testing_scene_optimized_mip_chain_uses_palette(
                activeMipChain
            )
        ) {
            rdpq_mode_tlut(TLUT_NONE);
        }

        t3d_state_set_drawflags(activeMipMaterial->renderFlags);
    }

    if (state.lastVertFXFunc != T3D_VERTEX_FX_NONE) {
        t3d_state_set_vertex_fx(T3D_VERTEX_FX_NONE, 0, 0);
    }
}

static void testing_scene_record_object_commands(
    const TestingSceneObject *object
) {
    if (!object || !object->model) return;

    if (object->optimizedMipDraw) {
        testing_scene_record_optimized_model_with_mipmaps(
            object->model,
            object->mipChains,
            object->boundMipMaterials,
            object->mipChainCount
        );
        return;
    }

    t3d_model_draw(object->model);
}

static void testing_scene_record_regular_object_display_list(
    TestingSceneObject *object
) {
    if (!object || !object->model) return;

    rspq_block_begin();
        testing_scene_record_object_commands(object);
    object->dpl = rspq_block_end();
    object->drawCustom = false;
}

static bool testing_scene_record_frozen_object_display_list(
    TestingSceneObject *object
) {
    if (!object || !object->model) return false;

    /*
     * The old block may still be referenced by queued RSP/RDP work. Free it
     * only after that work has completed, then record into a fresh allocation.
     * This avoids a synchronous rspq_wait() stall when a block becomes stale.
     */
    if (object->dpl) {
        rdpq_call_deferred(
            (void (*)(void *))rspq_block_free,
            object->dpl
        );
        object->dpl = NULL;
    }

    rspq_block_begin_frozen(NULL);
        testing_scene_record_object_commands(object);
    object->dpl = rspq_block_end_frozen();
    object->drawCustom = false;

    return object->dpl != NULL;
}

static void testing_scene_log_frozen_stale_reasons(
    const TestingSceneObject *object,
    int reasons,
    const char *context
) {
    debugf(
        "testing_scene: frozen block %s for %s; reasons=%08lx",
        context,
        object && object->path ? object->path : "<unknown>",
        (unsigned long)(uint32_t)reasons
    );

    for (int bitIndex = 0; bitIndex < 32; ++bitIndex) {
        uint32_t bit = 1U << bitIndex;

        if (((uint32_t)reasons & bit) != 0) {
            debugf(" %s", rdpq_block_stale_reason_str((int)bit));
        }
    }

    debugf("\n");
}

static bool testing_scene_object_load_optimized_mip_model(
    TestingSceneObject *object,
    const char *modelPath,
    const char *modelName,
    TestingSceneOptimizedMipChain *chains,
    int chainCount,
    bool resetMipSprites,
    bool frozen
)
{
    if (!object || !modelPath || !modelName || !chains || chainCount <= 0) {
        return false;
    }

    if (chainCount > TESTING_SCENE_OPTIMIZED_MAX_MIP_CHAIN_COUNT) {
        debugf(
            "testing_scene: %s requests %d mip chains; maximum is %d\n",
            modelName,
            chainCount,
            TESTING_SCENE_OPTIMIZED_MAX_MIP_CHAIN_COUNT
        );
        return false;
    }

    if (resetMipSprites) {
        testing_scene_free_optimized_mip_chains(chains, chainCount);
    }

    bool loaded = testing_scene_object_load_internal(
        object,
        modelPath,
        true,
        NULL,
        frozen
    );

    if (!loaded) return false;

    int readyChainCount = 0;
    T3DMaterial *boundMaterials[
        TESTING_SCENE_OPTIMIZED_MAX_MIP_CHAIN_COUNT
    ] = { NULL };

    for (int chainIndex = 0; chainIndex < chainCount; ++chainIndex) {
        TestingSceneOptimizedMipChain *chain = &chains[chainIndex];
        T3DMaterial *material = t3d_model_get_material(
            object->model,
            chain->materialName
        );

        if (!material) {
            debugf(
                "testing_scene: material '%s' not found in %s; "
                "that material will use its normal draw\n",
                chain->materialName,
                modelName
            );
            continue;
        }

        if (material->textureB.texPath
            || material->textureB.texReference
        ) {
            debugf(
                "testing_scene: material '%s' in %s already uses TILE1; "
                "that material cannot use the custom mip chain\n",
                chain->materialName,
                modelName
            );
            continue;
        }

        if (!material->textureA.texPath) {
            debugf(
                "testing_scene: material '%s' in %s has no loadable "
                "textureA path; "
                "that material will use its normal draw\n",
                chain->materialName,
                modelName
            );
            continue;
        }

        bool chainAvailable = chain->ready
            ? testing_scene_optimized_mip_chain_matches_material(
                chain,
                material
            )
            : testing_scene_load_optimized_mip_chain(chain, material);

        if (!chainAvailable) {
            debugf(
                "testing_scene: material '%s' in %s custom mip chain '%s' "
                "is invalid; "
                "that material will use its normal draw\n",
                chain->materialName,
                modelName,
                chain->chainName
            );
            continue;
        }

        boundMaterials[chainIndex] = material;
        ++readyChainCount;
        debugf(
            "testing_scene: material '%s' in %s using mip chain '%s': "
            "%d textures across %d RDP mip levels\n",
            chain->materialName,
            modelName,
            chain->chainName,
            chain->sourceTextureCount,
            TESTING_SCENE_OPTIMIZED_MIP_RDP_LEVEL_COUNT
        );
    }

    object->drawCustom = false;
    object->frozen = frozen;
    object->optimizedMipDraw = readyChainCount > 0;
    object->mipChains = object->optimizedMipDraw ? chains : NULL;
    object->mipChainCount = object->optimizedMipDraw
        ? (uint8_t)chainCount
        : 0;
    memcpy(
        object->boundMipMaterials,
        boundMaterials,
        sizeof(object->boundMipMaterials)
    );

    if (readyChainCount == 0) {
        debugf(
            "testing_scene: no custom mip chains are available for %s; "
            "using the model's normal draw\n",
            modelName
        );
    }

    /*
     * Frozen display lists are recorded lazily by testing_scene_object_draw()
     * after the object's Z-buffer/pass state has been established.
     */
    if (!object->frozen) {
        testing_scene_record_regular_object_display_list(object);
    }

    return true;
}

static bool testing_scene_object_load_optimized_floor(bool frozen)
{
    return testing_scene_object_load_optimized_mip_model(
        &s_optimizedFloor,
        TESTING_SCENE_PATH_PREFIX "test-floor-opt.t3dm",
        "test-floor-opt",
        s_optimizedFloorMipChains,
        TESTING_SCENE_OPTIMIZED_FLOOR_MIP_CHAIN_COUNT,
        true,
        frozen
    );
}

static bool testing_scene_object_load_optimized_walls(bool frozen)
{
    return testing_scene_object_load_optimized_mip_model(
        &s_optimizedWalls,
        TESTING_SCENE_PATH_PREFIX "test-walls-opt.t3dm",
        "test-walls-opt",
        s_optimizedWallsMipChains,
        TESTING_SCENE_OPTIMIZED_WALLS_MIP_CHAIN_COUNT,
        false,
        frozen
    );
}

static bool testing_scene_object_load_optimized_walls_back(bool frozen)
{
    return testing_scene_object_load_optimized_mip_model(
        &s_optimizedWallsBack,
        TESTING_SCENE_PATH_PREFIX "test-walls_back-opt.t3dm",
        "test-walls_back-opt",
        s_optimizedWallsMipChains,
        TESTING_SCENE_OPTIMIZED_WALLS_BACK_MIP_CHAIN_COUNT,
        false,
        frozen
    );
}

static void testing_scene_object_draw(TestingSceneObject *object)
{
    if (!object || !object->matrix || !object->model) return;

    t3d_matrix_set(object->matrix, true);

    if (object->drawCustom) {
        t3d_model_draw_custom(object->model, (T3DModelDrawConf){
            .userData = object->scrollParams,
            .tileCb = object->scrollParams
                ? (object->doubleScroll ? tile_double_scroll : tile_scroll)
                : NULL,
        });
        return;
    }

    if (object->frozen) {
        if (rspq_block_run_frozen(object->dpl)) {
            return;
        }

        if (object->dpl) {
            testing_scene_log_frozen_stale_reasons(
                object,
                rdpq_block_stale_reasons(object->dpl),
                "stale"
            );
        }

        if (!testing_scene_record_frozen_object_display_list(object)) {
            debugf(
                "testing_scene: failed to record frozen block for %s\n",
                object->path ? object->path : "<unknown>"
            );
            return;
        }

        if (!rspq_block_run_frozen(object->dpl)) {
            int reasons = rdpq_block_stale_reasons(object->dpl);
            testing_scene_log_frozen_stale_reasons(
                object,
                reasons,
                "immediately stale after recording"
            );

            /*
             * Preserve the frame visually while exposing the state-tracking
             * failure in the debug log. This fallback should never be reached
             * in a stable pass.
             */
            testing_scene_record_object_commands(object);
        }

        return;
    }

    if (object->dpl) {
        rspq_block_run(object->dpl);
    }
}

// ------------------------------------------------------------
// Environment load/delete
// ------------------------------------------------------------

static void testing_scene_load_test_environment(void)
{
    /* Load only models used by testing_scene_draw_test_environment(). */
    testing_scene_object_load(&s_testFloor,          TESTING_SCENE_PATH_PREFIX "test-floor.t3dm",           false, NULL);
    testing_scene_object_load(&s_testFloorTiles,     TESTING_SCENE_PATH_PREFIX "test-floor_tiles.t3dm",     false, NULL);
    testing_scene_object_load(&s_testWalls,          TESTING_SCENE_PATH_PREFIX "test-walls.t3dm",           false, NULL);
    testing_scene_object_load(&s_testCeiling,        TESTING_SCENE_PATH_PREFIX "test-ceiling.t3dm",         false, NULL);
    testing_scene_object_load(&s_testCeilingRing,    TESTING_SCENE_PATH_PREFIX "test-ceiling_ring.t3dm",    false, NULL);
    testing_scene_object_load(&s_testWallsBack,      TESTING_SCENE_PATH_PREFIX "test-walls_back.t3dm",      false, NULL);
    testing_scene_object_load(&s_testPillars,        TESTING_SCENE_PATH_PREFIX "test-pillars.t3dm",         false, NULL);
    testing_scene_object_load(&s_testFogDoor,        TESTING_SCENE_PATH_PREFIX "test-fog_door.t3dm",        true,  &s_fogScrollParams);
    testing_scene_object_load(&s_testNichesWindows,  TESTING_SCENE_PATH_PREFIX "test-niches_windows.t3dm",  false, NULL);
    testing_scene_object_load(&s_testDecalsLayer1,   TESTING_SCENE_PATH_PREFIX "test-decals_layer1.t3dm",   false, NULL);
    testing_scene_object_load(&s_testDecalsLayer2,   TESTING_SCENE_PATH_PREFIX "test-decals_layer2.t3dm",   false, NULL);
}

static void testing_scene_load_optimized_environment(bool frozen)
{
    /* No test, no-material, or decimated-frozen mesh is loaded in this state. */
    testing_scene_object_load_optimized_floor(frozen);

    /*
     * Reset the shared wall sprite chains once, then bind and record each wall
     * model independently without reloading or invalidating those sprites.
     */
    testing_scene_free_optimized_mip_chains(
        s_optimizedWallsMipChains,
        TESTING_SCENE_OPTIMIZED_WALLS_MIP_CHAIN_COUNT
    );
    testing_scene_object_load_optimized_walls(frozen);
    testing_scene_object_load_optimized_walls_back(frozen);
    testing_scene_object_load_static(
        &s_optimizedCeiling,
        TESTING_SCENE_PATH_PREFIX "test-ceiling-opt.t3dm",
        frozen
    );
    testing_scene_object_load_static(
        &s_optimizedPillars,
        TESTING_SCENE_PATH_PREFIX "test-pillars-opt.t3dm",
        frozen
    );
    testing_scene_object_load(
        &s_optimizedFogDoor,
        TESTING_SCENE_PATH_PREFIX "test-fog_door.t3dm",
        true,
        &s_fogScrollParams
    );
}

static void testing_scene_load_optimized_no_mips_environment(bool frozen)
{
    /*
     * Use the exact optimized meshes with Tiny3D's normal single-level texture
     * loading. Frozen mode changes only how the static display lists execute.
     */
    testing_scene_object_load_static(
        &s_optimizedNoMipsFloor,
        TESTING_SCENE_PATH_PREFIX "test-floor-opt.t3dm",
        frozen
    );
    testing_scene_object_load_static(
        &s_optimizedNoMipsWalls,
        TESTING_SCENE_PATH_PREFIX "test-walls-opt.t3dm",
        frozen
    );
    testing_scene_object_load_static(
        &s_optimizedNoMipsWallsBack,
        TESTING_SCENE_PATH_PREFIX "test-walls_back-opt.t3dm",
        frozen
    );
    testing_scene_object_load_static(
        &s_optimizedNoMipsCeiling,
        TESTING_SCENE_PATH_PREFIX "test-ceiling-opt.t3dm",
        frozen
    );
    testing_scene_object_load_static(
        &s_optimizedNoMipsPillars,
        TESTING_SCENE_PATH_PREFIX "test-pillars-opt.t3dm",
        frozen
    );
    testing_scene_object_load(
        &s_optimizedNoMipsFogDoor,
        TESTING_SCENE_PATH_PREFIX "test-fog_door.t3dm",
        true,
        &s_fogScrollParams
    );
}

static void testing_scene_load_no_materials_environment(void)
{
    /* No test, optimized, or decimated-frozen mesh is loaded in this state. */
    testing_scene_object_load(&s_noMaterialsFloor,      TESTING_SCENE_PATH_PREFIX "test-floor-nomat.t3dm",       false, NULL);
    testing_scene_object_load(&s_noMaterialsFloorTiles, TESTING_SCENE_PATH_PREFIX "test-floor_tiles-nomat.t3dm", false, NULL);
    testing_scene_object_load(&s_noMaterialsWalls,      TESTING_SCENE_PATH_PREFIX "test-walls-nomat.t3dm",       false, NULL);
    testing_scene_object_load(&s_noMaterialsCeiling,    TESTING_SCENE_PATH_PREFIX "test-ceiling-nomat.t3dm",     false, NULL);
    testing_scene_object_load(&s_noMaterialsWallsBack,  TESTING_SCENE_PATH_PREFIX "test-walls_back-nomat.t3dm",  false, NULL);
    testing_scene_object_load(&s_noMaterialsPillars,    TESTING_SCENE_PATH_PREFIX "test-pillars-nomat.t3dm",     false, NULL);
    testing_scene_object_load(&s_noMaterialsFogDoor,    TESTING_SCENE_PATH_PREFIX "test-fog_door.t3dm",          true,  &s_fogScrollParams);
}

static void testing_scene_load_decimated_frozen_environment(void)
{
    /*
     * The decimated meshes retain their historical -solid filenames. Every
     * static model is recorded lazily as a frozen block after its draw-pass
     * RDP state is established. The scrolling fog door remains dynamic.
     */
    testing_scene_object_load_frozen(&s_decimatedFrozenFloor,      TESTING_SCENE_PATH_PREFIX "test-floor-solid.t3dm");
    testing_scene_object_load_frozen(&s_decimatedFrozenFloorTiles, TESTING_SCENE_PATH_PREFIX "test-floor_tiles-solid.t3dm");
    testing_scene_object_load_frozen(&s_decimatedFrozenWalls,      TESTING_SCENE_PATH_PREFIX "test-walls-solid.t3dm");
    testing_scene_object_load_frozen(&s_decimatedFrozenCeiling,    TESTING_SCENE_PATH_PREFIX "test-ceiling-solid.t3dm");
    testing_scene_object_load_frozen(&s_decimatedFrozenWallsBack,  TESTING_SCENE_PATH_PREFIX "test-walls_back-solid.t3dm");
    testing_scene_object_load_frozen(&s_decimatedFrozenPillars,    TESTING_SCENE_PATH_PREFIX "test-pillars-solid.t3dm");
    testing_scene_object_load(
        &s_decimatedFrozenFogDoor,
        TESTING_SCENE_PATH_PREFIX "test-fog_door.t3dm",
        true,
        &s_fogScrollParams
    );
}

static void testing_scene_load_original_environment(void)
{
    testing_scene_object_load(&s_originalRoom,           TESTING_SCENE_PATH_PREFIX "room.t3dm",             false, NULL);
    testing_scene_object_load(&s_originalFloor,          TESTING_SCENE_PATH_PREFIX "floor.t3dm",            false, NULL);
    testing_scene_object_load(&s_originalRoomLedgeWalls, TESTING_SCENE_PATH_PREFIX "room_ledge_walls.t3dm", false, NULL);
    testing_scene_object_load(&s_originalPillars,        TESTING_SCENE_PATH_PREFIX "pillars.t3dm",          false, NULL);
    testing_scene_object_load(&s_originalPillarsFront,   TESTING_SCENE_PATH_PREFIX "pillars_front.t3dm",    false, NULL);
    testing_scene_object_load(&s_originalFog,            TESTING_SCENE_PATH_PREFIX "fog.t3dm",              true,  &s_fogScrollParams);
    testing_scene_object_load_double_scroll(&s_originalSunshafts, TESTING_SCENE_PATH_PREFIX "sunshafts.t3dm", &s_sunshaftsScrollParams);
    testing_scene_object_load(&s_originalWindows,        TESTING_SCENE_PATH_PREFIX "windows.t3dm",          false, NULL);
}

static void testing_scene_delete_test_environment(void)
{
    testing_scene_object_free(&s_testFloor);
    testing_scene_object_free(&s_testFloorTiles);
    testing_scene_object_free(&s_testWalls);
    testing_scene_object_free(&s_testCeiling);
    testing_scene_object_free(&s_testCeilingRing);
    testing_scene_object_free(&s_testWallsBack);
    testing_scene_object_free(&s_testPillars);
    testing_scene_object_free(&s_testFogDoor);
    testing_scene_object_free(&s_testNichesWindows);
    testing_scene_object_free(&s_testDecalsLayer1);
    testing_scene_object_free(&s_testDecalsLayer2);
}

static void testing_scene_delete_optimized_environment(void)
{
    testing_scene_object_free(&s_optimizedFloor);
    testing_scene_free_optimized_mip_chains(
        s_optimizedFloorMipChains,
        TESTING_SCENE_OPTIMIZED_FLOOR_MIP_CHAIN_COUNT
    );
    /*
     * Both wall draw lists reference the shared baseboard/detail mip sprites;
     * the front wall list additionally references the door chain. Free both
     * display lists before releasing any of those sprites.
     */
    testing_scene_object_free(&s_optimizedWalls);
    testing_scene_object_free(&s_optimizedWallsBack);
    testing_scene_free_optimized_mip_chains(
        s_optimizedWallsMipChains,
        TESTING_SCENE_OPTIMIZED_WALLS_MIP_CHAIN_COUNT
    );
    testing_scene_object_free(&s_optimizedCeiling);
    testing_scene_object_free(&s_optimizedPillars);
    testing_scene_object_free(&s_optimizedFogDoor);
}

static void testing_scene_delete_optimized_no_mips_environment(void)
{
    testing_scene_object_free(&s_optimizedNoMipsFloor);
    testing_scene_object_free(&s_optimizedNoMipsWalls);
    testing_scene_object_free(&s_optimizedNoMipsWallsBack);
    testing_scene_object_free(&s_optimizedNoMipsCeiling);
    testing_scene_object_free(&s_optimizedNoMipsPillars);
    testing_scene_object_free(&s_optimizedNoMipsFogDoor);
}

static void testing_scene_delete_no_materials_environment(void)
{
    testing_scene_object_free(&s_noMaterialsFloor);
    testing_scene_object_free(&s_noMaterialsFloorTiles);
    testing_scene_object_free(&s_noMaterialsWalls);
    testing_scene_object_free(&s_noMaterialsCeiling);
    testing_scene_object_free(&s_noMaterialsWallsBack);
    testing_scene_object_free(&s_noMaterialsPillars);
    testing_scene_object_free(&s_noMaterialsFogDoor);
}

static void testing_scene_delete_decimated_frozen_environment(void)
{
    testing_scene_object_free(&s_decimatedFrozenFloor);
    testing_scene_object_free(&s_decimatedFrozenFloorTiles);
    testing_scene_object_free(&s_decimatedFrozenWalls);
    testing_scene_object_free(&s_decimatedFrozenCeiling);
    testing_scene_object_free(&s_decimatedFrozenWallsBack);
    testing_scene_object_free(&s_decimatedFrozenPillars);
    testing_scene_object_free(&s_decimatedFrozenFogDoor);
}

static void testing_scene_delete_original_environment(void)
{
    testing_scene_object_free(&s_originalRoom);
    testing_scene_object_free(&s_originalFloor);
    testing_scene_object_free(&s_originalRoomLedgeWalls);
    testing_scene_object_free(&s_originalPillars);
    testing_scene_object_free(&s_originalPillarsFront);
    testing_scene_object_free(&s_originalFog);
    testing_scene_object_free(&s_originalSunshafts);
    testing_scene_object_free(&s_originalWindows);
}

static void testing_scene_load_requested_environment(void)
{
    switch (s_requestedEnvironment) {
        case TESTING_SCENE_ENVIRONMENT_TEST:
            testing_scene_load_test_environment();
            s_loadedEnvironment = TESTING_SCENE_ENVIRONMENT_TEST;
            break;

        case TESTING_SCENE_ENVIRONMENT_OPTIMIZED:
            testing_scene_load_optimized_environment(false);
            s_loadedEnvironment = TESTING_SCENE_ENVIRONMENT_OPTIMIZED;
            break;

        case TESTING_SCENE_ENVIRONMENT_OPTIMIZED_FROZEN:
            testing_scene_load_optimized_environment(true);
            s_loadedEnvironment = TESTING_SCENE_ENVIRONMENT_OPTIMIZED_FROZEN;
            break;

        case TESTING_SCENE_ENVIRONMENT_OPTIMIZED_NO_MIPS:
            testing_scene_load_optimized_no_mips_environment(false);
            s_loadedEnvironment = TESTING_SCENE_ENVIRONMENT_OPTIMIZED_NO_MIPS;
            break;

        case TESTING_SCENE_ENVIRONMENT_OPTIMIZED_NO_MIPS_FROZEN:
            testing_scene_load_optimized_no_mips_environment(true);
            s_loadedEnvironment =
                TESTING_SCENE_ENVIRONMENT_OPTIMIZED_NO_MIPS_FROZEN;
            break;

        case TESTING_SCENE_ENVIRONMENT_NO_MATERIALS:
            testing_scene_load_no_materials_environment();
            s_loadedEnvironment = TESTING_SCENE_ENVIRONMENT_NO_MATERIALS;
            break;

        case TESTING_SCENE_ENVIRONMENT_DECIMATED_FROZEN:
            testing_scene_load_decimated_frozen_environment();
            s_loadedEnvironment = TESTING_SCENE_ENVIRONMENT_DECIMATED_FROZEN;
            break;

        case TESTING_SCENE_ENVIRONMENT_ORIGINAL:
            testing_scene_load_original_environment();
            s_loadedEnvironment = TESTING_SCENE_ENVIRONMENT_ORIGINAL;
            break;

        default:
            debugf("testing_scene: invalid requested environment %d; loading original environment\n", s_requestedEnvironment);
            s_requestedEnvironment = TESTING_SCENE_ENVIRONMENT_ORIGINAL;
            testing_scene_load_original_environment();
            s_loadedEnvironment = TESTING_SCENE_ENVIRONMENT_ORIGINAL;
            break;
    }

    testing_scene_apply_extra_fx_for_loaded_environment();
}

static void testing_scene_delete_loaded_environment(void)
{
    switch (s_loadedEnvironment) {
        case TESTING_SCENE_ENVIRONMENT_TEST:
            testing_scene_delete_test_environment();
            break;

        case TESTING_SCENE_ENVIRONMENT_OPTIMIZED:
        case TESTING_SCENE_ENVIRONMENT_OPTIMIZED_FROZEN:
            testing_scene_delete_optimized_environment();
            break;

        case TESTING_SCENE_ENVIRONMENT_OPTIMIZED_NO_MIPS:
        case TESTING_SCENE_ENVIRONMENT_OPTIMIZED_NO_MIPS_FROZEN:
            testing_scene_delete_optimized_no_mips_environment();
            break;

        case TESTING_SCENE_ENVIRONMENT_NO_MATERIALS:
            testing_scene_delete_no_materials_environment();
            break;

        case TESTING_SCENE_ENVIRONMENT_DECIMATED_FROZEN:
            testing_scene_delete_decimated_frozen_environment();
            break;

        case TESTING_SCENE_ENVIRONMENT_ORIGINAL:
            testing_scene_delete_original_environment();
            break;

        default:
            break;
    }

    s_loadedEnvironment = TESTING_SCENE_ENVIRONMENT_COUNT;
}

// ------------------------------------------------------------
// Scene setup/update
// ------------------------------------------------------------

static void testing_scene_setup_camera(void)
{
    camera_reset();
    cameraState = CAMERA_CUSTOM;
    lastCameraState = CAMERA_CUSTOM;
    camera_mode(CAMERA_CUSTOM);

    customCamPos = (T3DVec3){{ 0.0f, 120.0f, 360.0f }};
    customCamTarget = (T3DVec3){{ 0.0f, 45.0f, 0.0f }};
}

static void testing_scene_setup_lighting(void)
{
    game_lighting_initialize();

    colorAmbient[0] = 0xFF;
    colorAmbient[1] = 0xFF;
    colorAmbient[2] = 0xFF;
    colorAmbient[3] = 0xFF;

    //t3d_light_set_exposure(1.5f);
}

static bool testing_scene_should_use_extra_fx(void)
{
    // Extra FX are intentionally disabled for every comparison state.
    return false;
}

static const char *testing_scene_get_environment_name(TestingSceneEnvironment environment)
{
    switch (environment) {
        case TESTING_SCENE_ENVIRONMENT_ORIGINAL:
            return "original";

        case TESTING_SCENE_ENVIRONMENT_TEST:
            return "test";

        case TESTING_SCENE_ENVIRONMENT_OPTIMIZED:
            return "optimized (mipmaps)";

        case TESTING_SCENE_ENVIRONMENT_OPTIMIZED_FROZEN:
            return "optimized (mipmaps, frozen)";

        case TESTING_SCENE_ENVIRONMENT_OPTIMIZED_NO_MIPS:
            return "optimized (no mipmaps)";

        case TESTING_SCENE_ENVIRONMENT_OPTIMIZED_NO_MIPS_FROZEN:
            return "optimized (no mipmaps, frozen)";

        case TESTING_SCENE_ENVIRONMENT_NO_MATERIALS:
            return "no materials";

        case TESTING_SCENE_ENVIRONMENT_DECIMATED_FROZEN:
            return "decimated frozen";

        default:
            return "invalid";
    }
}

static void testing_scene_draw_environment_label(void)
{
    /*
     * Draw after this scene's 3D geometry and particles. The font is loaded and
     * registered once by main() under FONT_BUILTIN_DEBUG_MONO.
     */
    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_text_printf(
        NULL,
        FONT_BUILTIN_DEBUG_MONO,
        TESTING_SCENE_LABEL_X,
        TESTING_SCENE_LABEL_Y,
        "Environment: %s",
        testing_scene_get_environment_name(s_loadedEnvironment)
    );
}

static void testing_scene_apply_extra_fx_for_loaded_environment(void)
{
    bool extraFxEnabled = testing_scene_should_use_extra_fx();

    display_utility_set_bloom_enabled(
        extraFxEnabled && s_testingSceneBloomEnabled
    );

    dust_particles_fx_set_ambient_enabled(extraFxEnabled);

    if (!extraFxEnabled) {
        dust_particles_fx_reset();
    }

    debugf(
        "testing_scene: extra fx %s for %s environment\n",
        extraFxEnabled ? "enabled" : "disabled",
        testing_scene_get_environment_name(s_loadedEnvironment)
    );
}

static void testing_scene_set_bloom_enabled(bool enabled)
{
    s_testingSceneBloomEnabled = enabled;

    display_utility_set_bloom_enabled(
        testing_scene_should_use_extra_fx() && s_testingSceneBloomEnabled
    );

    debugf(
        "testing_scene: cheap bloom preference %s\n",
        s_testingSceneBloomEnabled ? "enabled" : "disabled"
    );
}

static void testing_scene_toggle_bloom(void)
{
    testing_scene_set_bloom_enabled(!s_testingSceneBloomEnabled);
}

static void testing_scene_update_test_environment(void)
{
    // Test-environment-only animation/debug hooks can go here.
}

static void testing_scene_update_original_environment(void)
{
    // Original-environment-only animation/debug hooks can go here.
}

static void testing_scene_reload_with_requested_environment(void)
{
    rspq_wait();

    testing_scene_delete_loaded_environment();
    testing_scene_load_requested_environment();
    testing_scene_reset();
}

/*
 * main updates the camera before it updates this scene. While the testing
 * scene is running, A/B belong to environment cycling, so cancel only the
 * vertical translation that freecam already applied this frame.
 *
 * The dev menu pauses scene updates. This function therefore does not run
 * while that menu is open, leaving A/B freecam movement unchanged there.
 */
static void testing_scene_cancel_freecam_cycle_button_movement(bool aHeld, bool bHeld)
{
    if (cameraState != CAMERA_FREECAM) {
        return;
    }

    float verticalOffset = 0.0f;

    if (bHeld) {
        verticalOffset += deltaTime * TESTING_SCENE_FREECAM_VERTICAL_SPEED;
    }

    if (aHeld) {
        verticalOffset -= deltaTime * TESTING_SCENE_FREECAM_VERTICAL_SPEED;
    }

    if (verticalOffset == 0.0f) {
        return;
    }

    camPos.v[1] -= verticalOffset;
    camTarget.v[1] -= verticalOffset;
    s_freecamViewNeedsRefresh = true;
}

static void testing_scene_update_scene_control_input(void)
{
    /*
     * Read the controller's actual held state here and create local rising-edge
     * events. This avoids depending on the shared `btn` pressed snapshot, which
     * does not reliably expose every button in this scene (notably Start).
     */
    bool aHeld = joypad.btn.a;
    bool bHeld = joypad.btn.b;
    bool startHeld = joypad.btn.start;

    testing_scene_cancel_freecam_cycle_button_movement(aHeld, bHeld);

    bool aJustPressed = aHeld && !s_lastAHeld;
    bool bJustPressed = bHeld && !s_lastBHeld;
    bool startJustPressed = startHeld && !s_lastStartHeld;

    s_lastAHeld = aHeld;
    s_lastBHeld = bHeld;
    s_lastStartHeld = startHeld;

    if (aJustPressed) {
        testing_scene_cycle_requested_environment_forward();
        testing_scene_reload_with_requested_environment();
        return;
    }

    if (bJustPressed) {
        testing_scene_cycle_requested_environment_backward();
        testing_scene_reload_with_requested_environment();
        return;
    }

    if (startJustPressed) {
        testing_scene_start_autoplay_cutscene();
    }

    if (btn.c_up && testing_scene_should_use_extra_fx()) {
        testing_scene_toggle_bloom();
        testing_scene_apply_extra_fx_for_loaded_environment();
    }

    bool zHeld = joypad.btn.z;
    bool zJustPressed = zHeld && !s_lastZHeld;
    s_lastZHeld = zHeld;

    if (zJustPressed) {
        s_cutscenePaused = !s_cutscenePaused;
    }
}

// ------------------------------------------------------------
// Draw
// ------------------------------------------------------------

static void testing_scene_draw_test_environment(void)
{
    t3d_matrix_push_pos(1);

        rdpq_mode_zbuf(false, false);
        testing_scene_object_draw(&s_testWallsBack);
        testing_scene_object_draw(&s_testWalls);
        testing_scene_object_draw(&s_testCeiling);
        //testing_scene_object_draw(&s_testCeilingRing);

        rdpq_mode_zbuf(true, true);
        testing_scene_object_draw(&s_testFloor);
        testing_scene_object_draw(&s_testFloorTiles);
        testing_scene_object_draw(&s_testPillars);

        rdpq_mode_zbuf(true, false);
        testing_scene_object_draw(&s_testFogDoor);

        // // Niches/windows draw last among the depth-tested geometry.
        // rdpq_mode_zbuf(true, true);
        // testing_scene_object_draw(&s_testNichesWindows);
        //
        // // Decal layers draw on top with depth test/write disabled, layer2 last.
        // rdpq_mode_zbuf(false, false);
        // testing_scene_object_draw(&s_testDecalsLayer1);
        // testing_scene_object_draw(&s_testDecalsLayer2);

        rdpq_mode_zbuf(true, true);

    t3d_matrix_pop(1);
}

static void testing_scene_draw_optimized_environment(void)
{
    t3d_matrix_push_pos(1);

        rdpq_mode_zbuf(false, false);
        testing_scene_object_draw(&s_optimizedWallsBack);
        testing_scene_object_draw(&s_optimizedWalls);
        testing_scene_object_draw(&s_optimizedCeiling);

        rdpq_mode_zbuf(true, true);
        testing_scene_object_draw(&s_optimizedFloor);
        testing_scene_object_draw(&s_optimizedPillars);

        rdpq_mode_zbuf(true, false);
        testing_scene_object_draw(&s_optimizedFogDoor);

        rdpq_mode_zbuf(true, true);

    t3d_matrix_pop(1);
}

static void testing_scene_draw_optimized_no_mips_environment(void)
{
    t3d_matrix_push_pos(1);

        rdpq_mode_zbuf(false, false);
        testing_scene_object_draw(&s_optimizedNoMipsWallsBack);
        testing_scene_object_draw(&s_optimizedNoMipsWalls);
        testing_scene_object_draw(&s_optimizedNoMipsCeiling);

        rdpq_mode_zbuf(true, true);
        testing_scene_object_draw(&s_optimizedNoMipsFloor);
        testing_scene_object_draw(&s_optimizedNoMipsPillars);

        rdpq_mode_zbuf(true, false);
        testing_scene_object_draw(&s_optimizedNoMipsFogDoor);

        rdpq_mode_zbuf(true, true);

    t3d_matrix_pop(1);
}

static void testing_scene_draw_no_materials_environment(void)
{
    t3d_matrix_push_pos(1);

        rdpq_mode_zbuf(false, false);
        testing_scene_object_draw(&s_noMaterialsWallsBack);
        testing_scene_object_draw(&s_noMaterialsWalls);
        testing_scene_object_draw(&s_noMaterialsCeiling);

        rdpq_mode_zbuf(true, true);
        testing_scene_object_draw(&s_noMaterialsFloor);
        testing_scene_object_draw(&s_noMaterialsFloorTiles);
        testing_scene_object_draw(&s_noMaterialsPillars);

        rdpq_mode_zbuf(true, false);
        testing_scene_object_draw(&s_noMaterialsFogDoor);

        rdpq_mode_zbuf(true, true);

    t3d_matrix_pop(1);
}

static void testing_scene_draw_decimated_frozen_environment(void)
{
    t3d_matrix_push_pos(1);

        rdpq_mode_zbuf(false, false);
        testing_scene_object_draw(&s_decimatedFrozenWallsBack);
        testing_scene_object_draw(&s_decimatedFrozenWalls);
        testing_scene_object_draw(&s_decimatedFrozenCeiling);

        rdpq_mode_zbuf(true, true);
        testing_scene_object_draw(&s_decimatedFrozenFloor);
        testing_scene_object_draw(&s_decimatedFrozenFloorTiles);
        testing_scene_object_draw(&s_decimatedFrozenPillars);

        rdpq_mode_zbuf(true, false);
        testing_scene_object_draw(&s_decimatedFrozenFogDoor);

        rdpq_mode_zbuf(true, true);

    t3d_matrix_pop(1);
}

static void testing_scene_draw_original_environment(void)
{
    t3d_matrix_push_pos(1);

        // Match the Guardian scene's old room order: windows/base room first with no depth.
        rdpq_mode_zbuf(false, false);
        testing_scene_object_draw(&s_originalWindows);
        testing_scene_object_draw(&s_originalRoom);

        if (testing_scene_should_draw_sunshafts()) {
            testing_scene_object_draw(&s_originalSunshafts);
        }

        // Main depth-tested room geometry.

        rdpq_mode_zbuf(true, true);
        testing_scene_object_draw(&s_originalFloor);
        testing_scene_object_draw(&s_originalRoomLedgeWalls);
        testing_scene_object_draw(&s_originalPillars);
        testing_scene_object_draw(&s_originalPillarsFront);

        // Transparent fog door late.
        rdpq_mode_zbuf(true, false);
        testing_scene_object_draw(&s_originalFog);

        rdpq_mode_zbuf(true, true);

    t3d_matrix_pop(1);
}

// ------------------------------------------------------------
// Scene lifecycle
// ------------------------------------------------------------

static void testing_scene_cycle_requested_environment_forward(void)
{
    s_requestedEnvironment = (TestingSceneEnvironment)(
        (s_requestedEnvironment + 1) % TESTING_SCENE_ENVIRONMENT_COUNT
    );
}

static void testing_scene_cycle_requested_environment_backward(void)
{
    s_requestedEnvironment = (TestingSceneEnvironment)(
        (s_requestedEnvironment + TESTING_SCENE_ENVIRONMENT_COUNT - 1)
        % TESTING_SCENE_ENVIRONMENT_COUNT
    );
}

void testing_scene_init(void)
{
    rspq_wait();

    s_freecamViewNeedsRefresh = false;

    testing_scene_delete_loaded_environment();
    testing_scene_clear_all_object_handles();
    testing_scene_setup_lighting();
    testing_scene_setup_camera();

    dust_particles_fx_init();
    dust_particles_fx_reset();

    dust_particles_fx_set_ambient_volume(
        -560.0f, 560.0f,
          20.0f, 470.0f,
        -560.0f, 560.0f
    );

    /*
     * Do not enable ambient dust globally here.
     * The selected environment decides whether particle FX are active.
     */
    dust_particles_fx_set_ambient_enabled(false);

    display_utility_set_bloom_alpha(TESTING_SCENE_BLOOM_ALPHA);
    testing_scene_set_bloom_enabled(true);

    testing_scene_load_requested_environment();
    testing_scene_start_autoplay_cutscene();

    /* Do not treat buttons already held while entering the scene as presses. */
    s_lastAHeld = joypad.btn.a;
    s_lastBHeld = joypad.btn.b;
    s_lastStartHeld = joypad.btn.start;
}

void testing_scene_reset(void)
{
    // Runtime-only reset. Do not allocate/free here.
    // Preserve freecam and its transform across A/B environment reloads.
    // For the normal cinematic camera, keep the existing reset behavior.
    if (cameraState != CAMERA_FREECAM) {
        cameraState = CAMERA_CUSTOM;
        lastCameraState = CAMERA_CUSTOM;
        camera_mode(CAMERA_CUSTOM);
    }

    // Always keep the cinematic's background pose current. While freecam is
    // active this updates only customCamPos/customCamTarget, not camPos/camTarget.
    testing_scene_update_cutscene_camera(
        testing_scene_get_cutscene_state_duration(s_cutsceneState)
    );

    // Avoid a held Z from toggling pause immediately after a reset/reload.
    s_lastZHeld = joypad.btn.z;
}

void testing_scene_restart(void)
{
    rspq_wait();

    testing_scene_delete_loaded_environment();
    testing_scene_reset();
    testing_scene_load_requested_environment();
}

void testing_scene_update(void)
{
    scroll_update();
    testing_scene_update_scene_control_input();
    testing_scene_update_cutscene();

    if (testing_scene_should_use_extra_fx()) {
        dust_particles_fx_update(deltaTime);
    }

    switch (s_loadedEnvironment) {
        case TESTING_SCENE_ENVIRONMENT_TEST:
        case TESTING_SCENE_ENVIRONMENT_OPTIMIZED:
        case TESTING_SCENE_ENVIRONMENT_OPTIMIZED_FROZEN:
        case TESTING_SCENE_ENVIRONMENT_OPTIMIZED_NO_MIPS:
        case TESTING_SCENE_ENVIRONMENT_OPTIMIZED_NO_MIPS_FROZEN:
        case TESTING_SCENE_ENVIRONMENT_NO_MATERIALS:
        case TESTING_SCENE_ENVIRONMENT_DECIMATED_FROZEN:
            testing_scene_update_test_environment();
            break;

        case TESTING_SCENE_ENVIRONMENT_ORIGINAL:
            testing_scene_update_original_environment();
            break;

        default:
            break;
    }
}

void testing_scene_fixed_update(void)
{
}

void testing_scene_draw(T3DViewport *viewport)
{
    if (!viewport) return;

    t3d_frame_start();

    if (!DITHER_ENABLED) {
        rdpq_mode_dithering(DITHER_NONE_BAYER);
    }

    /*
     * A/B movement was cancelled after main's camera update, so rebuild the
     * freecam view before this frame's viewport is attached and drawn.
     */
    if (s_freecamViewNeedsRefresh) {
        if (cameraState == CAMERA_FREECAM) {
            t3d_viewport_look_at(viewport, &camPos, &camTarget, &up);
        }

        s_freecamViewNeedsRefresh = false;
    }

    t3d_viewport_attach(viewport);

    color_t fogColor = (color_t){ 0, 0, 0, 0xFF };
    rdpq_mode_fog(RDPQ_FOG_STANDARD);
    rdpq_set_fog_color(fogColor);
    testing_scene_draw_cutscene_fog();
    t3d_fog_set_enabled(true);

    t3d_screen_clear_color(RGBA32(0, 0, 0, 0xFF));
    t3d_screen_clear_depth();

    t3d_light_set_ambient(colorAmbient);

    switch (s_loadedEnvironment) {
        case TESTING_SCENE_ENVIRONMENT_TEST:
            testing_scene_draw_test_environment();
            break;

        case TESTING_SCENE_ENVIRONMENT_OPTIMIZED:
        case TESTING_SCENE_ENVIRONMENT_OPTIMIZED_FROZEN:
            testing_scene_draw_optimized_environment();
            break;

        case TESTING_SCENE_ENVIRONMENT_OPTIMIZED_NO_MIPS:
        case TESTING_SCENE_ENVIRONMENT_OPTIMIZED_NO_MIPS_FROZEN:
            testing_scene_draw_optimized_no_mips_environment();
            break;

        case TESTING_SCENE_ENVIRONMENT_NO_MATERIALS:
            testing_scene_draw_no_materials_environment();
            break;

        case TESTING_SCENE_ENVIRONMENT_DECIMATED_FROZEN:
            testing_scene_draw_decimated_frozen_environment();
            break;

        case TESTING_SCENE_ENVIRONMENT_ORIGINAL:
            testing_scene_draw_original_environment();
            break;

        default:
            break;
    }

    if (testing_scene_should_use_extra_fx()) {
        dust_particles_fx_draw(viewport);
    }

    testing_scene_draw_environment_label();
}

void testing_scene_cleanup(void)
{
    rspq_wait();

    s_freecamViewNeedsRefresh = false;

    testing_scene_set_bloom_enabled(false);

    dust_particles_fx_set_ambient_enabled(false);
    dust_particles_fx_cleanup();

    testing_scene_delete_loaded_environment();
    camera_reset();
}
