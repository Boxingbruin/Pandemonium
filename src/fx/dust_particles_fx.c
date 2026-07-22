#include "dust_particles_fx.h"

#include <libdragon.h>

#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/tpx.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "particle_system.h"
#include "../utilities/general_utility.h"

#define DUST_PARTICLES_FX_FORCE_UNTEXTURED 0
#define DUST_PARTICLES_FX_DEBUG_LOGS 1

/*
 * Ambient dust is a fixed resident pool.
 *
 * No spawn interval.
 * No ambient death.
 * No fade in/out cycle.
 * No flicker from particles constantly entering/leaving.
 */
#define DUST_PARTICLES_FX_AMBIENT_COUNT 96

#define DUST_PARTICLES_FX_AMBIENT_SIZE_MIN 5.0f
#define DUST_PARTICLES_FX_AMBIENT_SIZE_MAX 10.0f

#define DUST_PARTICLES_FX_AMBIENT_ALPHA_SCALE 0.35f

/*
 * Ambient movement.
 *
 * Base drift moves the actual particle through the room.
 * Wobble is visual-only and makes the dust feel less mechanically linear.
 */
#define DUST_PARTICLES_FX_AMBIENT_DRIFT_XZ_MIN 1.60f
#define DUST_PARTICLES_FX_AMBIENT_DRIFT_XZ_MAX 6.00f

#define DUST_PARTICLES_FX_AMBIENT_DRIFT_Y_MIN -1.20f
#define DUST_PARTICLES_FX_AMBIENT_DRIFT_Y_MAX  1.40f

#define DUST_PARTICLES_FX_AMBIENT_WOBBLE_XZ 7.0f
#define DUST_PARTICLES_FX_AMBIENT_WOBBLE_Y  2.5f

#define DUST_PARTICLES_FX_AMBIENT_WOBBLE_SPEED_X 0.52f
#define DUST_PARTICLES_FX_AMBIENT_WOBBLE_SPEED_Y 0.31f
#define DUST_PARTICLES_FX_AMBIENT_WOBBLE_SPEED_Z 0.43f

enum {
    /*
     * 96 persistent ambient particles + room for temporary burst particles.
     */
    DUST_PARTICLES_FX_MAX = 160,
    DUST_PARTICLES_FX_FB_COUNT = 3,

    /*
     * Keep this matching the known-working burst path.
     */
    DUST_PARTICLES_FX_ALPHA_BUCKETS = 8
};

typedef enum DustParticleFxKind {
    DUST_PARTICLE_FX_KIND_NONE = 0,
    DUST_PARTICLE_FX_KIND_BURST,
    DUST_PARTICLE_FX_KIND_AMBIENT,
} DustParticleFxKind;

typedef struct DustParticleFx {
    bool active;
    DustParticleFxKind kind;

    float pos[3];
    float vel[3];

    float age;
    float life;

    float size;
    float alphaScale;
} DustParticleFx;

typedef struct DustParticleFxVolume {
    float minX;
    float maxX;

    float minY;
    float maxY;

    float minZ;
    float maxZ;
} DustParticleFxVolume;

static DustParticleFx s_dust[DUST_PARTICLES_FX_MAX];

static TPXParticleS16 *s_tpxParticles = NULL;
static T3DMat4FP *s_particleMatrices = NULL;

/*
 * Keep the original smash/burst dust sprite separate from the ambient sprite.
 */
static sprite_t *s_burstDustSprite = NULL;
static surface_t s_burstDustSurf = {0};

static sprite_t *s_ambientDustSprite = NULL;
static surface_t s_ambientDustSurf = {0};

static int s_frameIdx = 0;
static bool s_initialized = false;

static bool s_ambientEnabled = false;
static uint32_t s_ambientSpawnIndex = 0;

#if DUST_PARTICLES_FX_DEBUG_LOGS
static int s_debugDrawFrame = 0;
static int s_debugUpdateFrame = 0;
#endif

/*
 * Wide room-volume ambient spread.
 *
 * If testing_scene calls dust_particles_fx_set_ambient_volume(), that call
 * overrides this.
 */
static DustParticleFxVolume s_ambientVolume = {
    .minX = -260.0f,
    .maxX =  260.0f,

    .minY =   20.0f,
    .maxY =  170.0f,

    .minZ = -260.0f,
    .maxZ =  260.0f,
};

