#ifndef SIMPLE_COLLISION_UTILITY_H
#define SIMPLE_COLLISION_UTILITY_H

#include <stdint.h>
#include <stdbool.h>

#include "game_math.h"   // FixedVec3, FIXED_MUL, FIXED_DIV, FIXED_SHIFT, TO_FIXED, fixed_saturate, etc.

/* Fixed-point primitives (Q16.16) */

typedef struct {
    FixedVec3 center;   // Q16.16 coords
    int32_t   radius;   // Q16.16 radius
} SCU_SphereFixed;

typedef struct {
    FixedVec3 a;        // segment start (Q16.16)
    FixedVec3 b;        // segment end   (Q16.16)
    int32_t   radius;   // Q16.16 radius
} SCU_CapsuleFixed;

typedef struct {
    FixedVec3 min;      // AABB min (Q16.16)
    FixedVec3 max;      // AABB max (Q16.16)
} SCU_RectFixed;

/* Core helpers */

int64_t scu_fixed_vec_dot      (const FixedVec3 *a, const FixedVec3 *b);
void    scu_fixed_vec_sub      (FixedVec3 *out, const FixedVec3 *a, const FixedVec3 *b);
int64_t scu_fixed_vec_len2     (const FixedVec3 *a);
int64_t scu_fixed_vec_dist2    (const FixedVec3 *a, const FixedVec3 *b);

FixedVec3 scu_fixed_closest_point_on_segment(const FixedVec3 *A,
                                             const FixedVec3 *B,
                                             const FixedVec3 *P);

int64_t scu_fixed_segment_segment_dist2(const FixedVec3 *p1,
                                        const FixedVec3 *q1,
                                        const FixedVec3 *p2,
                                        const FixedVec3 *q2);

/* Collision tests (all fixed-point, return true if overlapping) */

bool scu_fixed_sphere_vs_sphere   (const SCU_SphereFixed   *s1,
                                   const SCU_SphereFixed   *s2);

bool scu_fixed_sphere_vs_rect     (const SCU_SphereFixed   *s,
                                   const SCU_RectFixed     *r);

bool scu_fixed_rect_vs_rect       (const SCU_RectFixed     *a,
                                   const SCU_RectFixed     *b);

bool scu_fixed_capsule_vs_sphere  (const SCU_CapsuleFixed  *cap,
                                   const SCU_SphereFixed   *s);

bool scu_fixed_capsule_vs_rect    (const SCU_CapsuleFixed  *cap,
                                   const SCU_RectFixed     *r);

bool scu_fixed_capsule_vs_capsule (const SCU_CapsuleFixed  *c1,
                                   const SCU_CapsuleFixed  *c2);

#endif