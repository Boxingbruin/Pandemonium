
#include "path_ribbon.h"

#include <t3d/t3d.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include <libdragon.h>
#include <rdpq.h>
#include <rdpq_sprite.h>
#include <sprite.h>
#include <rspq.h>

// ============================================================
// CONFIG
// ============================================================
#define PR_MAX_POINTS_DRAW 64

#define PR_DRAWBUF_RING 64

#define PR_WALL_U_SCALE 1.0f

#define PR_WALL_V_SCALE 1.0f

// ============================================================
// Texture (set externally)
// ============================================================
static sprite_t *s_pr_wall_tex = NULL;

void path_ribbon_set_wall_texture(sprite_t *spr) {
    s_pr_wall_tex = spr;
}

// ============================================================
// Global scroll state (shared across all ribbons)
// ============================================================
static float s_pr_wall_scroll_u = 0.0f; // pixels
static float s_pr_wall_scroll_v = 0.0f;

// Default speeds (pixels/sec).
static float s_pr_wall_scroll_u_speed = 0.0f;
static float s_pr_wall_scroll_v_speed = 5.0f;

void path_ribbon_set_wall_scroll_speed(float u_px_per_sec, float v_px_per_sec) {
    s_pr_wall_scroll_u_speed = u_px_per_sec;
    s_pr_wall_scroll_v_speed = v_px_per_sec;
}

void path_ribbon_reset_wall_scroll(void) {
    s_pr_wall_scroll_u = 0.0f;
    s_pr_wall_scroll_v = 0.0f;
}

// Wrap helper (keep values bounded to avoid float blowup)
static inline float pr_wrap_scroll(float x, float period) {
    if (period <= 0.0f) return x;
    x = fmodf(x, period);
    if (x < 0.0f) x += period;
    return x;
}

// ============================================================
// Uncached aligned alloc (16B)
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
// Helpers
// ============================================================
static inline float pr_clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline float pr_dist2_xz(float ax, float az, float bx, float bz) {
    float dx = ax - bx;
    float dz = az - bz;
    return dx*dx + dz*dz;
}