static inline float dust_particles_fx_clampf(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline float dust_particles_fx_lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

static inline float dust_particles_fx_rand_range(float min, float max)
{
    return dust_particles_fx_lerp(min, max, rand_custom_float());
}

static uint32_t dust_particles_fx_hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static float dust_particles_fx_hash01(uint32_t x)
{
    return (float)(dust_particles_fx_hash_u32(x) & 0x00FFFFFFU) / 16777215.0f;
}

static float dust_particles_fx_hash_range(uint32_t seed, float min, float max)
{
    return dust_particles_fx_lerp(min, max, dust_particles_fx_hash01(seed));
}

static inline int16_t dust_particles_fx_to_s16(float x)
{
    if (x < -32768.0f) return -32768;
    if (x >  32767.0f) return  32767;
    return (int16_t)x;
}

static inline int8_t dust_particles_fx_to_s8_size(float x)
{
    if (x < 0.0f) return 0;
    if (x > 127.0f) return 127;
    return (int8_t)x;
}

static inline float dust_particles_fx_life01(const DustParticleFx *p)
{
    if (!p || p->life <= 0.0f) return 1.0f;
    return dust_particles_fx_clampf(p->age / p->life, 0.0f, 1.0f);
}

static inline float dust_particles_fx_alpha01(const DustParticleFx *p)
{
    if (!p) return 0.0f;

    if (p->kind == DUST_PARTICLE_FX_KIND_AMBIENT) {
        /*
         * Ambient particles are persistent.
         * Constant alpha removes birth/death flicker.
         */
        return dust_particles_fx_clampf(p->alphaScale, 0.0f, 1.0f);
    }

    /*
     * Original burst fade behavior.
     */
    float t = dust_particles_fx_life01(p);
    float a = 1.0f - t;
    a = a * a;
    a *= p->alphaScale;

    return dust_particles_fx_clampf(a, 0.0f, 1.0f);
}

static int dust_particles_fx_alpha_bucket(float alpha01)
{
    if (alpha01 < 0.06f) return -1;

    if (alpha01 > 1.0f) alpha01 = 1.0f;

    int bucket = (int)(alpha01 * (float)DUST_PARTICLES_FX_ALPHA_BUCKETS);

    if (bucket >= DUST_PARTICLES_FX_ALPHA_BUCKETS) {
        bucket = DUST_PARTICLES_FX_ALPHA_BUCKETS - 1;
    }

    return bucket;
}

static uint8_t dust_particles_fx_bucket_alpha_u8(int bucket)
{
    float a = (float)(bucket + 1) / (float)DUST_PARTICLES_FX_ALPHA_BUCKETS;
    a = dust_particles_fx_clampf(a, 0.0f, 1.0f);

    /*
     * Same max alpha scale as the known-working burst dust.
     */
    return (uint8_t)(a * 180.0f);
}

static int dust_particles_fx_find_inactive_slot(void)
{
    for (int i = 0; i < DUST_PARTICLES_FX_MAX; i++) {
        if (!s_dust[i].active) {
            return i;
        }
    }

    return -1;
}

static int dust_particles_fx_alloc_burst_slot(void)
{
    int inactive = dust_particles_fx_find_inactive_slot();
    if (inactive >= 0) return inactive;

    /*
     * Prefer replacing an older burst particle before touching ambient dust.
     */
    int oldestBurst = -1;
    float oldestBurstAge = -1.0f;

    for (int i = 0; i < DUST_PARTICLES_FX_MAX; i++) {
        if (!s_dust[i].active) continue;
        if (s_dust[i].kind != DUST_PARTICLE_FX_KIND_BURST) continue;

        if (oldestBurst < 0 || s_dust[i].age > oldestBurstAge) {
            oldestBurst = i;
            oldestBurstAge = s_dust[i].age;
        }
    }

    if (oldestBurst >= 0) {
        return oldestBurst;
    }

    /*
     * Last-resort fallback.
     */
    int oldest = 0;
    float oldestAge = s_dust[0].age;

    for (int i = 1; i < DUST_PARTICLES_FX_MAX; i++) {
        if (s_dust[i].age > oldestAge) {
            oldestAge = s_dust[i].age;
            oldest = i;
        }
    }

    return oldest;
}

static int dust_particles_fx_alloc_ambient_slot(void)
{
    int inactive = dust_particles_fx_find_inactive_slot();
    if (inactive >= 0) return inactive;

    /*
     * This should usually not happen because ambient is seeded into an empty
     * ambient pool. If it does happen, recycle the oldest ambient particle.
     */
    int oldestAmbient = -1;
    float oldestAmbientAge = -1.0f;

    for (int i = 0; i < DUST_PARTICLES_FX_MAX; i++) {
        if (!s_dust[i].active) continue;
        if (s_dust[i].kind != DUST_PARTICLE_FX_KIND_AMBIENT) continue;

        if (oldestAmbient < 0 || s_dust[i].age > oldestAmbientAge) {
            oldestAmbient = i;
            oldestAmbientAge = s_dust[i].age;
        }
    }

    return oldestAmbient;
}

static int dust_particles_fx_count_ambient_particles(void)
{
    int count = 0;

    for (int i = 0; i < DUST_PARTICLES_FX_MAX; i++) {
        if (!s_dust[i].active) continue;
        if (s_dust[i].kind != DUST_PARTICLE_FX_KIND_AMBIENT) continue;

        count++;
    }

    return count;
}

static int dust_particles_fx_count_burst_particles(void)
{
    int count = 0;

    for (int i = 0; i < DUST_PARTICLES_FX_MAX; i++) {
        if (!s_dust[i].active) continue;
        if (s_dust[i].kind != DUST_PARTICLE_FX_KIND_BURST) continue;

        count++;
    }

    return count;
}

static void dust_particles_fx_clear_tpx_buffer(void)
{
    if (!s_tpxParticles) return;

    uint32_t pairCount = (DUST_PARTICLES_FX_MAX + 1) / 2;
    memset(s_tpxParticles, 0, sizeof(TPXParticleS16) * pairCount);
}

static void dust_particles_fx_deactivate_ambient_particles(void)
{
    for (int i = 0; i < DUST_PARTICLES_FX_MAX; i++) {
        if (s_dust[i].kind == DUST_PARTICLE_FX_KIND_AMBIENT) {
            memset(&s_dust[i], 0, sizeof(s_dust[i]));
        }
    }
}

static void dust_particles_fx_keep_ambient_inside_volume(DustParticleFx *p)
{
    if (!p) return;

    if (p->pos[0] < s_ambientVolume.minX) {
        p->pos[0] = s_ambientVolume.minX;
        p->vel[0] = fabsf(p->vel[0]);
    } else if (p->pos[0] > s_ambientVolume.maxX) {
        p->pos[0] = s_ambientVolume.maxX;
        p->vel[0] = -fabsf(p->vel[0]);
    }

    if (p->pos[1] < s_ambientVolume.minY) {
        p->pos[1] = s_ambientVolume.minY;
        p->vel[1] = fabsf(p->vel[1]);
    } else if (p->pos[1] > s_ambientVolume.maxY) {
        p->pos[1] = s_ambientVolume.maxY;
        p->vel[1] = -fabsf(p->vel[1]);
    }

    if (p->pos[2] < s_ambientVolume.minZ) {
        p->pos[2] = s_ambientVolume.minZ;
        p->vel[2] = fabsf(p->vel[2]);
    } else if (p->pos[2] > s_ambientVolume.maxZ) {
        p->pos[2] = s_ambientVolume.maxZ;
        p->vel[2] = -fabsf(p->vel[2]);
    }
}

static void dust_particles_fx_spawn_ambient_particle(bool randomizeAge)
{
    int idx = dust_particles_fx_alloc_ambient_slot();
    if (idx < 0) return;

    DustParticleFx *p = &s_dust[idx];
    memset(p, 0, sizeof(*p));

    uint32_t seed = ++s_ambientSpawnIndex;

    p->active = true;
    p->kind = DUST_PARTICLE_FX_KIND_AMBIENT;

    /*
     * Ambient particles are permanent while ambient is enabled.
     */
    p->age = 0.0f;
    p->life = 999999.0f;

    if (randomizeAge) {
        p->age = dust_particles_fx_hash_range(seed * 11U + 1U, 0.0f, 100.0f);
    }

    p->size = dust_particles_fx_hash_range(
        seed * 11U + 2U,
        DUST_PARTICLES_FX_AMBIENT_SIZE_MIN,
        DUST_PARTICLES_FX_AMBIENT_SIZE_MAX
    );

    p->alphaScale = DUST_PARTICLES_FX_AMBIENT_ALPHA_SCALE;

    /*
     * Wide hash-based placement.
     */
    p->pos[0] = dust_particles_fx_hash_range(seed * 11U + 3U, s_ambientVolume.minX, s_ambientVolume.maxX);
    p->pos[1] = dust_particles_fx_hash_range(seed * 11U + 4U, s_ambientVolume.minY, s_ambientVolume.maxY);
    p->pos[2] = dust_particles_fx_hash_range(seed * 11U + 5U, s_ambientVolume.minZ, s_ambientVolume.maxZ);

    /*
     * Permanent slow drift.
     *
     * These values are fast enough to be visible, but still slow enough
     * to read as floating room dust instead of projectile particles.
     */
    p->vel[0] = dust_particles_fx_hash_range(
        seed * 11U + 6U,
        -DUST_PARTICLES_FX_AMBIENT_DRIFT_XZ_MAX,
         DUST_PARTICLES_FX_AMBIENT_DRIFT_XZ_MAX
    );

    p->vel[1] = dust_particles_fx_hash_range(
        seed * 11U + 7U,
        DUST_PARTICLES_FX_AMBIENT_DRIFT_Y_MIN,
        DUST_PARTICLES_FX_AMBIENT_DRIFT_Y_MAX
    );

    p->vel[2] = dust_particles_fx_hash_range(
        seed * 11U + 8U,
        -DUST_PARTICLES_FX_AMBIENT_DRIFT_XZ_MAX,
         DUST_PARTICLES_FX_AMBIENT_DRIFT_XZ_MAX
    );

    /*
     * Avoid particles being almost motionless in X/Z.
     */
    if (fabsf(p->vel[0]) < DUST_PARTICLES_FX_AMBIENT_DRIFT_XZ_MIN) {
        p->vel[0] = p->vel[0] < 0.0f
            ? -DUST_PARTICLES_FX_AMBIENT_DRIFT_XZ_MIN
            :  DUST_PARTICLES_FX_AMBIENT_DRIFT_XZ_MIN;
    }

    if (fabsf(p->vel[2]) < DUST_PARTICLES_FX_AMBIENT_DRIFT_XZ_MIN) {
        p->vel[2] = p->vel[2] < 0.0f
            ? -DUST_PARTICLES_FX_AMBIENT_DRIFT_XZ_MIN
            :  DUST_PARTICLES_FX_AMBIENT_DRIFT_XZ_MIN;
    }

    dust_particles_fx_keep_ambient_inside_volume(p);
}

static void dust_particles_fx_seed_ambient_particles(void)
{
    if (!s_ambientEnabled) return;

    /*
     * Fixed resident ambient pool.
     */
    dust_particles_fx_deactivate_ambient_particles();

    for (int i = 0; i < DUST_PARTICLES_FX_AMBIENT_COUNT; i++) {
        dust_particles_fx_spawn_ambient_particle(true);
    }
}

static uint32_t dust_particles_fx_write_tpx_particle(
    uint32_t particleIdx,
    const DustParticleFx *p
) {
    if (!p) return particleIdx;

    float t = dust_particles_fx_life01(p);

    /*
     * Burst expands outward visibly.
     * Ambient barely grows, because it is room dust, not impact smoke.
     */
    float grow = 1.0f;

    if (p->kind == DUST_PARTICLE_FX_KIND_AMBIENT) {
        grow = 1.0f;
    } else {
        grow = 1.0f + 1.15f * t;
    }

    float size = p->size * grow;

    float renderX = p->pos[0];
    float renderY = p->pos[1];
    float renderZ = p->pos[2];

    if (p->kind == DUST_PARTICLE_FX_KIND_AMBIENT) {
        /*
         * Visual-only ambient wobble.
         *
         * This does not affect burst dust.
         * It also does not affect the actual particle bounds/bounce logic.
         */
        float phaseX = (p->age * DUST_PARTICLES_FX_AMBIENT_WOBBLE_SPEED_X) + (p->size * 12.9898f);
        float phaseY = (p->age * DUST_PARTICLES_FX_AMBIENT_WOBBLE_SPEED_Y) + (p->pos[0] * 0.019f);
        float phaseZ = (p->age * DUST_PARTICLES_FX_AMBIENT_WOBBLE_SPEED_Z) + (p->pos[2] * 0.017f);

        renderX += sinf(phaseX) * DUST_PARTICLES_FX_AMBIENT_WOBBLE_XZ;
        renderY += sinf(phaseY) * DUST_PARTICLES_FX_AMBIENT_WOBBLE_Y;
        renderZ += cosf(phaseZ) * DUST_PARTICLES_FX_AMBIENT_WOBBLE_XZ;
    }

    int16_t *pos = tpx_buffer_s16_get_pos(s_tpxParticles, particleIdx);
    int8_t *particleSize = tpx_buffer_s16_get_size(s_tpxParticles, particleIdx);
    uint8_t *rgba = tpx_buffer_s16_get_rgba(s_tpxParticles, particleIdx);

    pos[0] = dust_particles_fx_to_s16(renderX);
    pos[1] = dust_particles_fx_to_s16(renderY);
    pos[2] = dust_particles_fx_to_s16(renderZ);

    *particleSize = dust_particles_fx_to_s8_size(size);

    /*
     * Same warm dust color for both.
     * Texture difference now comes from the sprite selected per particle kind.
     */
    rgba[0] = 215;
    rgba[1] = 210;
    rgba[2] = 200;
    rgba[3] = 0;

    if (particleIdx & 1) {
        s_tpxParticles[particleIdx / 2].texOffsetB = 0;
    } else {
        s_tpxParticles[particleIdx / 2].texOffsetA = 0;
    }

    return particleIdx + 1;
}

static void dust_particles_fx_hide_tpx_particle(uint32_t particleIdx)
{
    if (!s_tpxParticles) return;

    int8_t *particleSize = tpx_buffer_s16_get_size(s_tpxParticles, particleIdx);
    *particleSize = 0;

    if (particleIdx & 1) {
        s_tpxParticles[particleIdx / 2].texOffsetB = 0;
    } else {
        s_tpxParticles[particleIdx / 2].texOffsetA = 0;
    }
}

static sprite_t *dust_particles_fx_get_sprite_for_kind(DustParticleFxKind kind)
{
    if (kind == DUST_PARTICLE_FX_KIND_AMBIENT) {
        return s_ambientDustSprite;
    }

    return s_burstDustSprite;
}

static surface_t *dust_particles_fx_get_surface_for_kind(DustParticleFxKind kind)
{
    if (kind == DUST_PARTICLE_FX_KIND_AMBIENT) {
        return &s_ambientDustSurf;
    }

    return &s_burstDustSurf;
}

static int dust_particles_fx_texture_scale_log(sprite_t *sprite)
{
    if (!sprite) return 0;

    int h = sprite->height;
    if (h <= 8) return 0;

    int sections = h / 8;
    if (sections <= 0) return 0;

    return -__builtin_ctz((unsigned int)sections);
}

static void dust_particles_fx_prepare_matrix(void)
{
    s_frameIdx = (s_frameIdx + 1) % DUST_PARTICLES_FX_FB_COUNT;

    t3d_mat4fp_from_srt_euler(
        &s_particleMatrices[s_frameIdx],
        (float[3]){ 1.0f, 1.0f, 1.0f },
        (float[3]){ 0.0f, 0.0f, 0.0f },
        (float[3]){ 0.0f, 0.0f, 0.0f }
    );
}

static bool dust_particles_fx_has_texture(DustParticleFxKind kind)
{
    if (DUST_PARTICLES_FX_FORCE_UNTEXTURED) return false;

    sprite_t *sprite = dust_particles_fx_get_sprite_for_kind(kind);
    surface_t *surf = dust_particles_fx_get_surface_for_kind(kind);

    if (!sprite) return false;
    if (!surf) return false;
    if (surf->width <= 0 || surf->height <= 0) return false;

    return true;
}

static void dust_particles_fx_upload_texture(DustParticleFxKind kind)
{
    sprite_t *sprite = dust_particles_fx_get_sprite_for_kind(kind);
    if (!sprite) return;

    rdpq_texparms_t p = {0};

    p.s.repeats = REPEAT_INFINITE;
    p.t.repeats = REPEAT_INFINITE;
    p.s.scale_log = dust_particles_fx_texture_scale_log(sprite);
    p.t.scale_log = dust_particles_fx_texture_scale_log(sprite);
    p.s.mirror = false;
    p.t.mirror = false;

    rdpq_sprite_upload(TILE0, sprite, &p);
}

static void dust_particles_fx_draw_tpx_textured(uint32_t drawCount, uint8_t globalAlpha)
{
    rdpq_set_env_color(RGBA32(255, 255, 255, globalAlpha));

    rdpq_mode_combiner(
        RDPQ_COMBINER1(
            (PRIM, 0, TEX0, 0),
            (TEX0, 0, ENV, 0)
        )
    );

    tpx_state_from_t3d();

    tpx_matrix_push(&s_particleMatrices[s_frameIdx]);
    tpx_state_set_scale(1.0f, 1.0f);

    tpx_state_set_tex_params(0, 0);

    tpx_particle_draw_tex_s16(s_tpxParticles, drawCount);

    tpx_matrix_pop(1);
}

static void dust_particles_fx_draw_tpx_untextured(uint32_t drawCount, uint8_t globalAlpha)
{
    rdpq_set_env_color(RGBA32(255, 255, 255, globalAlpha));

    rdpq_mode_combiner(
        RDPQ_COMBINER1(
            (PRIM, 0, ENV, 0),
            (0, 0, 0, ENV)
        )
    );

    tpx_state_from_t3d();

    tpx_matrix_push(&s_particleMatrices[s_frameIdx]);
    tpx_state_set_scale(1.0f, 1.0f);

    tpx_particle_draw_s16(s_tpxParticles, drawCount);

    tpx_matrix_pop(1);
}

static uint32_t dust_particles_fx_draw_kind_bucket(DustParticleFxKind kind, int bucket)
{
    uint32_t drawCount = 0;

    for (int i = 0; i < DUST_PARTICLES_FX_MAX; i++) {
        const DustParticleFx *p = &s_dust[i];
        if (!p->active) continue;
        if (p->kind != kind) continue;

        float alpha01 = dust_particles_fx_alpha01(p);
        int particleBucket = dust_particles_fx_alpha_bucket(alpha01);

        if (particleBucket != bucket) {
            continue;
        }

        drawCount = dust_particles_fx_write_tpx_particle(drawCount, p);
    }

    if (drawCount == 0) {
        return 0;
    }

    if (drawCount & 1) {
        dust_particles_fx_hide_tpx_particle(drawCount);
        drawCount++;
    }

    uint8_t globalAlpha = dust_particles_fx_bucket_alpha_u8(bucket);
    bool useTexture = dust_particles_fx_has_texture(kind);

    if (useTexture) {
        dust_particles_fx_upload_texture(kind);
        dust_particles_fx_draw_tpx_textured(drawCount, globalAlpha);
    } else {
        dust_particles_fx_draw_tpx_untextured(drawCount, globalAlpha);
    }

    return drawCount;
}

void dust_particles_fx_init(void)
{
    if (s_initialized) return;

    particle_system_init();

    uint32_t pairCount = (DUST_PARTICLES_FX_MAX + 1) / 2;

    s_tpxParticles = malloc_uncached(sizeof(TPXParticleS16) * pairCount);
    s_particleMatrices = malloc_uncached(sizeof(T3DMat4FP) * DUST_PARTICLES_FX_FB_COUNT);

    if (!s_tpxParticles || !s_particleMatrices) {
        debugf("dust_particles_fx: allocation failed\n");

        if (s_tpxParticles) {
            free_uncached(s_tpxParticles);
            s_tpxParticles = NULL;
        }

        if (s_particleMatrices) {
            free_uncached(s_particleMatrices);
            s_particleMatrices = NULL;
        }

        return;
    }

    if (!s_burstDustSprite) {
        /*
         * Original smash/burst dust texture.
         * Leave this path alone.
         */
        s_burstDustSprite = sprite_load("rom:/dustParticle.ia8.sprite");

        if (s_burstDustSprite) {
            s_burstDustSurf = sprite_get_pixels(s_burstDustSprite);

#if DUST_PARTICLES_FX_DEBUG_LOGS
            debugf(
                "dust_particles_fx: loaded burst sprite %dx%d\n",
                s_burstDustSprite->width,
                s_burstDustSprite->height
            );
#endif
        } else {
            debugf("dust_particles_fx: FAILED to load rom:/dustParticle.ia8.sprite\n");
        }
    }

    if (!s_ambientDustSprite) {
        /*
         * New ambient-only dust texture.
         */
        s_ambientDustSprite = sprite_load("rom:/ambient_dust.ia8.sprite");

        if (s_ambientDustSprite) {
            s_ambientDustSurf = sprite_get_pixels(s_ambientDustSprite);

#if DUST_PARTICLES_FX_DEBUG_LOGS
            debugf(
                "dust_particles_fx: loaded ambient sprite %dx%d\n",
                s_ambientDustSprite->width,
                s_ambientDustSprite->height
            );
#endif
        } else {
            debugf("dust_particles_fx: FAILED to load rom:/ambient_dust.ia8.sprite\n");
        }
    }

    memset(s_dust, 0, sizeof(s_dust));
    dust_particles_fx_clear_tpx_buffer();

    for (int i = 0; i < DUST_PARTICLES_FX_FB_COUNT; i++) {
        t3d_mat4fp_from_srt_euler(
            &s_particleMatrices[i],
            (float[3]){ 1.0f, 1.0f, 1.0f },
            (float[3]){ 0.0f, 0.0f, 0.0f },
            (float[3]){ 0.0f, 0.0f, 0.0f }
        );
    }

    s_frameIdx = 0;
    s_ambientSpawnIndex = 0;
    s_initialized = true;

    if (s_ambientEnabled) {
        dust_particles_fx_seed_ambient_particles();
    }

#if DUST_PARTICLES_FX_DEBUG_LOGS
    debugf(
        "dust_particles_fx: initialized max=%d ambientEnabled=%d ambient=%d burst=%d spawnIndex=%u\n",
        DUST_PARTICLES_FX_MAX,
        s_ambientEnabled,
        dust_particles_fx_count_ambient_particles(),
        dust_particles_fx_count_burst_particles(),
        (unsigned int)s_ambientSpawnIndex
    );
#endif
}

void dust_particles_fx_cleanup(void)
{
    memset(s_dust, 0, sizeof(s_dust));

    if (s_tpxParticles) {
        free_uncached(s_tpxParticles);
        s_tpxParticles = NULL;
    }

    if (s_particleMatrices) {
        free_uncached(s_particleMatrices);
        s_particleMatrices = NULL;
    }

    if (s_burstDustSprite) {
        sprite_free(s_burstDustSprite);
        s_burstDustSprite = NULL;
        s_burstDustSurf = (surface_t){0};
    }

    if (s_ambientDustSprite) {
        sprite_free(s_ambientDustSprite);
        s_ambientDustSprite = NULL;
        s_ambientDustSurf = (surface_t){0};
    }

    s_frameIdx = 0;
    s_ambientSpawnIndex = 0;
    s_initialized = false;

#if DUST_PARTICLES_FX_DEBUG_LOGS
    debugf("dust_particles_fx: cleanup\n");
#endif
}

void dust_particles_fx_reset(void)
{
    memset(s_dust, 0, sizeof(s_dust));
    dust_particles_fx_clear_tpx_buffer();

    s_ambientSpawnIndex = 0;

    if (s_ambientEnabled) {
        dust_particles_fx_seed_ambient_particles();
    }

#if DUST_PARTICLES_FX_DEBUG_LOGS
    debugf(
        "dust_particles_fx: reset ambientEnabled=%d ambient=%d burst=%d spawnIndex=%u\n",
        s_ambientEnabled,
        dust_particles_fx_count_ambient_particles(),
        dust_particles_fx_count_burst_particles(),
        (unsigned int)s_ambientSpawnIndex
    );
#endif
}

void dust_particles_fx_update(float dt)
{
    if (!s_initialized) return;

    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.25f) dt = 0.25f;

    if (!s_ambientEnabled) {
        dust_particles_fx_deactivate_ambient_particles();
        s_ambientSpawnIndex = 0;
    }

    for (int i = 0; i < DUST_PARTICLES_FX_MAX; i++) {
        DustParticleFx *p = &s_dust[i];
        if (!p->active) continue;

        p->age += dt;

        /*
         * Ambient particles do not die.
         * Only burst particles expire.
         */
        if (p->kind != DUST_PARTICLE_FX_KIND_AMBIENT && p->age >= p->life) {
            memset(p, 0, sizeof(*p));
            continue;
        }

        if (p->kind == DUST_PARTICLE_FX_KIND_AMBIENT) {
            /*
             * Ambient particles keep drifting. No damping here; otherwise
             * they eventually stop and the room becomes static.
             */
            p->pos[0] += p->vel[0] * dt;
            p->pos[1] += p->vel[1] * dt;
            p->pos[2] += p->vel[2] * dt;

            dust_particles_fx_keep_ambient_inside_volume(p);
        } else {
            /*
             * Original burst motion.
             */
            float dampXZ = expf(-5.0f * dt);
            float dampY = expf(-2.5f * dt);

            p->vel[0] *= dampXZ;
            p->vel[2] *= dampXZ;
            p->vel[1] *= dampY;

            p->pos[0] += p->vel[0] * dt;
            p->pos[1] += p->vel[1] * dt;
            p->pos[2] += p->vel[2] * dt;
        }
    }

#if DUST_PARTICLES_FX_DEBUG_LOGS
    s_debugUpdateFrame++;

    if ((s_debugUpdateFrame % 120) == 0) {
        debugf(
            "dust_particles_fx: update ambientEnabled=%d ambient=%d burst=%d spawnIndex=%u\n",
            s_ambientEnabled,
            dust_particles_fx_count_ambient_particles(),
            dust_particles_fx_count_burst_particles(),
            (unsigned int)s_ambientSpawnIndex
        );
    }
#endif
}

