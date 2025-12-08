
#ifndef GAME_MATH_H
#define GAME_MATH_H

#include <libdragon.h>
#include <t3d/t3d.h>

#define FIXED_SHIFT 16
#define FIXED_ONE   (1 << FIXED_SHIFT)
#define TO_FIXED(x) ((int32_t)((x) * (float)FIXED_ONE))
#define TO_FIXED64(x) ((int32_t)((int64_t)((x) * (float)FIXED_ONE)))
#define FROM_FIXED(x) ((x) / (float)FIXED_ONE)
#define EPSILON_FIXED (TO_FIXED(0.001f))
#define FIXED_ABS(x) (((x) < 0) ? -(x) : (x))

#define FIXED_MUL(a, b) ((int32_t)(((int64_t)(a) * (b)) >> FIXED_SHIFT))
#define FIXED_DIV(a, b) ((int32_t)(((int64_t)(a) << FIXED_SHIFT) / (b)))

typedef struct {
    int32_t v[3];
} FixedVec3;

int32_t clamp_fixed(int32_t val, int32_t minVal, int32_t maxVal);
int64_t clamp_fixed64(int64_t val, int64_t minVal, int64_t maxVal);
int safe_float_to_int(float x);
int32_t fixed_saturate(int32_t x);

int is_finite_vec3(const T3DVec3* v);
void fixedvec3_to_world_vec3(T3DVec3 *out, const FixedVec3 *in);
void vec3_lerp(T3DVec3* out, const T3DVec3* a, const T3DVec3* b, float t);

int32_t fixed_saturate(int32_t x);
int64_t vec3_dist_squared_fixed(const FixedVec3* a, const FixedVec3* b);
void vec3_cross_fixed(FixedVec3* out, const FixedVec3* a, const FixedVec3* b);
int64_t vec3_dot_fixed(const FixedVec3* a, const FixedVec3* b);
void vec3_normalize_fixed(FixedVec3* out, const FixedVec3* in);
void vec3_to_fixed(FixedVec3* out, const T3DVec3* in);
void vec3_sub_fixed(FixedVec3* out, const FixedVec3* a, const FixedVec3* b);
void vec3_mad_fixed(FixedVec3* out, const FixedVec3* a, const FixedVec3* b, int32_t t);

#endif