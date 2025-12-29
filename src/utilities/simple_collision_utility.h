#ifndef SIMPLE_COLLISION_UTILITY_H
#define SIMPLE_COLLISION_UTILITY_H

#include <stdbool.h>
#include <stdint.h>
#include "game_math.h"

/* ------------------------------------------------------------------
 * Fixed-point types (Q16.16)
 * ------------------------------------------------------------------ */

typedef struct {
    FixedVec3 a;        // segment start (Q16.16)
    FixedVec3 b;        // segment end   (Q16.16)
    int32_t   radius;   // Q16.16 radius
} SCU_CapsuleFixed;

/* ------------------------------------------------------------------
 * Fixed-point API
 * ------------------------------------------------------------------ */

// capsule vs capsule (fixed-point)
bool scu_fixed_capsule_vs_capsule(
    const SCU_CapsuleFixed *c1,
    const SCU_CapsuleFixed *c2);

/* ------------------------------------------------------------------
 * Public float-space API
 * These take float positions/radii in game space and convert to Q16.16
 * internally before doing collision math.
 * ------------------------------------------------------------------ */

// sphere vs sphere (float space)
bool scu_sphere_vs_sphere_f(
    const float c1[3], float r1,
    const float c2[3], float r2);

// sphere vs AABB (float space)
bool scu_sphere_vs_rect_f(
    const float center[3], float radius,
    const float rect_min[3], const float rect_max[3]);

// AABB vs AABB (float space)
bool scu_rect_vs_rect_f(
    const float amin[3], const float amax[3],
    const float bmin[3], const float bmax[3]);

// capsule vs sphere (float space)
bool scu_capsule_vs_sphere_f(
    const float cap_a[3], const float cap_b[3], float cap_radius,
    const float sphere_center[3], float sphere_radius);

// capsule vs AABB (float space)
bool scu_capsule_vs_rect_f(
    const float cap_a[3], const float cap_b[3], float cap_radius,
    const float rect_min[3], const float rect_max[3]);

// capsule vs capsule (float space)
bool scu_capsule_vs_capsule_f(
    const float a0[3], const float a1[3], float radiusA,
    const float b0[3], const float b1[3], float radiusB);

#endif