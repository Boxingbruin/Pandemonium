#include "multi_sword_attacks.h"

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmodel.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <rspq.h>
#include <rdpq.h>
#include <sprite.h>

#include <assert.h>

#include "character.h"
#include "simple_collision_utility.h"
#include "debug_draw.h"
#include "dev.h"
#include "globals.h"
#include "game_math.h"

#include "path_ribbon.h"
#include "fx/lightning_fx.h"

// ============================================================
// PERF / FEATURE TOGGLES
// ============================================================
#define MSA_DO_MOVEMENT 1
#define MSA_DO_BODY_COLLISION 1
#define MSA_DO_WALL_COLLISION 1
#define MSA_DIR_RENORM_PERIOD 4
#define MSA_FACE_DIR 1

#define MSA_WALLS_BLOCKING 1

// ============================================================
// CONFIG
// ============================================================
#define MSA_MAX_SWORDS 16
#define MSA_COLLISION_HZ 30

#define MSA_PATH_MAX_POINTS 13
#define MSA_PATH_MIN_STEP   48.0f

// ============================================================
// SPEED / TIMING TUNABLES
// ============================================================
#define MSA_FIG8_TIME_SCALE 0.1f
#define MSA_MOVE_SPEED_MULT 0.13f
#define MSA_DROP_FALL_TIME_SEC 1.0f
#define MSA_DESCEND_SPEED 110.0f

// ============================================================
// ATTACK TIMING / GEOMETRY
// ============================================================
static float gFloorY   = 3.0f;
static const float CEILING_Y = 595.0f;

static const float CEILING_HOLD_SEC = 5.0f;

static const float HAZARD_HEIGHT = 20.0f;
static const float WALL_HEIGHT = 15.0f;

static float gClusterRadius = 220.0f;
static float gMinSpacing    = 80.0f;

static const float DROP_INTERVAL_SEC  = 0.36f;   // was 0.18f
static const float LAND_PAUSE_SEC = 2.0f;

// ============================================================
// MOVEMENT STAGE (FIGURE-8)
// ============================================================
static const float FIG8_STAGE_SEC = 10.0f;       // was 5.0f
static const float FIG8_FREQ_HZ   = 0.55f;
static const float FIG8_AMP_X     = 160.0f;
static const float FIG8_AMP_Z     = 110.0f;
static const float FIG8_DRIFT_SPEED = 40.0f;

static const float DESPAWN_Y     = -120.0f;

static const float MSA_MAX_XZ_SPEED = 520.0f;

static const float SWORD_RADIUS = 9.0f;

static const float WALL_THICKNESS = 10.0f;

static const float DMG_BODY     = 22.0f;
static const float DMG_WALL     = 12.0f;
static const float HIT_COOLDOWN = 0.25f;

// Model forward axis is -X, so yaw needs a PI flip.
static const float MSA_MODEL_YAW_OFFSET = (float)T3D_PI;

// ============================================================
// MODEL ASSETS (owned by MSA)
// ============================================================
static T3DModel*      floorGlowModel   = NULL;
static rspq_block_t*  floorGlowDpl     = NULL;
static void*          floorGlowMatrixBase = NULL;
static T3DMat4FP*     floorGlowMatrix  = NULL; // [MSA_MAX_SWORDS]

static ScrollParams floorGlowScrollParams = {
    .xSpeed = 0.0f,
    .ySpeed = 10.0f,
    .scale  = 64
};

static T3DModel*      swordModel       = NULL;
static rspq_block_t*  swordDpl         = NULL;
static void*          swordMatrixBase  = NULL;
static T3DMat4FP*     swordMatrix      = NULL; // [MSA_MAX_SWORDS]

// Lightning FX instance (opaque => keep pointer)
static LightningFX *gLightningFx = NULL;

static sprite_t *sWallFogSpr = NULL;

// ============================================================
// INTERNAL TYPES
// ============================================================
typedef enum {
    MSA_PHASE_CEILING_SETUP = 0,
    MSA_PHASE_DROPPING      = 1,
    MSA_PHASE_POST_LAND     = 2,
    MSA_PHASE_SCURVE        = 3,
    MSA_PHASE_DESCEND       = 4
} MsaAttackPhase;

typedef enum {
    SW_INACTIVE = 0,
    SW_CEILING  = 1,
    SW_FALLING  = 2,
    SW_LANDED   = 3,
    SW_SCURVE   = 4,
    SW_DESCEND  = 5,
    SW_AERIAL_FLY = 6,
    SW_AERIAL_AIM = 7,
    SW_AERIAL_STUCK = 8
} MsaSwordState;

typedef struct {
    float pos[3];
    float dir[2];
    float t;
    uint32_t seed;

    MsaSwordState state;

    float spawnX;
    float spawnZ;

    float fallT;     // 0..1
    float fallTime;  // sec

    float figPhase;      // 0..2pi
    float driftDirX;     // unit-ish
    float driftDirZ;     // unit-ish

    PathRibbon ribbon;

    float lastRibbonXZ[2];

    uint8_t renormTick;

    uint8_t glowVisible;
} MsaSword;

// ============================================================
// GLOBALS
// ============================================================
static int  gCount   = 5;
static bool gEnabled = true;
static MsaPattern gPattern = MSA_PATTERN_GROUND_SWEEP;

static MsaSword gSwords[MSA_MAX_SWORDS];

static float gHitCd = 0.0f;
static float gCollisionAcc = 0.0f;

static MsaAttackPhase gPhase = MSA_PHASE_CEILING_SETUP;
static float gPhaseT = 0.0f;

static int   gDropOrder[MSA_MAX_SWORDS];
static int   gDropNext = 0;
static float gDropAcc  = 0.0f;

static float gLoopDelay = 0.0f;

static uint8_t gDidSpawnThisCycle = 0;

static bool gAerialMode = false;
static float gAerialTargets[MSA_MAX_SWORDS][3];

// Ground-sweep single-cycle mode: set true while the boss is running the attack;
// cleared + gGroundSweepDone set true once the DESCEND phase fully completes.
static bool gGroundSweepActive = false;
static bool gGroundSweepDone   = false;
static const float AERIAL_SPEED = 1000.0f;
static const float AERIAL_MODEL_PITCH_OFFSET = 0.0f;
static float gAerialAimTimer[MSA_MAX_SWORDS];
static const float AERIAL_AIM_TIME = 0.55f;  // how long the aim-rotate window lasts
static float gAerialStickTimer[MSA_MAX_SWORDS];
static const float AERIAL_STUCK_TIME = 0.75f;
static const float AERIAL_SINK_SPEED = 120.0f;
static const float AERIAL_SINK_DEPTH = 28.0f;

// Per-sword start angles (captured when fired from SW_CEILING)
static float gAerialStartYaw  [MSA_MAX_SWORDS];
static float gAerialStartPitch[MSA_MAX_SWORDS];
static float gAerialStartRoll [MSA_MAX_SWORDS];
// Per-sword landing angles (captured at moment of impact)
static float gAerialLandYaw   [MSA_MAX_SWORDS];
static float gAerialLandPitch [MSA_MAX_SWORDS];
static float gAerialLandRoll  [MSA_MAX_SWORDS];

