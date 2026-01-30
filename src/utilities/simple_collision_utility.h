#ifndef SIMPLE_COLLISION_UTILITY_H
#define SIMPLE_COLLISION_UTILITY_H

#include <stdbool.h>
#include <stdint.h>
#include "game_math.h"


typedef struct {
    float center[3]; // x,y,z
    float half[3];   // hx,hy,hz
    float yaw;       // radians
} SCU_OBB;

/* ------------------------------------------------------------------
 * Public float-space API
 * These take float positions/radii in game space and convert to Q16.16
 * internally before doing collision math.
 * ------------------------------------------------------------------ */

// circle vs obb
bool scu_circle_vs_obb_resolve_xz_f(
    float *px, float *pz, float radius,
    const SCU_OBB *obb);

// capsule vs obb
bool scu_capsule_vs_obb_push_xz_f(
    const float capA[3], const float capB[3], float radius,
    const SCU_OBB *obb,
    float push_out[3],
    float n_out[3]     // XZ normal in world space
);

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