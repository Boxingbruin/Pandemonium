// src/game/effects/sword_trail.c
#include "sword_trail.h"

#include <t3d/t3d.h>
#include <libdragon.h>
#include <rdpq.h>
#include <rspq.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "game_math.h" // clampf

// ============================================================
// Defaults (copied into per-instance fields at init)
// ============================================================
static const float   TRAIL_DEFAULT_LIFETIME_SEC     = 0.20f;
static const float   TRAIL_DEFAULT_MIN_SAMPLE_DIST  = 2.5f;
static const float   TRAIL_DEFAULT_SUBDIV_DIST      = 4.0f;
static const int     TRAIL_DEFAULT_SUBDIV_MAX       = 4;

static const uint8_t TRAIL_DEFAULT_MAX_ALPHA        = 140;
static const uint8_t TRAIL_DEFAULT_COLOR_R          = 200;
static const uint8_t TRAIL_DEFAULT_COLOR_G          = 220;
static const uint8_t TRAIL_DEFAULT_COLOR_B          = 255;

static SwordTrail s_player;
static SwordTrail s_boss;

// ============================================================
// N64 SAFETY BUDGETS
//   - Cap geometry per-trail so "all swords converge" can't explode.
//   - Subdiv cap strongly affects triangle count.
// ============================================================

// How many "ring samples" we will consider for DRAW (update can keep more).
#ifndef TRAIL_MAX_SAMPLES_DRAW
#define TRAIL_MAX_SAMPLES_DRAW 12
#endif

// Hard cap on ribbon "points" (each point => 2 verts => 2 tris per segment).
#ifndef TRAIL_MAX_POINTS_DRAW
#define TRAIL_MAX_POINTS_DRAW 40
#endif

// Force a low subdiv ceiling for N64 stability (can override per-instance, but we clamp).
#ifndef TRAIL_SUBDIV_MAX_N64
#define TRAIL_SUBDIV_MAX_N64 2
#endif

// ============================================================
// Small math helpers
// ============================================================
static inline float v3_dist(const float a[3], const float b[3]) {
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

static inline void v3_catmull_rom(const float p0[3], const float p1[3], const float p2[3], const float p3[3],
                                  float t, float out[3]) {
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

static inline float age_to_alpha01(const SwordTrail *t, float age) {
    float life = (t && t->lifetime_sec > 0.001f) ? t->lifetime_sec : 0.001f;
    float u = clampf(age / life, 0.0f, 1.0f);
    float a = 1.0f - u;
    return a * a;
}

// ============================================================
// Public per-instance knobs
// ============================================================
void sword_trail_instance_set_lifetime(SwordTrail *t, float seconds) {
    if (!t) return;
    t->lifetime_sec = clampf(seconds, 0.02f, 3.0f);
}

void sword_trail_instance_set_min_sample_dist(SwordTrail *t, float dist) {
    if (!t) return;
    t->min_sample_dist = clampf(dist, 0.5f, 200.0f);
}

void sword_trail_instance_set_subdiv(SwordTrail *t, float subdiv_dist, int subdiv_max) {
    if (!t) return;
    t->subdiv_dist = clampf(subdiv_dist, 1.0f, 200.0f);
    if (subdiv_max < 1) subdiv_max = 1;
    if (subdiv_max > 8) subdiv_max = 8;
    t->subdiv_max = subdiv_max;
}

void sword_trail_instance_set_color(SwordTrail *t, uint8_t r, uint8_t g, uint8_t b, uint8_t max_alpha) {
    if (!t) return;
    t->color_r = r;
    t->color_g = g;
    t->color_b = b;
    t->max_alpha = max_alpha;
}

// ============================================================
// Ring indexing helpers
// ============================================================
static inline int sample_index_newest_minus(const SwordTrail *t, int i) {
    int idx = t->head - i;
    while (idx < 0) idx += TRAIL_MAX_SAMPLES;
    return idx;
}

static inline int sample_index_oldest_plus(const SwordTrail *t, int i) {
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
        t->head = (t->head + 1) % TRAIL_MAX_SAMPLES;
    }

    SwordTrailSample *s = &t->samples[t->head];
    memcpy(s->base, base_world, sizeof(float) * 3);
    memcpy(s->tip,  tip_world,  sizeof(float) * 3);
    s->age = 0.0f;
    s->valid = true;
}

// ============================================================
// Accessors
// ============================================================
SwordTrail* sword_trail_get_player(void) { return &s_player; }
SwordTrail* sword_trail_get_boss(void)   { return &s_boss;   }

// ============================================================
// Lifecycle
// ============================================================
void sword_trail_instance_init(SwordTrail *t) {
    if (!t) return;
    sword_trail_instance_reset(t);
    t->inited = true;

    t->lifetime_sec    = TRAIL_DEFAULT_LIFETIME_SEC;
    t->min_sample_dist = TRAIL_DEFAULT_MIN_SAMPLE_DIST;
    t->subdiv_dist     = TRAIL_DEFAULT_SUBDIV_DIST;
    t->subdiv_max      = TRAIL_DEFAULT_SUBDIV_MAX;
    t->max_alpha       = TRAIL_DEFAULT_MAX_ALPHA;
    t->color_r         = TRAIL_DEFAULT_COLOR_R;
    t->color_g         = TRAIL_DEFAULT_COLOR_G;
    t->color_b         = TRAIL_DEFAULT_COLOR_B;
}

void sword_trail_instance_reset(SwordTrail *t) {
    if (!t) return;
    memset(t->samples, 0, sizeof(t->samples));
    t->count = 0;
    t->head  = 0;
}

// ============================================================
// Update
// ============================================================
void sword_trail_instance_update(SwordTrail *t, float dt, bool emitting,
                                const float base_world[3], const float tip_world[3]) {
    if (!t) return;
    if (!t->inited) sword_trail_instance_init(t);

    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.25f) dt = 0.25f;

    for (int i = 0; i < t->count; i++) {
        int idx = sample_index_newest_minus(t, i);
        if (t->samples[idx].valid) t->samples[idx].age += dt;
    }

    while (t->count > 0) {
        int oldest = sample_index_oldest_plus(t, 0);
        if (!t->samples[oldest].valid || t->samples[oldest].age > t->lifetime_sec) {
            t->samples[oldest].valid = false;
            t->count--;
        } else break;
    }

    if (!emitting || !base_world || !tip_world) return;

    if (t->count > 0) {
        const SwordTrailSample *newest = &t->samples[t->head];
        float d0 = v3_dist(base_world, newest->base);
        float d1 = v3_dist(tip_world,  newest->tip);
        if (fmaxf(d0, d1) < t->min_sample_dist) return;
    }

    push_sample(t, base_world, tip_world);
}