// Short-path lerp between two angles.
static float aerial_angle_lerp(float a, float b, float t) {
    float d = b - a;
    while (d >  (float)T3D_PI) d -= 2.0f * (float)T3D_PI;
    while (d < -(float)T3D_PI) d += 2.0f * (float)T3D_PI;
    return a + d * t;
}

// ============================================================
// UN-CACHED 16-BYTE ALIGNED ALLOC
// ============================================================
static void* alloc_uncached_aligned16(size_t bytes, void** out_base) {
    void* base = malloc_uncached(bytes + 15);
    if (!base) {
        *out_base = NULL;
        return NULL;
    }
    uintptr_t p = (uintptr_t)base;
    uintptr_t aligned = (p + 15u) & ~(uintptr_t)15u;
    *out_base = base;
    return (void*)aligned;
}

// ============================================================
// FINITE + CLAMP GUARDS (RSP SAFETY)
// ============================================================
static inline int msa_isfinite3(float x, float y, float z) {
    return isfinite(x) && isfinite(y) && isfinite(z);
}

static inline float msa_clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// ============================================================
// TRIG LUT
// ============================================================
static const float MSA_TWO_PI = 6.2831853071795864769f;
static const float MSA_INV_TWO_PI = 1.0f / 6.2831853071795864769f;

#define MSA_TRIG_LUT_BITS 10
#define MSA_TRIG_LUT_SIZE (1 << MSA_TRIG_LUT_BITS)
#define MSA_TRIG_LUT_MASK (MSA_TRIG_LUT_SIZE - 1)

static float sSinLut[MSA_TRIG_LUT_SIZE];
static float sCosLut[MSA_TRIG_LUT_SIZE];
static uint8_t sTrigInited = 0;

static void msa_trig_init_once(void) {
    if (sTrigInited) return;
    sTrigInited = 1;

    for (int i = 0; i < MSA_TRIG_LUT_SIZE; i++) {
        float a = ((float)i / (float)MSA_TRIG_LUT_SIZE) * MSA_TWO_PI;
        float s = sinf(a);
        float c = cosf(a);

        if (s >  1.0f) s =  1.0f;
        if (s < -1.0f) s = -1.0f;
        if (c >  1.0f) c =  1.0f;
        if (c < -1.0f) c = -1.0f;

        sSinLut[i] = s;
        sCosLut[i] = c;
    }
}

static inline float wrap_angle_0_2pi_fast(float a) {
    if (a >= MSA_TWO_PI) a -= MSA_TWO_PI;
    if (a <  0.0f)      a += MSA_TWO_PI;
    if (a >= MSA_TWO_PI || a < 0.0f) {
        while (a >= MSA_TWO_PI) a -= MSA_TWO_PI;
        while (a <  0.0f)      a += MSA_TWO_PI;
    }
    return a;
}

static inline int lut_index(float a) {
    int idx = (int)(a * (MSA_INV_TWO_PI * (float)MSA_TRIG_LUT_SIZE));
    return idx & MSA_TRIG_LUT_MASK;
}

static inline float lut_sin(float a) { return sSinLut[lut_index(a)]; }
static inline float lut_cos(float a) { return sCosLut[lut_index(a)]; }

// ============================================================
// SMALL HELPERS
// ============================================================
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

static inline uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static inline float frand01(uint32_t *s) {
    return (float)(xorshift32(s) & 0x00FFFFFF) / (float)0x01000000;
}

static inline float dist2(float ax, float az, float bx, float bz) {
    float dx = ax - bx;
    float dz = az - bz;
    return dx*dx + dz*dz;
}

// fast rsqrt (1 NR step)
static inline float fast_rsqrtf(float number) {
    if (number <= 0.0f) return 0.0f;
    union { float f; uint32_t i; } conv;
    conv.f = number;
    conv.i = 0x5f3759df - (conv.i >> 1);
    float y = conv.f;
    const float x2 = number * 0.5f;
    y = y * (1.5f - (x2 * y * y));
    return y;
}

static inline void step_toward_xz(MsaSword *s, float tx, float tz, float max_step) {
    float ox = s->pos[0];
    float oz = s->pos[2];
    float dx = tx - ox;
    float dz = tz - oz;

    float d2 = dx*dx + dz*dz;
    if (d2 < 0.000001f) {
        s->pos[0] = tx;
        s->pos[2] = tz;
        return;
    }

    float max2 = max_step * max_step;
    float stepX, stepZ;

    if (d2 <= max2) {
        stepX = dx;
        stepZ = dz;
        s->pos[0] = tx;
        s->pos[2] = tz;
    } else {
        float invD = fast_rsqrtf(d2);
        float k = max_step * invD;
        stepX = dx * k;
        stepZ = dz * k;
        s->pos[0] = ox + stepX;
        s->pos[2] = oz + stepZ;
    }

    float m2 = stepX*stepX + stepZ*stepZ;
    if (m2 > 0.0001f) {
        s->dir[0] = stepX;
        s->dir[1] = stepZ;

        s->renormTick++;
        if (s->renormTick >= (uint8_t)MSA_DIR_RENORM_PERIOD) {
            s->renormTick = 0;
            float inv = fast_rsqrtf(m2);
            s->dir[0] *= inv;
            s->dir[1] *= inv;
        }
    }
}

// ============================================================
// CHARACTER CAPSULE HELPERS
// ============================================================
static void getCharacterCapsuleWorld(float outA[3], float outB[3], float *outR) {
    outA[0] = character.pos[0] + character.capsuleCollider.localCapA.v[0];
    outA[1] = character.pos[1] + character.capsuleCollider.localCapA.v[1];
    outA[2] = character.pos[2] + character.capsuleCollider.localCapA.v[2];

    outB[0] = character.pos[0] + character.capsuleCollider.localCapB.v[0];
    outB[1] = character.pos[1] + character.capsuleCollider.localCapB.v[1];
    outB[2] = character.pos[2] + character.capsuleCollider.localCapB.v[2];

    *outR = character.capsuleCollider.radius;
}

static void swordBodyAabb(const MsaSword *s, float outMin[3], float outMax[3]) {
    const float r = SWORD_RADIUS;

    outMin[0] = s->pos[0] - r;
    outMax[0] = s->pos[0] + r;

    outMin[2] = s->pos[2] - r;
    outMax[2] = s->pos[2] + r;

    outMin[1] = gFloorY;
    outMax[1] = gFloorY + HAZARD_HEIGHT;
}

