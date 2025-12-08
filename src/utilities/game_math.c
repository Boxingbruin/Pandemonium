#include <t3d/t3d.h>
#include "game_math.h"
#include "globals.h"

int safe_float_to_int(float x)
{
    if (x != x) return 0;
    if (x > 32767.0f) return 32767;
    if (x < -32768.0f) return -32768;
    return (int)x;
}

int64_t game_math_isqrt64(int64_t x)
{
    if (x <= 0) return 0;

    int64_t res = 0;
    int64_t bit = (int64_t)1 << 62;

    while (bit > x) bit >>= 2;

    while (bit != 0) {
        if (x >= res + bit) {
            x -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }

    return res << (FIXED_SHIFT / 2);
}

int32_t fixed_saturate(int32_t x) {
    if (x < 0) return 0;
    if (x > FIXED_ONE) return FIXED_ONE;
    return x;
}

int32_t clamp_fixed(int32_t val, int32_t minVal, int32_t maxVal) {
    return (val < minVal) ? minVal : (val > maxVal) ? maxVal : val;
}

int64_t clamp_fixed64(int64_t val, int64_t minVal, int64_t maxVal) {
    return (val < minVal) ? minVal : (val > maxVal) ? maxVal : val;
}

void vec3_normalize_fixed(FixedVec3* out, const FixedVec3* in) {
    int64_t len2 = 0;
    for (int i = 0; i < 3; i++)
        len2 += ((int64_t)in->v[i] * in->v[i]) >> FIXED_SHIFT;

    if (len2 == 0) {
        out->v[0] = out->v[1] = out->v[2] = 0;
        return;
    }

    int64_t len = game_math_isqrt64(len2);
    
    for (int i = 0; i < 3; i++)
        out->v[i] = (int32_t)(((int64_t)in->v[i] << FIXED_SHIFT) / len);
}

int64_t vec3_dist_squared_fixed(const FixedVec3* a, const FixedVec3* b)
{
    int64_t dx = (int64_t)a->v[0] - b->v[0];
    int64_t dy = (int64_t)a->v[1] - b->v[1];
    int64_t dz = (int64_t)a->v[2] - b->v[2];
    return ((dx*dx) + (dy*dy) + (dz*dz)) >> FIXED_SHIFT;
}

int64_t vec3_dot_fixed(const FixedVec3* a, const FixedVec3* b) {
    int64_t sum = 0;
    for (int i = 0; i < 3; i++) {
        int64_t ai = (int64_t)a->v[i];
        int64_t bi = (int64_t)b->v[i];
        int64_t prod = ai * bi;
        int64_t shifted = prod >> FIXED_SHIFT;
        sum += shifted;
    }
    return sum;
}

void vec3_cross_fixed(FixedVec3* out, const FixedVec3* a, const FixedVec3* b)
{
    out->v[0] = FIXED_MUL(a->v[1], b->v[2]) - FIXED_MUL(a->v[2], b->v[1]);
    out->v[1] = FIXED_MUL(a->v[2], b->v[0]) - FIXED_MUL(a->v[0], b->v[2]);
    out->v[2] = FIXED_MUL(a->v[0], b->v[1]) - FIXED_MUL(a->v[1], b->v[0]);
}


void vec3_sub_fixed(FixedVec3* out, const FixedVec3* a, const FixedVec3* b) {
    out->v[0] = a->v[0] - b->v[0];
    out->v[1] = a->v[1] - b->v[1];
    out->v[2] = a->v[2] - b->v[2];
}

void vec3_mad_fixed(FixedVec3* out, const FixedVec3* a, const FixedVec3* b, int32_t t) {
    out->v[0] = a->v[0] + (int32_t)(((int64_t)b->v[0] * t) >> FIXED_SHIFT);
    out->v[1] = a->v[1] + (int32_t)(((int64_t)b->v[1] * t) >> FIXED_SHIFT);
    out->v[2] = a->v[2] + (int32_t)(((int64_t)b->v[2] * t) >> FIXED_SHIFT);
}

void fixedvec3_to_world_vec3(T3DVec3 *out, const FixedVec3 *in)
{
    for (int i = 0; i < 3; i++)
        out->v[i] = ((float)in->v[i] / 65536.0f) * MODEL_SCALE;
}

void vec3_lerp(T3DVec3* out, const T3DVec3* a, const T3DVec3* b, float t) {
    for (int i = 0; i < 3; ++i)
        out->v[i] = a->v[i] + t * (b->v[i] - a->v[i]);
}

int is_finite_vec3(const T3DVec3* v) {
    return (v->v[0]*0.0f == 0.0f && v->v[1]*0.0f == 0.0f && v->v[2]*0.0f == 0.0f);
}

void vec3_to_fixed(FixedVec3* out, const T3DVec3* in) {
    for (int i = 0; i < 3; i++)
        out->v[i] = TO_FIXED(in->v[i] * 16.0f);
}