#ifndef DEBUG_DRAW_H
#define DEBUG_DRAW_H

#include <stdint.h>
#include <t3d/t3d.h>
#include "game_math.h"

// Simple AABB type in game space
typedef struct {
    T3DVec3 min;
    T3DVec3 max;
} AABB;

extern uint16_t DEBUG_COLORS[5];

void debug_draw_aabb(
    T3DViewport *vp,
    const T3DVec3 *min,
    const T3DVec3 *max,
    uint16_t color);

void debug_draw_sphere(
    T3DViewport *vp,
    const T3DVec3 *center,
    float radius,
    uint16_t color);

// New capsule function (float radius, not fixed)
void debug_draw_capsule(
    T3DViewport *vp,
    const T3DVec3 *a,
    const T3DVec3 *b,
    float radius,
    uint16_t color);

void debug_draw_cross(
    T3DViewport *vp,
    const T3DVec3 *center,
    float half_length,
    uint16_t color);

void debug_draw_dot(
    T3DViewport *vp,
    const T3DVec3 *center,
    float radius,
    uint16_t color);

void debug_draw_tri_wire(
    T3DViewport *vp,
    const T3DVec3 *p0,
    const T3DVec3 *p1,
    const T3DVec3 *p2,
    uint16_t color);

// Capsule vs list of AABBs
void debug_draw_capsule_vs_aabb_list(
    T3DViewport   *vp,
    const T3DVec3 *capA,
    const T3DVec3 *capB,
    float          radius,
    const AABB    *aabbs,
    int            aabbCount,
    uint16_t       colorNoHit,
    uint16_t       colorHit);

#endif