// ============================================================
// WALL COLLISION: OBB PER RIBBON SEGMENT
// ============================================================
static inline void msa_build_wall_obb_from_seg(SCU_OBB *o, float x0, float z0, float x1, float z1) {
    float mx = 0.5f * (x0 + x1);
    float mz = 0.5f * (z0 + z1);

    float dx = x1 - x0;
    float dz = z1 - z0;
    float len = sqrtf(dx*dx + dz*dz);

    if (len < 0.001f) len = 0.001f;

    o->center[0] = mx;
    o->center[1] = gFloorY + 0.5f * WALL_HEIGHT;
    o->center[2] = mz;

    o->half[0] = 0.5f * len;
    o->half[1] = 0.5f * WALL_HEIGHT;
    o->half[2] = 0.5f * WALL_THICKNESS;

    o->yaw = atan2f(dz, dx);
}

static bool msa_wall_hit_or_block_capsule(
    float capA[3], float capB[3], float r,
    float *io_vx, float *io_vz)
{
#if !MSA_DO_WALL_COLLISION
    (void)capA; (void)capB; (void)r; (void)io_vx; (void)io_vz;
    return false;
#else
    float yMin = fminf(capA[1], capB[1]) - r;
    float yMax = fmaxf(capA[1], capB[1]) + r;
    float wallY0 = gFloorY;
    float wallY1 = gFloorY + WALL_HEIGHT;
    if (yMax < wallY0 || yMin > wallY1) return false;

    bool anyHit = false;

    for (int si = 0; si < gCount; si++) {
        const MsaSword *sw = &gSwords[si];
        const PathRibbon *pr = &sw->ribbon;

        if (pr->dead) continue;
        int n = (int)pr->count;
        if (n < 2) continue;

        for (int i = 0; i < n - 1; i++) {
            float x0 = pr->pts[i][0];
            float z0 = pr->pts[i][2];
            float x1 = pr->pts[i+1][0];
            float z1 = pr->pts[i+1][2];

            if (!isfinite(x0) || !isfinite(z0) || !isfinite(x1) || !isfinite(z1)) continue;

            SCU_OBB o;
            msa_build_wall_obb_from_seg(&o, x0, z0, x1, z1);

            float push[3] = {0}, nrm[3] = {0};

            if (scu_capsule_vs_obb_push_xz_f(capA, capB, r, &o, push, nrm)) {
                anyHit = true;

#if MSA_WALLS_BLOCKING
                character.pos[0] += push[0];
                character.pos[2] += push[2];

                capA[0] += push[0]; capA[2] += push[2];
                capB[0] += push[0]; capB[2] += push[2];

                if (io_vx && io_vz) {
                    float vx = *io_vx;
                    float vz = *io_vz;
                    float vn = vx * nrm[0] + vz * nrm[2];
                    if (vn < 0.0f) {
                        vx -= vn * nrm[0];
                        vz -= vn * nrm[2];
                        *io_vx = vx;
                        *io_vz = vz;
                    }
                }
#else
                (void)push; (void)nrm;
#endif
            }
        }
    }

    return anyHit;
#endif
}

// ============================================================
// T3D MATRIX BUILD
// ============================================================
static inline void msa_build_srt_scaled(T3DMat4FP *out, float scale1, float x, float y, float z, float yaw) {
    const float scale[3] = { scale1, scale1, scale1 };
    const float rot[3]   = { 0.0f,   yaw,    0.0f   };
    const float trans[3] = { x,      y,      z      };
    t3d_mat4fp_from_srt_euler(out, scale, rot, trans);
}

// ============================================================
// ASSET INIT/SHUTDOWN
// ============================================================
static void msa_assets_init(void) {
    // Create lightning first
    if (!gLightningFx) {
        gLightningFx = lightning_fx_create("rom:/boss/boss_back_sword_lightning.t3dm");
        assert(gLightningFx && "lightning_fx_create failed");
    }

    if (swordModel && swordDpl && swordMatrix &&
        floorGlowModel && floorGlowDpl && floorGlowMatrix) {
        return;
    }

    swordModel     = t3d_model_load("rom:/boss/boss_back_sword.t3dm");
    floorGlowModel = t3d_model_load("rom:/boss/boss_back_sword_glow.t3dm");

    rspq_block_begin();
    t3d_model_draw(swordModel);
    swordDpl = rspq_block_end();

    rspq_block_begin();
    t3d_model_draw(floorGlowModel);
    floorGlowDpl = rspq_block_end();

    if (!swordMatrix) {
        void* aligned = alloc_uncached_aligned16(sizeof(T3DMat4FP) * MSA_MAX_SWORDS, &swordMatrixBase);
        swordMatrix = (T3DMat4FP*)aligned;
    }
    if (!floorGlowMatrix) {
        void* aligned = alloc_uncached_aligned16(sizeof(T3DMat4FP) * MSA_MAX_SWORDS, &floorGlowMatrixBase);
        floorGlowMatrix = (T3DMat4FP*)aligned;
    }

    for (int i = 0; i < MSA_MAX_SWORDS; i++) {
        msa_build_srt_scaled(&swordMatrix[i],     MODEL_SCALE, 0, -9999, 0, 0);
        msa_build_srt_scaled(&floorGlowMatrix[i], MODEL_SCALE, 0, -9999, 0, 0);
    }
}

static void msa_assets_shutdown(void) {
    if (swordMatrixBase) {
        free_uncached(swordMatrixBase);
        swordMatrixBase = NULL;
        swordMatrix = NULL;
    }
    if (floorGlowMatrixBase) {
        free_uncached(floorGlowMatrixBase);
        floorGlowMatrixBase = NULL;
        floorGlowMatrix = NULL;
    }

#ifdef RSPQ_BLOCK_FREE_SUPPORTED
    if (swordDpl) rspq_block_free(swordDpl);
    if (floorGlowDpl) rspq_block_free(floorGlowDpl);
#endif
    swordDpl = NULL;
    floorGlowDpl = NULL;

    if (swordModel) {
        t3d_model_free(swordModel);
        swordModel = NULL;
    }
    if (floorGlowModel) {
        t3d_model_free(floorGlowModel);
        floorGlowModel = NULL;
    }

    if (gLightningFx) {
        lightning_fx_destroy(gLightningFx);
        gLightningFx = NULL;
    }
}

// ============================================================
// WALL TEXTURE INIT (ONCE)
// ============================================================
static void msa_wall_tex_init_once(void) {
    if (sWallFogSpr) {
        path_ribbon_set_wall_texture(sWallFogSpr);
        return;
    }

    sWallFogSpr = sprite_load("rom:/boss_room/dust.ia8.sprite");

    path_ribbon_set_wall_texture(sWallFogSpr);
}

// ============================================================
// ATTACK LOGIC HELPERS
// ============================================================
static void reset_sword_runtime(MsaSword *s) {
    s->t = 0.0f;
    s->fallT = 0.0f;
    s->fallTime = MSA_DROP_FALL_TIME_SEC;

    s->renormTick = 0;

    path_ribbon_clear(&s->ribbon);
    path_ribbon_set_floor(&s->ribbon, gFloorY);
    path_ribbon_set_seed(&s->ribbon, s->seed);

    s->glowVisible = 1;

    s->lastRibbonXZ[0] = s->pos[0];
    s->lastRibbonXZ[1] = s->pos[2];
}

