// fx/lightning_fx.c
#include "fx/lightning_fx.h"

#include <t3d/t3d.h>
#include <t3d/t3dmodel.h>

#include <libdragon.h>
#include <rdpq.h>

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "globals.h"

// ------------------------------------------------------------
// Tunables
// ------------------------------------------------------------
#ifndef LIGHTNING_SCALE_MULT
#define LIGHTNING_SCALE_MULT 2.0f
#endif

#ifndef LIGHTNING_LIFETIME_SEC
#define LIGHTNING_LIFETIME_SEC 0.25f
#endif

#ifndef LIGHTNING_FLICKER_HZ
#define LIGHTNING_FLICKER_HZ 38.0f
#endif

// ------------------------------------------------------------
// Internal type
// ------------------------------------------------------------
typedef struct LightningFX {
    T3DModel*  model;

    void*      mat_base;
    T3DMat4FP* mat;          // single matrix

    float      pos[3];
    float      yaw;

    float      t;            // time since strike
    float      lifetime;

    float      flicker_acc;
    bool       visible;
    bool       active;

    // ---- auto ring strike ----
    bool  ring_enabled;
    float ring_rmin, ring_rmax;
    float ring_y;
    float ring_min_interval, ring_max_interval;

    float ring_timer;
    float ring_next_strike;

    // if true, draw should force prim=white (ring effect)
    bool  force_white;

    uint32_t rng;
} LightningFX;

// ------------------------------------------------------------
// Module-owned singleton (no passing pointers around)
// ------------------------------------------------------------
static LightningFX* g_fx = NULL;

// ------------------------------------------------------------
// Uncached aligned helper (16-byte)
// ------------------------------------------------------------
static void* alloc_uncached_aligned16(size_t bytes, void** out_base) {
    void* base = malloc_uncached(bytes + 15);
    if (!base) { *out_base = NULL; return NULL; }
    uintptr_t p = (uintptr_t)base;
    uintptr_t aligned = (p + 15u) & ~(uintptr_t)15u;
    *out_base = base;
    return (void*)aligned;
}

static inline uint32_t xorshift32(uint32_t* s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static inline float frand01_rng(uint32_t* s) {
    uint32_t r = xorshift32(s);
    return (float)r / 4294967295.0f; // [0,1]
}

static inline float frand_range(uint32_t* s, float a, float b) {
    return a + (b - a) * frand01_rng(s);
}

// Uniform-over-area point in an XZ annulus (ring)
static inline void rand_point_ring_xz(uint32_t* s, float rMin, float rMax, float* outX, float* outZ)
{
    float u = frand01_rng(s);
    float v = frand01_rng(s);

    float a = v * (T3D_PI * 2.0f);

    float rMin2 = rMin * rMin;
    float rMax2 = rMax * rMax;
    float r = sqrtf(rMin2 + u * (rMax2 - rMin2));

    *outX = fm_cosf(a) * r;
    *outZ = fm_sinf(a) * r;
}

static inline void build_srt_scaled(T3DMat4FP* out, float scale1, float x, float y, float z, float yaw) {
    const float scale[3] = { scale1, scale1, scale1 };
    const float rot[3]   = { 0.0f,   yaw,    0.0f   };
    const float trans[3] = { x,      y,      z      };
    t3d_mat4fp_from_srt_euler(out, scale, rot, trans);
}

// ------------------------------------------------------------
// Internal core API
// ------------------------------------------------------------
static LightningFX* lightning_fx_create_internal(const char* rom_model_path) {
    assert(rom_model_path);

    LightningFX* fx = (LightningFX*)malloc(sizeof(LightningFX));
    assert(fx);
    memset(fx, 0, sizeof(*fx));

    fx->rng = 0xC0FFEEu ^ (uint32_t)(uintptr_t)fx;

    fx->model = t3d_model_load(rom_model_path);
    assert(fx->model && "t3d_model_load failed");

    void* aligned = alloc_uncached_aligned16(sizeof(T3DMat4FP), &fx->mat_base);
    fx->mat = (T3DMat4FP*)aligned;
    assert(fx->mat && (((uintptr_t)fx->mat & 0xF) == 0));

    fx->active = false;
    fx->visible = false;
    fx->lifetime = LIGHTNING_LIFETIME_SEC;

    fx->pos[0] = 0.0f;
    fx->pos[1] = -9999.0f;
    fx->pos[2] = 0.0f;
    fx->yaw = 0.0f;
    fx->t = 0.0f;
    fx->flicker_acc = 0.0f;

    // auto ring defaults
    fx->ring_enabled      = false;
    fx->ring_rmin         = 40.0f;
    fx->ring_rmax         = 100.0f;
    fx->ring_y            = 0.0f;
    fx->ring_min_interval = 0.15f;
    fx->ring_max_interval = 0.60f;
    fx->ring_timer        = 0.0f;
    fx->ring_next_strike  = frand_range(&fx->rng, fx->ring_min_interval, fx->ring_max_interval);

    fx->force_white = false;

    return fx;
}

static void lightning_fx_destroy_internal(LightningFX* fx) {
    if (!fx) return;

    if (fx->mat_base) {
        free_uncached(fx->mat_base);
        fx->mat_base = NULL;
        fx->mat = NULL;
    }

    if (fx->model) {
        t3d_model_free(fx->model);
        fx->model = NULL;
    }

    free(fx);
}

static void lightning_fx_strike_internal(LightningFX* fx, float x, float y, float z, float yaw, bool force_white) {
    if (!fx) return;

    fx->pos[0] = x;
    fx->pos[1] = y;
    fx->pos[2] = z;
    fx->yaw    = yaw;

    fx->t = 0.0f;
    fx->flicker_acc = 0.0f;

    fx->active  = true;
    fx->visible = true;

    fx->force_white = force_white;

    fx->rng ^= (uint32_t)((int)x * 73856093);
    fx->rng ^= (uint32_t)((int)z * 19349663);
    (void)xorshift32(&fx->rng);
}

static inline bool lightning_fx_is_lit_internal(const LightningFX* fx)
{
    return fx && fx->active && fx->visible;
}

static void lightning_fx_update_internal(LightningFX* fx, float dt) {
    if (!fx) return;

    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.05f) dt = 0.05f;

    // ---- auto ring strike scheduler ----
    if (fx->ring_enabled) {
        fx->ring_timer += dt;
        if (fx->ring_timer >= fx->ring_next_strike) {
            fx->ring_timer = 0.0f;
            fx->ring_next_strike = frand_range(&fx->rng, fx->ring_min_interval, fx->ring_max_interval);

            float x, z;
            rand_point_ring_xz(&fx->rng, fx->ring_rmin, fx->ring_rmax, &x, &z);

            // face toward origin (optional)
            float yaw = atan2f(-x, -z);

            // ring strikes => force white always
            lightning_fx_strike_internal(fx, x, fx->ring_y, z, yaw, true);
        }
    }

    if (!fx->active) return;

    fx->t += dt;
    if (fx->t >= fx->lifetime) {
        fx->active = false;
        fx->visible = false;
        return;
    }

    fx->flicker_acc += dt;

    float period = 1.0f / (float)LIGHTNING_FLICKER_HZ;

    uint32_t r = xorshift32(&fx->rng) & 0xFFu;
    float jitter = 0.75f + (float)r * (0.50f / 255.0f);
    float next = period * jitter;

    if (fx->flicker_acc >= next) {
        fx->flicker_acc = 0.0f;
        fx->visible = !fx->visible;
    }
}

