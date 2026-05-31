#include "dust_particles_fx.h"

#include <libdragon.h>

#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/tpx.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../utilities/general_utility.h"

/*
 * Dust particles using Tiny3D TPX microcode.
 *
 * Textured TPX notes:
 * - The texture must be uploaded with RDPQ before the TPX draw.
 * - TPX maps particle UVs as if the texture section is 8x8.
 * - For textured particles, the particle alpha byte is used as a per-particle
 *   texture U offset, NOT particle opacity.
 *
 * Because of that, per-particle transparency is approximated by drawing particles
 * in alpha buckets. Each bucket uses ENV alpha as a global fade.
 */

#define DUST_PARTICLES_FX_FORCE_UNTEXTURED 0

enum {
    DUST_PARTICLES_FX_MAX = 64,
    DUST_PARTICLES_FX_FB_COUNT = 3,

    /*
     * More buckets = smoother fade, more TPX draw calls.
     *
     * 4 was visibly stepped.
     * 8 is a good compromise for 64 max particles.
     * 16 would be smoother but starts getting wasteful for this effect.
     */
    DUST_PARTICLES_FX_ALPHA_BUCKETS = 8
};

typedef struct {
    bool active;

    float pos[3];
    float vel[3];

    float age;
    float life;

    /*
     * TPX size value, not the old screen-space sprite pixel radius.
     */
    float size;
} DustParticleFx;

static DustParticleFx s_dust[DUST_PARTICLES_FX_MAX];

static TPXParticleS16 *s_tpxParticles = NULL;
static T3DMat4FP *s_particleMatrices = NULL;

static sprite_t *s_dustSprite = NULL;
static surface_t s_dustSurf = {0};

static int s_frameIdx = 0;
static bool s_initialized = false;
static bool s_tpxInitializedFromHere = false;

static inline float dust_particles_fx_clampf(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
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
    float t = dust_particles_fx_life01(p);
    float a = 1.0f - t;

    /*
     * Eased fade.
     *
     * This is NOT written into TPX particle alpha.
     * Textured TPX uses particle alpha as texture offset.
     */
    return a * a;
}

static int dust_particles_fx_alpha_bucket(float alpha01)
{
    /*
     * Skip extremely low alpha particles.
     * This avoids spending a bucket draw on almost invisible dust.
     */
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
    /*
     * Use bucket + 1 instead of bucket center so the top bucket can reach
     * the requested max opacity.
     *
     * With 8 buckets and max 180, levels are roughly:
     * 22, 45, 67, 90, 112, 135, 157, 180
     */
    float a = (float)(bucket + 1) / (float)DUST_PARTICLES_FX_ALPHA_BUCKETS;
    a = dust_particles_fx_clampf(a, 0.0f, 1.0f);

    /*
     * Cap below 255 so dust never appears like opaque paper.
     */
    return (uint8_t)(a * 180.0f);
}