void dust_particles_fx_draw(T3DViewport *viewport)
{
    if (!viewport) return;
    if (!s_initialized) return;
    if (!s_tpxParticles || !s_particleMatrices) return;

    dust_particles_fx_prepare_matrix();

    rdpq_sync_pipe();
    rdpq_set_mode_standard();

    rdpq_mode_antialias(AA_NONE);
    rdpq_mode_dithering(DITHER_NONE_NONE);

    /*
     * Keep the known-working render state.
     */
    rdpq_mode_zbuf(true, false);
    rdpq_mode_zoverride(true, 0, 0);

    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_alphacompare(0);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    uint32_t totalDrawn = 0;

    /*
     * Draw high alpha to low alpha.
     *
     * Ambient and burst are separated per bucket so each can upload and use
     * its own texture:
     *
     *   ambient -> rom:/ambient_dust.ia8.sprite
     *   burst   -> rom:/dustParticle.ia8.sprite
     *
     * Burst is drawn after ambient in the same bucket so smash dust remains
     * visually stronger.
     */
    for (int bucket = DUST_PARTICLES_FX_ALPHA_BUCKETS - 1; bucket >= 0; bucket--) {
        totalDrawn += dust_particles_fx_draw_kind_bucket(DUST_PARTICLE_FX_KIND_AMBIENT, bucket);
        totalDrawn += dust_particles_fx_draw_kind_bucket(DUST_PARTICLE_FX_KIND_BURST, bucket);
    }

#if DUST_PARTICLES_FX_DEBUG_LOGS
    s_debugDrawFrame++;

    if ((s_debugDrawFrame % 120) == 0) {
        debugf(
            "dust_particles_fx: draw totalDrawn=%u ambient=%d burst=%d ambientTex=%d burstTex=%d spawnIndex=%u\n",
            (unsigned int)totalDrawn,
            dust_particles_fx_count_ambient_particles(),
            dust_particles_fx_count_burst_particles(),
            dust_particles_fx_has_texture(DUST_PARTICLE_FX_KIND_AMBIENT),
            dust_particles_fx_has_texture(DUST_PARTICLE_FX_KIND_BURST),
            (unsigned int)s_ambientSpawnIndex
        );
    }
#endif

    rdpq_mode_zoverride(false, 0.0f, 0);
    rdpq_mode_zbuf(false, false);
}

