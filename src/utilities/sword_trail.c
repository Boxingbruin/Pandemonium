#include "sword_trail.h"

#include <libdragon.h>
#include <rdpq.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

// Minimal Tiny3D types/prototypes needed for projection.
// Keeps this module lightweight and avoids relying on editor compile flags.
typedef struct { float v[3]; } T3DVec3;
void t3d_viewport_calc_viewspace_pos(void *vp, T3DVec3 *out, const T3DVec3 *pos);

typedef struct {
    float base[3];
    float tip[3];
    float age;
    bool  valid;
} SwordTrailSample;

// Tuning
enum { TRAIL_MAX_SAMPLES = 16 };

struct SwordTrail {
    SwordTrailSample samples[TRAIL_MAX_SAMPLES];
    int count;
    int head;      // newest element index when count>0
    bool inited;
};

static const float TRAIL_LIFETIME_SEC = 0.20f;
static const float TRAIL_MIN_SAMPLE_DIST = 6.0f;  // world units
// Additive blending helps overlapping trails "mix" instead of reading as separate layers.
// NOTE: libdragon documents additive as overflow-prone on real RDP, so we keep alpha modest.
static const uint8_t TRAIL_MAX_ALPHA = 96;
static const uint8_t TRAIL_COLOR_R = 200;
static const uint8_t TRAIL_COLOR_G = 220;
static const uint8_t TRAIL_COLOR_B = 255;

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

static inline float age_to_alpha01(float age) {
    float t = clampf(age / TRAIL_LIFETIME_SEC, 0.0f, 1.0f);
    // nicer falloff than linear
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
SwordTrail* sword_trail_get_boss(void) { return &s_boss; }

void sword_trail_instance_init(SwordTrail *t) {
    if (!t) return;
    sword_trail_instance_reset(t);
    t->inited = true;
}

void sword_trail_instance_reset(SwordTrail *t) {
    if (!t) return;
    memset(t->samples, 0, sizeof(t->samples));
    t->count = 0;
    t->head = 0;
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
            // head stays the same; count shrinks which effectively advances the oldest
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

    // 2D render state for filled triangles.
    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_ADDITIVE);

    // Build and draw as a series of quads between consecutive samples (oldest->newest).
    for (int i = 0; i < t->count - 1; i++) {
        int ia = sample_index_oldest_plus(t, i);
        int ib = sample_index_oldest_plus(t, i + 1);
        const SwordTrailSample *a = &t->samples[ia];
        const SwordTrailSample *b = &t->samples[ib];
        if (!a->valid || !b->valid) continue;

        // Project 4 points to screen space.
        T3DVec3 baseA_scr, tipA_scr, baseB_scr, tipB_scr;
        t3d_viewport_calc_viewspace_pos(viewport, &baseA_scr, (const T3DVec3*)a->base);
        t3d_viewport_calc_viewspace_pos(viewport, &tipA_scr,  (const T3DVec3*)a->tip);
        t3d_viewport_calc_viewspace_pos(viewport, &baseB_scr, (const T3DVec3*)b->base);
        t3d_viewport_calc_viewspace_pos(viewport, &tipB_scr,  (const T3DVec3*)b->tip);

        // Skip segments behind the camera.
        if (baseA_scr.v[2] >= 1.0f || tipA_scr.v[2] >= 1.0f ||
            baseB_scr.v[2] >= 1.0f || tipB_scr.v[2] >= 1.0f) {
            continue;
        }

        float a0 = age_to_alpha01(a->age);
        float a1 = age_to_alpha01(b->age);
        float alpha01 = 0.5f * (a0 + a1);
        uint8_t alpha = (uint8_t)clampf(alpha01 * (float)TRAIL_MAX_ALPHA, 0.0f, 255.0f);
        if (alpha == 0) continue;

        rdpq_set_prim_color(RGBA32(TRAIL_COLOR_R, TRAIL_COLOR_G, TRAIL_COLOR_B, alpha));

        float p0[2] = { baseA_scr.v[0], baseA_scr.v[1] };
        float p1[2] = { tipA_scr.v[0],  tipA_scr.v[1]  };
        float p2[2] = { baseB_scr.v[0], baseB_scr.v[1] };
        float p3[2] = { tipB_scr.v[0],  tipB_scr.v[1]  };

        // Two triangles forming the quad strip segment.
        rdpq_triangle(&TRIFMT_FILL, p0, p1, p2);
        rdpq_triangle(&TRIFMT_FILL, p1, p3, p2);
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