static void make_drop_order(uint32_t *seed) {
    for (int i = 0; i < gCount; i++) gDropOrder[i] = i;

    for (int i = gCount - 1; i > 0; i--) {
        uint32_t r = xorshift32(seed);
        int j = (int)(r % (uint32_t)(i + 1));
        int tmp = gDropOrder[i];
        gDropOrder[i] = gDropOrder[j];
        gDropOrder[j] = tmp;
    }

    gDropNext = 0;
    gDropAcc  = 0.0f;
}

static void spawn_cluster_above_player(uint32_t *seed) {
    const float px = character.pos[0];
    const float pz = character.pos[2];
    const float minSp2 = gMinSpacing * gMinSpacing;

    for (int i = 0; i < gCount; i++) {
        MsaSword *s = &gSwords[i];

        float sx = px;
        float sz = pz;

        const int MAX_TRIES = 64;
        for (int attempt = 0; attempt < MAX_TRIES; attempt++) {
            float a = frand01(seed) * MSA_TWO_PI;
            float r = frand01(seed);
            r = sqrtf(r) * gClusterRadius;

            float cx = px + lut_cos(a) * r;
            float cz = pz + lut_sin(a) * r;

            int ok = 1;
            for (int j = 0; j < i; j++) {
                float d2 = dist2(cx, cz, gSwords[j].spawnX, gSwords[j].spawnZ);
                if (d2 < minSp2) { ok = 0; break; }
            }
            if (ok) { sx = cx; sz = cz; break; }
            if (attempt == MAX_TRIES - 1) { sx = cx; sz = cz; }
        }

        s->spawnX = sx;
        s->spawnZ = sz;

        s->pos[0] = sx;
        s->pos[1] = CEILING_Y;
        s->pos[2] = sz;

        float da = frand01(seed) * MSA_TWO_PI;
        s->dir[0] = lut_cos(da);
        s->dir[1] = lut_sin(da);

        float ph = frand01(seed) * MSA_TWO_PI;
        s->figPhase  = ph;
        s->driftDirX = lut_cos(ph);
        s->driftDirZ = lut_sin(ph);

        s->state = SW_CEILING;
        reset_sword_runtime(s);
    }
}

static int all_swords_in_state(MsaSwordState st) {
    for (int i = 0; i < gCount; i++) {
        if (gSwords[i].state != st) return 0;
    }
    return 1;
}

static int any_swords_active(void) {
    for (int i = 0; i < gCount; i++) {
        if (gSwords[i].state != SW_INACTIVE) return 1;
    }
    return 0;
}

// ============================================================
// PUBLIC API
// ============================================================
void msa_set_enabled(bool enabled) { gEnabled = enabled; }
void msa_set_floor_y(float y) { gFloorY = y; }

void msa_set_sword_count(int count) {
    if (count < 1) count = 1;
    if (count > MSA_MAX_SWORDS) count = MSA_MAX_SWORDS;
    gCount = count;
}

void msa_set_cluster_spacing(float minSpacing, float radius) {
    if (minSpacing < 10.0f) minSpacing = 10.0f;
    if (radius < minSpacing) radius = minSpacing;
    gMinSpacing = minSpacing;
    gClusterRadius = radius;
}

void msa_set_pattern(MsaPattern p) { gPattern = p; (void)gPattern; }

// Start a single ground-sweep cycle driven by the boss AI.
// Resets the MSA to CEILING_SETUP, runs one full cycle, then signals done.
void msa_ground_sweep_start(void) {
    // Reset all swords
    for (int i = 0; i < MSA_MAX_SWORDS; i++) {
        MsaSword *s = &gSwords[i];
        s->state = SW_INACTIVE;
        s->glowVisible = 0;
        path_ribbon_clear(&s->ribbon);
    }

    gPhase          = MSA_PHASE_CEILING_SETUP;
    gPhaseT         = 0.0f;
    gLoopDelay      = 0.0f;
    gDidSpawnThisCycle = 0;
    gDropNext       = 0;
    gDropAcc        = 0.0f;

    gGroundSweepActive = true;
    gGroundSweepDone   = false;
    gEnabled           = true;
}

bool msa_ground_sweep_is_done(void) {
    return gGroundSweepDone;
}

// ============================================================
// INIT / SHUTDOWN
// ============================================================
void msa_init(void) {
    msa_trig_init_once();
    msa_assets_init();
    msa_wall_tex_init_once();

    memset(gSwords, 0, sizeof(gSwords));

    gHitCd = 0.0f;
    gCollisionAcc = 0.0f;

    gPhase = MSA_PHASE_CEILING_SETUP;
    gPhaseT = 0.0f;

    gDropNext = 0;
    gDropAcc  = 0.0f;

    gLoopDelay = 0.0f;

    gDidSpawnThisCycle = 0;

    gAerialMode = false;
    memset(gAerialTargets, 0, sizeof(gAerialTargets));
    memset(gAerialAimTimer,    0, sizeof(gAerialAimTimer));
    memset(gAerialStickTimer,   0, sizeof(gAerialStickTimer));
    memset(gAerialStartYaw,     0, sizeof(gAerialStartYaw));
    memset(gAerialStartPitch,   0, sizeof(gAerialStartPitch));
    memset(gAerialStartRoll,    0, sizeof(gAerialStartRoll));
    memset(gAerialLandYaw,      0, sizeof(gAerialLandYaw));
    memset(gAerialLandPitch,    0, sizeof(gAerialLandPitch));
    memset(gAerialLandRoll,     0, sizeof(gAerialLandRoll));

    gGroundSweepActive = false;
    gGroundSweepDone   = false;

    uint32_t seed = 0xA123BEEF;

    for (int i = 0; i < MSA_MAX_SWORDS; i++) {
        MsaSword *s = &gSwords[i];
        s->seed = xorshift32(&seed) ^ (uint32_t)(i * 0x9E3779B9u);
        s->state = SW_INACTIVE;
        s->glowVisible = 0;

        path_ribbon_init(&s->ribbon, (uint8_t)MSA_PATH_MAX_POINTS, (float)MSA_PATH_MIN_STEP);
        path_ribbon_set_floor(&s->ribbon, gFloorY);
        path_ribbon_set_seed(&s->ribbon, s->seed);

        s->ribbon.wall_height = WALL_HEIGHT;
        s->ribbon.wall_color_bot = (PRColor){ 255, 210, 0, 155 };
        s->ribbon.wall_color_top = (PRColor){ 255, 210, 0, 0 };

        s->ribbon.crack_color = (PRColor){ 57, 38, 25, 255 };

        s->ribbon.crack_w_start   = 1.5f;
        s->ribbon.crack_w_end     = 3.5f;
        s->ribbon.crack_w_noise   = 0.22f;
        s->ribbon.crack_tip_taper = 0.22f;
    }
}

