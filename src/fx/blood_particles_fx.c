#include "blood_particles_fx.h"

#include <libdragon.h>

#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/tpx.h>

#include <math.h>
#include <stdbool.h>
#include <string.h>

#include "../utilities/general_utility.h"

#define BLOOD_PARTICLES_FX_CUTOUT 0

enum {
    BLOOD_PARTICLES_FX_MAX = 48,
    BLOOD_PARTICLES_FX_FB_COUNT = 3,
    BLOOD_PARTICLES_FX_ALPHA_BUCKETS = 6
};

typedef struct {
    bool active;

    float pos[3];
    float vel[3];

    float age;
    float life;

    float size;

    uint8_t r;
    uint8_t g;
    uint8_t b;
} BloodParticleFx;

static BloodParticleFx s_blood[BLOOD_PARTICLES_FX_MAX];

static TPXParticleS16 *s_tpxBucketParticles[BLOOD_PARTICLES_FX_ALPHA_BUCKETS] = {0};

static T3DMat4FP *s_particleMatrices = NULL;

static sprite_t *s_bloodSprite = NULL;
static surface_t s_bloodSurf = {0};

static int s_frameIdx = 0;
static bool s_initialized = false;

static inline float blood_particles_fx_clampf(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline int16_t blood_particles_fx_to_s16(float x)
{
    if (x < -32768.0f) return -32768;
    if (x >  32767.0f) return  32767;
    return (int16_t)x;
}

static inline int8_t blood_particles_fx_to_s8_size(float x)
{
    if (x < 0.0f) return 0;
    if (x > 127.0f) return 127;
    return (int8_t)x;
}

static inline float blood_particles_fx_life01(const BloodParticleFx *p)
{
    if (!p || p->life <= 0.0f) return 1.0f;
    return blood_particles_fx_clampf(p->age / p->life, 0.0f, 1.0f);
}

static inline float blood_particles_fx_alpha01(const BloodParticleFx *p)
{
    if (!p || p->life <= 0.0f) return 0.0f;

    float t = blood_particles_fx_life01(p);

    if (t < 0.55f) {
        return 1.0f;
    }

    float u = (t - 0.55f) / 0.45f;
    u = blood_particles_fx_clampf(u, 0.0f, 1.0f);

    float a = 1.0f - u;
    return a * a;
}

static int blood_particles_fx_alpha_bucket(float alpha01)
{
    if (BLOOD_PARTICLES_FX_CUTOUT) {
        return alpha01 > 0.0f ? BLOOD_PARTICLES_FX_ALPHA_BUCKETS - 1 : -1;
    }

    if (alpha01 < 0.06f) {
        return -1;
    }

    if (alpha01 > 1.0f) {
        alpha01 = 1.0f;
    }

    int bucket = (int)(alpha01 * (float)BLOOD_PARTICLES_FX_ALPHA_BUCKETS);

    if (bucket >= BLOOD_PARTICLES_FX_ALPHA_BUCKETS) {
        bucket = BLOOD_PARTICLES_FX_ALPHA_BUCKETS - 1;
    }

    return bucket;
}

static uint8_t blood_particles_fx_bucket_alpha_u8(int bucket)
{
    if (BLOOD_PARTICLES_FX_CUTOUT) {
        return 255;
    }

    float a = (float)(bucket + 1) / (float)BLOOD_PARTICLES_FX_ALPHA_BUCKETS;
    a = blood_particles_fx_clampf(a, 0.0f, 1.0f);

    return (uint8_t)(a * 230.0f);
}

static int blood_particles_fx_alloc_slot(void)
{
    for (int i = 0; i < BLOOD_PARTICLES_FX_MAX; i++) {
        if (!s_blood[i].active) {
            return i;
        }
    }

    int oldest = 0;
    float oldestAge = s_blood[0].age;

    for (int i = 1; i < BLOOD_PARTICLES_FX_MAX; i++) {
        if (s_blood[i].age > oldestAge) {
            oldestAge = s_blood[i].age;
            oldest = i;
        }
    }

    return oldest;
}

static uint32_t blood_particles_fx_pair_count(void)
{
    return (BLOOD_PARTICLES_FX_MAX + 1) / 2;
}

static bool blood_particles_fx_alloc_tpx_bucket_buffers(void)
{
    uint32_t pairCount = blood_particles_fx_pair_count();

    for (int bucket = 0; bucket < BLOOD_PARTICLES_FX_ALPHA_BUCKETS; bucket++) {
        s_tpxBucketParticles[bucket] = malloc_uncached(sizeof(TPXParticleS16) * pairCount);

        if (!s_tpxBucketParticles[bucket]) {
            return false;
        }

        memset(s_tpxBucketParticles[bucket], 0, sizeof(TPXParticleS16) * pairCount);
    }

    return true;
}

static void blood_particles_fx_free_tpx_bucket_buffers(void)
{
    for (int bucket = 0; bucket < BLOOD_PARTICLES_FX_ALPHA_BUCKETS; bucket++) {
        if (s_tpxBucketParticles[bucket]) {
            free_uncached(s_tpxBucketParticles[bucket]);
            s_tpxBucketParticles[bucket] = NULL;
        }
    }
}

static void blood_particles_fx_clear_tpx_bucket_buffers(void)
{
    uint32_t pairCount = blood_particles_fx_pair_count();

    for (int bucket = 0; bucket < BLOOD_PARTICLES_FX_ALPHA_BUCKETS; bucket++) {
        TPXParticleS16 *buffer = s_tpxBucketParticles[bucket];
        if (!buffer) continue;

        memset(buffer, 0, sizeof(TPXParticleS16) * pairCount);
    }
}

static uint32_t blood_particles_fx_write_tpx_particle(
    TPXParticleS16 *buffer,
    uint32_t particleIdx,
    const BloodParticleFx *p
) {
    if (!buffer) return particleIdx;
    if (!p) return particleIdx;

    int16_t *pos = tpx_buffer_s16_get_pos(buffer, particleIdx);
    int8_t *particleSize = tpx_buffer_s16_get_size(buffer, particleIdx);
    uint8_t *rgba = tpx_buffer_s16_get_rgba(buffer, particleIdx);

    pos[0] = blood_particles_fx_to_s16(p->pos[0]);
    pos[1] = blood_particles_fx_to_s16(p->pos[1]);
    pos[2] = blood_particles_fx_to_s16(p->pos[2]);

    *particleSize = blood_particles_fx_to_s8_size(p->size);

    rgba[0] = p->r;
    rgba[1] = p->g;
    rgba[2] = p->b;
    rgba[3] = 0;

    if (particleIdx & 1) {
        buffer[particleIdx / 2].texOffsetB = 0;
    } else {
        buffer[particleIdx / 2].texOffsetA = 0;
    }

    return particleIdx + 1;
}

static void blood_particles_fx_hide_tpx_particle(TPXParticleS16 *buffer, uint32_t particleIdx)
{
    if (!buffer) return;

    int8_t *particleSize = tpx_buffer_s16_get_size(buffer, particleIdx);
    *particleSize = 0;

    if (particleIdx & 1) {
        buffer[particleIdx / 2].texOffsetB = 0;
    } else {
        buffer[particleIdx / 2].texOffsetA = 0;
    }
}

static int blood_particles_fx_texture_scale_log(void)
{
    if (!s_bloodSprite) return 0;

    int h = s_bloodSprite->height;
    if (h <= 8) return 0;

    int sections = h / 8;
    if (sections <= 0) return 0;

    return -__builtin_ctz((unsigned int)sections);
}

static bool blood_particles_fx_has_texture(void)
{
    if (!s_bloodSprite) return false;
    if (s_bloodSurf.width <= 0 || s_bloodSurf.height <= 0) return false;
    return true;
}

static float blood_particles_fx_sprite_aspect(void)
{
    if (s_bloodSurf.width <= 0 || s_bloodSurf.height <= 0) {
        return 1.0f;
    }

    float aspect = (float)s_bloodSurf.width / (float)s_bloodSurf.height;

    if (aspect < 0.25f) aspect = 0.25f;
    if (aspect > 4.0f)  aspect = 4.0f;

    return aspect;
}

static void blood_particles_fx_prepare_matrix(void)
{
    s_frameIdx = (s_frameIdx + 1) % BLOOD_PARTICLES_FX_FB_COUNT;

    t3d_mat4fp_from_srt_euler(
        &s_particleMatrices[s_frameIdx],
        (float[3]){ 1.0f, 1.0f, 1.0f },
        (float[3]){ 0.0f, 0.0f, 0.0f },
        (float[3]){ 0.0f, 0.0f, 0.0f }
    );
}

static void blood_particles_fx_upload_texture(void)
{
    if (!s_bloodSprite) return;

    rdpq_texparms_t p = {0};

    p.s.repeats = REPEAT_INFINITE;
    p.t.repeats = REPEAT_INFINITE;
    p.s.scale_log = blood_particles_fx_texture_scale_log();
    p.t.scale_log = blood_particles_fx_texture_scale_log();
    p.s.mirror = false;
    p.t.mirror = false;

    rdpq_sprite_upload(TILE0, s_bloodSprite, &p);
}

static void blood_particles_fx_draw_tpx_textured(
    TPXParticleS16 *buffer,
    uint32_t drawCount,
    uint8_t globalAlpha
) {
    if (!buffer) return;

    rdpq_set_env_color(RGBA32(255, 255, 255, globalAlpha));

    rdpq_mode_combiner(
        RDPQ_COMBINER1(
            (PRIM, 0, TEX0, 0),
            (TEX0, 0, ENV, 0)
        )
    );

    tpx_state_from_t3d();

    tpx_matrix_push(&s_particleMatrices[s_frameIdx]);

    float aspect = blood_particles_fx_sprite_aspect();
    tpx_state_set_scale(aspect, 1.0f);

    tpx_state_set_tex_params(0, 0);

    tpx_particle_draw_tex_s16(buffer, drawCount);

    tpx_matrix_pop(1);
}

static void blood_particles_fx_draw_tpx_untextured(
    TPXParticleS16 *buffer,
    uint32_t drawCount,
    uint8_t globalAlpha
) {
    if (!buffer) return;

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
    tpx_particle_draw_s16(buffer, drawCount);
    tpx_matrix_pop(1);
}

void blood_particles_fx_init(void)
{
    if (s_initialized) return;

    if (!blood_particles_fx_alloc_tpx_bucket_buffers()) {
        blood_particles_fx_free_tpx_bucket_buffers();
        return;
    }

    s_particleMatrices = malloc_uncached(sizeof(T3DMat4FP) * BLOOD_PARTICLES_FX_FB_COUNT);

    if (!s_particleMatrices) {
        blood_particles_fx_free_tpx_bucket_buffers();
        return;
    }

    s_bloodSprite = sprite_load("rom:/blood/blood_large.ia8.sprite");

    if (s_bloodSprite) {
        s_bloodSurf = sprite_get_pixels(s_bloodSprite);
    }

    memset(s_blood, 0, sizeof(s_blood));
    blood_particles_fx_clear_tpx_bucket_buffers();

    for (int i = 0; i < BLOOD_PARTICLES_FX_FB_COUNT; i++) {
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

void blood_particles_fx_cleanup(void)
{
    memset(s_blood, 0, sizeof(s_blood));

    blood_particles_fx_free_tpx_bucket_buffers();

    if (s_particleMatrices) {
        free_uncached(s_particleMatrices);
        s_particleMatrices = NULL;
    }

    if (s_bloodSprite) {
        sprite_free(s_bloodSprite);
        s_bloodSprite = NULL;
        s_bloodSurf = (surface_t){0};
    }

    s_frameIdx = 0;
    s_initialized = false;
}

void blood_particles_fx_reset(void)
{
    memset(s_blood, 0, sizeof(s_blood));
    blood_particles_fx_clear_tpx_bucket_buffers();
}

void blood_particles_fx_update(float dt)
{
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.25f) dt = 0.25f;

    const float gravity = 900.0f;

    for (int i = 0; i < BLOOD_PARTICLES_FX_MAX; i++) {
        BloodParticleFx *p = &s_blood[i];
        if (!p->active) continue;

        p->age += dt;

        if (p->age >= p->life) {
            p->active = false;
            continue;
        }

        float dampXZ = expf(-1.5f * dt);

        p->vel[0] *= dampXZ;
        p->vel[2] *= dampXZ;

        p->vel[1] -= gravity * dt;

        p->pos[0] += p->vel[0] * dt;
        p->pos[1] += p->vel[1] * dt;
        p->pos[2] += p->vel[2] * dt;
    }
}

void blood_particles_fx_draw(T3DViewport *viewport)
{
    if (!viewport) return;
    if (!s_initialized) return;
    if (!s_particleMatrices) return;

    bool useTexture = blood_particles_fx_has_texture();

    blood_particles_fx_prepare_matrix();

    rdpq_sync_pipe();
    rdpq_set_mode_standard();

    rdpq_mode_antialias(AA_NONE);
    rdpq_mode_dithering(DITHER_NONE_NONE);

    rdpq_mode_zbuf(true, false);
    rdpq_mode_zoverride(true, 0, 0);

    rdpq_mode_filter(FILTER_BILINEAR);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

#if BLOOD_PARTICLES_FX_CUTOUT
    rdpq_mode_alphacompare(1);
#else
    rdpq_mode_alphacompare(0);
#endif

    if (useTexture) {
        blood_particles_fx_upload_texture();
    }

    for (int bucket = BLOOD_PARTICLES_FX_ALPHA_BUCKETS - 1; bucket >= 0; bucket--) {
        TPXParticleS16 *buffer = s_tpxBucketParticles[bucket];

        if (!buffer) {
            continue;
        }

        uint32_t drawCount = 0;

        for (int i = 0; i < BLOOD_PARTICLES_FX_MAX; i++) {
            const BloodParticleFx *p = &s_blood[i];
            if (!p->active) continue;

            float alpha01 = blood_particles_fx_alpha01(p);
            int particleBucket = blood_particles_fx_alpha_bucket(alpha01);

            if (particleBucket != bucket) {
                continue;
            }

            drawCount = blood_particles_fx_write_tpx_particle(buffer, drawCount, p);
        }

        if (drawCount == 0) {
            continue;
        }

        if (drawCount & 1) {
            blood_particles_fx_hide_tpx_particle(buffer, drawCount);
            drawCount++;
        }

        uint8_t globalAlpha = blood_particles_fx_bucket_alpha_u8(bucket);

        if (useTexture) {
            blood_particles_fx_draw_tpx_textured(buffer, drawCount, globalAlpha);
        } else {
            blood_particles_fx_draw_tpx_untextured(buffer, drawCount, globalAlpha);
        }
    }

    rdpq_mode_zoverride(false, 0.0f, 0);
    rdpq_mode_zbuf(false, false);
}

void blood_particles_fx_spawn_burst(float x, float y, float z, float strength)
{
    if (strength < 0.05f) return;
    if (strength > 3.0f) strength = 3.0f;

    int largeCount = 1;
    int mediumCount = 3 + (int)(strength * 1.5f);
    int tinyCount = 7 + (int)(strength * 4.0f);

    if (mediumCount < 3) mediumCount = 3;
    if (mediumCount > 7) mediumCount = 7;

    if (tinyCount < 7) tinyCount = 7;
    if (tinyCount > 18) tinyCount = 18;

    int total = largeCount + mediumCount + tinyCount;
    if (total > 28) total = 28;

    for (int i = 0; i < total; i++) {
        int idx = blood_particles_fx_alloc_slot();
        BloodParticleFx *p = &s_blood[idx];

        memset(p, 0, sizeof(*p));

        bool isLarge = i < largeCount;
        bool isMedium = !isLarge && i < largeCount + mediumCount;

        p->active = true;
        p->age = 0.0f;

        if (isLarge) {
            p->life = 0.60f + rand_custom_float() * 0.18f;
            p->size = 9.0f + 2.0f * strength;
        } else if (isMedium) {
            p->life = 0.45f + rand_custom_float() * 0.18f;
            p->size = 5.5f + rand_custom_float() * (1.25f + 0.5f * strength);
        } else {
            p->life = 0.32f + rand_custom_float() * 0.16f;
            p->size = 2.5f + rand_custom_float() * (1.0f + 0.35f * strength);
        }

        p->r = (uint8_t)(120 + (int)(rand_custom_float() * 70.0f));
        p->g = (uint8_t)(0   + (int)(rand_custom_float() * 18.0f));
        p->b = (uint8_t)(0   + (int)(rand_custom_float() * 12.0f));

        p->pos[0] = x + (rand_custom_float() - 0.5f) * 5.0f;
        p->pos[1] = y + (rand_custom_float() - 0.5f) * 5.0f;
        p->pos[2] = z + (rand_custom_float() - 0.5f) * 5.0f;

        float ang = rand_custom_float() * (2.0f * T3D_PI);

        float horiz = isLarge
            ? (0.25f + rand_custom_float() * 0.35f)
            : (0.45f + rand_custom_float() * 0.55f);

        float dirX = cosf(ang) * horiz;
        float dirZ = sinf(ang) * horiz;
        float dirY = isLarge
            ? (0.15f + rand_custom_float() * 0.25f)
            : (0.35f + rand_custom_float() * 0.65f);

        float len = sqrtf(dirX * dirX + dirY * dirY + dirZ * dirZ);
        if (len > 0.001f) {
            dirX /= len;
            dirY /= len;
            dirZ /= len;
        }

        /*
         Speeds:
         - big anchor moves slowly
         - medium drops spray outward
         - tiny drops spray fastest
         */
        float baseSpeed;

        if (isLarge) {
            baseSpeed = 30.0f + 20.0f * strength;
        } else if (isMedium) {
            baseSpeed = 110.0f + 60.0f * strength;
        } else {
            baseSpeed = 170.0f + 90.0f * strength;
        }

        float speed = baseSpeed * (0.75f + rand_custom_float() * 0.5f);

        p->vel[0] = dirX * speed;
        p->vel[1] = dirY * speed;
        p->vel[2] = dirZ * speed;
    }
}