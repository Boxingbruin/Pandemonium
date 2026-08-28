#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "guardian_scene.h"
#include "guardian_scene_sfx.h"
#include "scene_controller.h"

#include "audio_controller.h"

#include "camera_controller.h"

#include "joypad_utility.h"
#include "general_utility.h"
#include "game_lighting.h"
#include "game_time.h"

#include "globals.h"
#include "../../utilities/button_prompt_utility.h"

#include "../../managers/cutscene_manager.h"
#include "../../managers/cutscene_manager_internal.h"
#include "guardian_scene_context.h"

#include "character.h"
#include "character_ui.h"
#include "../../game/boss/boss.h"
#include "../../game/boss/boss_ai.h"
#include "../../game/boss/boss_render.h"
#include "../../game/boss/boss_ui.h"
#include "../../game/boss/environmental_mechanics/boss_ground_crush.h"
#include "../../controllers/dialog_controller.h"
#include "display_utility.h"
#include "../../controllers/menu_controller.h"
#include "save_controller.h"
#include "collision_system.h"
#include "letterbox_utility.h"
#include "../../fx/sword_trail.h"
#include "../../fx/screen_shake.h"

// TODO: This should not be declared in the header file, as it is only used externally (temp)
#include "dev.h"
#include "debug_draw.h"
#include "utilities/simple_collision_utility.h"

#include "../../controllers/fmv_controller.h"

#include "multi_sword_attacks.h" // TODO: call only from boss
#include "fx/lightning_fx.h"
#include "systems/particle_system.h"
#include "fx/dust_particles_fx.h"
#include "fx/blood_particles_fx.h"
//#include "boulder_hazard.h" // close-range ground-boulder hazard

// Declared here in case general_utility.h has not exposed the prototype yet.
void tile_double_scroll(void *userData, rdpq_texparms_t *tp, rdpq_tile_t tile);

// Forward declaration: scene_init() and scene_restart() start the Guardian intro.
void scene_init_cutscene(void);

/* Debug-start helpers are defined later in this file. */
void scene_init_playing(bool skippedCutscene);
static void scene_debug_force_boss_defeated(void);

T3DModel* chainsModel;
rspq_block_t* chainsDpl;
T3DMat4FP* chainsMatrix;

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

/*
 * Temporary startup override.
 *
 * 1: skip the Guardian intro/fight and begin directly in the post-defeat state.
 * 0: use the normal phase-1 intro and boss fight.
 */
#define GUARDIAN_DEBUG_START_BOSS_DEFEATED 0

#define GUARDIAN_ENVIRONMENT_PATH_PREFIX "rom:/boss_room/"

/* Reduce every replacement-room model uniformly by one third. */
#define GUARDIAN_ENVIRONMENT_SCALE (MODEL_SCALE * (2.0f / 3.0f))

#define GUARDIAN_ENVIRONMENT_OPTIMIZED_MAX_MIP_TEXTURE_COUNT 3
#define GUARDIAN_ENVIRONMENT_OPTIMIZED_FLOOR_MIP_CHAIN_COUNT 4
#define GUARDIAN_ENVIRONMENT_OPTIMIZED_WALLS_MIP_CHAIN_COUNT 4
#define GUARDIAN_ENVIRONMENT_OPTIMIZED_WALLS_BACK_MIP_CHAIN_COUNT 3
#define GUARDIAN_ENVIRONMENT_OPTIMIZED_MAX_MIP_CHAIN_COUNT 4

