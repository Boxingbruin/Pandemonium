#include "fx/lightning_fx.h"

#include <t3d/t3d.h>
#include <t3d/t3dmodel.h>

#include <libdragon.h>
#include <rspq.h>

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
struct LightningFX {
    T3DModel*     model;
    rspq_block_t* dpl;

    void*         mat_base;
    T3DMat4FP*    mat;          // single matrix

    float         pos[3];
    float         yaw;

    float         t;            // time since strike
    float         lifetime;

    float         flicker_acc;
    bool          visible;
    bool          active;

    uint32_t      rng;
};

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

// Build matrix like your other code does.
static inline void build_srt_scaled(T3DMat4FP* out, float scale1, float x, float y, float z, float yaw) {
    const float scale[3] = { scale1, scale1, scale1 };
    const float rot[3]   = { 0.0f,   yaw,    0.0f   };
    const float trans[3] = { x,      y,      z      };
    t3d_mat4fp_from_srt_euler(out, scale, rot, trans);
}

// ------------------------------------------------------------
// Public API
// ------------------------------------------------------------
LightningFX* lightning_fx_create(const char* rom_model_path) {
    assert(rom_model_path);

    LightningFX* fx = (LightningFX*)malloc(sizeof(LightningFX));
    assert(fx);
    memset(fx, 0, sizeof(*fx));

    fx->rng = 0xC0FFEEu ^ (uint32_t)(uintptr_t)fx;

    fx->model = t3d_model_load(rom_model_path);
    assert(fx->model && "t3d_model_load failed");

    // Record model draw to a rspq block (faster than calling draw each frame).
    rspq_block_begin();
        t3d_model_draw(fx->model);
    fx->dpl = rspq_block_end();
    assert(fx->dpl && "rspq_block_end failed");

    void* aligned = alloc_uncached_aligned16(sizeof(T3DMat4FP), &fx->mat_base);
    fx->mat = (T3DMat4FP*)aligned;
    assert(fx->mat && (((uintptr_t)fx->mat & 0xF) == 0));

    fx->active = false;
    fx->visible = false;
    fx->lifetime = LIGHTNING_LIFETIME_SEC;

    // park it offscreen
    fx->pos[0] = 0.0f;
    fx->pos[1] = -9999.0f;
    fx->pos[2] = 0.0f;
    fx->yaw = 0.0f;
    fx->t = 0.0f;
    fx->flicker_acc = 0.0f;

    return fx;
}

void lightning_fx_destroy(LightningFX* fx) {
    if (!fx) return;

    if (fx->mat_base) {
        free_uncached(fx->mat_base);
        fx->mat_base = NULL;
        fx->mat = NULL;
    }

#ifdef RSPQ_BLOCK_FREE_SUPPORTED
    if (fx->dpl) rspq_block_free(fx->dpl);
#endif
    fx->dpl = NULL;

    if (fx->model) {
        t3d_model_free(fx->model);
        fx->model = NULL;
    }

    free(fx);
}

void lightning_fx_strike(LightningFX* fx, float x, float y, float z, float yaw) {
    if (!fx) return;

    fx->pos[0] = x;
    fx->pos[1] = y;
    fx->pos[2] = z;
    fx->yaw    = yaw;

    fx->t = 0.0f;
    fx->flicker_acc = 0.0f;

    fx->active  = true;
    fx->visible = true;

    // reseed a bit so each strike has different flicker
    fx->rng ^= (uint32_t)((int)x * 73856093);
    fx->rng ^= (uint32_t)((int)z * 19349663);
    (void)xorshift32(&fx->rng);
}

void lightning_fx_update(LightningFX* fx, float dt) {
    if (!fx || !fx->active) return;

    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.05f) dt = 0.05f;

    fx->t += dt;
    if (fx->t >= fx->lifetime) {
        fx->active = false;
        fx->visible = false;
        return;
    }

    // Flicker: toggle visibility at LIGHTNING_FLICKER_HZ with random jitter.
    fx->flicker_acc += dt;

    float period = 1.0f / (float)LIGHTNING_FLICKER_HZ;

    // add a tiny random jitter per toggle so it doesn't look perfectly periodic
    // jitter range: 0.75 - 1.25 * period
    uint32_t r = xorshift32(&fx->rng) & 0xFFu;
    float jitter = 0.75f + (float)r * (0.50f / 255.0f);
    float next = period * jitter;

    if (fx->flicker_acc >= next) {
        fx->flicker_acc = 0.0f;
        fx->visible = !fx->visible;
    }
}

void lightning_fx_draw(LightningFX* fx) {
    if (!fx || !fx->active || !fx->visible) return;
    if (!fx->model || !fx->dpl || !fx->mat) return;

    const float scale = (float)MODEL_SCALE * (float)LIGHTNING_SCALE_MULT;

    build_srt_scaled(fx->mat, scale,
        fx->pos[0], fx->pos[1], fx->pos[2],
        fx->yaw
    );

    // Draw with the matrix set.
    t3d_matrix_push_pos(1);
        t3d_matrix_set(fx->mat, true);
        rspq_block_run(fx->dpl);
    t3d_matrix_pop(1);
}
