#include "room_dust_fx.h"

#include <libdragon.h>

#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/tpx.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "particle_system.h"
#include "../utilities/general_utility.h"

/*
 * Room ambience dust.
 *
 * This version intentionally keeps the TPX textured draw path extremely close
 * to the known-working boss-smash dust_particles_fx.c render path.
 */
#define ROOM_DUST_FX_FORCE_UNTEXTURED 0
#define ROOM_DUST_FX_DEBUG_LOGS 1

enum {
    ROOM_DUST_FX_MAX = 64,
    ROOM_DUST_FX_FB_COUNT = 3,

    /*
     * Match the working boss dust path for now.
     * We can reduce this later if draw-call cost matters.
     */
    ROOM_DUST_FX_ALPHA_BUCKETS = 8
};

/*
 * Test-visible room volume.
 *
 * This is intentionally tighter and larger-than-final so you can confirm the
 * particles are visible before we make them subtle.
 */
#define ROOM_DUST_X_MIN (-150.0f)
#define ROOM_DUST_X_MAX ( 150.0f)

#define ROOM_DUST_Y_MIN (  10.0f)
#define ROOM_DUST_Y_MAX ( 115.0f)

#define ROOM_DUST_Z_MIN (-150.0f)
#define ROOM_DUST_Z_MAX ( 150.0f)

/*
 * Larger-than-final while proving the textured path.
 *
 * Final ambience can probably be:
 *   3.0f - 7.0f
 * or:
 *   4.0f - 8.0f
 */
#define ROOM_DUST_SIZE_MIN 8.0f
#define ROOM_DUST_SIZE_MAX 16.0f

#define ROOM_DUST_LIFE_MIN 8.0f
#define ROOM_DUST_LIFE_MAX 16.0f

/*
 * Slow ambient drift. Units are world-units per second.
 */
#define ROOM_DUST_DRIFT_XZ 3.0f
#define ROOM_DUST_DRIFT_Y  1.25f

typedef struct RoomDustParticle {
    bool active;

    float pos[3];
    float vel[3];

    float age;
    float life;

    float size;
} RoomDustParticle;

static RoomDustParticle s_dust[ROOM_DUST_FX_MAX];

static TPXParticleS16 *s_tpxParticles = NULL;
static T3DMat4FP *s_particleMatrices = NULL;

static sprite_t *s_dustSprite = NULL;
static surface_t s_dustSurf = {0};

static int s_frameIdx = 0;
static bool s_initialized = false;
static bool s_enabled = true;

#if ROOM_DUST_FX_DEBUG_LOGS
static int s_debugDrawFrame = 0;
#endif