void msa_shutdown(void) {
    if (sWallFogSpr) {
        sprite_free(sWallFogSpr);
        sWallFogSpr = NULL;
        path_ribbon_set_wall_texture(NULL);
    }

    msa_assets_shutdown();
}

// ============================================================
// UPDATE
// ============================================================
void msa_update(float dt) {
    if (!gEnabled) return;

    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.05f) dt = 0.05f;

    for (int i = 0; i < MSA_MAX_SWORDS; i++) {
        path_ribbon_update(&gSwords[i].ribbon, dt);
    }

    if (gHitCd > 0.0f) gHitCd -= dt;

    if (gLightningFx) lightning_fx_update(gLightningFx, dt);

    if (gAerialMode) {
        bool anyAerialSword = false;
        for (int i = 0; i < gCount; i++) {
            MsaSword *s = &gSwords[i];

            if (s->state == SW_CEILING || s->state == SW_AERIAL_AIM || s->state == SW_AERIAL_FLY || s->state == SW_AERIAL_STUCK) {
                anyAerialSword = true;
            }

            if (s->state == SW_AERIAL_STUCK) {
                if (gAerialStickTimer[i] > 0.0f) {
                    gAerialStickTimer[i] -= dt;
                } else {
                    s->pos[1] -= AERIAL_SINK_SPEED * dt;
                    float sinkEndY = gAerialTargets[i][1] - AERIAL_SINK_DEPTH;
                    if (s->pos[1] <= sinkEndY) {
                        s->state = SW_INACTIVE;
                        s->glowVisible = 0;
                        path_ribbon_clear(&s->ribbon);
                    }
                }
                continue;
            }

            if (s->state == SW_AERIAL_AIM) {
                float tx = gAerialTargets[i][0];
                float tz = gAerialTargets[i][2];

                float dx = tx - s->pos[0];
                float dz = tz - s->pos[2];
                float d2 = dx*dx + dz*dz;
                if (d2 > 0.0001f) {
                    float inv = fast_rsqrtf(d2);
                    s->dir[0] = dx * inv;
                    s->dir[1] = dz * inv;
                }

                gAerialAimTimer[i] -= dt;
                if (gAerialAimTimer[i] <= 0.0f) {
                    s->state = SW_AERIAL_FLY;
                }
                continue;
            }

            if (s->state != SW_AERIAL_FLY) continue;

            float tx = gAerialTargets[i][0];
            float ty = gAerialTargets[i][1];
            float tz = gAerialTargets[i][2];

            float dx = tx - s->pos[0];
            float dy = ty - s->pos[1];
            float dz = tz - s->pos[2];
            float d2 = dx*dx + dy*dy + dz*dz;

            if (d2 < 1.0f) {
                float pdx = character.pos[0] - tx;
                float pdy = character.pos[1] - ty;
                float pdz = character.pos[2] - tz;
                float p2 = pdx*pdx + pdy*pdy + pdz*pdz;

                if (p2 <= (32.0f * 32.0f) && gHitCd <= 0.0f) {
                    character_apply_damage(DMG_BODY);
                    gHitCd = HIT_COOLDOWN;
                }

                // Lock in the flying orientation at the moment of impact.
                {
                    float ldx = tx - s->pos[0];
                    float ldy = ty - s->pos[1];
                    float ldz = tz - s->pos[2];
                    float lxz = sqrtf(ldx*ldx + ldz*ldz);
                    if (lxz < 0.001f) { ldx = s->dir[0]; ldz = s->dir[1]; lxz = 1.0f; ldy = 0.0f; }
                    gAerialLandYaw  [i] = atan2f(ldz, ldx) + MSA_MODEL_YAW_OFFSET;
                    gAerialLandPitch[i] = -atan2f(ldy, lxz + 0.0001f) + AERIAL_MODEL_PITCH_OFFSET;
                    gAerialLandRoll [i] = (float)T3D_PI * 0.5f;
                }

                s->pos[0] = tx;
                s->pos[1] = ty;
                s->pos[2] = tz;

                s->state = SW_AERIAL_STUCK;
                gAerialStickTimer[i] = AERIAL_STUCK_TIME;
                s->glowVisible = 0;
                continue;
            }

            float invD = fast_rsqrtf(d2);
            float nx = dx * invD;
            float ny = dy * invD;
            float nz = dz * invD;

            float step = AERIAL_SPEED * dt;
            float dist = sqrtf(d2);
            if (step > dist) step = dist;

            s->pos[0] += nx * step;
            s->pos[1] += ny * step;
            s->pos[2] += nz * step;

            float xz2 = nx*nx + nz*nz;
            if (xz2 > 0.0001f) {
                s->dir[0] = nx;
                s->dir[1] = nz;
            }

            // Keep tip facing player while flying (visual polish)
            float pdx = character.pos[0] - s->pos[0];
            float pdz = character.pos[2] - s->pos[2];
            float pd2 = pdx*pdx + pdz*pdz;
            if (pd2 > 0.0001f) {
                float pinv = fast_rsqrtf(pd2);
                s->dir[0] = pdx * pinv;
                s->dir[1] = pdz * pinv;
            }
        }

        if (!anyAerialSword) {
            gAerialMode = false;
            gCount = 0;
        }

        return;
    }

    float charA[3], charB[3], charR;
    getCharacterCapsuleWorld(charA, charB, &charR);

    gPhaseT += dt;

    if (gLoopDelay > 0.0f) {
        gLoopDelay -= dt;
        if (gLoopDelay > 0.0f) return;
    }

    if (gPhase == MSA_PHASE_CEILING_SETUP) {
        if (!gDidSpawnThisCycle) {
            gDidSpawnThisCycle = 1;

            uint32_t seed2 = 0xD00DFEEDu
                ^ (uint32_t)((int)character.pos[0] * 17)
                ^ (uint32_t)((int)character.pos[2] * 31);

            spawn_cluster_above_player(&seed2);
            make_drop_order(&seed2);

            gDropNext = 0;
            gDropAcc  = 0.0f;
        }

        if (gPhaseT >= CEILING_HOLD_SEC) {
            gPhase  = MSA_PHASE_DROPPING;
            gPhaseT = 0.0f;
        }
    }

    if (gPhase == MSA_PHASE_DROPPING) {
        gDropAcc += dt;

        while (gDropNext < gCount && gDropAcc >= DROP_INTERVAL_SEC) {
            gDropAcc -= DROP_INTERVAL_SEC;

            int idx = gDropOrder[gDropNext++];
            MsaSword *s = &gSwords[idx];

            if (s->state == SW_CEILING) {
                s->state = SW_FALLING;
                s->fallT = 0.0f;
                s->fallTime = MSA_DROP_FALL_TIME_SEC;
            }
        }

        for (int i = 0; i < gCount; i++) {
            MsaSword *s = &gSwords[i];
            if (s->state != SW_FALLING) continue;

            float t = s->fallT;
            t += (s->fallTime > 0.0001f) ? (dt / s->fallTime) : 1.0f;
            if (t > 1.0f) t = 1.0f;

            float e = t * t;
            s->pos[0] = s->spawnX;
            s->pos[2] = s->spawnZ;
            s->pos[1] = lerpf(CEILING_Y, gFloorY, e);

            s->fallT = t;

            if (t >= 1.0f) {
                s->state = SW_LANDED;
                s->pos[1] = gFloorY;

                s->glowVisible = 0;

                path_ribbon_set_floor(&s->ribbon, gFloorY);

                float dx = character.pos[0] - s->spawnX;
                float dz = character.pos[2] - s->spawnZ;
                float lightningYaw = atan2f(dz, dx) + MSA_MODEL_YAW_OFFSET;

                if (gLightningFx) {
                    lightning_fx_strike(gLightningFx, s->spawnX, gFloorY, s->spawnZ, lightningYaw);
                }
            }
        }

        if (gDropNext >= gCount && all_swords_in_state(SW_LANDED)) {
            gPhase = MSA_PHASE_POST_LAND;
            gPhaseT = 0.0f;
        }
    }

    if (gPhase == MSA_PHASE_POST_LAND) {
        if (gPhaseT >= LAND_PAUSE_SEC) {
            for (int i = 0; i < gCount; i++) {
                MsaSword *s = &gSwords[i];
                s->state = SW_SCURVE;

                path_ribbon_clear(&s->ribbon);
                path_ribbon_set_floor(&s->ribbon, gFloorY);
                path_ribbon_set_seed(&s->ribbon, s->seed);

                s->lastRibbonXZ[0] = s->pos[0];
                s->lastRibbonXZ[1] = s->pos[2];
                path_ribbon_try_add(&s->ribbon, s->pos[0], s->pos[2]);
                path_ribbon_try_add(&s->ribbon, s->pos[0], s->pos[2]);

                s->glowVisible = 0;

                float ph = frand01(&s->seed) * MSA_TWO_PI;
                s->figPhase  = ph;
                s->driftDirX = lut_cos(ph);
                s->driftDirZ = lut_sin(ph);
            }

            gPhase = MSA_PHASE_SCURVE;
            gPhaseT = 0.0f;
        }
    }

    if (gPhase == MSA_PHASE_SCURVE) {
        int done = (gPhaseT >= FIG8_STAGE_SEC);

        const float cx = character.pos[0];
        const float cz = character.pos[2];

        const float tStage = gPhaseT;
        const float tMove  = tStage * (float)MSA_FIG8_TIME_SCALE;

        const float omega = MSA_TWO_PI * FIG8_FREQ_HZ;

        for (int i = 0; i < gCount; i++) {
            MsaSword *s = &gSwords[i];
            if (s->state != SW_SCURVE) continue;

#if MSA_DO_MOVEMENT
            float a  = wrap_angle_0_2pi_fast(s->figPhase + omega * tMove);
            float a2 = wrap_angle_0_2pi_fast(a + a);

            float offX = lut_sin(a)  * FIG8_AMP_X;
            float offZ = lut_sin(a2) * FIG8_AMP_Z;

            float drift = FIG8_DRIFT_SPEED * tMove;

            float tx = cx + offX + s->driftDirX * drift;
            float tz = cz + offZ + s->driftDirZ * drift;

            tx = msa_clampf(tx, -4096.0f, 4096.0f);
            tz = msa_clampf(tz, -4096.0f, 4096.0f);

            const float maxStep = (MSA_MAX_XZ_SPEED * (float)MSA_MOVE_SPEED_MULT) * dt;
            step_toward_xz(s, tx, tz, maxStep);
#endif

            s->pos[1] = gFloorY;
            s->glowVisible = 0;

            if (!msa_isfinite3(s->pos[0], s->pos[1], s->pos[2])) {
                s->state = SW_INACTIVE;
                s->glowVisible = 0;
                path_ribbon_clear(&s->ribbon);
                continue;
            }

            (void)path_ribbon_try_add(&s->ribbon, s->pos[0], s->pos[2]);

            if (done) {
                s->state = SW_DESCEND;

                float descend_sec = (gFloorY - DESPAWN_Y) / (float)MSA_DESCEND_SPEED;
                if (descend_sec < 0.10f) descend_sec = 0.10f;
                path_ribbon_start_fade(&s->ribbon, descend_sec);
            }
        }

        if (done) {
            gPhase = MSA_PHASE_DESCEND;
            gPhaseT = 0.0f;
        }
    }

    if (gPhase == MSA_PHASE_DESCEND) {
        for (int i = 0; i < gCount; i++) {
            MsaSword *s = &gSwords[i];
            if (s->state != SW_DESCEND) continue;

            s->pos[1] -= (float)MSA_DESCEND_SPEED * dt;
            s->glowVisible = 0;

            if (s->pos[1] <= DESPAWN_Y) {
                s->state = SW_INACTIVE;
                s->glowVisible = 0;

                if (s->ribbon.dead) {
                    path_ribbon_clear(&s->ribbon);
                }
            }
        }

        if (!any_swords_active()) {
            if (gGroundSweepActive) {
                // One full cycle done — signal boss and stop.
                gGroundSweepActive = false;
                gGroundSweepDone   = true;
                gEnabled           = false;
            } else {
                gPhase = MSA_PHASE_CEILING_SETUP;
                gPhaseT = 0.0f;
                gLoopDelay = 0.25f;
                gDidSpawnThisCycle = 0;
            }
        }
    }

    // ========================================================
    // COLLISION
    // ========================================================
    bool hitBody = false;

