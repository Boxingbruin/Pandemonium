#include "sword_trail.h"

#include <t3d/t3d.h>
#include <libdragon.h>
#include <rdpq.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

// Tuning
static const float TRAIL_LIFETIME_SEC   = 0.20f;
static const float TRAIL_MIN_SAMPLE_DIST = 2.5f;  // world units
static const float TRAIL_SUBDIV_DIST     = 4.0f;  // world units (approx); smaller => smoother
static const int   TRAIL_SUBDIV_MAX      = 4;

// Additive blending helps overlapping trails "mix" instead of reading as separate layers.
// NOTE: libdragon documents additive as overflow-prone on real RDP, so we keep alpha modest.
static const uint8_t TRAIL_MAX_ALPHA = 96;
static const uint8_t TRAIL_COLOR_R   = 200;
static const uint8_t TRAIL_COLOR_G   = 220;
static const uint8_t TRAIL_COLOR_B   = 255;

static SwordTrail s_player;
static SwordTrail s_boss;

static inline float v3_dist(const float a[3], const float b[3]) {
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

static inline float clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

static inline void v3_catmull_rom(const float p0[3], const float p1[3], const float p2[3], const float p3[3], float t, float out[3]) {
    // Standard Catmull-Rom spline (uniform), produces smooth curve through p1->p2.
    float t2 = t * t;
    float t3 = t2 * t;
    for (int k = 0; k < 3; k++) {
        float a0 = -0.5f*p0[k] + 1.5f*p1[k] - 1.5f*p2[k] + 0.5f*p3[k];
        float a1 =  1.0f*p0[k] - 2.5f*p1[k] + 2.0f*p2[k] - 0.5f*p3[k];
        float a2 = -0.5f*p0[k] + 0.5f*p2[k];
        float a3 =  1.0f*p1[k];
        out[k] = ((a0 * t3) + (a1 * t2) + (a2 * t) + a3);
    }
}

static inline float age_to_alpha01(float age) {
    float t = clampf(age / TRAIL_LIFETIME_SEC, 0.0f, 1.0f);
    float a = 1.0f - t;
    return a * a;
}

static inline int sample_index_newest_minus(const SwordTrail *t, int i) {
    // i=0 => newest, i=count-1 => oldest
    int idx = t->head - i;
    while (idx < 0) idx += TRAIL_MAX_SAMPLES;
    return idx;
}

static inline int sample_index_oldest_plus(const SwordTrail *t, int i) {
    // i=0 => oldest, i=count-1 => newest
    return sample_index_newest_minus(t, (t->count - 1) - i);
}

static void push_sample(SwordTrail *t, const float base_world[3], const float tip_world[3]) {
    if (t->count <= 0) {
        t->head = 0;
        t->count = 1;
    } else if (t->count < TRAIL_MAX_SAMPLES) {
        t->head = (t->head + 1) % TRAIL_MAX_SAMPLES;
        t->count++;
    } else {
        // full: overwrite the oldest by moving head forward
        t->head = (t->head + 1) % TRAIL_MAX_SAMPLES;
    }

    SwordTrailSample *s = &t->samples[t->head];
    memcpy(s->base, base_world, sizeof(float) * 3);
    memcpy(s->tip,  tip_world,  sizeof(float) * 3);
    s->age = 0.0f;
    s->valid = true;
}

SwordTrail* sword_trail_get_player(void) { return &s_player; }
SwordTrail* sword_trail_get_boss(void)   { return &s_boss; }

void sword_trail_instance_init(SwordTrail *t) {
    if (!t) return;
    sword_trail_instance_reset(t);
    t->inited = true;
}

void sword_trail_instance_reset(SwordTrail *t) {
    if (!t) return;
    memset(t->samples, 0, sizeof(t->samples));
    t->count = 0;
    t->head  = 0;
}

void sword_trail_instance_update(SwordTrail *t, float dt, bool emitting, const float base_world[3], const float tip_world[3]) {
    if (!t) return;
    if (!t->inited) sword_trail_instance_init(t);

    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.25f) dt = 0.25f;

    // Age existing
    for (int i = 0; i < t->count; i++) {
        int idx = sample_index_newest_minus(t, i);
        if (t->samples[idx].valid) t->samples[idx].age += dt;
    }

    // Drop old samples from the oldest end.
    while (t->count > 0) {
        int oldest = sample_index_oldest_plus(t, 0);
        if (!t->samples[oldest].valid || t->samples[oldest].age > TRAIL_LIFETIME_SEC) {
            t->samples[oldest].valid = false;
            t->count--;
        } else {
            break;
        }
    }

    if (!emitting || !base_world || !tip_world) return;

    // Distance-gated sampling to avoid over-densifying on slow motion.
    if (t->count > 0) {
        const SwordTrailSample *newest = &t->samples[t->head];
        float d0 = v3_dist(base_world, newest->base);
        float d1 = v3_dist(tip_world,  newest->tip);
        if (fmaxf(d0, d1) < TRAIL_MIN_SAMPLE_DIST) return;
    }

    push_sample(t, base_world, tip_world);
}