static inline int pr_clampi(int v, int lo, int hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static inline int16_t pr_f2s16(float f) {
    int v = (int)lrintf(f);
    v = pr_clampi(v, -32760, 32760);
    return (int16_t)v;
}

static inline uint8_t pr_u8(float x) {
    int v = (int)lrintf(x);
    v = pr_clampi(v, 0, 255);
    return (uint8_t)v;
}

static inline PRColor pr_color_mul_alpha(PRColor c, float a_mul) {
    a_mul = pr_clampf(a_mul, 0.0f, 1.0f);
    c.a = pr_u8((float)c.a * a_mul);
    return c;
}

static inline float pr_smoothstep(float t) {
    t = pr_clampf(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// cheap deterministic hash -> [0,1)
static inline uint32_t pr_hash_u32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
static inline float pr_hash01(uint32_t x) {
    return (float)(pr_hash_u32(x) & 0x00FFFFFFu) / (float)0x01000000u;
}
static inline float pr_hash_m11(uint32_t x) {
    return pr_hash01(x) * 2.0f - 1.0f;
}

// ============================================================
// Identity matrix for Tiny3D
// ============================================================
static T3DMat4FP* s_pr_id_mat_fp = NULL;
static void*      s_pr_id_mat_base = NULL;

static void path_ribbon_ensure_id_mat(void) {
    if (s_pr_id_mat_fp) return;
    T3DMat4 id;
    t3d_mat4_identity(&id);
    void* aligned = alloc_uncached_aligned16(sizeof(T3DMat4FP), &s_pr_id_mat_base);
    s_pr_id_mat_fp = (T3DMat4FP*)aligned;
    if (s_pr_id_mat_fp) {
        t3d_mat4_to_fixed(s_pr_id_mat_fp, &id);
    }
}

// ============================================================
// Draw buffers (ring)
// ============================================================
static void*          s_pr_v_base[PR_DRAWBUF_RING] = {0};
static T3DVertPacked* s_pr_v_ring[PR_DRAWBUF_RING] = {0};

static rspq_syncpoint_t s_pr_sp[PR_DRAWBUF_RING];
static uint8_t          s_pr_sp_valid[PR_DRAWBUF_RING];

static int     s_pr_ring_idx = 0;
static uint8_t s_pr_drawbuf_inited = 0;

static void path_ribbon_ensure_draw_buffers(void) {
    if (s_pr_drawbuf_inited) return;
    s_pr_drawbuf_inited = 1;

    const size_t bytes = sizeof(T3DVertPacked) * PR_MAX_POINTS_DRAW;

    for (int i = 0; i < PR_DRAWBUF_RING; i++) {
        if (!s_pr_v_ring[i]) {
            void* aligned = alloc_uncached_aligned16(bytes, &s_pr_v_base[i]);
            s_pr_v_ring[i] = (T3DVertPacked*)aligned;
            if (s_pr_v_ring[i]) memset(s_pr_v_ring[i], 0, bytes);
        }
        s_pr_sp_valid[i] = 0;
    }
}

static inline void pr_slot_wait(int idx) {
    if (s_pr_sp_valid[idx]) {
        rspq_syncpoint_wait(s_pr_sp[idx]);
        s_pr_sp_valid[idx] = 0;
    }
}

static inline void pr_slot_fence(int idx) {
    rspq_syncpoint_t sp = rspq_syncpoint_new();
    rspq_flush();
    s_pr_sp[idx] = sp;
    s_pr_sp_valid[idx] = 1;
}

// ============================================================
// Tiny3D packed write using vertex index (v = 0..)
// ============================================================
static inline void pr_write_vert(T3DVertPacked *dst, int vtx,
                                 int16_t x, int16_t y, int16_t z,
                                 int16_t u_10_5, int16_t v_10_5,
                                 PRColor c, uint16_t norm)
{
    int pi  = vtx >> 1;
    int sel = vtx & 1;

    if (sel == 0) {
        dst[pi].posA[0] = x; dst[pi].posA[1] = y; dst[pi].posA[2] = z;
        dst[pi].normA   = norm;
        dst[pi].rgbaA   = ((uint32_t)c.r << 24) | ((uint32_t)c.g << 16) | ((uint32_t)c.b << 8) | (uint32_t)c.a;
    } else {
        dst[pi].posB[0] = x; dst[pi].posB[1] = y; dst[pi].posB[2] = z;
        dst[pi].normB   = norm;
        dst[pi].rgbaB   = ((uint32_t)c.r << 24) | ((uint32_t)c.g << 16) | ((uint32_t)c.b << 8) | (uint32_t)c.a;
    }

    {
        int16_t *uv = t3d_vertbuffer_get_uv(dst, vtx);
        uv[0] = u_10_5;
        uv[1] = v_10_5;
    }
}

// ============================================================
// Lifecycle
// ============================================================
void path_ribbon_init(PathRibbon* pr, uint8_t max_points, float min_step) {
    memset(pr, 0, sizeof(*pr));
    pr->max_points = max_points;
    if (pr->max_points < 2) pr->max_points = 2;
    if (pr->max_points > PR_MAX_POINTS_DRAW) pr->max_points = PR_MAX_POINTS_DRAW;

    pr->min_step = min_step;
    pr->floor_y = 0.0f;
    pr->floor_eps = 0.25f;

    pr->wall_height = 30.0f;
    pr->wall_w_mult = 1.0f;

    pr->crack_w_start   = 2.0f;
    pr->crack_w_end     = 4.0f;
    pr->crack_w_noise   = 0.0f;
    pr->crack_tip_taper = 0.20f;

    pr->crack_color = (PRColor){ 60, 60, 60, 255 };
    pr->wall_color_bot = (PRColor){ 255, 255, 255, 255 };
    pr->wall_color_top = (PRColor){ 255, 255, 255, 0 };

    pr->seed = 0;

    pr->alpha_mul = 1.0f;
    pr->fade_t = 0.0f;
    pr->fade_dur = 0.0f;
    pr->fading = 0;
    pr->dead = 0;
    pr->sealed = 0;
    pr->count = 0;
}

void path_ribbon_clear(PathRibbon* pr) {
    pr->count = 0;
    pr->sealed = 0;

    pr->alpha_mul = 1.0f;
    pr->fade_t = 0.0f;
    pr->fade_dur = 0.0f;
    pr->fading = 0;
    pr->dead = 0;
}

void path_ribbon_set_floor(PathRibbon* pr, float floor_y) { pr->floor_y = floor_y; }
void path_ribbon_set_seed(PathRibbon* pr, uint32_t seed) { pr->seed = seed; }

void path_ribbon_start_fade(PathRibbon* pr, float seconds) {
    if (seconds <= 0.0f) seconds = 0.001f;
    pr->fading = 1;
    pr->dead = 0;
    pr->fade_t = 0.0f;
    pr->fade_dur = seconds;
    pr->alpha_mul = 1.0f;
}

void path_ribbon_update(PathRibbon* pr, float dt) {
    if (!pr) return;

    // Update global scroll (shared across all ribbons).
    if (dt > 0.0f) {
        float texW = 64.0f, texH = 64.0f;
        if (s_pr_wall_tex) {
            texW = (float)s_pr_wall_tex->width;  if (texW < 1.0f) texW = 64.0f;
            texH = (float)s_pr_wall_tex->height; if (texH < 1.0f) texH = 64.0f;
        }

        s_pr_wall_scroll_u += s_pr_wall_scroll_u_speed * dt;
        s_pr_wall_scroll_v += s_pr_wall_scroll_v_speed * dt;

        s_pr_wall_scroll_u = pr_wrap_scroll(s_pr_wall_scroll_u, texW);
        s_pr_wall_scroll_v = pr_wrap_scroll(s_pr_wall_scroll_v, texH);
    }

    // Fade logic
    if (pr->dead) return;
    if (!pr->fading) return;

    pr->fade_t += dt;
    float t = (pr->fade_dur > 0.0f) ? (pr->fade_t / pr->fade_dur) : 1.0f;
    t = pr_clampf(t, 0.0f, 1.0f);

    pr->alpha_mul = 1.0f - t;

    if (t >= 1.0f || pr->alpha_mul <= 0.0f) {
        pr->alpha_mul = 0.0f;
        pr->fading = 0;
        pr->dead = 1;
    }
}

// ============================================================
// Tail-follow point logic
// ============================================================
bool path_ribbon_try_add(PathRibbon* pr, float x, float z) {
    if (!pr) return false;
    if (pr->dead) return false;
    if (pr->max_points < 2) { pr->sealed = 1; return false; }

    const float y = pr->floor_y;

    if (pr->count == 0) {
        pr->pts[0][0] = x; pr->pts[0][1] = y; pr->pts[0][2] = z;
        pr->count = 1;
        return true;
    }

    if (pr->count == 1) {
        pr->pts[1][0] = x; pr->pts[1][1] = y; pr->pts[1][2] = z;
        pr->count = 2;
        return true;
    }

    int tail = (int)pr->count - 1;
    pr->pts[tail][0] = x;
    pr->pts[tail][1] = y;
    pr->pts[tail][2] = z;

    if (pr->sealed) return false;

    int prev = tail - 1;
    float min2 = pr->min_step * pr->min_step;
    if (pr_dist2_xz(pr->pts[prev][0], pr->pts[prev][2], x, z) < min2) {
        return false;
    }

    if (pr->count >= pr->max_points) {
        pr->sealed = 1;
        return false;
    }

    pr->pts[pr->count][0] = x;
    pr->pts[pr->count][1] = y;
    pr->pts[pr->count][2] = z;
    pr->count++;

    if (pr->count >= pr->max_points) pr->sealed = 1;
    return true;
}

// ============================================================
// Draw helpers
// ============================================================
static int pr_effective_point_count(const PathRibbon *pr) {
    int n = (int)pr->count;
    if (n < 2) return 0;
    if (n > (int)pr->max_points) n = (int)pr->max_points;
    if (n > PR_MAX_POINTS_DRAW)  n = PR_MAX_POINTS_DRAW;
    return n;
}

// Compute end-taper multiplier: 0 at ends, 1 in middle.
// taper_portion is 0..0.49 of total length.
static inline float pr_end_taper(float t01, float taper_portion) {
    taper_portion = pr_clampf(taper_portion, 0.0f, 0.49f);
    if (taper_portion <= 0.0001f) return 1.0f;

    float a = taper_portion;
    float b = 1.0f - taper_portion;

    if (t01 <= a) {
        float u = (a > 0.0f) ? (t01 / a) : 0.0f;
        return pr_smoothstep(u); // 0->1
    }
    if (t01 >= b) {
        float u = (1.0f - t01) / (1.0f - b);
        return pr_smoothstep(u); // 1->0
    }
    return 1.0f;
}

// ============================================================
// DRAW: Crack
// ============================================================
void path_ribbon_draw_crack(const PathRibbon* pr) {
    if (!pr) return;
    if (pr->dead) return;

    int points = pr_effective_point_count(pr);
    if (points < 2) return;

    path_ribbon_ensure_draw_buffers();
    path_ribbon_ensure_id_mat();
    if (!s_pr_id_mat_fp) return;

    int idx = (s_pr_ring_idx++ % PR_DRAWBUF_RING);
    pr_slot_wait(idx);

    T3DVertPacked *vb = s_pr_v_ring[idx];
    if (!vb) return;

    const float a_mul = pr_clampf(pr->alpha_mul, 0.0f, 1.0f);
    if (a_mul <= 0.0f) return;

    t3d_fog_set_enabled(false);

    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_zbuf(true, false);
    rdpq_mode_alphacompare(0);
    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_dithering(DITHER_NONE_BAYER);

    t3d_state_set_drawflags(T3D_FLAG_SHADED | T3D_FLAG_DEPTH | T3D_FLAG_NO_LIGHT);
    t3d_state_set_depth_offset(-0x20);

    t3d_matrix_push(s_pr_id_mat_fp);

    const T3DVec3 nUp = {{0.0f, 1.0f, 0.0f}};
    const uint16_t norm = t3d_vert_pack_normal(&nUp);

    float totalLen = 0.0f;
    for (int i = 1; i < points; i++) {
        float dx = pr->pts[i][0] - pr->pts[i-1][0];
        float dz = pr->pts[i][2] - pr->pts[i-1][2];
        totalLen += sqrtf(dx*dx + dz*dz);
    }
    if (totalLen < 0.001f) totalLen = 0.001f;

    float accLen = 0.0f;

    for (int i = 0; i < points; i++) {
        float t01 = pr_clampf(accLen / totalLen, 0.0f, 1.0f);

        float w = pr->crack_w_start + (pr->crack_w_end - pr->crack_w_start) * t01;

        float endMul = pr_end_taper(t01, pr->crack_tip_taper);
        w *= endMul;

        if (pr->crack_w_noise > 0.0f) {
            float n = pr_hash_m11(pr->seed ^ (uint32_t)(i * 0x9E3779B9u));
            float jitter = 1.0f + n * pr_clampf(pr->crack_w_noise, 0.0f, 1.0f);
            w *= jitter;
        }

        if (w < 0.0f) w = 0.0f;

        float tx, tz;
        if (i < points - 1) {
            tx = pr->pts[i+1][0] - pr->pts[i][0];
            tz = pr->pts[i+1][2] - pr->pts[i][2];
        } else {
            tx = pr->pts[i][0] - pr->pts[i-1][0];
            tz = pr->pts[i][2] - pr->pts[i-1][2];
        }

        float len = sqrtf(tx*tx + tz*tz);
        if (len < 0.001f) { tx = 1.0f; tz = 0.0f; len = 1.0f; }
        float inv = 1.0f / len;
        tx *= inv; tz *= inv;

        float px = -tz;
        float pz =  tx;

        float x = pr->pts[i][0];
        float z = pr->pts[i][2];

        float x0 = x - px * w;
        float z0 = z - pz * w;
        float x1 = x + px * w;
        float z1 = z + pz * w;

        float y = pr->floor_y + pr->floor_eps;

        PRColor c = pr_color_mul_alpha(pr->crack_color, a_mul);

        int vL = i*2 + 0;
        int vR = i*2 + 1;

        pr_write_vert(vb, vL, pr_f2s16(x0), pr_f2s16(y), pr_f2s16(z0), 0, 0, c, norm);
        pr_write_vert(vb, vR, pr_f2s16(x1), pr_f2s16(y), pr_f2s16(z1), (int16_t)(1*32), 0, c, norm);

        if (i < points - 1) {
            float dx = pr->pts[i+1][0] - pr->pts[i][0];
            float dz = pr->pts[i+1][2] - pr->pts[i][2];
            accLen += sqrtf(dx*dx + dz*dz);
        }
    }

    const uint32_t vcount = (uint32_t)(points * 2);
    t3d_vert_load(vb, 0, vcount);

    int segs = points - 1;
    for (int i = 0; i < segs; i++) {
        uint32_t l0 = (uint32_t)(i*2 + 0);
        uint32_t r0 = (uint32_t)(i*2 + 1);
        uint32_t l1 = (uint32_t)(i*2 + 2);
        uint32_t r1 = (uint32_t)(i*2 + 3);
        t3d_tri_draw(l0, r0, l1);
        t3d_tri_draw(r0, r1, l1);
    }

    t3d_tri_sync();
    rdpq_sync_pipe();

    t3d_matrix_pop(1);

    pr_slot_fence(idx);

    // HARD RESET
    t3d_state_set_depth_offset(0);
    rdpq_sync_pipe();
    rdpq_set_mode_standard();

    t3d_fog_set_enabled(true);
}

// ============================================================
// DRAW: wall
// ============================================================
void path_ribbon_draw_wall(const PathRibbon* pr) {
    if (!pr) return;
    if (pr->dead) return;

    int points = pr_effective_point_count(pr);
    if (points < 2) return;

    path_ribbon_ensure_draw_buffers();
    path_ribbon_ensure_id_mat();
    if (!s_pr_id_mat_fp) return;

    int idx = (s_pr_ring_idx++ % PR_DRAWBUF_RING);
    pr_slot_wait(idx);

    T3DVertPacked *vb = s_pr_v_ring[idx];
    if (!vb) return;

    const float a_mul = pr_clampf(pr->alpha_mul, 0.0f, 1.0f);
    if (a_mul <= 0.0f) return;

    t3d_fog_set_enabled(false);

    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_zbuf(true, false);
    rdpq_mode_alphacompare(0);

    rdpq_mode_persp(true);

    float texW = 64.0f;
    float texH = 64.0f;

    static sprite_t *s_pr_last_uploaded = NULL;

    if (s_pr_wall_tex) {
        texW = (float)s_pr_wall_tex->width;  if (texW < 1.0f) texW = 64.0f;
        texH = (float)s_pr_wall_tex->height; if (texH < 1.0f) texH = 64.0f;

        if (s_pr_last_uploaded != s_pr_wall_tex) {
            rdpq_sprite_upload(TILE0, s_pr_wall_tex, NULL);
            s_pr_last_uploaded = s_pr_wall_tex;
        }

        rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
        t3d_state_set_drawflags(T3D_FLAG_TEXTURED | T3D_FLAG_SHADED | T3D_FLAG_DEPTH | T3D_FLAG_NO_LIGHT);
    } else {
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        t3d_state_set_drawflags(T3D_FLAG_SHADED | T3D_FLAG_DEPTH | T3D_FLAG_NO_LIGHT);
    }

    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_dithering(DITHER_NONE_BAYER);

    t3d_state_set_depth_offset(-0x10);

    t3d_matrix_push(s_pr_id_mat_fp);

    const T3DVec3 nAny = {{0.0f, 0.0f, 1.0f}};
    const uint16_t norm = t3d_vert_pack_normal(&nAny);

    float y0 = pr->floor_y;
    float y1 = pr->floor_y + pr->wall_height;

    float uAcc = 0.0f;

    float v_span = texH * PR_WALL_V_SCALE;
    if (v_span < 0.0f) v_span = 0.0f;

    float scrollU = s_pr_wall_scroll_u;
    float scrollV = s_pr_wall_scroll_v;

    float v0_period = texH;
    if (texH > 0.0f && v_span > 0.0f && v_span < texH) {
        v0_period = texH - v_span;
        if (v0_period < 1.0f) v0_period = 1.0f;
    }
    float v0_base = pr_wrap_scroll(scrollV, v0_period);

    for (int i = 0; i < points; i++) {
        if (i > 0) {
            float dx = pr->pts[i][0] - pr->pts[i-1][0];
            float dz = pr->pts[i][2] - pr->pts[i-1][2];
            uAcc += sqrtf(dx*dx + dz*dz);
        }

        float x = pr->pts[i][0];
        float z = pr->pts[i][2];

        PRColor colB = pr_color_mul_alpha(pr->wall_color_bot, a_mul);
        PRColor colT = pr_color_mul_alpha(pr->wall_color_top, a_mul);

        float u = uAcc * ((pr->wall_w_mult <= 0.0f) ? 1.0f : pr->wall_w_mult);
        u *= PR_WALL_U_SCALE;
        u += scrollU;
        u = pr_wrap_scroll(u, texW);

        float v0 = v0_base;
        float v1 = v0_base + v_span;

        v0 = pr_wrap_scroll(v0, texH);
        v1 = pr_wrap_scroll(v1, texH);

        int u10   = pr_clampi((int)lrintf(u  * 32.0f), -32760, 32760);
        int v0_10 = pr_clampi((int)lrintf(v0 * 32.0f), -32760, 32760);
        int v1_10 = pr_clampi((int)lrintf(v1 * 32.0f), -32760, 32760);

        int vB = i*2 + 0;
        int vT = i*2 + 1;

        pr_write_vert(vb, vB, pr_f2s16(x), pr_f2s16(y0), pr_f2s16(z), (int16_t)u10, (int16_t)v0_10, colB, norm);
        pr_write_vert(vb, vT, pr_f2s16(x), pr_f2s16(y1), pr_f2s16(z), (int16_t)u10, (int16_t)v1_10, colT, norm);
    }

    const uint32_t vcount = (uint32_t)(points * 2);
    t3d_vert_load(vb, 0, vcount);

    int segs = points - 1;
    for (int i = 0; i < segs; i++) {
        uint32_t b0 = (uint32_t)(i*2 + 0);
        uint32_t t0 = (uint32_t)(i*2 + 1);
        uint32_t b1 = (uint32_t)(i*2 + 2);
        uint32_t t1 = (uint32_t)(i*2 + 3);
        t3d_tri_draw(b0, t0, b1);
        t3d_tri_draw(t0, t1, b1);
    }

    t3d_tri_sync();
    rdpq_sync_pipe();

    t3d_matrix_pop(1);

    pr_slot_fence(idx);

    // HARD RESET
    t3d_state_set_depth_offset(0);

    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_persp(false);
    t3d_state_set_drawflags(0);

    t3d_fog_set_enabled(true);
}