#if MSA_DO_BODY_COLLISION
    for (int i = 0; i < gCount; i++) {
        MsaSword *s = &gSwords[i];
        if (s->state != SW_LANDED && s->state != SW_SCURVE) continue;

        float swordMin[3], swordMax[3];
        swordBodyAabb(s, swordMin, swordMax);
        if (scu_capsule_vs_rect_f(charA, charB, charR, swordMin, swordMax)) {
            hitBody = true;
            break;
        }
    }
#endif

    bool hitWall = false;

#if MSA_DO_WALL_COLLISION
    gCollisionAcc += dt;
    const float tick = 1.0f / (float)MSA_COLLISION_HZ;

    if (gCollisionAcc >= tick) {
        gCollisionAcc -= tick;

        float vx = 0.0f, vz = 0.0f;
#if MSA_WALLS_BLOCKING
        character_get_velocity(&vx, &vz);
#endif

        float capA[3] = { charA[0], charA[1], charA[2] };
        float capB[3] = { charB[0], charB[1], charB[2] };
        float r = charR;

        hitWall = msa_wall_hit_or_block_capsule(capA, capB, r,
#if MSA_WALLS_BLOCKING
            &vx, &vz
#else
            NULL, NULL
#endif
        );

#if MSA_WALLS_BLOCKING
        character_set_velocity_xz(vx, vz);
#endif
    }
#endif

    if (gHitCd <= 0.0f) {
        if (hitBody) {
            character_apply_damage(DMG_BODY);
            gHitCd = HIT_COOLDOWN;
        } else if (hitWall) {
            character_apply_damage(DMG_WALL);
            gHitCd = HIT_COOLDOWN;
        }
    }
}