static void lightning_fx_draw_internal(LightningFX* fx) {
    if (!fx || !fx->active || !fx->visible) return;
    if (!fx->model || !fx->mat) return;

    const float scale = (float)MODEL_SCALE * (float)LIGHTNING_SCALE_MULT;

    build_srt_scaled(fx->mat, scale,
        fx->pos[0], fx->pos[1], fx->pos[2],
        fx->yaw
    );

    if (fx->force_white) 
    {
        rdpq_set_prim_color(RGBA32(0,0,0,255));
    }
    else
    { 
        rdpq_set_prim_color(RGBA32(255,221,0,255));
    }

    t3d_matrix_push_pos(1);
        t3d_matrix_set(fx->mat, true);
        t3d_model_draw(fx->model);
    t3d_matrix_pop(1);
}

// ------------------------------------------------------------
// Public system API (no pointers)
// ------------------------------------------------------------
void lightning_fx_system_init(const char* rom_model_path)
{
    if (g_fx) return;
    g_fx = lightning_fx_create_internal(rom_model_path);
}

void lightning_fx_system_shutdown(void)
{
    if (!g_fx) return;
    lightning_fx_destroy_internal(g_fx);
    g_fx = NULL;
}

void lightning_fx_system_update(float dt)
{
    if (!g_fx) return;
    lightning_fx_update_internal(g_fx, dt);
}

void lightning_fx_system_draw(void)
{
    if (!g_fx) return;
    lightning_fx_draw_internal(g_fx);
}

void lightning_fx_system_ring_enable(bool enabled)
{
    if (!g_fx) return;
    g_fx->ring_enabled = enabled;
    g_fx->ring_timer = 0.0f;
    g_fx->ring_next_strike = frand_range(&g_fx->rng, g_fx->ring_min_interval, g_fx->ring_max_interval);
}

void lightning_fx_system_ring_config(float rMin, float rMax, float y,
                                     float minIntervalSec, float maxIntervalSec)
{
    if (!g_fx) return;

    if (rMin < 0.0f) rMin = 0.0f;
    if (rMax < rMin) rMax = rMin;

    if (minIntervalSec < 0.01f) minIntervalSec = 0.01f;
    if (maxIntervalSec < minIntervalSec) maxIntervalSec = minIntervalSec;

    g_fx->ring_rmin = rMin;
    g_fx->ring_rmax = rMax;
    g_fx->ring_y    = y;

    g_fx->ring_min_interval = minIntervalSec;
    g_fx->ring_max_interval = maxIntervalSec;

    g_fx->ring_timer = 0.0f;
    g_fx->ring_next_strike = frand_range(&g_fx->rng, g_fx->ring_min_interval, g_fx->ring_max_interval);
}

// Manual strike (MSA etc) => default prim color (force_white = false)
void lightning_fx_system_strike(float x, float y, float z, float yaw)
{
    if (!g_fx) return;
    lightning_fx_strike_internal(g_fx, x, y, z, yaw, false);
}

bool lightning_fx_system_is_lit(void)
{
    return lightning_fx_is_lit_internal(g_fx);
}