static int dust_particles_fx_alloc_slot(void)
{
    for (int i = 0; i < DUST_PARTICLES_FX_MAX; i++) {
        if (!s_dust[i].active) {
            return i;
        }
    }

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

static void dust_particles_fx_clear_tpx_buffer(void)
{
    if (!s_tpxParticles) return;

    uint32_t pairCount = (DUST_PARTICLES_FX_MAX + 1) / 2;
    memset(s_tpxParticles, 0, sizeof(TPXParticleS16) * pairCount);
}

static uint32_t dust_particles_fx_write_tpx_particle(
    uint32_t particleIdx,
    const DustParticleFx *p
) {
    if (!p) return particleIdx;

    float t = dust_particles_fx_life01(p);
    float grow = 1.0f + 1.15f * t;
    float size = p->size * grow;

    int16_t *pos = tpx_buffer_s16_get_pos(s_tpxParticles, particleIdx);
    int8_t *particleSize = tpx_buffer_s16_get_size(s_tpxParticles, particleIdx);
    uint8_t *rgba = tpx_buffer_s16_get_rgba(s_tpxParticles, particleIdx);

    pos[0] = dust_particles_fx_to_s16(p->pos[0]);
    pos[1] = dust_particles_fx_to_s16(p->pos[1]);
    pos[2] = dust_particles_fx_to_s16(p->pos[2]);

    *particleSize = dust_particles_fx_to_s8_size(size);

    /*
     * For textured TPX, rgba[3] is texture U offset, not opacity.
     * Keep it 0 for a single-frame dust texture.
     */
    rgba[0] = 215;
    rgba[1] = 210;
    rgba[2] = 200;
    rgba[3] = 0;

    /*
     * Explicitly clear tex offsets. Avoid tpx_buffer_s16_get_tex_offset()
     * because some Tiny3D versions had A/B reversed for that helper.
     */
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

static int dust_particles_fx_texture_scale_log(void)
{
    if (!s_dustSprite) return 0;

    int h = s_dustSprite->height;
    if (h <= 8) return 0;

    int sections = h / 8;
    if (sections <= 0) return 0;

    /*
     * TPX textured particles internally map UVs as 8x8.
     * Demo formula:
     *   scale_log = -ctz(texture_height / 8)
     */
    return -__builtin_ctz((unsigned int)sections);
}

static void dust_particles_fx_prepare_matrix(void)
{
    s_frameIdx = (s_frameIdx + 1) % DUST_PARTICLES_FX_FB_COUNT;

    /*
     * Identity/world matrix. Particle positions are already in world space.
     * Camera/projection state is copied from T3D with tpx_state_from_t3d().
     */
    t3d_mat4fp_from_srt_euler(
        &s_particleMatrices[s_frameIdx],
        (float[3]){ 1.0f, 1.0f, 1.0f },
        (float[3]){ 0.0f, 0.0f, 0.0f },
        (float[3]){ 0.0f, 0.0f, 0.0f }
    );
}

static bool dust_particles_fx_has_texture(void)
{
    if (DUST_PARTICLES_FX_FORCE_UNTEXTURED) return false;
    if (!s_dustSprite) return false;
    if (s_dustSurf.width <= 0 || s_dustSurf.height <= 0) return false;
    return true;
}

static void dust_particles_fx_upload_texture(void)
{
    rdpq_texparms_t p = {0};

    p.s.repeats = REPEAT_INFINITE;
    p.t.repeats = REPEAT_INFINITE;
    p.s.scale_log = dust_particles_fx_texture_scale_log();
    p.t.scale_log = dust_particles_fx_texture_scale_log();
    p.s.mirror = false;
    p.t.mirror = false;

    rdpq_sprite_upload(TILE0, s_dustSprite, &p);
}

static void dust_particles_fx_draw_tpx_textured(uint32_t drawCount, uint8_t globalAlpha)
{
    /*
     * Transparent textured dust:
     *
     * RGB:
     *   PRIM * TEX0
     *
     * Alpha:
     *   TEX0 alpha * ENV alpha
     *
     * ENV alpha is changed per bucket to approximate per-particle fading
     * without using the TPX particle alpha byte.
     */
    rdpq_set_env_color(RGBA32(255, 255, 255, globalAlpha));

    rdpq_mode_combiner(
        RDPQ_COMBINER1(
            (PRIM, 0, TEX0, 0),
            (TEX0, 0, ENV, 0)
        )
    );

    tpx_state_from_t3d();

    /*
     * First TPX stack operation after tpx_state_from_t3d() should be push.
     */
    tpx_matrix_push(&s_particleMatrices[s_frameIdx]);
    tpx_state_set_scale(1.0f, 1.0f);

    /*
     * Static single-frame texture: no global texture offset, no mirror point.
     */
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

void dust_particles_fx_init(void)
{
    if (s_initialized) return;

    /*
     * Better long-term: call tpx_init() once globally after t3d_init().
     * Kept here for now so the effect is self-contained during scene cleanup.
     */
    tpx_init((TPXInitParams){});
    s_tpxInitializedFromHere = true;

    uint32_t pairCount = (DUST_PARTICLES_FX_MAX + 1) / 2;

    s_tpxParticles = malloc_uncached(sizeof(TPXParticleS16) * pairCount);
    s_particleMatrices = malloc_uncached(sizeof(T3DMat4FP) * DUST_PARTICLES_FX_FB_COUNT);

    if (!s_tpxParticles || !s_particleMatrices) {
        if (s_tpxParticles) {
            free_uncached(s_tpxParticles);
            s_tpxParticles = NULL;
        }

        if (s_particleMatrices) {
            free_uncached(s_particleMatrices);
            s_particleMatrices = NULL;
        }

        if (s_tpxInitializedFromHere) {
            tpx_destroy();
            s_tpxInitializedFromHere = false;
        }

        return;
    }

    /*
     * The scene no longer owns this sprite. Load it once here.
     */
    if (!s_dustSprite) {
        s_dustSprite = sprite_load("rom:/dustParticle.ia8.sprite");

        if (s_dustSprite) {
            s_dustSurf = sprite_get_pixels(s_dustSprite);
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
    s_initialized = true;
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

    if (s_dustSprite) {
        sprite_free(s_dustSprite);
        s_dustSprite = NULL;
        s_dustSurf = (surface_t){0};
    }

    /*
     * If TPX later becomes globally initialized by your renderer/bootstrap,
     * remove tpx_init() from this module and remove this tpx_destroy() block.
     */
    if (s_tpxInitializedFromHere) {
        tpx_destroy();
        s_tpxInitializedFromHere = false;
    }

    s_frameIdx = 0;
    s_initialized = false;
}

void dust_particles_fx_reset(void)
{
    memset(s_dust, 0, sizeof(s_dust));
    dust_particles_fx_clear_tpx_buffer();
}

void dust_particles_fx_update(float dt)
{
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.25f) dt = 0.25f;

    for (int i = 0; i < DUST_PARTICLES_FX_MAX; i++) {
        DustParticleFx *p = &s_dust[i];
        if (!p->active) continue;

        p->age += dt;

        if (p->age >= p->life) {
            p->active = false;
            continue;
        }

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

void dust_particles_fx_draw(T3DViewport *viewport)
{
    if (!viewport) return;
    if (!s_initialized) return;
    if (!s_tpxParticles || !s_particleMatrices) return;

    bool useTexture = dust_particles_fx_has_texture();

    dust_particles_fx_prepare_matrix();

    rdpq_sync_pipe();
    rdpq_set_mode_standard();

    rdpq_mode_antialias(AA_NONE);
    rdpq_mode_dithering(DITHER_NONE_NONE);

    /*
     * Transparent particles should depth-test but not depth-write.
     * Otherwise the first dust quad can punch holes in later dust quads.
     */
    rdpq_mode_zbuf(true, false);
    rdpq_mode_zoverride(true, 0, 0);

    rdpq_mode_filter(FILTER_BILINEAR);

    /*
     * IMPORTANT:
     * No alpha compare for dust. Alpha compare makes it cutout.
     */
    rdpq_mode_alphacompare(0);

    /*
     * Keep using the same blender style as the existing IA8 sprite/UI code.
     */
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    if (useTexture) {
        dust_particles_fx_upload_texture();
    }

    /*
     * Draw from high alpha to low alpha.
     *
     * With depth-write disabled this gives stable-enough translucent layering
     * without sorting every particle.
     *
     * Empty buckets are skipped, so 8 buckets is "up to 8 draws", not always 8.
     */
    for (int bucket = DUST_PARTICLES_FX_ALPHA_BUCKETS - 1; bucket >= 0; bucket--) {
        uint32_t drawCount = 0;

        for (int i = 0; i < DUST_PARTICLES_FX_MAX; i++) {
            const DustParticleFx *p = &s_dust[i];
            if (!p->active) continue;

            float alpha01 = dust_particles_fx_alpha01(p);
            int particleBucket = dust_particles_fx_alpha_bucket(alpha01);

            if (particleBucket != bucket) {
                continue;
            }

            drawCount = dust_particles_fx_write_tpx_particle(drawCount, p);
        }

        if (drawCount == 0) {
            continue;
        }

        if (drawCount & 1) {
            dust_particles_fx_hide_tpx_particle(drawCount);
            drawCount++;
        }

        uint8_t globalAlpha = dust_particles_fx_bucket_alpha_u8(bucket);

        if (useTexture) {
            dust_particles_fx_draw_tpx_textured(drawCount, globalAlpha);
        } else {
            dust_particles_fx_draw_tpx_untextured(drawCount, globalAlpha);
        }
    }

    rdpq_mode_zoverride(false, 0.0f, 0);
    rdpq_mode_zbuf(false, false);
}

void dust_particles_fx_spawn_burst(float x, float y, float z, float strength)
{
    if (strength < 0.05f) return;
    if (strength > 3.0f) strength = 3.0f;

    int count = 6 + (int)(strength * 3.0f);
    if (count < 6) count = 6;
    if (count > 18) count = 18;

    float spawnX[18];
    float spawnZ[18];
    int spawned = 0;

    for (int i = 0; i < count; i++) {
        int idx = dust_particles_fx_alloc_slot();
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
        p->age = 0.0f;
        p->life = 0.65f + rand_custom_float() * 0.45f;

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