// ============================================================
// DRAW
// ============================================================
void msa_draw_visuals(T3DViewport *viewport) {
    (void)viewport;

    if (!gEnabled) return;
    if (!swordDpl || !swordMatrix || !floorGlowModel || !floorGlowMatrix) return;

    // 1) Swords (zbuf ON)
    t3d_matrix_push_pos(1);
    for (int i = 0; i < gCount; i++) {
        const MsaSword *s = &gSwords[i];
        if (s->state == SW_INACTIVE) continue;
        if (!msa_isfinite3(s->pos[0], s->pos[1], s->pos[2])) continue;

        float yaw = 0.0f;
#if MSA_FACE_DIR
        yaw = atan2f(s->dir[1], s->dir[0]) + MSA_MODEL_YAW_OFFSET;
#endif

        if (gAerialMode && s->state == SW_CEILING) {
            // Keep dormant ring swords straight-down until activated.
            const float scale[3] = { MODEL_SCALE*2.0f, MODEL_SCALE*2.0f, MODEL_SCALE*2.0f };
            const float rot[3] = { 0.0f, 0.0f, 0.0f };
            const float trans[3] = { s->pos[0], s->pos[1], s->pos[2] };
            t3d_mat4fp_from_srt_euler(&swordMatrix[i], scale, rot, trans);
        } else if (gAerialMode && (s->state == SW_AERIAL_AIM || s->state == SW_AERIAL_FLY)) {
            float tx = gAerialTargets[i][0];
            float ty = gAerialTargets[i][1];
            float tz = gAerialTargets[i][2];

            float dx = tx - s->pos[0];
            float dy = ty - s->pos[1];
            float dz = tz - s->pos[2];
            float xz = sqrtf(dx*dx + dz*dz);
            float tgtYaw   = atan2f(dz, dx) + MSA_MODEL_YAW_OFFSET;
            float tgtPitch = -atan2f(dy, xz + 0.0001f) + AERIAL_MODEL_PITCH_OFFSET;
            float tgtRoll  = (float)T3D_PI * 0.5f;

            float finalYaw, finalPitch, finalRoll;
            if (s->state == SW_AERIAL_AIM) {
                // Interpolate from the rest pose toward the target angle.
                float t = 1.0f - (gAerialAimTimer[i] / AERIAL_AIM_TIME);
                if (t < 0.0f) t = 0.0f;
                if (t > 1.0f) t = 1.0f;
                finalYaw   = aerial_angle_lerp(gAerialStartYaw[i],   tgtYaw,   t);
                finalPitch = gAerialStartPitch[i] + (tgtPitch - gAerialStartPitch[i]) * t;
                finalRoll  = gAerialStartRoll[i]  + (tgtRoll  - gAerialStartRoll[i])  * t;
            } else {
                finalYaw   = tgtYaw;
                finalPitch = tgtPitch;
                finalRoll  = tgtRoll;
            }

            const float scale[3] = { MODEL_SCALE*2.0f, MODEL_SCALE*2.0f, MODEL_SCALE*2.0f };
            const float rot[3] = { finalPitch, finalYaw, finalRoll };
            const float trans[3] = { s->pos[0], s->pos[1], s->pos[2] };
            t3d_mat4fp_from_srt_euler(&swordMatrix[i], scale, rot, trans);
        } else if (gAerialMode && s->state == SW_AERIAL_STUCK) {
            // Keep exactly the orientation locked in at the moment of impact —
            // no rotation, just sink straight into the ground.
            const float scale[3] = { MODEL_SCALE*2.0f, MODEL_SCALE*2.0f, MODEL_SCALE*2.0f };
            const float rot[3] = { gAerialLandPitch[i], gAerialLandYaw[i], gAerialLandRoll[i] };
            const float trans[3] = { s->pos[0], s->pos[1], s->pos[2] };
            t3d_mat4fp_from_srt_euler(&swordMatrix[i], scale, rot, trans);
        } else {
            msa_build_srt_scaled(&swordMatrix[i], MODEL_SCALE*2.0f, s->pos[0], s->pos[1], s->pos[2], yaw);
        }

        t3d_matrix_set(&swordMatrix[i], true);
        rspq_block_run(swordDpl);
    }
    t3d_matrix_pop(1);

    // Lightning FX
    if (gLightningFx) lightning_fx_draw(gLightningFx);

    // Glows
    t3d_matrix_push_pos(1);
    for (int i = 0; i < gCount; i++) {
        const MsaSword *s = &gSwords[i];
        if (s->state == SW_INACTIVE) continue;
        if (!s->glowVisible) continue;

        if (!isfinite(s->spawnX) || !isfinite(s->spawnZ) || !isfinite(gFloorY)) continue;

        float dx = character.pos[0] - s->spawnX;
        float dz = character.pos[2] - s->spawnZ;
        float glowYaw = atan2f(dz, dx) + MSA_MODEL_YAW_OFFSET;

        msa_build_srt_scaled(&floorGlowMatrix[i], MODEL_SCALE,
            s->spawnX, gFloorY + 0.5f, s->spawnZ,
            glowYaw
        );

        t3d_matrix_set(&floorGlowMatrix[i], true);
        t3d_model_draw_custom(floorGlowModel, (T3DModelDrawConf){
            .userData = &floorGlowScrollParams,
            .tileCb   = tile_scroll,
        });
    }
    t3d_matrix_pop(1);

    // Crack + Wall
    for (int i = 0; i < MSA_MAX_SWORDS; i++) {
        const MsaSword *s = &gSwords[i];
        if (s->ribbon.count < 2) continue;
        if (s->ribbon.dead) continue;

        path_ribbon_draw_crack(&s->ribbon);
        path_ribbon_draw_wall(&s->ribbon);
    }
}

// ============================================================
// DEBUG DRAW
// ============================================================
static void msa_debug_draw_obb_xz(T3DViewport *vp, const SCU_OBB *o, float y, uint16_t color) {
    float c = cosf(o->yaw);
    float s = sinf(o->yaw);

    float hx = o->half[0];
    float hz = o->half[2];

    float lx[4] = { -hx,  hx,  hx, -hx };
    float lz[4] = { -hz, -hz,  hz,  hz };

    T3DVec3 p[4];

    for (int i = 0; i < 4; i++) {
        float wx = o->center[0] + (c * lx[i] - s * lz[i]);
        float wz = o->center[2] + (s * lx[i] + c * lz[i]);
        p[i] = (T3DVec3){{ wx, y, wz }};
    }

    debug_draw_tri_wire(vp, &p[0], &p[1], &p[2], color);
    debug_draw_tri_wire(vp, &p[0], &p[2], &p[3], color);
}