// ============================================================
// Uncached aligned alloc (16B) for RSP safety
// ============================================================
static void* alloc_uncached_aligned16(size_t bytes, void** out_base) {
    void* base = malloc_uncached(bytes + 15);
    if (!base) { *out_base = NULL; return NULL; }
    uintptr_t p = (uintptr_t)base;
    uintptr_t aligned = (p + 15u) & ~(uintptr_t)15u;
    *out_base = base;
    return (void*)aligned;
}

// ============================================================
// 3D draw buffers (ring) - packed verts
// ============================================================

// identity matrix (uncached, aligned)
static T3DMat4FP* s_trail_id_mat_fp = NULL;
static void*      s_trail_id_mat_base = NULL;

// We cap draw points hard. PACKED_MAX == points.
#define POINTS_MAX_DRAW (TRAIL_MAX_POINTS_DRAW)
#define VERTS_MAX_DRAW  (POINTS_MAX_DRAW * 2)
#define PACKED_MAX_DRAW (POINTS_MAX_DRAW)

// Make this comfortably larger than max trails you can draw in one frame.
#ifndef TRAIL_DRAWBUF_RING
#define TRAIL_DRAWBUF_RING 32
#endif

static T3DVertPacked* s_trail_buf_ring[TRAIL_DRAWBUF_RING] = {0};
static void*          s_trail_buf_base[TRAIL_DRAWBUF_RING] = {0};
static int            s_trail_buf_ring_idx = 0;

static inline int clamp_i(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi) ? hi : v; }

static void ensure_trail_draw_buffers(void) {
    if (!s_trail_id_mat_fp) {
        T3DMat4 id;
        t3d_mat4_identity(&id);

        void* aligned = alloc_uncached_aligned16(sizeof(T3DMat4FP), &s_trail_id_mat_base);
        s_trail_id_mat_fp = (T3DMat4FP*)aligned;
        assert(s_trail_id_mat_fp && (((uintptr_t)s_trail_id_mat_fp & 0xF) == 0));

        t3d_mat4_to_fixed(s_trail_id_mat_fp, &id);
    }

    for (int i = 0; i < TRAIL_DRAWBUF_RING; i++) {
        if (!s_trail_buf_ring[i]) {
            void* aligned = alloc_uncached_aligned16(sizeof(T3DVertPacked) * PACKED_MAX_DRAW, &s_trail_buf_base[i]);
            s_trail_buf_ring[i] = (T3DVertPacked*)aligned;
            assert(s_trail_buf_ring[i] && (((uintptr_t)s_trail_buf_ring[i] & 0xF) == 0));

            memset(s_trail_buf_ring[i], 0, sizeof(T3DVertPacked) * PACKED_MAX_DRAW);
        }
    }
}