static inline float room_dust_fx_clampf(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline float room_dust_fx_lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

static inline float room_dust_fx_rand_range(float min, float max)
{
    return room_dust_fx_lerp(min, max, rand_custom_float());
}

static inline int16_t room_dust_fx_to_s16(float x)
{
    if (x < -32768.0f) return -32768;
    if (x >  32767.0f) return  32767;
    return (int16_t)x;
}

static inline int8_t room_dust_fx_to_s8_size(float x)
{
    if (x < 0.0f) return 0;
    if (x > 127.0f) return 127;
    return (int8_t)x;
}

static inline float room_dust_fx_life01(const RoomDustParticle *p)
{
    if (!p || p->life <= 0.0f) return 1.0f;
    return room_dust_fx_clampf(p->age / p->life, 0.0f, 1.0f);
}

static inline float room_dust_fx_alpha01(const RoomDustParticle *p)
{
    float t = room_dust_fx_life01(p);

    /*
     * Fade in, hold, fade out.
     */
    float fadeIn = room_dust_fx_clampf(t / 0.18f, 0.0f, 1.0f);
    float fadeOut = room_dust_fx_clampf((1.0f - t) / 0.35f, 0.0f, 1.0f);

    float a = fadeIn * fadeOut;

    return a * a;
}

static int room_dust_fx_alpha_bucket(float alpha01)
{
    /*
     * Same cutoff style as working boss dust.
     */
    if (alpha01 < 0.06f) return -1;

    if (alpha01 > 1.0f) alpha01 = 1.0f;

    int bucket = (int)(alpha01 * (float)ROOM_DUST_FX_ALPHA_BUCKETS);

    if (bucket >= ROOM_DUST_FX_ALPHA_BUCKETS) {
        bucket = ROOM_DUST_FX_ALPHA_BUCKETS - 1;
    }

    return bucket;
}

static uint8_t room_dust_fx_bucket_alpha_u8(int bucket)
{
    float a = (float)(bucket + 1) / (float)ROOM_DUST_FX_ALPHA_BUCKETS;
    a = room_dust_fx_clampf(a, 0.0f, 1.0f);

    /*
     * Match working boss dust max alpha for now.
     * We can lower this after visibility is confirmed.
     */
    return (uint8_t)(a * 180.0f);
}

static void room_dust_fx_clear_tpx_buffer(void)
{
    if (!s_tpxParticles) return;

    uint32_t pairCount = (ROOM_DUST_FX_MAX + 1) / 2;
    memset(s_tpxParticles, 0, sizeof(TPXParticleS16) * pairCount);
}

static void room_dust_fx_respawn_particle(int idx, bool randomizeAge)
{
    if (idx < 0 || idx >= ROOM_DUST_FX_MAX) return;

    RoomDustParticle *p = &s_dust[idx];
    memset(p, 0, sizeof(*p));

    p->active = true;

    p->life = room_dust_fx_rand_range(
        ROOM_DUST_LIFE_MIN,
        ROOM_DUST_LIFE_MAX
    );

    p->age = randomizeAge
        ? room_dust_fx_rand_range(0.0f, p->life)
        : 0.0f;

    p->size = room_dust_fx_rand_range(
        ROOM_DUST_SIZE_MIN,
        ROOM_DUST_SIZE_MAX
    );

    p->pos[0] = room_dust_fx_rand_range(ROOM_DUST_X_MIN, ROOM_DUST_X_MAX);
    p->pos[1] = room_dust_fx_rand_range(ROOM_DUST_Y_MIN, ROOM_DUST_Y_MAX);
    p->pos[2] = room_dust_fx_rand_range(ROOM_DUST_Z_MIN, ROOM_DUST_Z_MAX);

    p->vel[0] = room_dust_fx_rand_range(-ROOM_DUST_DRIFT_XZ, ROOM_DUST_DRIFT_XZ);
    p->vel[1] = room_dust_fx_rand_range(-ROOM_DUST_DRIFT_Y * 0.25f, ROOM_DUST_DRIFT_Y);
    p->vel[2] = room_dust_fx_rand_range(-ROOM_DUST_DRIFT_XZ, ROOM_DUST_DRIFT_XZ);
}

static bool room_dust_fx_particle_outside_room(const RoomDustParticle *p)
{
    if (!p) return true;

    if (p->pos[0] < ROOM_DUST_X_MIN) return true;
    if (p->pos[0] > ROOM_DUST_X_MAX) return true;

    if (p->pos[1] < ROOM_DUST_Y_MIN) return true;
    if (p->pos[1] > ROOM_DUST_Y_MAX) return true;

    if (p->pos[2] < ROOM_DUST_Z_MIN) return true;
    if (p->pos[2] > ROOM_DUST_Z_MAX) return true;

    return false;
}

static uint32_t room_dust_fx_write_tpx_particle(
    uint32_t particleIdx,
    const RoomDustParticle *p
) {
    if (!p) return particleIdx;

    float t = room_dust_fx_life01(p);

    /*
     * Ambient dust should barely grow. The boss dust grows a lot more.
     */
    float size = p->size * (1.0f + 0.15f * t);

    int16_t *pos = tpx_buffer_s16_get_pos(s_tpxParticles, particleIdx);
    int8_t *particleSize = tpx_buffer_s16_get_size(s_tpxParticles, particleIdx);
    uint8_t *rgba = tpx_buffer_s16_get_rgba(s_tpxParticles, particleIdx);

    pos[0] = room_dust_fx_to_s16(p->pos[0]);
    pos[1] = room_dust_fx_to_s16(p->pos[1]);
    pos[2] = room_dust_fx_to_s16(p->pos[2]);

    *particleSize = room_dust_fx_to_s8_size(size);

    /*
     * Warm neutral dust.
     * Global alpha is handled by ENV alpha per bucket.
     */
    rgba[0] = 220;
    rgba[1] = 214;
    rgba[2] = 195;
    rgba[3] = 0;

    if (particleIdx & 1) {
        s_tpxParticles[particleIdx / 2].texOffsetB = 0;
    } else {
        s_tpxParticles[particleIdx / 2].texOffsetA = 0;
    }

    return particleIdx + 1;
}

static void room_dust_fx_hide_tpx_particle(uint32_t particleIdx)
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

static int room_dust_fx_texture_scale_log(void)
{
    if (!s_dustSprite) return 0;

    int h = s_dustSprite->height;
    if (h <= 8) return 0;

    int sections = h / 8;
    if (sections <= 0) return 0;

    return -__builtin_ctz((unsigned int)sections);
}

static void room_dust_fx_prepare_matrix(void)
{
    s_frameIdx = (s_frameIdx + 1) % ROOM_DUST_FX_FB_COUNT;

    t3d_mat4fp_from_srt_euler(
        &s_particleMatrices[s_frameIdx],
        (float[3]){ 1.0f, 1.0f, 1.0f },
        (float[3]){ 0.0f, 0.0f, 0.0f },
        (float[3]){ 0.0f, 0.0f, 0.0f }
    );
}

static bool room_dust_fx_has_texture(void)
{
    if (ROOM_DUST_FX_FORCE_UNTEXTURED) return false;
    if (!s_dustSprite) return false;
    if (s_dustSurf.width <= 0 || s_dustSurf.height <= 0) return false;
    return true;
}

static void room_dust_fx_upload_texture(void)
{
    rdpq_texparms_t p = {0};

    p.s.repeats = REPEAT_INFINITE;
    p.t.repeats = REPEAT_INFINITE;
    p.s.scale_log = room_dust_fx_texture_scale_log();
    p.t.scale_log = room_dust_fx_texture_scale_log();
    p.s.mirror = false;
    p.t.mirror = false;

    rdpq_sprite_upload(TILE0, s_dustSprite, &p);
}

static void room_dust_fx_draw_tpx_textured(uint32_t drawCount, uint8_t globalAlpha)
{
    rdpq_set_env_color(RGBA32(255, 255, 255, globalAlpha));

    /*
     * Copied from the known-working boss-smash dust path.
     */
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

static void room_dust_fx_draw_tpx_untextured(uint32_t drawCount, uint8_t globalAlpha)
{
    rdpq_set_env_color(RGBA32(255, 255, 255, globalAlpha));

    /*
     * Copied from the known-working boss-smash dust path.
     */
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

void room_dust_fx_init(void)
{
    if (s_initialized) return;

    /*
     * Safe because particle_system_init() is guarded.
     */
    particle_system_init();

    uint32_t pairCount = (ROOM_DUST_FX_MAX + 1) / 2;

    s_tpxParticles = malloc_uncached(sizeof(TPXParticleS16) * pairCount);
    s_particleMatrices = malloc_uncached(sizeof(T3DMat4FP) * ROOM_DUST_FX_FB_COUNT);

    if (!s_tpxParticles || !s_particleMatrices) {
        debugf("room_dust_fx: allocation failed\n");

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

    if (!s_dustSprite) {
        s_dustSprite = sprite_load("rom:/dustParticle.ia8.sprite");

        if (s_dustSprite) {
            s_dustSurf = sprite_get_pixels(s_dustSprite);

            debugf(
                "room_dust_fx: loaded dust sprite %dx%d\n",
                s_dustSprite->width,
                s_dustSprite->height
            );
        } else {
            debugf("room_dust_fx: FAILED to load rom:/dustParticle.ia8.sprite\n");
        }
    }

    memset(s_dust, 0, sizeof(s_dust));
    room_dust_fx_clear_tpx_buffer();

    for (int i = 0; i < ROOM_DUST_FX_FB_COUNT; i++) {
        t3d_mat4fp_from_srt_euler(
            &s_particleMatrices[i],
            (float[3]){ 1.0f, 1.0f, 1.0f },
            (float[3]){ 0.0f, 0.0f, 0.0f },
            (float[3]){ 0.0f, 0.0f, 0.0f }
        );
    }

    for (int i = 0; i < ROOM_DUST_FX_MAX; i++) {
        room_dust_fx_respawn_particle(i, true);
    }

    s_frameIdx = 0;
    s_enabled = true;
    s_initialized = true;

    debugf(
        "room_dust_fx: initialized max=%d textured=%d\n",
        ROOM_DUST_FX_MAX,
        room_dust_fx_has_texture()
    );
}

void room_dust_fx_cleanup(void)
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

    s_frameIdx = 0;
    s_enabled = true;
    s_initialized = false;

    debugf("room_dust_fx: cleanup\n");
}

void room_dust_fx_reset(void)
{
    if (!s_initialized) return;

    memset(s_dust, 0, sizeof(s_dust));
    room_dust_fx_clear_tpx_buffer();

    for (int i = 0; i < ROOM_DUST_FX_MAX; i++) {
        room_dust_fx_respawn_particle(i, true);
    }

    debugf("room_dust_fx: reset\n");
}

void room_dust_fx_set_enabled(bool enabled)
{
    s_enabled = enabled;

    debugf(
        "room_dust_fx: %s\n",
        s_enabled ? "enabled" : "disabled"
    );
}

bool room_dust_fx_is_enabled(void)
{
    return s_enabled;
}

void room_dust_fx_update(float dt)
{
    if (!s_initialized) return;
    if (!s_enabled) return;

    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.25f) dt = 0.25f;

    for (int i = 0; i < ROOM_DUST_FX_MAX; i++) {
        RoomDustParticle *p = &s_dust[i];

        if (!p->active) {
            room_dust_fx_respawn_particle(i, false);
            continue;
        }

        p->age += dt;

        if (p->age >= p->life || room_dust_fx_particle_outside_room(p)) {
            room_dust_fx_respawn_particle(i, false);
            continue;
        }

        p->pos[0] += p->vel[0] * dt;
        p->pos[1] += p->vel[1] * dt;
        p->pos[2] += p->vel[2] * dt;

        /*
         * Light fake turbulence.
         */
        float bendX = room_dust_fx_rand_range(-0.12f, 0.12f);
        float bendZ = room_dust_fx_rand_range(-0.12f, 0.12f);

        p->vel[0] += bendX * dt;
        p->vel[2] += bendZ * dt;

        p->vel[0] = room_dust_fx_clampf(
            p->vel[0],
            -ROOM_DUST_DRIFT_XZ,
            ROOM_DUST_DRIFT_XZ
        );

        p->vel[1] = room_dust_fx_clampf(
            p->vel[1],
            -ROOM_DUST_DRIFT_Y * 0.25f,
            ROOM_DUST_DRIFT_Y
        );

        p->vel[2] = room_dust_fx_clampf(
            p->vel[2],
            -ROOM_DUST_DRIFT_XZ,
            ROOM_DUST_DRIFT_XZ
        );
    }
}

void room_dust_fx_draw(T3DViewport *viewport)
{
    if (!viewport) return;
    if (!s_initialized) return;
    if (!s_enabled) return;
    if (!s_tpxParticles || !s_particleMatrices) return;

    bool useTexture = room_dust_fx_has_texture();

    room_dust_fx_prepare_matrix();

    rdpq_sync_pipe();
    rdpq_set_mode_standard();

    rdpq_mode_antialias(AA_NONE);
    rdpq_mode_dithering(DITHER_NONE_NONE);

    /*
     * Match the known-working boss-smash dust render state.
     *
     * This was the important difference from the failed textured tests.
     */
    rdpq_mode_zbuf(true, false);
    rdpq_mode_zoverride(true, 0, 0);

    rdpq_mode_filter(FILTER_BILINEAR);

    rdpq_mode_alphacompare(0);

    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    if (useTexture) {
        room_dust_fx_upload_texture();
    }

    uint32_t totalDrawn = 0;

    /*
     * Draw from high alpha to low alpha.
     */
    for (int bucket = ROOM_DUST_FX_ALPHA_BUCKETS - 1; bucket >= 0; bucket--) {
        uint32_t drawCount = 0;

        for (int i = 0; i < ROOM_DUST_FX_MAX; i++) {
            const RoomDustParticle *p = &s_dust[i];
            if (!p->active) continue;

            float alpha01 = room_dust_fx_alpha01(p);
            int particleBucket = room_dust_fx_alpha_bucket(alpha01);

            if (particleBucket != bucket) {
                continue;
            }

            drawCount = room_dust_fx_write_tpx_particle(drawCount, p);
        }

        if (drawCount == 0) {
            continue;
        }

        if (drawCount & 1) {
            room_dust_fx_hide_tpx_particle(drawCount);
            drawCount++;
        }

        uint8_t globalAlpha = room_dust_fx_bucket_alpha_u8(bucket);

        if (useTexture) {
            room_dust_fx_draw_tpx_textured(drawCount, globalAlpha);
        } else {
            room_dust_fx_draw_tpx_untextured(drawCount, globalAlpha);
        }

        totalDrawn += drawCount;
    }

#if ROOM_DUST_FX_DEBUG_LOGS
    s_debugDrawFrame++;

    if ((s_debugDrawFrame % 120) == 0) {
        debugf(
            "room_dust_fx: draw frame=%d useTexture=%d totalDrawn=%lu\n",
            s_debugDrawFrame,
            useTexture,
            totalDrawn
        );
    }
#endif

    /*
     * Match the known-working cleanup state.
     */
    rdpq_mode_zoverride(false, 0.0f, 0);
    rdpq_mode_zbuf(false, false);
}