void msa_draw_debug(T3DViewport *viewport) {
    if (!gEnabled) return;
    if (!DEBUG_DRAW_ENVIRONMENTAL_HAZARDS) return;

    const uint16_t colSword = DEBUG_COLORS[0];
    const uint16_t colWall  = DEBUG_COLORS[2];

    for (int i = 0; i < gCount; i++) {
        if (gSwords[i].state == SW_INACTIVE) continue;
        T3DVec3 p = {{ gSwords[i].pos[0], gSwords[i].pos[1], gSwords[i].pos[2] }};
        debug_draw_cross(viewport, &p, 12.0f, colSword);
    }

    for (int si = 0; si < gCount; si++) {
        const MsaSword *s = &gSwords[si];
        const PathRibbon *pr = &s->ribbon;

        if (pr->dead) continue;
        int n = (int)pr->count;
        if (n < 2) continue;

        for (int i = 0; i < n - 1; i++) {
            float x0 = pr->pts[i][0];
            float z0 = pr->pts[i][2];
            float x1 = pr->pts[i+1][0];
            float z1 = pr->pts[i+1][2];

            if (!isfinite(x0) || !isfinite(z0) || !isfinite(x1) || !isfinite(z1)) continue;

            SCU_OBB o;
            msa_build_wall_obb_from_seg(&o, x0, z0, x1, z1);

            msa_debug_draw_obb_xz(viewport, &o, gFloorY, colWall);
        }
    }
}

void msa_draw(T3DViewport *viewport) {
    msa_draw_visuals(viewport);
}

// ============================================================
// AERIAL ATTACK SUPPORT
// ============================================================

void msa_spawn_aerial_ring(float centerX, float centerY, float centerZ, float radius, int count) {
    if (count < 1) count = 1;
    if (count > MSA_MAX_SWORDS) count = MSA_MAX_SWORDS;

    gEnabled = true;
    gAerialMode = true;
    gCount = count;

    for (int i = 0; i < MSA_MAX_SWORDS; i++) {
        gSwords[i].state = SW_INACTIVE;
        gSwords[i].glowVisible = 0;
        path_ribbon_clear(&gSwords[i].ribbon);
        gAerialAimTimer[i] = 0.0f;
        gAerialStickTimer[i] = 0.0f;
    }
    
    for (int i = 0; i < count; i++) {
        MsaSword *s = &gSwords[i];
        
        // Calculate ring position
        float angle = (float)i / (float)count * 2.0f * T3D_PI;
        float swordX = centerX + cosf(angle) * radius;
        float swordZ = centerZ + sinf(angle) * radius;
        
        // Set spawn position
        s->spawnX = swordX;
        s->spawnZ = swordZ;
        
        // Position at specified height
        s->pos[0] = swordX;
        s->pos[1] = centerY;
        s->pos[2] = swordZ;
        
        // Set state to stationary at ceiling height
        s->state = SW_CEILING;
        
        // Direction pointing toward center initially
        float dirAngle = angle + T3D_PI;
        s->dir[0] = cosf(dirAngle);
        s->dir[1] = sinf(dirAngle);
        
        // Initialize movement parameters
        s->figPhase = (float)i / (float)count * 2.0f * T3D_PI;
        s->driftDirX = cosf(s->figPhase);
        s->driftDirZ = sinf(s->figPhase);
        
        // Reset timing
        s->t = 0.0f;
        s->fallT = 0.0f;
        s->fallTime = 2.0f; // 2 seconds to fall/attack
        
        // Initialize other fields
        s->seed = i * 12345;
        s->renormTick = 0;
        s->glowVisible = 1;

        path_ribbon_clear(&s->ribbon);
        path_ribbon_set_floor(&s->ribbon, centerY);
        path_ribbon_set_seed(&s->ribbon, s->seed);

        s->lastRibbonXZ[0] = s->pos[0];
        s->lastRibbonXZ[1] = s->pos[2];

        gAerialTargets[i][0] = s->pos[0];
        gAerialTargets[i][1] = s->pos[1];
        gAerialTargets[i][2] = s->pos[2];
    }
}

void msa_update_aerial_ring_pose(float centerX, float centerY, float centerZ, float radius,
                                 float targetX, float targetY, float targetZ) {
    (void)targetX;
    (void)targetY;
    (void)targetZ;
    if (!gAerialMode) return;

    for (int i = 0; i < gCount; i++) {
        MsaSword *s = &gSwords[i];
        if (s->state != SW_CEILING) continue;

        float angle = ((float)i / (float)gCount) * 2.0f * T3D_PI;
        float swordX = centerX + cosf(angle) * radius;
        float swordZ = centerZ + sinf(angle) * radius;

        s->spawnX = swordX;
        s->spawnZ = swordZ;
        s->pos[0] = swordX;
        s->pos[1] = centerY;
        s->pos[2] = swordZ;

        // Keep waiting swords unrotated until activated.
    }
}

void msa_fire_aerial_sword(int index, float targetX, float targetY, float targetZ) {
    if (index < 0 || index >= gCount) return;
    
    MsaSword *s = &gSwords[index];
    if (s->state == SW_INACTIVE) return;
    
    // Calculate direction to target
    float dx = targetX - s->pos[0];
    float dz = targetZ - s->pos[2];
    float dist = sqrtf(dx*dx + dz*dz);
    
    // Set direction (MSA uses 2D direction in XZ plane)
    if (dist > 0.001f) {
        s->dir[0] = dx / dist;
        s->dir[1] = dz / dist;
    }

    gAerialTargets[index][0] = targetX;
    gAerialTargets[index][1] = targetY;
    gAerialTargets[index][2] = targetZ;

    // Capture the sword's current (SW_CEILING) rest orientation as the
    // start of the aim rotation so we can smoothly lerp to the target.
    gAerialStartYaw  [index] = 0.0f;
    gAerialStartPitch[index] = 0.0f;
    gAerialStartRoll [index] = 0.0f;

    s->state = SW_AERIAL_AIM;
    gAerialAimTimer[index] = AERIAL_AIM_TIME;
    gAerialStickTimer[index] = 0.0f;
    s->fallT = 0.0f;
    s->t = 0.0f;
    s->glowVisible = 0;
}

bool msa_has_active_aerial_swords(void) {
    if (!gAerialMode) return false;

    for (int i = 0; i < gCount; i++) {
        MsaSwordState st = gSwords[i].state;
        if (st == SW_CEILING || st == SW_AERIAL_AIM || st == SW_AERIAL_FLY || st == SW_AERIAL_STUCK) {
            return true;
        }
    }
    return false;
}

void msa_cleanup_aerial_swords(void) {
    gAerialMode = false;

    // Mark all swords as inactive
    for (int i = 0; i < gCount; i++) {
        MsaSword *s = &gSwords[i];
        s->state = SW_INACTIVE;
        s->pos[0] = 0.0f;
        s->pos[1] = -9999.0f; // Move off-screen
        s->pos[2] = 0.0f;
        gAerialAimTimer[i] = 0.0f;
        gAerialStickTimer[i] = 0.0f;
        
        // Cleanup ribbon
        path_ribbon_clear(&s->ribbon);
    }
    
    gCount = 0;
}