void sword_trail_instance_draw(SwordTrail *t, void *viewport) {
    if (!t) return;
    if (!viewport) return;
    if (t->count < 2) return;

    // 2D render state for shaded triangles (per-vertex alpha for smooth fade between samples).
    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    rdpq_mode_blender(RDPQ_BLENDER_ADDITIVE);
    rdpq_mode_dithering(DITHER_NOISE_NOISE);

    const float cr = (float)TRAIL_COLOR_R / 255.0f;
    const float cg = (float)TRAIL_COLOR_G / 255.0f;
    const float cb = (float)TRAIL_COLOR_B / 255.0f;
    const float a_scale = (float)TRAIL_MAX_ALPHA / 255.0f;

    T3DViewport *vp = (T3DViewport*)viewport;

    // Build and draw as a series of quads between consecutive samples (oldest->newest),
    // with optional subdivision + spline interpolation to reduce visible "chunking".
    for (int i = 0; i < t->count - 1; i++) {
        int i0 = sample_index_oldest_plus(t, (i - 1) < 0 ? 0 : (i - 1));
        int i1 = sample_index_oldest_plus(t, i);
        int i2 = sample_index_oldest_plus(t, i + 1);
        int i3 = sample_index_oldest_plus(t, (i + 2) >= t->count ? (t->count - 1) : (i + 2));

        const SwordTrailSample *s0 = &t->samples[i0];
        const SwordTrailSample *s1 = &t->samples[i1];
        const SwordTrailSample *s2 = &t->samples[i2];
        const SwordTrailSample *s3 = &t->samples[i3];
        if (!s1->valid || !s2->valid) continue;

        float d0 = v3_dist(s1->base, s2->base);
        float d1 = v3_dist(s1->tip,  s2->tip);
        float d  = fmaxf(d0, d1);

        int subdiv = (int)ceilf(d / TRAIL_SUBDIV_DIST);
        if (subdiv < 1) subdiv = 1;
        if (subdiv > TRAIL_SUBDIV_MAX) subdiv = TRAIL_SUBDIV_MAX;

        // Previous interpolated point at t=0 (== s1).
        float prev_base_w[3], prev_tip_w[3];
        v3_catmull_rom(s0->base, s1->base, s2->base, s3->base, 0.0f, prev_base_w);
        v3_catmull_rom(s0->tip,  s1->tip,  s2->tip,  s3->tip,  0.0f, prev_tip_w);
        float prev_age = s1->age;

        T3DVec3 prev_base_scr, prev_tip_scr;
        t3d_viewport_calc_viewspace_pos(vp, &prev_base_scr, (const T3DVec3*)prev_base_w);
        t3d_viewport_calc_viewspace_pos(vp, &prev_tip_scr,  (const T3DVec3*)prev_tip_w);
        if (prev_base_scr.v[2] >= 1.0f || prev_tip_scr.v[2] >= 1.0f) continue;

        float prev_alpha = clampf(age_to_alpha01(prev_age) * a_scale, 0.0f, 1.0f);

        for (int s = 1; s <= subdiv; s++) {
            float tt = (float)s / (float)subdiv;

            float base_w[3], tip_w[3];
            v3_catmull_rom(s0->base, s1->base, s2->base, s3->base, tt, base_w);
            v3_catmull_rom(s0->tip,  s1->tip,  s2->tip,  s3->tip,  tt, tip_w);
            float age = lerpf(s1->age, s2->age, tt);

            T3DVec3 base_scr, tip_scr;
            t3d_viewport_calc_viewspace_pos(vp, &base_scr, (const T3DVec3*)base_w);
            t3d_viewport_calc_viewspace_pos(vp, &tip_scr,  (const T3DVec3*)tip_w);
            if (base_scr.v[2] >= 1.0f || tip_scr.v[2] >= 1.0f) break;

            float alpha = clampf(age_to_alpha01(age) * a_scale, 0.0f, 1.0f);

            if (prev_alpha <= 0.0f && alpha <= 0.0f) {
                prev_base_scr = base_scr;
                prev_tip_scr  = tip_scr;
                prev_alpha    = alpha;
                continue;
            }

            float v0[6] = { prev_base_scr.v[0], prev_base_scr.v[1], cr, cg, cb, prev_alpha };
            float v1[6] = { prev_tip_scr.v[0],  prev_tip_scr.v[1],  cr, cg, cb, prev_alpha };
            float v2[6] = { base_scr.v[0],      base_scr.v[1],      cr, cg, cb, alpha };
            float v3[6] = { tip_scr.v[0],       tip_scr.v[1],       cr, cg, cb, alpha };

            rdpq_triangle(&TRIFMT_SHADE, v0, v1, v2);
            rdpq_triangle(&TRIFMT_SHADE, v1, v3, v2);

            prev_base_scr = base_scr;
            prev_tip_scr  = tip_scr;
            prev_alpha    = alpha;
        }
    }
}

// Back-compat wrappers (player trail)
void sword_trail_init(void) { sword_trail_instance_init(&s_player); }
void sword_trail_reset(void) { sword_trail_instance_reset(&s_player); }
void sword_trail_update(float dt, bool emitting, const float base_world[3], const float tip_world[3]) {
    sword_trail_instance_update(&s_player, dt, emitting, base_world, tip_world);
}
void sword_trail_draw(void *viewport) { sword_trail_instance_draw(&s_player, viewport); }

void sword_trail_draw_all(void *viewport) {
    // Draw player first, then boss so boss trail "wins" in overlaps (arbitrary but consistent).
    sword_trail_instance_draw(&s_player, viewport);
    sword_trail_instance_draw(&s_boss, viewport);
}