static inline uint32_t pack_rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return ((uint32_t)r << 24) | ((uint32_t)g << 16) | ((uint32_t)b << 8) | (uint32_t)a;
}

static inline void write_vert(T3DVertPacked *dst, int vi, int16_t x, int16_t y, int16_t z,
                              uint32_t rgba, uint16_t norm) {
    int pi  = vi >> 1;
    int sel = vi & 1;
    if (sel == 0) {
        dst[pi].posA[0] = x; dst[pi].posA[1] = y; dst[pi].posA[2] = z;
        dst[pi].normA   = norm;
        dst[pi].rgbaA   = rgba;
    } else {
        dst[pi].posB[0] = x; dst[pi].posB[1] = y; dst[pi].posB[2] = z;
        dst[pi].normB   = norm;
        dst[pi].rgbaB   = rgba;
    }
}

// ============================================================
// Draw (TRUE 3D ribbon; N64-safe budgets + sync)
// ============================================================
void sword_trail_instance_draw(SwordTrail *t, void *viewport) {
    (void)viewport;
    if (!t) return;
    if (t->count < 2) return;

    ensure_trail_draw_buffers();

    // Grab a unique big buffer for THIS draw-call
    T3DVertPacked *vb = s_trail_buf_ring[s_trail_buf_ring_idx];
    s_trail_buf_ring_idx = (s_trail_buf_ring_idx + 1) % TRAIL_DRAWBUF_RING;

    assert(vb && (((uintptr_t)vb & 0xF) == 0));
    assert(s_trail_id_mat_fp && (((uintptr_t)s_trail_id_mat_fp & 0xF) == 0));

    // Keep fog OFF for trails (tiny3d fog can stomp alpha)
    t3d_fog_set_enabled(false);

    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_zbuf(true, false);           // depth test on, no depth write
    rdpq_mode_alphacompare(0);
    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_dithering(DITHER_NONE_BAYER);

    // Vertex RGBA, no lighting touch
    t3d_state_set_drawflags(T3D_FLAG_SHADED | T3D_FLAG_DEPTH | T3D_FLAG_NO_LIGHT);

    // Optional pull-forward to reduce z-fight flicker
    t3d_state_set_depth_offset(-0x20);

    t3d_matrix_push(s_trail_id_mat_fp);

    uint16_t norm = t3d_vert_pack_normal(&(T3DVec3){{ 0, 0, 1 }});

    const float a_scale = (float)t->max_alpha / 255.0f;
    const uint8_t r8 = t->color_r, g8 = t->color_g, b8 = t->color_b;

    // We draw only the most recent N samples to keep geometry stable.
    int draw_count = t->count;
    if (draw_count > TRAIL_MAX_SAMPLES_DRAW) draw_count = TRAIL_MAX_SAMPLES_DRAW;

    // Build points (each point => 2 verts) into vb, hard-capped.
    int vcount = 0;

    // Compute index window [start..end] over the ring in "oldest-plus" space.
    // oldest_plus(0) is oldest in entire buffer; we want the newest draw_count samples,
    // i.e. oldest_plus(t->count - draw_count) .. oldest_plus(t->count - 1)
    int start_i = t->count - draw_count;
    int end_i   = t->count - 1;

    for (int i = start_i; i < end_i; i++) {
        // Build Catmull over local window, clamped to [start_i..end_i]
        int im1 = (i - 1 < start_i) ? start_i : (i - 1);
        int i0  = i;
        int i1  = i + 1;
        int i2  = (i + 2 > end_i) ? end_i : (i + 2);

        int idx0 = sample_index_oldest_plus(t, im1);
        int idx1 = sample_index_oldest_plus(t, i0);
        int idx2 = sample_index_oldest_plus(t, i1);
        int idx3 = sample_index_oldest_plus(t, i2);

        const SwordTrailSample *s0 = &t->samples[idx0];
        const SwordTrailSample *s1 = &t->samples[idx1];
        const SwordTrailSample *s2 = &t->samples[idx2];
        const SwordTrailSample *s3 = &t->samples[idx3];
        if (!s1->valid || !s2->valid) continue;

        float d0 = v3_dist(s1->base, s2->base);
        float d1 = v3_dist(s1->tip,  s2->tip);
        float d  = fmaxf(d0, d1);

        int subdiv = (int)ceilf(d / t->subdiv_dist);
        if (subdiv < 1) subdiv = 1;

        // Clamp subdiv hard for N64 stability.
        int max_sub = t->subdiv_max;
        if (max_sub > TRAIL_SUBDIV_MAX_N64) max_sub = TRAIL_SUBDIV_MAX_N64;
        if (subdiv > max_sub) subdiv = max_sub;

        int ss_start = (vcount == 0) ? 0 : 1;

        for (int ss = ss_start; ss <= subdiv; ss++) {
            if ((vcount / 2) >= POINTS_MAX_DRAW) break; // points cap
            float tt = (float)ss / (float)subdiv;

            float base_w[3], tip_w[3];
            v3_catmull_rom(s0->base, s1->base, s2->base, s3->base, tt, base_w);
            v3_catmull_rom(s0->tip,  s1->tip,  s2->tip,  s3->tip,  tt, tip_w);

            float age = lerpf(s1->age, s2->age, tt);
            float a01 = clampf(age_to_alpha01(t, age) * a_scale, 0.0f, 1.0f);
            uint8_t a8 = (uint8_t)clamp_i((int)lrintf(a01 * 255.0f), 0, 255);

            uint32_t rgba = pack_rgba8(r8, g8, b8, a8);

            int16_t bx = (int16_t)clamp_i((int)lrintf(base_w[0]), -32760, 32760);
            int16_t by = (int16_t)clamp_i((int)lrintf(base_w[1]), -32760, 32760);
            int16_t bz = (int16_t)clamp_i((int)lrintf(base_w[2]), -32760, 32760);

            int16_t tx = (int16_t)clamp_i((int)lrintf(tip_w[0]),  -32760, 32760);
            int16_t ty = (int16_t)clamp_i((int)lrintf(tip_w[1]),  -32760, 32760);
            int16_t tz = (int16_t)clamp_i((int)lrintf(tip_w[2]),  -32760, 32760);

            if (vcount + 2 > VERTS_MAX_DRAW) break;

            write_vert(vb, vcount + 0, bx, by, bz, rgba, norm);
            write_vert(vb, vcount + 1, tx, ty, tz, rgba, norm);
            vcount += 2;
        }

        if ((vcount / 2) >= POINTS_MAX_DRAW) break;
    }

    if (vcount >= 4) {
        // Submit in chunks of <=70 verts (==35 points). Overlap by 1 point.
        const int POINTS_PER_CHUNK = 35; // 35*2=70 verts
        int points_total = vcount / 2;

        int point_start = 0;
        while (point_start < points_total - 1) {
            int chunk_points = points_total - point_start;
            if (chunk_points > POINTS_PER_CHUNK) chunk_points = POINTS_PER_CHUNK;

            int chunk_verts = chunk_points * 2;
            int start_vert  = point_start * 2;

            // start_vert is even => packed pointer aligned
            const T3DVertPacked *src = vb + (start_vert / 2);
            t3d_vert_load(src, 0, (uint32_t)chunk_verts);

            int steps = chunk_points - 1;
            for (int j = 0; j < steps; j++) {
                int base0 = (j * 2) + 0;
                int tip0  = (j * 2) + 1;
                int base1 = (j * 2) + 2;
                int tip1  = (j * 2) + 3;

                t3d_tri_draw((uint32_t)base0, (uint32_t)tip0, (uint32_t)base1);
                t3d_tri_draw((uint32_t)tip0,  (uint32_t)tip1, (uint32_t)base1);
            }

            point_start += (chunk_points - 1); // 1-point overlap
        }
    }

    // Flush trail geometry so we don't build up an enormous tri queue across many trails.
    t3d_tri_sync();
    rdpq_sync_pipe();

    t3d_matrix_pop(1);

    t3d_state_set_depth_offset(0);
    t3d_fog_set_enabled(true);
}

// ============================================================
// Back-compat wrappers (player trail)
// ============================================================
void sword_trail_init(void) { sword_trail_instance_init(&s_player); }
void sword_trail_reset(void) { sword_trail_instance_reset(&s_player); }

void sword_trail_update(float dt, bool emitting, const float base_world[3], const float tip_world[3]) {
    sword_trail_instance_update(&s_player, dt, emitting, base_world, tip_world);
}

void sword_trail_draw(void *viewport) { sword_trail_instance_draw(&s_player, viewport); }

void sword_trail_draw_all(void *viewport) {
    sword_trail_instance_draw(&s_player, viewport);
    sword_trail_instance_draw(&s_boss, viewport);
}
