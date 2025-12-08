#ifndef DEBUG_DRAW_H
#define DEBUG_DRAW_H

#include <t3d/t3d.h>
#include "game_math.h"

extern uint16_t DEBUG_COLORS[5];

void debug_draw_aabb(T3DViewport *vp, const FixedVec3 *min, const FixedVec3 *max, uint16_t color);
void debug_draw_sphere(T3DViewport *vp, const T3DVec3 *center, float radius, uint16_t color);
void debug_draw_capsule_from_fixed(T3DViewport *vp, const FixedVec3 *a, const FixedVec3 *b, int32_t radius_fixed, uint16_t color);
void debug_draw_cross(T3DViewport *vp, const T3DVec3 *center, float half_length, uint16_t color);

#endif