_Static_assert(
    GUARDIAN_ENVIRONMENT_OPTIMIZED_FLOOR_MIP_CHAIN_COUNT
        <= GUARDIAN_ENVIRONMENT_OPTIMIZED_MAX_MIP_CHAIN_COUNT
        && GUARDIAN_ENVIRONMENT_OPTIMIZED_WALLS_MIP_CHAIN_COUNT
            <= GUARDIAN_ENVIRONMENT_OPTIMIZED_MAX_MIP_CHAIN_COUNT
        && GUARDIAN_ENVIRONMENT_OPTIMIZED_WALLS_BACK_MIP_CHAIN_COUNT
            <= GUARDIAN_ENVIRONMENT_OPTIMIZED_WALLS_MIP_CHAIN_COUNT,
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
#define GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP1_START_RDP_LEVEL 2
#define GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP2_START_RDP_LEVEL 4
#define GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP_RDP_LEVEL_COUNT 8

_Static_assert(
    GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP_RDP_LEVEL_COUNT >= 3
        && GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP_RDP_LEVEL_COUNT <= 8,
    "optimized mipmapping requires between 3 and 8 RDP levels"
);

_Static_assert(
    GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP1_START_RDP_LEVEL > 0
        && GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP1_START_RDP_LEVEL
            < GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP2_START_RDP_LEVEL
        && GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP2_START_RDP_LEVEL
            < GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP_RDP_LEVEL_COUNT,
    "optimized mip start levels must be ordered and inside the RDP chain"
);

typedef struct GuardianEnvironmentMipChain {
    const char *chainName;
    const char *materialName;
    uint8_t sourceTextureCount;
    const char *texturePaths[
        GUARDIAN_ENVIRONMENT_OPTIMIZED_MAX_MIP_TEXTURE_COUNT
    ];
    uint8_t sourceStartRdpLevels[
        GUARDIAN_ENVIRONMENT_OPTIMIZED_MAX_MIP_TEXTURE_COUNT
    ];
    sprite_t *sprites[
        GUARDIAN_ENVIRONMENT_OPTIMIZED_MAX_MIP_TEXTURE_COUNT
    ];
    bool ready;
} GuardianEnvironmentMipChain;

static GuardianEnvironmentMipChain s_environmentFloorMipChains[
    GUARDIAN_ENVIRONMENT_OPTIMIZED_FLOOR_MIP_CHAIN_COUNT
] = {
    {
        .chainName = "floor6",
        .materialName = "floor",
        .sourceTextureCount = 3,
        /* Source 0 uses the material's own textureA.texPath. */
        .texturePaths = {
            NULL,
            GUARDIAN_ENVIRONMENT_PATH_PREFIX "floor6-mip1.i4.sprite",
            GUARDIAN_ENVIRONMENT_PATH_PREFIX "floor6-mip2.i4.sprite",
        },
        .sourceStartRdpLevels = {
            0,
            GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP1_START_RDP_LEVEL,
            GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP2_START_RDP_LEVEL,
        },
    },
    {
        .chainName = "floor_ornate11",
        .materialName = "floor_ornate",
        .sourceTextureCount = 3,
        /* Source 0 uses floor_ornate11.i4.sprite from the material. */
        .texturePaths = {
            NULL,
            GUARDIAN_ENVIRONMENT_PATH_PREFIX "floor_ornate11-mip1.i4.sprite",
            GUARDIAN_ENVIRONMENT_PATH_PREFIX "floor_ornate11-mip2.i4.sprite",
        },
        .sourceStartRdpLevels = {
            0,
            GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP1_START_RDP_LEVEL,
            GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP2_START_RDP_LEVEL,
        },
    },
    {
        .chainName = "floor_debris_pile5",
        .materialName = "floor_debris_pile2",
        .sourceTextureCount = 3,
        /* Debris uses its explicitly renamed I4 source at all three stages. */
        .texturePaths = {
            GUARDIAN_ENVIRONMENT_PATH_PREFIX "floor_debris_pile5.i4.sprite",
            GUARDIAN_ENVIRONMENT_PATH_PREFIX "floor_debris_pile5-mip1.i4.sprite",
            GUARDIAN_ENVIRONMENT_PATH_PREFIX "floor_debris_pile5-mip2.i4.sprite",
        },
        .sourceStartRdpLevels = {
            0,
            GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP1_START_RDP_LEVEL,
            GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP2_START_RDP_LEVEL,
        },
    },
    {
        .chainName = "carpet_border8",
        .materialName = "carpet_border",
        .sourceTextureCount = 3,
        /* CI8 mip levels use source 0's palette. */
        .texturePaths = {
            GUARDIAN_ENVIRONMENT_PATH_PREFIX "carpet_border8.ci8.sprite",
            GUARDIAN_ENVIRONMENT_PATH_PREFIX "carpet_border8-mip1.ci8.sprite",
            GUARDIAN_ENVIRONMENT_PATH_PREFIX "carpet_border8-mip2.ci8.sprite",
        },
        /* Do not repeat mip1: mip2 starts on the immediately following level. */
        .sourceStartRdpLevels = {
            0,
            GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP1_START_RDP_LEVEL,
            GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP1_START_RDP_LEVEL + 1,
        },
    },
};

static GuardianEnvironmentMipChain s_environmentWallsMipChains[
    GUARDIAN_ENVIRONMENT_OPTIMIZED_WALLS_MIP_CHAIN_COUNT
] = {
    {
        .chainName = "baseboard8",
        .materialName = "baseboard",
        .sourceTextureCount = 3,
        .texturePaths = {
            GUARDIAN_ENVIRONMENT_PATH_PREFIX "baseboard8.i4.sprite",
            GUARDIAN_ENVIRONMENT_PATH_PREFIX "baseboard8-mip1.i4.sprite",
            GUARDIAN_ENVIRONMENT_PATH_PREFIX "baseboard8-mip2.i4.sprite",
        },
        /* Keep mip1 at level 2, but start mip2 on the following level. */
        .sourceStartRdpLevels = {
            0,
            GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP1_START_RDP_LEVEL,
            GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP1_START_RDP_LEVEL + 1,
        },
    },
    {
        .chainName = "door_detail_top3",
        /* This 80x48 source matches the door_detail_top material. */
        .materialName = "door_detail_top",
        .sourceTextureCount = 2,
        .texturePaths = {
            GUARDIAN_ENVIRONMENT_PATH_PREFIX "door_detail_top3.ia4.sprite",
            GUARDIAN_ENVIRONMENT_PATH_PREFIX "door_detail_top3-mip1.ia4.sprite",
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
            GUARDIAN_ENVIRONMENT_PATH_PREFIX "door_detail_pillar4.i4.sprite",
            GUARDIAN_ENVIRONMENT_PATH_PREFIX "door_detail_pillar4-mip1.i4.sprite",
            GUARDIAN_ENVIRONMENT_PATH_PREFIX "door_detail_pillar4-mip2.i4.sprite",
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
            GUARDIAN_ENVIRONMENT_PATH_PREFIX "door6.i4.sprite",
            GUARDIAN_ENVIRONMENT_PATH_PREFIX "door6-mip1.i4.sprite",
            GUARDIAN_ENVIRONMENT_PATH_PREFIX "door6-mip2.i4.sprite",
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
static const rdpq_mipmap_t GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP_MODE = MIPMAP_NEAREST;


static ScrollParams s_environmentFogScrollParams = {
    .xSpeed = 0.0f,
    .ySpeed = 10.0f,
    .scale  = 64,
};

static ScrollParams s_environmentSunshaftsScrollParams = {
    // TILE0: main/streak texture.
    .xSpeed = -3.0f,
    .ySpeed = -3.0f,
    .scale  = 32,

    // TILE1: secondary/dust texture.
    .xSpeedTwo = -0.0f,
    .ySpeedTwo = -8.0f,
    .scaleTwo  = 32,
};

typedef struct GuardianEnvironmentObject {
    const char *path;
    T3DModel *model;
    rspq_block_t *dpl;
    T3DMat4FP *matrix;
    bool drawCustom;
    bool doubleScroll;
    bool frozen;
    bool optimizedMipDraw;
    ScrollParams *scrollParams;
    GuardianEnvironmentMipChain *mipChains;
    T3DMaterial *boundMipMaterials[
        GUARDIAN_ENVIRONMENT_OPTIMIZED_MAX_MIP_CHAIN_COUNT
    ];
    uint8_t mipChainCount;
} GuardianEnvironmentObject;


// Complete frozen optimized environment
// ------------------------------------------------------------

static GuardianEnvironmentObject s_environmentFloor;
static GuardianEnvironmentObject s_environmentWalls;
static GuardianEnvironmentObject s_environmentWallsBack;
static GuardianEnvironmentObject s_environmentPillars;
static GuardianEnvironmentObject s_environmentBrokenStatue;
static GuardianEnvironmentObject s_environmentNichesWindows;
static GuardianEnvironmentObject s_environmentDecalsLayer1;
static GuardianEnvironmentObject s_environmentDecalsLayer2;
static GuardianEnvironmentObject s_environmentFogDoor;
static GuardianEnvironmentObject s_environmentSunshafts;

/*
 * Shared realtime actor shadows
 * -----------------------------
 *
 * One 128x64 I8 render target is split into two non-owning 64x64 sub-surfaces:
 *
 *   x = 0..63    character silhouette
 *   x = 64..127  boss silhouette
 *
 * Both actors are captured during one attach / clear / Tiny3D pass. The two
 * 64x64 I8 sub-surfaces are then uploaded independently because either one
 * exactly fills the RDP's 4 KiB TMEM.
 *
 * The projected quads are still independent world-space geometry, allowing the
 * character and boss to use different capture framing and floor-shadow sizes.
 */
#define GUARDIAN_RT_SHADOW_TILE_WIDTH 64
#define GUARDIAN_RT_SHADOW_TILE_HEIGHT 64
#define GUARDIAN_RT_SHADOW_ATLAS_WIDTH \
    (GUARDIAN_RT_SHADOW_TILE_WIDTH * 2)
#define GUARDIAN_RT_SHADOW_ATLAS_HEIGHT \
    GUARDIAN_RT_SHADOW_TILE_HEIGHT

/*
 * Capture both silhouettes together every second rendered frame. The most
 * recent two 64x64 textures and both projected quads are reused on the skipped
 * frame.
 */
#define GUARDIAN_RT_SHADOW_CAPTURE_INTERVAL 2

/* Shared light-ray direction and draw state. */
#define GUARDIAN_RT_SHADOW_GROUND_Y 4.10f
#define GUARDIAN_RT_SHADOW_DEPTH_OFFSET (-0x60)

/* Character capture framing and projection dimensions. */
#define GUARDIAN_CHARACTER_RT_SHADOW_CAPTURE_DISTANCE 40.0f
#define GUARDIAN_CHARACTER_RT_SHADOW_CAPTURE_TARGET_HEIGHT 14.0f
#define GUARDIAN_CHARACTER_RT_SHADOW_CAPTURE_FOV_DEG 32.0f
#define GUARDIAN_CHARACTER_RT_SHADOW_CAPTURE_ASPECT 1.0f
#define GUARDIAN_CHARACTER_RT_SHADOW_NEAR_PLANE 5.0f
#define GUARDIAN_CHARACTER_RT_SHADOW_FAR_PLANE 450.0f

#define GUARDIAN_CHARACTER_RT_SHADOW_LENGTH 40.0f
#define GUARDIAN_CHARACTER_RT_SHADOW_HALF_WIDTH 20.0f

/*
 * Pull the near edge slightly farther behind the character so the projected
 * silhouette reaches underneath the feet rather than beginning just beyond
 * the contact point.
 */
#define GUARDIAN_CHARACTER_RT_SHADOW_ORIGIN_BACK_OFFSET 3.0f
#define GUARDIAN_CHARACTER_RT_SHADOW_ALPHA 70

/*
 * Initial boss framing values.
 *
 * The boss is much larger than the character, so its camera is farther away
 * and centered higher. These constants are intentionally grouped here for
 * visual tuning after the first hardware test.
 */
#define GUARDIAN_BOSS_RT_SHADOW_CAPTURE_DISTANCE 140.0f
#define GUARDIAN_BOSS_RT_SHADOW_CAPTURE_TARGET_HEIGHT 40.0f
#define GUARDIAN_BOSS_RT_SHADOW_CAPTURE_FOV_DEG 32.0f
#define GUARDIAN_BOSS_RT_SHADOW_CAPTURE_ASPECT 1.0f
#define GUARDIAN_BOSS_RT_SHADOW_NEAR_PLANE 5.0f
#define GUARDIAN_BOSS_RT_SHADOW_FAR_PLANE 700.0f

#define GUARDIAN_BOSS_RT_SHADOW_LENGTH 80.0f
#define GUARDIAN_BOSS_RT_SHADOW_HALF_WIDTH 40.0f
#define GUARDIAN_BOSS_RT_SHADOW_ORIGIN_BACK_OFFSET 4.0f
#define GUARDIAN_BOSS_RT_SHADOW_ALPHA 70

/*
 * Actor light and shadow source
 * -----------------------------
 *
 * The conceptual source sits 400 units above and 400 units along world Z-:
 *
 *     source = (0, 400, -400)
 *
 * Light rays travel from that source toward scene origin at 45 degrees:
 *
 *     shadow ray = (0, -0.7071, +0.7071)
 *
 * Tiny3D's actor-light vector is supplied in the opposite, surface-to-light
 * direction:
 *
 *     actor light = (0, +0.7071, -0.7071)
 *
 * The source position independently derives each actor's shadow ray, allowing
 * projected shadows to rotate as the character and boss move around the room.
 */
#define GUARDIAN_ACTOR_LIGHT_SOURCE_X 0.0f
#define GUARDIAN_ACTOR_LIGHT_SOURCE_Y 400.0f
#define GUARDIAN_ACTOR_LIGHT_SOURCE_Z (-400.0f)

#define GUARDIAN_ACTOR_DIRECTIONAL_LIGHT_LEVEL 255

/*
 * Hard-coded actor-only room-light falloff.
 *
 * The middle of the room is treated as the brightest area. Each animated actor
 * independently becomes darker when moving toward either Z direction or toward
 * positive X. Moving toward negative X alone does not darken the actor.
 *
 *   directional distance <= FULL_RADIUS -> CENTER_SCALE
 *   directional distance >= DARK_RADIUS -> EDGE_SCALE
 *
 * Between those distances, a smoothstep curve prevents a visible lighting seam.
 * Actor ambient and directional intensity both fade, but directional light begins fading farther from the center. The room, fog, particles,
 * projections, shadows, and UI keep their existing lighting.
 */
#define GUARDIAN_ACTOR_LIGHT_ROOM_CENTER_X 0.0f
#define GUARDIAN_ACTOR_LIGHT_ROOM_CENTER_Z 0.0f

#define GUARDIAN_ACTOR_AMBIENT_FULL_RADIUS 80.0f
#define GUARDIAN_ACTOR_AMBIENT_DARK_RADIUS 385.0f

#define GUARDIAN_ACTOR_AMBIENT_CENTER_SCALE 0.55f
#define GUARDIAN_ACTOR_AMBIENT_EDGE_SCALE 0.235f

/*
 * Directional light holds its strength farther from the room center than the
 * ambient component, then fades over a wider range.
 */
#define GUARDIAN_ACTOR_DIRECTIONAL_FULL_RADIUS 140.0f
#define GUARDIAN_ACTOR_DIRECTIONAL_DARK_RADIUS 520.0f

#define GUARDIAN_ACTOR_DIRECTIONAL_CENTER_LEVEL 255.0f
#define GUARDIAN_ACTOR_DIRECTIONAL_EDGE_LEVEL 150.0f

static uint8_t s_actorDirectionalLightColor[4] = {
    GUARDIAN_ACTOR_DIRECTIONAL_LIGHT_LEVEL,
    GUARDIAN_ACTOR_DIRECTIONAL_LIGHT_LEVEL,
    GUARDIAN_ACTOR_DIRECTIONAL_LIGHT_LEVEL,
    255,
};

/* Updated from colorAmbient immediately before each actor draw. */
static uint8_t s_actorAmbientColor[4] = {0, 0, 0, 255};

static const T3DVec3 s_actorDirectionalLightDirection = {{
    0.0f,
    0.70710678f,
    -0.70710678f,
}};

static const T3DVec3 s_rtShadowLightSource = {{
    GUARDIAN_ACTOR_LIGHT_SOURCE_X,
    GUARDIAN_ACTOR_LIGHT_SOURCE_Y,
    GUARDIAN_ACTOR_LIGHT_SOURCE_Z,
}};

static float guardian_actor_light_distance_at(
    float actorX,
    float actorZ
) {
    /*
     * Lighting darkens in both Z directions and toward positive X.
     * Negative X alone does not contribute to the falloff.
     */
    float positiveXDistance =
        actorX - GUARDIAN_ACTOR_LIGHT_ROOM_CENTER_X;

    if (positiveXDistance < 0.0f) {
        positiveXDistance = 0.0f;
    }

    float zDistance =
        actorZ - GUARDIAN_ACTOR_LIGHT_ROOM_CENTER_Z;

    return sqrtf(
        positiveXDistance * positiveXDistance
        + zDistance * zDistance
    );
}

static float guardian_actor_smooth_falloff(
    float distance,
    float fullRadius,
    float darkRadius
) {
    float t =
        (distance - fullRadius)
        / (darkRadius - fullRadius);

    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    return t * t * (3.0f - 2.0f * t);
}

/*
 * Call only after the onscreen viewport has been attached. Tiny3D transforms
 * directional lights into the current viewport's view space when configured.
 */
static void guardian_actor_directional_light_enable(
    float actorX,
    float actorZ
) {
    float distance =
        guardian_actor_light_distance_at(actorX, actorZ);

    float ambientFalloff =
        guardian_actor_smooth_falloff(
            distance,
            GUARDIAN_ACTOR_AMBIENT_FULL_RADIUS,
            GUARDIAN_ACTOR_AMBIENT_DARK_RADIUS
        );

    float directionalFalloff =
        guardian_actor_smooth_falloff(
            distance,
            GUARDIAN_ACTOR_DIRECTIONAL_FULL_RADIUS,
            GUARDIAN_ACTOR_DIRECTIONAL_DARK_RADIUS
        );

    float ambientScale =
        GUARDIAN_ACTOR_AMBIENT_CENTER_SCALE
        + (
            GUARDIAN_ACTOR_AMBIENT_EDGE_SCALE
            - GUARDIAN_ACTOR_AMBIENT_CENTER_SCALE
        ) * ambientFalloff;

    /*
     * Derive actor ambient from the current scene ambient so cutscene or room
     * lighting changes are preserved, then apply the actor's positional
     * falloff.
     */
    for (int channel = 0; channel < 3; ++channel) {
        float scaled =
            (float)colorAmbient[channel] * ambientScale;

        if (scaled < 0.0f) scaled = 0.0f;
        if (scaled > 255.0f) scaled = 255.0f;

        s_actorAmbientColor[channel] = (uint8_t)lroundf(scaled);
    }
    s_actorAmbientColor[3] = colorAmbient[3];

    /*
     * Reduce the directional source at the same time. This prevents actors at
     * the darker sides of the room from retaining a full-strength highlight
     * while only their ambient component changes.
     */
    float directionalLevel =
        GUARDIAN_ACTOR_DIRECTIONAL_CENTER_LEVEL
        + (
            GUARDIAN_ACTOR_DIRECTIONAL_EDGE_LEVEL
            - GUARDIAN_ACTOR_DIRECTIONAL_CENTER_LEVEL
        ) * directionalFalloff;

    if (directionalLevel < 0.0f) directionalLevel = 0.0f;
    if (directionalLevel > 255.0f) directionalLevel = 255.0f;

    uint8_t directionalByte =
        (uint8_t)lroundf(directionalLevel);

    s_actorDirectionalLightColor[0] = directionalByte;
    s_actorDirectionalLightColor[1] = directionalByte;
    s_actorDirectionalLightColor[2] = directionalByte;
    s_actorDirectionalLightColor[3] = 255;

    t3d_light_set_ambient(s_actorAmbientColor);
    t3d_light_set_directional(
        0,
        s_actorDirectionalLightColor,
        &s_actorDirectionalLightDirection
    );
    t3d_light_set_count(1);
}

static void guardian_actor_directional_light_disable(void)
{
    /*
     * Return immediately to the room's normal ambient-only state so fog door,
     * chains, projections, particles and UI do not inherit actor lighting.
     */
    t3d_light_set_count(0);
    t3d_light_set_ambient(colorAmbient);
}

/* Dedicated low-poly skinned caster models used only by the realtime pass. */
#define GUARDIAN_CHARACTER_RT_SHADOW_MODEL_PATH \
    "rom:/knight/knight_shadow.t3dm"
#define GUARDIAN_BOSS_RT_SHADOW_MODEL_PATH \
    "rom:/boss/boss_anim_shadow.t3dm"

static T3DModel *s_characterRtShadowModel = NULL;
static T3DModel *s_bossRtShadowModel = NULL;

/* One owned capture buffer and two non-owning 64x64 texture views. */
static surface_t s_rtShadowAtlas;
static surface_t s_characterRtShadowTexture;
static surface_t s_bossRtShadowTexture;

/* Independent viewport/camera state for each 64x64 atlas region. */
static T3DViewport s_characterRtShadowViewport;
static T3DViewport s_bossRtShadowViewport;

/* Independent projected floor quads; both use one shared identity matrix. */
static T3DVertPacked *s_characterRtShadowVertices = NULL;
static T3DVertPacked *s_bossRtShadowVertices = NULL;
static T3DMat4FP *s_rtShadowIdentityMatrix = NULL;

/*
 * Geometry-only command blocks for the dedicated low-poly caster models.
 * They reference the actors' existing live skeleton matrices and intentionally
 * contain no materials or textures.
 */
static rspq_block_t *s_characterRtShadowGeometryDpl = NULL;
static rspq_block_t *s_bossRtShadowGeometryDpl = NULL;

static bool s_rtShadowsInitialized = false;
static uint8_t s_rtShadowCaptureCountdown = 0;

static void guardian_rt_shadows_cleanup(void);

static inline int16_t guardian_rt_shadow_to_s16(float value)
{
    if (value < -32768.0f) value = -32768.0f;
    if (value > 32767.0f) value = 32767.0f;
    return (int16_t)lroundf(value);
}

static inline int16_t guardian_rt_shadow_uv(float texel)
{
    /* Tiny3D UVs use signed 10.5 fixed-point pixel coordinates. */
    return (int16_t)lroundf(texel * 32.0f);
}

static void guardian_rt_shadow_write_vertex(
    T3DVertPacked *pair,
    bool vertexB,
    float x,
    float y,
    float z,
    int16_t s,
    int16_t t,
    uint16_t packedNormal
) {
    if (!pair) return;

    if (vertexB) {
        pair->posB[0] = guardian_rt_shadow_to_s16(x);
        pair->posB[1] = guardian_rt_shadow_to_s16(y);
        pair->posB[2] = guardian_rt_shadow_to_s16(z);
        pair->normB = packedNormal;
        pair->rgbaB = 0xFFFFFFFFu;
        pair->stB[0] = s;
        pair->stB[1] = t;
    } else {
        pair->posA[0] = guardian_rt_shadow_to_s16(x);
        pair->posA[1] = guardian_rt_shadow_to_s16(y);
        pair->posA[2] = guardian_rt_shadow_to_s16(z);
        pair->normA = packedNormal;
        pair->rgbaA = 0xFFFFFFFFu;
        pair->stA[0] = s;
        pair->stA[1] = t;
    }
}

static rspq_block_t *guardian_rt_shadow_record_geometry(
    const T3DModel *model,
    const T3DMat4FP *boneMatrices
) {
    if (!model || !boneMatrices) return NULL;

    rspq_block_begin();

        T3DModelIter iterator = t3d_model_iter_create(
            model,
            T3D_CHUNK_TYPE_OBJECT
        );

        while (t3d_model_iter_next(&iterator)) {
            t3d_model_draw_object(iterator.object, boneMatrices);
        }

    return rspq_block_end();
}

static bool guardian_rt_shadows_init(Boss *boss)
{
    if (s_rtShadowsInitialized) return true;

    if (!character.skeleton
        || !character.skeleton->boneMatricesFP
    ) {
        debugf("guardian realtime shadows: character model/skeleton unavailable\n");
        return false;
    }

    if (!boss
        || !boss->skeleton
        || !((T3DSkeleton *)boss->skeleton)->boneMatricesFP
    ) {
        debugf("guardian realtime shadows: boss model/skeleton unavailable\n");
        return false;
    }

    s_characterRtShadowModel = t3d_model_load(
        GUARDIAN_CHARACTER_RT_SHADOW_MODEL_PATH
    );
    s_bossRtShadowModel = t3d_model_load(
        GUARDIAN_BOSS_RT_SHADOW_MODEL_PATH
    );

    if (!s_characterRtShadowModel || !s_bossRtShadowModel) {
        debugf(
            "guardian realtime shadows: failed to load dedicated caster models\n"
        );
        goto fail;
    }

    memset(&s_rtShadowAtlas, 0, sizeof(s_rtShadowAtlas));
    memset(&s_characterRtShadowTexture, 0, sizeof(s_characterRtShadowTexture));
    memset(&s_bossRtShadowTexture, 0, sizeof(s_bossRtShadowTexture));
    memset(&s_characterRtShadowViewport, 0, sizeof(s_characterRtShadowViewport));
    memset(&s_bossRtShadowViewport, 0, sizeof(s_bossRtShadowViewport));

    /*
     * I8 is the smallest format that can be used directly as an RDP color
     * target. The complete atlas is 128x64x1 byte = 8192 bytes.
     */
    s_rtShadowAtlas = surface_alloc(
        FMT_I8,
        GUARDIAN_RT_SHADOW_ATLAS_WIDTH,
        GUARDIAN_RT_SHADOW_ATLAS_HEIGHT
    );

    if (!s_rtShadowAtlas.buffer) {
        debugf("guardian realtime shadows: failed to allocate 128x64 I8 atlas\n");
        goto fail;
    }

    /*
     * These are only rectangular views into the atlas. No additional pixel
     * buffers or copies are allocated.
     */
    s_characterRtShadowTexture = surface_make_sub(
        &s_rtShadowAtlas,
        0,
        0,
        GUARDIAN_RT_SHADOW_TILE_WIDTH,
        GUARDIAN_RT_SHADOW_TILE_HEIGHT
    );

    s_bossRtShadowTexture = surface_make_sub(
        &s_rtShadowAtlas,
        GUARDIAN_RT_SHADOW_TILE_WIDTH,
        0,
        GUARDIAN_RT_SHADOW_TILE_WIDTH,
        GUARDIAN_RT_SHADOW_TILE_HEIGHT
    );

    s_characterRtShadowVertices = malloc_uncached(
        sizeof(T3DVertPacked) * 2
    );
    s_bossRtShadowVertices = malloc_uncached(
        sizeof(T3DVertPacked) * 2
    );
    s_rtShadowIdentityMatrix = malloc_uncached(sizeof(T3DMat4FP));

    if (!s_characterRtShadowVertices
        || !s_bossRtShadowVertices
        || !s_rtShadowIdentityMatrix
    ) {
        debugf("guardian realtime shadows: failed to allocate quad/matrix data\n");
        goto fail;
    }

    memset(
        s_characterRtShadowVertices,
        0,
        sizeof(T3DVertPacked) * 2
    );
    memset(
        s_bossRtShadowVertices,
        0,
        sizeof(T3DVertPacked) * 2
    );
    t3d_mat4fp_identity(s_rtShadowIdentityMatrix);

    T3DSkeleton *bossSkeleton = (T3DSkeleton *)boss->skeleton;

    s_characterRtShadowGeometryDpl = guardian_rt_shadow_record_geometry(
        s_characterRtShadowModel,
        character.skeleton->boneMatricesFP
    );
    s_bossRtShadowGeometryDpl = guardian_rt_shadow_record_geometry(
        s_bossRtShadowModel,
        bossSkeleton->boneMatricesFP
    );

    if (!s_characterRtShadowGeometryDpl
        || !s_bossRtShadowGeometryDpl
    ) {
        debugf("guardian realtime shadows: failed to record actor geometry blocks\n");
        goto fail;
    }

    s_characterRtShadowViewport = t3d_viewport_create_buffered(
        FRAME_BUFFER_COUNT
    );
    s_bossRtShadowViewport = t3d_viewport_create_buffered(
        FRAME_BUFFER_COUNT
    );

    t3d_viewport_set_area(
        &s_characterRtShadowViewport,
        0,
        0,
        GUARDIAN_RT_SHADOW_TILE_WIDTH,
        GUARDIAN_RT_SHADOW_TILE_HEIGHT
    );
    t3d_viewport_set_perspective(
        &s_characterRtShadowViewport,
        T3D_DEG_TO_RAD(GUARDIAN_CHARACTER_RT_SHADOW_CAPTURE_FOV_DEG),
        GUARDIAN_CHARACTER_RT_SHADOW_CAPTURE_ASPECT,
        GUARDIAN_CHARACTER_RT_SHADOW_NEAR_PLANE,
        GUARDIAN_CHARACTER_RT_SHADOW_FAR_PLANE
    );

    t3d_viewport_set_area(
        &s_bossRtShadowViewport,
        GUARDIAN_RT_SHADOW_TILE_WIDTH,
        0,
        GUARDIAN_RT_SHADOW_TILE_WIDTH,
        GUARDIAN_RT_SHADOW_TILE_HEIGHT
    );
    t3d_viewport_set_perspective(
        &s_bossRtShadowViewport,
        T3D_DEG_TO_RAD(GUARDIAN_BOSS_RT_SHADOW_CAPTURE_FOV_DEG),
        GUARDIAN_BOSS_RT_SHADOW_CAPTURE_ASPECT,
        GUARDIAN_BOSS_RT_SHADOW_NEAR_PLANE,
        GUARDIAN_BOSS_RT_SHADOW_FAR_PLANE
    );

    s_rtShadowCaptureCountdown = 0;
    s_rtShadowsInitialized = true;
    return true;

fail:
    guardian_rt_shadows_cleanup();
    return false;
}

static void guardian_rt_shadows_cleanup(void)
{
    if (s_characterRtShadowGeometryDpl) {
        rspq_block_free(s_characterRtShadowGeometryDpl);
        s_characterRtShadowGeometryDpl = NULL;
    }

    if (s_bossRtShadowGeometryDpl) {
        rspq_block_free(s_bossRtShadowGeometryDpl);
        s_bossRtShadowGeometryDpl = NULL;
    }

    /*
     * The recorded blocks reference object data from these models, so free the
     * blocks first and then release the dedicated caster assets.
     */
    if (s_characterRtShadowModel) {
        t3d_model_free(s_characterRtShadowModel);
        s_characterRtShadowModel = NULL;
    }

    if (s_bossRtShadowModel) {
        t3d_model_free(s_bossRtShadowModel);
        s_bossRtShadowModel = NULL;
    }

    if (s_characterRtShadowVertices) {
        free_uncached(s_characterRtShadowVertices);
        s_characterRtShadowVertices = NULL;
    }

    if (s_bossRtShadowVertices) {
        free_uncached(s_bossRtShadowVertices);
        s_bossRtShadowVertices = NULL;
    }

    if (s_rtShadowIdentityMatrix) {
        free_uncached(s_rtShadowIdentityMatrix);
        s_rtShadowIdentityMatrix = NULL;
    }

    /*
     * Sub-surfaces do not own memory. surface_free only clears their surface
     * structures; the atlas owns and frees the actual 8192-byte buffer.
     */
    surface_free(&s_characterRtShadowTexture);
    surface_free(&s_bossRtShadowTexture);

    if (s_rtShadowAtlas.buffer) {
        surface_free(&s_rtShadowAtlas);
    }

    t3d_viewport_destroy(&s_characterRtShadowViewport);
    t3d_viewport_destroy(&s_bossRtShadowViewport);

    memset(&s_rtShadowAtlas, 0, sizeof(s_rtShadowAtlas));
    memset(&s_characterRtShadowTexture, 0, sizeof(s_characterRtShadowTexture));
    memset(&s_bossRtShadowTexture, 0, sizeof(s_bossRtShadowTexture));
    memset(&s_characterRtShadowViewport, 0, sizeof(s_characterRtShadowViewport));
    memset(&s_bossRtShadowViewport, 0, sizeof(s_bossRtShadowViewport));

    s_rtShadowCaptureCountdown = 0;
    s_rtShadowsInitialized = false;
}

static T3DVec3 guardian_rt_shadow_get_ray_direction(
    float actorX,
    float actorY,
    float actorZ
) {
    /*
     * Direction travelled by light rays: source -> actor/floor.
     * At scene origin this exactly matches the directional actor light.
     */
    T3DVec3 rayDirection = {{
        actorX - s_rtShadowLightSource.v[0],
        actorY - s_rtShadowLightSource.v[1],
        actorZ - s_rtShadowLightSource.v[2],
    }};

    float lengthSq =
        rayDirection.v[0] * rayDirection.v[0]
        + rayDirection.v[1] * rayDirection.v[1]
        + rayDirection.v[2] * rayDirection.v[2];

    if (lengthSq <= 0.000001f) {
        return s_actorDirectionalLightDirection;
    }

    t3d_vec3_norm(&rayDirection);
    return rayDirection;
}

static void guardian_rt_shadow_position_camera(
    T3DViewport *viewport,
    const T3DVec3 *rayDirection,
    float actorX,
    float actorY,
    float actorZ,
    float targetHeight,
    float distance
) {
    if (!viewport || !rayDirection) return;

    const T3DVec3 worldUp = {{0.0f, 1.0f, 0.0f}};

    T3DVec3 target = {{
        actorX,
        actorY + targetHeight,
        actorZ,
    }};

    /*
     * The camera looks along the same direction travelled by the light rays.
     * Placing the eye opposite that direction puts it toward the light source.
     */
    T3DVec3 eye = {{
        target.v[0] - rayDirection->v[0] * distance,
        target.v[1] - rayDirection->v[1] * distance,
        target.v[2] - rayDirection->v[2] * distance,
    }};

    t3d_viewport_look_at(viewport, &eye, &target, &worldUp);
}


static void guardian_rt_shadows_capture(Boss *boss)
{
    if (!s_rtShadowsInitialized) return;
    if (!s_rtShadowAtlas.buffer) return;

    bool characterReady =
        character.visible
        && character.modelMat
        && s_characterRtShadowGeometryDpl;

    bool bossReady =
        boss
        && boss_is_active(boss)
        && boss->visible
        && boss->modelMat
        && s_bossRtShadowGeometryDpl;

    if (!characterReady && !bossReady) return;

    /*
     * Reuse both most recent 64x64 textures on the skipped frame. Both actors
     * share one countdown so their silhouettes are always captured together.
     */
    if (s_rtShadowCaptureCountdown > 0) {
        --s_rtShadowCaptureCountdown;
        return;
    }

    s_rtShadowCaptureCountdown = GUARDIAN_RT_SHADOW_CAPTURE_INTERVAL - 1;

    if (characterReady) {
        T3DVec3 characterRayDirection =
            guardian_rt_shadow_get_ray_direction(
                character.pos[0],
                character.pos[1],
                character.pos[2]
            );

        guardian_rt_shadow_position_camera(
            &s_characterRtShadowViewport,
            &characterRayDirection,
            character.pos[0],
            character.pos[1],
            character.pos[2],
            GUARDIAN_CHARACTER_RT_SHADOW_CAPTURE_TARGET_HEIGHT,
            GUARDIAN_CHARACTER_RT_SHADOW_CAPTURE_DISTANCE
        );
    }

    if (bossReady) {
        T3DVec3 bossRayDirection =
            guardian_rt_shadow_get_ray_direction(
                boss->pos[0],
                boss->pos[1],
                boss->pos[2]
            );

        guardian_rt_shadow_position_camera(
            &s_bossRtShadowViewport,
            &bossRayDirection,
            boss->pos[0],
            boss->pos[1],
            boss->pos[2],
            GUARDIAN_BOSS_RT_SHADOW_CAPTURE_TARGET_HEIGHT,
            GUARDIAN_BOSS_RT_SHADOW_CAPTURE_DISTANCE
        );
    }

    /*
     * One offscreen attach, one 128x64 clear and one shared render-state setup
     * for both actor silhouettes.
     */
    rdpq_attach(&s_rtShadowAtlas, NULL);

    t3d_frame_start();

    /*
     * Clear the complete atlas before either 64x64 viewport narrows the
     * scissor to its own half.
     */
    rdpq_set_scissor(
        0,
        0,
        GUARDIAN_RT_SHADOW_ATLAS_WIDTH,
        GUARDIAN_RT_SHADOW_ATLAS_HEIGHT
    );
    t3d_screen_clear_color(RGBA32(255, 255, 255, 255));

    t3d_fog_set_enabled(false);
    t3d_light_set_count(0);

    rdpq_mode_begin();
        rdpq_set_mode_standard();
        rdpq_mode_persp(true);
        rdpq_mode_fog(0);
        rdpq_mode_zbuf(false, false);
        rdpq_mode_antialias(AA_NONE);
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_mode_blender(0);
    rdpq_mode_end();

    rdpq_set_prim_color(RGBA32(0, 0, 0, 255));
    t3d_state_set_drawflags(T3D_FLAG_NO_LIGHT);

    if (characterReady) {
        t3d_viewport_attach(&s_characterRtShadowViewport);

        t3d_matrix_push_pos(1);
            t3d_matrix_set(character.modelMat, true);
            rspq_block_run(s_characterRtShadowGeometryDpl);
        t3d_matrix_pop(1);
    }

    if (bossReady) {
        t3d_viewport_attach(&s_bossRtShadowViewport);

        t3d_matrix_push_pos(1);
            t3d_matrix_set((T3DMat4FP *)boss->modelMat, true);
            rspq_block_run(s_bossRtShadowGeometryDpl);
        t3d_matrix_pop(1);
    }

    t3d_tri_sync();
    rdpq_detach();
}

static void guardian_rt_shadow_update_quad(
    T3DVertPacked *vertices,
    float actorX,
    float actorY,
    float actorZ,
    float groundY,
    float length,
    float halfWidth,
    float originBackOffset
) {
    if (!vertices) return;

    T3DVec3 rayDirection = guardian_rt_shadow_get_ray_direction(
        actorX,
        actorY,
        actorZ
    );

    float dirX = rayDirection.v[0];
    float dirZ = rayDirection.v[2];
    float dirLength = sqrtf(dirX * dirX + dirZ * dirZ);

    if (dirLength <= 0.0001f) {
        /*
         * The source is directly above the actor. Preserve the requested Z+
         * orientation as the horizontal fallback.
         */
        dirX = 0.0f;
        dirZ = 1.0f;
    } else {
        dirX /= dirLength;
        dirZ /= dirLength;
    }

    /* This matches both capture cameras' horizontal screen axis. */
    float rightX = -dirZ;
    float rightZ = dirX;

    float nearCenterX = actorX - dirX * originBackOffset;
    float nearCenterZ = actorZ - dirZ * originBackOffset;
    float farCenterX = nearCenterX + dirX * length;
    float farCenterZ = nearCenterZ + dirZ * length;

    float nearLeftX = nearCenterX - rightX * halfWidth;
    float nearLeftZ = nearCenterZ - rightZ * halfWidth;
    float nearRightX = nearCenterX + rightX * halfWidth;
    float nearRightZ = nearCenterZ + rightZ * halfWidth;
    float farLeftX = farCenterX - rightX * halfWidth;
    float farLeftZ = farCenterZ - rightZ * halfWidth;
    float farRightX = farCenterX + rightX * halfWidth;
    float farRightZ = farCenterZ + rightZ * halfWidth;

    const T3DVec3 floorNormal = {{0.0f, 1.0f, 0.0f}};
    uint16_t packedNormal = t3d_vert_pack_normal(&floorNormal);

    /*
     * Each sub-surface is exposed to the RDP as an independent local 64x64
     * texture, so both actor quads use the same 0..63 UV range.
     */
    const int16_t uvLeft = guardian_rt_shadow_uv(0.0f);
    const int16_t uvRight = guardian_rt_shadow_uv(
        (float)(GUARDIAN_RT_SHADOW_TILE_WIDTH - 1)
    );
    const int16_t uvTop = guardian_rt_shadow_uv(0.0f);
    const int16_t uvBottom = guardian_rt_shadow_uv(
        (float)(GUARDIAN_RT_SHADOW_TILE_HEIGHT - 1)
    );

    guardian_rt_shadow_write_vertex(
        &vertices[0],
        false,
        nearLeftX,
        groundY,
        nearLeftZ,
        uvLeft,
        uvBottom,
        packedNormal
    );
    guardian_rt_shadow_write_vertex(
        &vertices[0],
        true,
        nearRightX,
        groundY,
        nearRightZ,
        uvRight,
        uvBottom,
        packedNormal
    );
    guardian_rt_shadow_write_vertex(
        &vertices[1],
        false,
        farLeftX,
        groundY,
        farLeftZ,
        uvLeft,
        uvTop,
        packedNormal
    );
    guardian_rt_shadow_write_vertex(
        &vertices[1],
        true,
        farRightX,
        groundY,
        farRightZ,
        uvRight,
        uvTop,
        packedNormal
    );
}

static void guardian_rt_shadow_draw_quad(
    const surface_t *texture,
    T3DVertPacked *vertices,
    uint8_t alpha
) {
    if (!texture || !texture->buffer || !vertices) return;

    rdpq_set_prim_color(RGBA32(0, 0, 0, alpha));

    rdpq_texparms_t textureParams = (rdpq_texparms_t){};
    textureParams.s.repeats = 1;
    textureParams.t.repeats = 1;

    /*
     * One 64x64 I8 texture is 4096 bytes and therefore occupies all TMEM.
     * Upload the requested atlas half, draw it, flush the Tiny3D triangle,
     * then allow the next actor upload to replace TMEM.
     */
    rdpq_tex_upload(TILE0, texture, &textureParams);

    t3d_vert_load(vertices, 0, 4);
    t3d_quad_draw_unindexed(0, 1);
    t3d_tri_sync();
}

static void guardian_rt_shadows_draw(Boss *boss)
{
    if (!s_rtShadowsInitialized) return;
    if (!s_rtShadowIdentityMatrix) return;

    bool characterReady =
        character.visible
        && s_characterRtShadowVertices
        && s_characterRtShadowTexture.buffer;

    bool bossReady =
        boss
        && boss_is_active(boss)
        && boss->visible
        && s_bossRtShadowVertices
        && s_bossRtShadowTexture.buffer;

    if (!characterReady && !bossReady) return;

    if (characterReady) {
        guardian_rt_shadow_update_quad(
            s_characterRtShadowVertices,
            character.pos[0],
            character.pos[1],
            character.pos[2],
            GUARDIAN_RT_SHADOW_GROUND_Y,
            GUARDIAN_CHARACTER_RT_SHADOW_LENGTH,
            GUARDIAN_CHARACTER_RT_SHADOW_HALF_WIDTH,
            GUARDIAN_CHARACTER_RT_SHADOW_ORIGIN_BACK_OFFSET
        );
    }

    if (bossReady) {
        guardian_rt_shadow_update_quad(
            s_bossRtShadowVertices,
            boss->pos[0],
            boss->pos[1],
            boss->pos[2],
            GUARDIAN_RT_SHADOW_GROUND_Y,
            GUARDIAN_BOSS_RT_SHADOW_LENGTH,
            GUARDIAN_BOSS_RT_SHADOW_HALF_WIDTH,
            GUARDIAN_BOSS_RT_SHADOW_ORIGIN_BACK_OFFSET
        );
    }

    /*
     * Flush the preceding world triangles before changing the projection
     * combiner/texture state. No manual rdpq_sync_pipe() is needed.
     */
    t3d_tri_sync();

    rdpq_mode_push();

    rdpq_mode_begin();
        rdpq_set_mode_standard();

        if (!DITHER_ENABLED && !debugDraw) {
            rdpq_mode_dithering(DITHER_NONE_BAYER);
        }

        rdpq_mode_persp(true);
        rdpq_mode_fog(0);
        rdpq_mode_zbuf(true, false);
        rdpq_mode_antialias(AA_NONE);
        rdpq_mode_filter(FILTER_BILINEAR);
        rdpq_mode_combiner(
            RDPQ_COMBINER1(
                (0, 0, 0, PRIM),
                (1, TEX0, PRIM, 0)
            )
        );
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_end();

    t3d_state_set_drawflags(
        T3D_FLAG_DEPTH
        | T3D_FLAG_TEXTURED
        | T3D_FLAG_NO_LIGHT
    );
    t3d_state_set_depth_offset(GUARDIAN_RT_SHADOW_DEPTH_OFFSET);

    t3d_matrix_push_pos(1);
        t3d_matrix_set(s_rtShadowIdentityMatrix, true);

        if (characterReady) {
            guardian_rt_shadow_draw_quad(
                &s_characterRtShadowTexture,
                s_characterRtShadowVertices,
                GUARDIAN_CHARACTER_RT_SHADOW_ALPHA
            );
        }

        if (bossReady) {
            guardian_rt_shadow_draw_quad(
                &s_bossRtShadowTexture,
                s_bossRtShadowVertices,
                GUARDIAN_BOSS_RT_SHADOW_ALPHA
            );
        }

    t3d_matrix_pop(1);

    t3d_state_set_depth_offset(0);

    rdpq_mode_pop();
    rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
}


static void guardian_environment_object_clear(GuardianEnvironmentObject *object)
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
static void guardian_environment_clear_all_object_handles(void)
{
    guardian_environment_object_clear(&s_environmentFloor);
    guardian_environment_object_clear(&s_environmentWalls);
    guardian_environment_object_clear(&s_environmentWallsBack);
    guardian_environment_object_clear(&s_environmentPillars);
    guardian_environment_object_clear(&s_environmentBrokenStatue);
    guardian_environment_object_clear(&s_environmentNichesWindows);
    guardian_environment_object_clear(&s_environmentDecalsLayer1);
    guardian_environment_object_clear(&s_environmentDecalsLayer2);
    guardian_environment_object_clear(&s_environmentFogDoor);
    guardian_environment_object_clear(&s_environmentSunshafts);
}

static void guardian_environment_object_free(GuardianEnvironmentObject *object)
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

static bool guardian_environment_object_load_internal(
    GuardianEnvironmentObject *object,
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
        debugf("guardian_scene environment: failed to load model: %s\n", path);
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
        debugf("guardian_scene environment: failed to allocate matrix: %s\n", path);
        guardian_environment_object_free(object);
        return false;
    }

    t3d_mat4fp_from_srt_euler(
        object->matrix,
        (float[3]){
            GUARDIAN_ENVIRONMENT_SCALE,
            GUARDIAN_ENVIRONMENT_SCALE,
            GUARDIAN_ENVIRONMENT_SCALE
        },
        (float[3]){ 0.0f, 0.0f, 0.0f },
        (float[3]){ 0.0f, roomY, 0.0f }
    );

    return true;
}

static bool guardian_environment_object_load(
    GuardianEnvironmentObject *object,
    const char *path,
    bool drawCustom,
    ScrollParams *scrollParams
) {
    return guardian_environment_object_load_internal(
        object,
        path,
        drawCustom,
        scrollParams,
        false
    );
}

static bool guardian_environment_object_load_frozen(
    GuardianEnvironmentObject *object,
    const char *path
) {
    return guardian_environment_object_load_internal(
        object,
        path,
        false,
        NULL,
        true
    );
}

static bool guardian_environment_object_load_double_scroll(
    GuardianEnvironmentObject *object,
    const char *path,
    ScrollParams *scrollParams
) {
    bool loaded = guardian_environment_object_load(
        object,
        path,
        true,
        scrollParams
    );
    if (!loaded) return false;

    object->doubleScroll = true;
    return true;
}
static void guardian_environment_free_optimized_mip_chain_sprites(
    GuardianEnvironmentMipChain *chain
)
{
    if (!chain) return;

    for (int sourceIndex = 0;
         sourceIndex < GUARDIAN_ENVIRONMENT_OPTIMIZED_MAX_MIP_TEXTURE_COUNT;
         ++sourceIndex
    ) {
        if (chain->sprites[sourceIndex]) {
            sprite_free(chain->sprites[sourceIndex]);
            chain->sprites[sourceIndex] = NULL;
        }
    }

    chain->ready = false;
}

static void guardian_environment_free_optimized_mip_chains(
    GuardianEnvironmentMipChain *chains,
    int chainCount
)
{
    if (!chains || chainCount <= 0) return;

    for (int chainIndex = 0; chainIndex < chainCount; ++chainIndex) {
        guardian_environment_free_optimized_mip_chain_sprites(&chains[chainIndex]);
    }
}

static bool guardian_environment_optimized_mip_chain_uses_palette(
    const GuardianEnvironmentMipChain *chain
) {
    if (!chain || !chain->sprites[0]) return false;

    tex_format_t format = sprite_get_format(chain->sprites[0]);
    return format == FMT_CI4 || format == FMT_CI8;
}

static bool guardian_environment_is_power_of_two_u16(uint16_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

/* Match the tile settings Tiny3D would derive from the original material. */
static rdpq_texparms_t guardian_environment_get_material_texture_params(
    const T3DMaterialTexture *texture
) {
    rdpq_texparms_t params = (rdpq_texparms_t){};

    params.s.translate = texture->s.low;
    params.s.mirror = texture->s.mirror;
    params.s.repeats = REPEAT_INFINITE;
    params.s.scale_log = (int)texture->s.shift;

    if (texture->s.clamp) {
        params.s.repeats = guardian_environment_is_power_of_two_u16(texture->texWidth)
            ? (texture->s.height - texture->s.low + 1.0f) / (float)texture->texWidth
            : 1.0f;
    }

    params.t.translate = texture->t.low;
    params.t.mirror = texture->t.mirror;
    params.t.repeats = REPEAT_INFINITE;
    params.t.scale_log = (int)texture->t.shift;

    if (texture->t.clamp) {
        params.t.repeats = guardian_environment_is_power_of_two_u16(texture->texHeight)
            ? (texture->t.height - texture->t.low + 1.0f) / (float)texture->texHeight
            : 1.0f;
    }

    return params;
}

static bool guardian_environment_load_optimized_mip_chain(
    GuardianEnvironmentMipChain *chain,
    const T3DMaterial *material
) {
    if (!chain || !material) return false;

    guardian_environment_free_optimized_mip_chain_sprites(chain);

    if (chain->sourceTextureCount < 2
        || chain->sourceTextureCount
            > GUARDIAN_ENVIRONMENT_OPTIMIZED_MAX_MIP_TEXTURE_COUNT
    ) {
        debugf(
            "guardian_scene environment: mip chain '%s' has invalid source texture count %d\n",
            chain->chainName,
            chain->sourceTextureCount
        );
        return false;
    }

    if (chain->sourceStartRdpLevels[0] != 0) {
        debugf(
            "guardian_scene environment: mip chain '%s' source 0 must start at RDP level 0\n",
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
            || currentStart >= GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP_RDP_LEVEL_COUNT
        ) {
            debugf(
                "guardian_scene environment: mip chain '%s' has invalid source %d "
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
            "guardian_scene environment: mip chain '%s' leaves no texture shift room for %d mip textures\n",
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
                "guardian_scene environment: mip chain '%s' source %d has no texture path\n",
                chain->chainName,
                sourceIndex
            );
            guardian_environment_free_optimized_mip_chain_sprites(chain);
            return false;
        }

        chain->sprites[sourceIndex] = sprite_load(path);

        if (!chain->sprites[sourceIndex]) {
            debugf(
                "guardian_scene environment: failed to load mip chain '%s' source %d: %s\n",
                chain->chainName,
                sourceIndex,
                path
            );
            guardian_environment_free_optimized_mip_chain_sprites(chain);
            return false;
        }
    }

    sprite_t *base = chain->sprites[0];
    tex_format_t baseFormat = sprite_get_format(base);

    if (base->width != material->textureA.texWidth
        || base->height != material->textureA.texHeight
    ) {
        debugf(
            "guardian_scene environment: mip chain '%s' base is %dx%d; model expects %dx%d\n",
            chain->chainName,
            base->width,
            base->height,
            material->textureA.texWidth,
            material->textureA.texHeight
        );
        guardian_environment_free_optimized_mip_chain_sprites(chain);
        return false;
    }

    if ((baseFormat == FMT_CI4 || baseFormat == FMT_CI8)
        && !sprite_get_palette(base)
    ) {
        debugf(
            "guardian_scene environment: CI mip chain '%s' source 0 has no palette\n",
            chain->chainName
        );
        guardian_environment_free_optimized_mip_chain_sprites(chain);
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
                "guardian_scene environment: mip chain '%s' source %d is %dx%d; expected %dx%d\n",
                chain->chainName,
                sourceIndex,
                current->width,
                current->height,
                expectedWidth,
                expectedHeight
            );
            guardian_environment_free_optimized_mip_chain_sprites(chain);
            return false;
        }

        if (sprite_get_format(current) != baseFormat) {
            debugf(
                "guardian_scene environment: mip chain '%s' source %d format differs from source 0\n",
                chain->chainName,
                sourceIndex
            );
            guardian_environment_free_optimized_mip_chain_sprites(chain);
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
static bool guardian_environment_optimized_mip_chain_matches_material(
    const GuardianEnvironmentMipChain *chain,
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
            "guardian_scene environment: shared mip chain '%s' base is %dx%d; "
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
            "guardian_scene environment: shared mip chain '%s' leaves no texture shift "
            "room for this material\n",
            chain->chainName
        );
        return false;
    }

    return true;
}

static void guardian_environment_upload_optimized_mip_chain(
    const GuardianEnvironmentMipChain *chain,
    const T3DMaterial *material
) {
    rdpq_texparms_t sourceParams =
        guardian_environment_get_material_texture_params(&material->textureA);
    bool usesPalette =
        guardian_environment_optimized_mip_chain_uses_palette(chain);
    int sourceIndex = 0;

    rdpq_tex_multi_begin();

    for (int rdpLevel = 0;
         rdpLevel < GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP_RDP_LEVEL_COUNT;
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

static GuardianEnvironmentMipChain *guardian_environment_find_optimized_mip_chain(
    T3DMaterial *material,
    GuardianEnvironmentMipChain *chains,
    T3DMaterial *const *boundMaterials,
    int chainCount
)
{
    if (!material || !chains || !boundMaterials || chainCount <= 0) {
        return NULL;
    }

    for (int chainIndex = 0; chainIndex < chainCount; ++chainIndex) {
        GuardianEnvironmentMipChain *chain = &chains[chainIndex];

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
static void guardian_environment_record_optimized_model_with_mipmaps(
    const T3DModel *model,
    GuardianEnvironmentMipChain *chains,
    T3DMaterial *const *boundMaterials,
    int chainCount
) {
    T3DModelState state = t3d_model_state_create();
    T3DModelIter iterator = t3d_model_iter_create(
        model,
        T3D_CHUNK_TYPE_OBJECT
    );
    GuardianEnvironmentMipChain *activeMipChain = NULL;
    T3DMaterial *activeMipMaterial = NULL;

    while (t3d_model_iter_next(&iterator)) {
        T3DObject *modelObject = iterator.object;
        T3DMaterial *material = modelObject->material;

        if (material) {
            GuardianEnvironmentMipChain *requestedMipChain =
                guardian_environment_find_optimized_mip_chain(
                    material,
                    chains,
                    boundMaterials,
                    chainCount
                );

            if (requestedMipChain && requestedMipChain != activeMipChain) {
                guardian_environment_upload_optimized_mip_chain(
                    requestedMipChain,
                    material
                );
                rdpq_mode_mipmap(
                    GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP_MODE,
                    GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP_RDP_LEVEL_COUNT
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

                if (guardian_environment_optimized_mip_chain_uses_palette(
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

        if (guardian_environment_optimized_mip_chain_uses_palette(
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

static void guardian_environment_record_object_commands(
    const GuardianEnvironmentObject *object
) {
    if (!object || !object->model) return;

    if (object->optimizedMipDraw) {
        guardian_environment_record_optimized_model_with_mipmaps(
            object->model,
            object->mipChains,
            object->boundMipMaterials,
            object->mipChainCount
        );
        return;
    }

    t3d_model_draw(object->model);
}

static void guardian_environment_record_regular_object_display_list(
    GuardianEnvironmentObject *object
) {
    if (!object || !object->model) return;

    rspq_block_begin();
        guardian_environment_record_object_commands(object);
    object->dpl = rspq_block_end();
    object->drawCustom = false;
}

static bool guardian_environment_record_frozen_object_display_list(
    GuardianEnvironmentObject *object
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
        guardian_environment_record_object_commands(object);
    object->dpl = rspq_block_end_frozen();
    object->drawCustom = false;

    return object->dpl != NULL;
}

static void guardian_environment_log_frozen_stale_reasons(
    const GuardianEnvironmentObject *object,
    int reasons,
    const char *context
) {
    debugf(
        "guardian_scene environment: frozen block %s for %s; reasons=%08lx",
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

static bool guardian_environment_object_load_optimized_mip_model(
    GuardianEnvironmentObject *object,
    const char *modelPath,
    const char *modelName,
    GuardianEnvironmentMipChain *chains,
    int chainCount,
    bool resetMipSprites,
    bool frozen
)
{
    if (!object || !modelPath || !modelName || !chains || chainCount <= 0) {
        return false;
    }

    if (chainCount > GUARDIAN_ENVIRONMENT_OPTIMIZED_MAX_MIP_CHAIN_COUNT) {
        debugf(
            "guardian_scene environment: %s requests %d mip chains; maximum is %d\n",
            modelName,
            chainCount,
            GUARDIAN_ENVIRONMENT_OPTIMIZED_MAX_MIP_CHAIN_COUNT
        );
        return false;
    }

    if (resetMipSprites) {
        guardian_environment_free_optimized_mip_chains(chains, chainCount);
    }

    bool loaded = guardian_environment_object_load_internal(
        object,
        modelPath,
        true,
        NULL,
        frozen
    );

    if (!loaded) return false;

    int readyChainCount = 0;
    T3DMaterial *boundMaterials[
        GUARDIAN_ENVIRONMENT_OPTIMIZED_MAX_MIP_CHAIN_COUNT
    ] = { NULL };

    for (int chainIndex = 0; chainIndex < chainCount; ++chainIndex) {
        GuardianEnvironmentMipChain *chain = &chains[chainIndex];
        T3DMaterial *material = t3d_model_get_material(
            object->model,
            chain->materialName
        );

        if (!material) {
            debugf(
                "guardian_scene environment: material '%s' not found in %s; "
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
                "guardian_scene environment: material '%s' in %s already uses TILE1; "
                "that material cannot use the custom mip chain\n",
                chain->materialName,
                modelName
            );
            continue;
        }

        if (!material->textureA.texPath) {
            debugf(
                "guardian_scene environment: material '%s' in %s has no loadable "
                "textureA path; "
                "that material will use its normal draw\n",
                chain->materialName,
                modelName
            );
            continue;
        }

        bool chainAvailable = chain->ready
            ? guardian_environment_optimized_mip_chain_matches_material(
                chain,
                material
            )
            : guardian_environment_load_optimized_mip_chain(chain, material);

        if (!chainAvailable) {
            debugf(
                "guardian_scene environment: material '%s' in %s custom mip chain '%s' "
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
            "guardian_scene environment: material '%s' in %s using mip chain '%s': "
            "%d textures across %d RDP mip levels\n",
            chain->materialName,
            modelName,
            chain->chainName,
            chain->sourceTextureCount,
            GUARDIAN_ENVIRONMENT_OPTIMIZED_MIP_RDP_LEVEL_COUNT
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
            "guardian_scene environment: no custom mip chains are available for %s; "
            "using the model's normal draw\n",
            modelName
        );
    }

    /*
     * Frozen display lists are recorded lazily by guardian_environment_object_draw()
     * after the object's Z-buffer/pass state has been established.
     */
    if (!object->frozen) {
        guardian_environment_record_regular_object_display_list(object);
    }

    return true;
}

static bool guardian_environment_object_load_optimized_floor(bool frozen)
{
    return guardian_environment_object_load_optimized_mip_model(
        &s_environmentFloor,
        GUARDIAN_ENVIRONMENT_PATH_PREFIX "test-floor-opt.t3dm",
        "test-floor-opt",
        s_environmentFloorMipChains,
        GUARDIAN_ENVIRONMENT_OPTIMIZED_FLOOR_MIP_CHAIN_COUNT,
        true,
        frozen
    );
}

static bool guardian_environment_object_load_optimized_walls(bool frozen)
{
    return guardian_environment_object_load_optimized_mip_model(
        &s_environmentWalls,
        GUARDIAN_ENVIRONMENT_PATH_PREFIX "test-walls-opt.t3dm",
        "test-walls-opt",
        s_environmentWallsMipChains,
        GUARDIAN_ENVIRONMENT_OPTIMIZED_WALLS_MIP_CHAIN_COUNT,
        false,
        frozen
    );
}

static bool guardian_environment_object_load_optimized_walls_back(bool frozen)
{
    return guardian_environment_object_load_optimized_mip_model(
        &s_environmentWallsBack,
        GUARDIAN_ENVIRONMENT_PATH_PREFIX "test-walls_back-opt.t3dm",
        "test-walls_back-opt",
        s_environmentWallsMipChains,
        GUARDIAN_ENVIRONMENT_OPTIMIZED_WALLS_BACK_MIP_CHAIN_COUNT,
        false,
        frozen
    );
}

static void guardian_environment_object_draw(GuardianEnvironmentObject *object)
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
            guardian_environment_log_frozen_stale_reasons(
                object,
                rdpq_block_stale_reasons(object->dpl),
                "stale"
            );
        }

        if (!guardian_environment_record_frozen_object_display_list(object)) {
            debugf(
                "guardian_scene environment: failed to record frozen block for %s\n",
                object->path ? object->path : "<unknown>"
            );
            return;
        }

        if (!rspq_block_run_frozen(object->dpl)) {
            int reasons = rdpq_block_stale_reasons(object->dpl);
            guardian_environment_log_frozen_stale_reasons(
                object,
                reasons,
                "immediately stale after recording"
            );

            /*
             * Preserve the frame visually while exposing the state-tracking
             * failure in the debug log. This fallback should never be reached
             * in a stable pass.
             */
            guardian_environment_record_object_commands(object);
        }

        return;
    }

    if (object->dpl) {
        rspq_block_run(object->dpl);
    }
}


static void guardian_environment_load(void)
{
    guardian_environment_object_load_optimized_floor(true);

    /*
     * The wall models share their manually loaded mip sprites. Reset the shared
     * chains once, then bind each model independently.
     */
    guardian_environment_free_optimized_mip_chains(
        s_environmentWallsMipChains,
        GUARDIAN_ENVIRONMENT_OPTIMIZED_WALLS_MIP_CHAIN_COUNT
    );
    guardian_environment_object_load_optimized_walls(true);
    guardian_environment_object_load_optimized_walls_back(true);

    guardian_environment_object_load_frozen(
        &s_environmentPillars,
        GUARDIAN_ENVIRONMENT_PATH_PREFIX "test-pillars-opt.t3dm"
    );
    guardian_environment_object_load_frozen(
        &s_environmentBrokenStatue,
        GUARDIAN_ENVIRONMENT_PATH_PREFIX "test-broken_statue-opt.t3dm"
    );
    guardian_environment_object_load_frozen(
        &s_environmentNichesWindows,
        GUARDIAN_ENVIRONMENT_PATH_PREFIX "test-niches_windows-opt.t3dm"
    );
    guardian_environment_object_load_frozen(
        &s_environmentDecalsLayer1,
        GUARDIAN_ENVIRONMENT_PATH_PREFIX "test-decals_layer1-opt.t3dm"
    );
    guardian_environment_object_load_frozen(
        &s_environmentDecalsLayer2,
        GUARDIAN_ENVIRONMENT_PATH_PREFIX "test-decals_layer2-opt.t3dm"
    );

    /* The scrolling fog door remains dynamic. */
    guardian_environment_object_load(
        &s_environmentFogDoor,
        GUARDIAN_ENVIRONMENT_PATH_PREFIX "test-fog_door.t3dm",
        true,
        &s_environmentFogScrollParams
    );

    /*
     * test-sunshafts.glb is converted by the asset pipeline to this runtime
     * T3DM. It stays dynamic because both texture tiles scroll every frame.
     */
    guardian_environment_object_load_double_scroll(
        &s_environmentSunshafts,
        GUARDIAN_ENVIRONMENT_PATH_PREFIX "test-sunshafts.t3dm",
        &s_environmentSunshaftsScrollParams
    );
}

static void guardian_environment_delete(void)
{
    guardian_environment_object_free(&s_environmentFloor);
    guardian_environment_free_optimized_mip_chains(
        s_environmentFloorMipChains,
        GUARDIAN_ENVIRONMENT_OPTIMIZED_FLOOR_MIP_CHAIN_COUNT
    );

    /*
     * Free both wall blocks before releasing the mip sprites referenced by
     * their recorded command streams.
     */
    guardian_environment_object_free(&s_environmentWalls);
    guardian_environment_object_free(&s_environmentWallsBack);
    guardian_environment_free_optimized_mip_chains(
        s_environmentWallsMipChains,
        GUARDIAN_ENVIRONMENT_OPTIMIZED_WALLS_MIP_CHAIN_COUNT
    );

    guardian_environment_object_free(&s_environmentPillars);
    guardian_environment_object_free(&s_environmentBrokenStatue);
    guardian_environment_object_free(&s_environmentNichesWindows);
    guardian_environment_object_free(&s_environmentDecalsLayer1);
    guardian_environment_object_free(&s_environmentDecalsLayer2);
    guardian_environment_object_free(&s_environmentFogDoor);
    guardian_environment_object_free(&s_environmentSunshafts);
}


/*
 * Guardian scene dither policy
 * ----------------------------
 *
 * In the non-dithered display path:
 *
 * - Tiny3D distance fog on opaque geometry uses DITHER_NONE_NONE so the fog
 *   gradient is not converted into a Bayer transparency pattern.
 * - Explicit transparent effects use DITHER_NONE_BAYER, preserving Bayer
 *   alpha dithering for decals, fog-door geometry, sunshafts, floor effects,
 *   projected shadows, trails, particles and UI.
 *
 * These modes are established per pass, after viewport attachment. Each frozen
 * environment object is therefore always recorded and replayed under the same
 * dither state.
 */
static void guardian_scene_use_undithered_fog(void)
{
    if (DITHER_ENABLED || debugDraw) {
        return;
    }

    rdpq_mode_begin();
        rdpq_mode_dithering(DITHER_NONE_NONE);
    rdpq_mode_end();
}

static void guardian_scene_use_dithered_transparency(void)
{
    if (DITHER_ENABLED || debugDraw) {
        return;
    }

    rdpq_mode_begin();
        rdpq_mode_dithering(DITHER_NONE_BAYER);
    rdpq_mode_end();
}


// ------------------------------------------------------------
// Optimized environment draw passes
// ------------------------------------------------------------

void scene_draw_environment_walls(void)
{
    guardian_scene_use_undithered_fog();
    rdpq_mode_zbuf(false, false);

    t3d_matrix_push_pos(1);
        guardian_environment_object_draw(&s_environmentWallsBack);
        guardian_environment_object_draw(&s_environmentWalls);
    t3d_matrix_pop(1);

    rdpq_mode_zbuf(true, true);
}

void scene_draw_environment_floor(void)
{
    guardian_scene_use_undithered_fog();
    rdpq_mode_zbuf(true, true);

    t3d_matrix_push_pos(1);
        guardian_environment_object_draw(&s_environmentFloor);
    t3d_matrix_pop(1);
}

void scene_draw_environment_niches_windows(void)
{
    guardian_scene_use_undithered_fog();
    rdpq_mode_zbuf(true, true);

    t3d_matrix_push_pos(1);
        guardian_environment_object_draw(&s_environmentNichesWindows);
    t3d_matrix_pop(1);
}

void scene_draw_environment_decals(void)
{
    guardian_scene_use_dithered_transparency();
    rdpq_mode_zbuf(false, false);

    t3d_matrix_push_pos(1);
        guardian_environment_object_draw(&s_environmentDecalsLayer1);
        guardian_environment_object_draw(&s_environmentDecalsLayer2);
    t3d_matrix_pop(1);

    rdpq_mode_zbuf(true, true);
    guardian_scene_use_undithered_fog();
}

void scene_draw_environment_pillars_statue(void)
{
    guardian_scene_use_undithered_fog();
    rdpq_mode_zbuf(true, true);

    t3d_matrix_push_pos(1);
        guardian_environment_object_draw(&s_environmentPillars);
        guardian_environment_object_draw(&s_environmentBrokenStatue);
    t3d_matrix_pop(1);
}

void scene_draw_environment_sunshafts(void)
{
    guardian_scene_use_dithered_transparency();
    rdpq_mode_zbuf(false, false);

    t3d_matrix_push_pos(1);
        guardian_environment_object_draw(&s_environmentSunshafts);
    t3d_matrix_pop(1);

    rdpq_mode_zbuf(true, true);
    guardian_scene_use_undithered_fog();
}

void scene_draw_environment_fog_door(void)
{
    guardian_scene_use_dithered_transparency();
    rdpq_mode_zbuf(true, false);

    t3d_matrix_push_pos(1);
        guardian_environment_object_draw(&s_environmentFogDoor);
    t3d_matrix_pop(1);

    rdpq_mode_zbuf(true, true);
    guardian_scene_use_undithered_fog();
}

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
    guardian_environment_load();

    // Persistent ceiling chains remain separate from the replacement room.
    chainsModel = t3d_model_load("rom:/boss_room/ceiling_chains.t3dm");
    if (chainsModel) {
        rspq_block_begin();
            t3d_model_draw(chainsModel);
        chainsDpl = rspq_block_end();
    }

    chainsMatrix = malloc_uncached(sizeof(T3DMat4FP));
    if (chainsMatrix) {
        t3d_mat4fp_from_srt_euler(
            chainsMatrix,
            (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
            (float[3]){0.0f, 0.0f, 0.0f},
            (float[3]){0.0f, roomY, 0.0f}
        );
    }

    // Existing death-state floor glow remains independent of the room meshes.
    floorGlowModel = t3d_model_load("rom:/boss_room/floor_glow.t3dm");
    if (floorGlowModel) {
        rspq_block_begin();
            t3d_model_draw(floorGlowModel);
        floorGlowDpl = rspq_block_end();
    }

    floorGlowMatrix = malloc_uncached(sizeof(T3DMat4FP));
    if (floorGlowMatrix) {
        t3d_mat4fp_from_srt_euler(
            floorGlowMatrix,
            (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
            (float[3]){0.0f, 0.0f, 0.0f},
            (float[3]){0.0f, roomY, 0.0f}
        );
    }

    // Existing phase-2 boss-chain glow remains a boss/cutscene asset.
    bossChainsGlowModel = t3d_model_load("rom:/boss/boss_chain_glow.t3dm");
    if (bossChainsGlowModel) {
        rspq_block_begin();
            t3d_model_draw(bossChainsGlowModel);
        bossChainsGlowDpl = rspq_block_end();
    }

    bossChainsGlowMatrix = malloc_uncached(sizeof(T3DMat4FP));
    if (bossChainsGlowMatrix) {
        t3d_mat4fp_from_srt_euler(
            bossChainsGlowMatrix,
            (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
            (float[3]){0.0f, 0.0f, 0.0f},
            (float[3]){0.0f, roomY, 0.0f}
        );
    }
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

    guardian_environment_clear_all_object_handles();
    scene_load_environment();

    g_boss = boss_spawn();
    if (!g_boss) {
        // Handle error
        return;
    }

    // The scene owns when the boss is commanded into/out of gameplay,
    // but the boss owns the actual active/cinematic/combat mode internally.
    boss_deactivate(g_boss);

    /*
     * The shared actor-shadow system needs both the already-owned character
     * model/skeleton and the newly spawned boss model/skeleton.
     */
    if (!guardian_rt_shadows_init(g_boss)) {
        debugf("guardian scene: realtime actor shadows failed to initialize\n");
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

#if GUARDIAN_DEBUG_START_BOSS_DEFEATED
    /*
     * Enter normal gameplay first so the character, camera and boss transforms
     * are initialized exactly as they would be after skipping the intro.
     */
    gameState = GAME_STATE_PLAYING;
    cutscene_manager_reset();
    scene_init_playing(true);

    /*
     * Reuse the existing debug defeat path so the boss is fully stopped,
     * non-attacking, collider-safe and placed in its post-defeat mode.
     */
    scene_debug_force_boss_defeated();

    /*
     * This startup override is for free testing, not for showing the victory
     * card or recording a completed boss attempt.
     */
    gameState = GAME_STATE_PLAYING;
    victoryTitleTimer = 0.0f;
    victoryTitleDone = true;
    bossWasDead = true;

    s_bossRunActive = false;
    s_bossRunStartS = 0.0;
    s_pendingBossLoopMusic = false;

    cameraLockOnActive = false;
    cameraLockBlend = 0.0f;
    bossTitleFade = 0.0f;
    bossUiIntro = 1.0f;
    boss_ui_set_intro(bossUiIntro);

    audio_stop_music();
    audio_stop_all_sfx();
#else
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
#endif
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
    screen_shake_set_shake_mag(0.0f);

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
    screen_shake_set_shake_mag(0.0f);
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
             * Render the complete replacement room while this camera/dialog-only
             * cutscene is active.
             */
            scene_draw_environment_walls();
            scene_draw_environment_sunshafts();
            scene_draw_environment_floor();

            guardian_scene_use_dithered_transparency();
            rdpq_mode_zbuf(false, false);
            t3d_matrix_push_pos(1);
                /* Character blob shadow is replaced by the realtime projection. */

                if (g_boss) {
                    boss_draw_shadow(g_boss);
                }
            t3d_matrix_pop(1);
            guardian_scene_use_undithered_fog();

            scene_draw_environment_niches_windows();
            scene_draw_environment_decals();
            scene_draw_environment_pillars_statue();

            rdpq_mode_zbuf(true, true);

            t3d_matrix_push_pos(1);
                guardian_actor_directional_light_enable(
                    character.pos[0],
                    character.pos[2]
                );
                character_draw();

                if (g_boss) {
                    guardian_actor_directional_light_enable(
                        g_boss->pos[0],
                        g_boss->pos[2]
                    );
                    boss_draw(g_boss);
                }
            t3d_matrix_pop(1);

            guardian_actor_directional_light_disable();

            scene_draw_environment_fog_door();

            // Persistent gameplay chains only.
            t3d_matrix_push_pos(1);
                if (chainsMatrix && chainsDpl) {
                    t3d_matrix_set(chainsMatrix, true);
                    rspq_block_run(chainsDpl);
                }
            t3d_matrix_pop(1);

            if (cutsceneDialogActive) {
                guardian_scene_use_dithered_transparency();

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

    /*
     * Capture only during normal gameplay. The main framebuffer is already
     * attached by main.c and is restored automatically by rdpq_detach().
     */
    if (cutsceneState == CUTSCENE_NONE) {
        guardian_rt_shadows_capture(g_boss);
    }

    /* Start the real onscreen Tiny3D pass from a clean state. */
    t3d_frame_start();
    t3d_viewport_attach(viewport);

    // Fog
    color_t fogColor = (color_t){0, 0, 0, 0xFF};

    rdpq_mode_fog(RDPQ_FOG_STANDARD);
    rdpq_set_fog_color(fogColor);

    t3d_screen_clear_color(RGBA32(0, 0, 0, 0xFF));
    t3d_screen_clear_depth();

    if(cutsceneState != CUTSCENE_NONE){
        cutscene_manager_draw_fog();
    }else{
        t3d_fog_set_range(100.0f, 450.0f);
    }
    t3d_fog_set_enabled(true);

    /*
     * Tiny3D distance fog begins with both RGB and alpha dithering disabled.
     * Explicit transparent passes opt back into Bayer alpha dithering locally.
     */
    guardian_scene_use_undithered_fog();

    // Lighting: environment remains ambient-only.
    t3d_light_set_ambient(colorAmbient);
    t3d_light_set_count(0);

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

    scene_draw_environment_walls();
    //scene_draw_environment_sunshafts();
    scene_draw_environment_floor();

    // Projection effects remain between the floor and room details.
    guardian_scene_use_dithered_transparency();
    rdpq_mode_zbuf(false, false);
    t3d_matrix_push_pos(1);
        boss_ground_crush_draw();
        /* Character and boss blob shadows are replaced by realtime projections. */
    t3d_matrix_pop(1);
    guardian_scene_use_undithered_fog();

    scene_draw_environment_niches_windows();
    scene_draw_environment_decals();
    scene_draw_environment_pillars_statue();

    // Existing death-state floor glow remains separate from the room replacement.
    if (g_boss && g_boss->health <= 0 && floorGlowMatrix && floorGlowModel) {
        guardian_scene_use_dithered_transparency();
        rdpq_mode_zbuf(false, false);
        t3d_matrix_push_pos(1);
            t3d_matrix_set(floorGlowMatrix, true);
            t3d_model_draw_custom(floorGlowModel, (T3DModelDrawConf){
                .userData = &floorGlowScrollParams,
                .tileCb = tile_scroll,
            });
        t3d_matrix_pop(1);
        guardian_scene_use_undithered_fog();
    }

    /*
     * Enable the directional light only while drawing the two animated actors.
     * Environment, projection effects, particles and UI remain ambient-only.
     */
    guardian_scene_use_undithered_fog();
    rdpq_mode_zbuf(true, true);

    t3d_matrix_push_pos(1);
        guardian_actor_directional_light_enable(
            character.pos[0],
            character.pos[2]
        );
        character_draw();

        if (g_boss) {
            guardian_actor_directional_light_enable(
                g_boss->pos[0],
                g_boss->pos[2]
            );
            boss_draw(g_boss);
        }
    t3d_matrix_pop(1);

    guardian_actor_directional_light_disable();

    scene_draw_environment_fog_door();

    guardian_scene_use_undithered_fog();
    t3d_matrix_push_pos(1);
        if (chainsMatrix && chainsDpl) {
            t3d_matrix_set(chainsMatrix, true);
            rspq_block_run(chainsDpl);
        }
    t3d_matrix_pop(1);

    /*
     * Draw the realtime floor projections after all normal actor/environment
     * models. They depth-test against the completed scene without writing depth.
     */
    guardian_rt_shadows_draw(g_boss);

    /*
     * These are explicit transparent effects rather than Tiny3D distance fog.
     */
    guardian_scene_use_dithered_transparency();

    // Screen-space ribbon trails, drawn right after 3D so they feel "in world"
    sword_trail_draw_all(viewport);

    // Dust puffs (boss landings/impacts)
    dust_particles_fx_update(deltaTime);
    dust_particles_fx_draw(viewport);

    // Blood splatters from boss hits (drawn after dust so red reads on top of puffs)
    blood_particles_fx_update(deltaTime);
    blood_particles_fx_draw(viewport);

    /*
     * UI and overlays also use transparent sprites and rectangles, so Bayer
     * alpha dithering remains selected for the rest of the frame.
     */

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
        // Boss UI is owned by boss_render/boss_ui.
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
    guardian_environment_delete();

    if (chainsDpl) {
        rspq_block_free(chainsDpl);
        chainsDpl = NULL;
    }
    if (floorGlowDpl) {
        rspq_block_free(floorGlowDpl);
        floorGlowDpl = NULL;
    }
    if (bossChainsGlowDpl) {
        rspq_block_free(bossChainsGlowDpl);
        bossChainsGlowDpl = NULL;
    }

    if (chainsModel) {
        t3d_model_free(chainsModel);
        chainsModel = NULL;
    }
    if (floorGlowModel) {
        t3d_model_free(floorGlowModel);
        floorGlowModel = NULL;
    }
    if (bossChainsGlowModel) {
        t3d_model_free(bossChainsGlowModel);
        bossChainsGlowModel = NULL;
    }

    if (chainsMatrix) {
        free_uncached(chainsMatrix);
        chainsMatrix = NULL;
    }
    if (floorGlowMatrix) {
        free_uncached(floorGlowMatrix);
        floorGlowMatrix = NULL;
    }
    if (bossChainsGlowMatrix) {
        free_uncached(bossChainsGlowMatrix);
        bossChainsGlowMatrix = NULL;
    }
}


void scene_cleanup(void)
{
    rspq_wait();
    guardian_rt_shadows_cleanup();
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