void dust_particles_fx_spawn_burst(float x, float y, float z, float strength)
{
    if (!s_initialized) return;

    if (strength < 0.05f) return;
    if (strength > 3.0f) strength = 3.0f;

    int count = 6 + (int)(strength * 3.0f);
    if (count < 6) count = 6;
    if (count > 18) count = 18;

    float spawnX[18];
    float spawnZ[18];
    int spawned = 0;

    for (int i = 0; i < count; i++) {
        int idx = dust_particles_fx_alloc_burst_slot();
        DustParticleFx *p = &s_dust[idx];

        memset(p, 0, sizeof(*p));

        float dirX = 1.0f;
        float dirZ = 0.0f;
        float radius = 0.0f;
        float px = x;
        float pz = z;

        const float minSep = 18.0f;

        for (int attempt = 0; attempt < 10; attempt++) {
            float jitter = (rand_custom_float() - 0.5f) * 0.35f;
            float t = ((float)i + 0.5f + jitter) / (float)count;
            float ang = t * (2.0f * T3D_PI);

            float r01 = sqrtf(rand_custom_float());

            float rMin = 10.0f;
            float rMax = 42.0f + (18.0f * strength);
            radius = rMin + r01 * (rMax - rMin);

            dirX = cosf(ang);
            dirZ = sinf(ang);

            px = x + dirX * radius;
            pz = z + dirZ * radius;

            bool ok = true;
            for (int j = 0; j < spawned; j++) {
                float dx = px - spawnX[j];
                float dz = pz - spawnZ[j];

                if ((dx * dx + dz * dz) < (minSep * minSep)) {
                    ok = false;
                    break;
                }
            }

            if (ok) break;
        }

        p->active = true;
        p->kind = DUST_PARTICLE_FX_KIND_BURST;

        p->age = 0.0f;
        p->life = 0.65f + rand_custom_float() * 0.45f;

        p->alphaScale = 1.0f;

        float bigBias = (i < 2) ? 1.0f : 0.0f;

        float base = 12.0f + (bigBias * 8.0f);
        float var  = 10.0f + (8.0f * strength);
        p->size = base + rand_custom_float() * var;

        p->pos[0] = px;
        p->pos[1] = y + 0.5f + rand_custom_float() * 1.0f;
        p->pos[2] = pz;

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

void dust_particles_fx_set_ambient_enabled(bool enabled)
{
    s_ambientEnabled = enabled;
    s_ambientSpawnIndex = 0;

#if DUST_PARTICLES_FX_DEBUG_LOGS
    debugf(
        "dust_particles_fx: ambient %s\n",
        s_ambientEnabled ? "enabled" : "disabled"
    );
#endif

    if (!s_initialized) return;

    if (!s_ambientEnabled) {
        dust_particles_fx_deactivate_ambient_particles();
        return;
    }

    dust_particles_fx_seed_ambient_particles();

#if DUST_PARTICLES_FX_DEBUG_LOGS
    debugf(
        "dust_particles_fx: ambient count after enable=%d spawnIndex=%u\n",
        dust_particles_fx_count_ambient_particles(),
        (unsigned int)s_ambientSpawnIndex
    );
#endif
}

bool dust_particles_fx_is_ambient_enabled(void)
{
    return s_ambientEnabled;
}

void dust_particles_fx_set_ambient_volume(
    float minX, float maxX,
    float minY, float maxY,
    float minZ, float maxZ
) {
    if (minX > maxX) {
        float tmp = minX;
        minX = maxX;
        maxX = tmp;
    }

    if (minY > maxY) {
        float tmp = minY;
        minY = maxY;
        maxY = tmp;
    }

    if (minZ > maxZ) {
        float tmp = minZ;
        minZ = maxZ;
        maxZ = tmp;
    }

    s_ambientVolume.minX = minX;
    s_ambientVolume.maxX = maxX;

    s_ambientVolume.minY = minY;
    s_ambientVolume.maxY = maxY;

    s_ambientVolume.minZ = minZ;
    s_ambientVolume.maxZ = maxZ;

#if DUST_PARTICLES_FX_DEBUG_LOGS
    debugf(
        "dust_particles_fx: ambient volume x[%f,%f] y[%f,%f] z[%f,%f]\n",
        s_ambientVolume.minX,
        s_ambientVolume.maxX,
        s_ambientVolume.minY,
        s_ambientVolume.maxY,
        s_ambientVolume.minZ,
        s_ambientVolume.maxZ
    );
#endif

    if (!s_initialized) return;
    if (!s_ambientEnabled) return;

    /*
     * Re-seed the fixed resident cloud into the new volume.
     * Burst particles are left alone.
     */
    dust_particles_fx_deactivate_ambient_particles();
    s_ambientSpawnIndex = 0;
    dust_particles_fx_seed_ambient_particles();

#if DUST_PARTICLES_FX_DEBUG_LOGS
    debugf(
        "dust_particles_fx: ambient count after volume=%d spawnIndex=%u\n",
        dust_particles_fx_count_ambient_particles(),
        (unsigned int)s_ambientSpawnIndex
    );
#endif
}