#ifndef SWORD_TRAIL_H
#define SWORD_TRAIL_H

#include <stdbool.h>

/**
 * Screen-space sword trail ribbon.
 *
 * Implementation detail: this is rendered as projected 2D triangles (RDP),
 * using world-space base/tip samples. This keeps it lightweight and avoids
 * needing dynamic Tiny3D geometry support.
 */

typedef struct SwordTrail SwordTrail;

// Global instances (player + boss)
SwordTrail* sword_trail_get_player(void);
SwordTrail* sword_trail_get_boss(void);

// Instance API (use for boss / future trails)
void sword_trail_instance_init(SwordTrail *t);
void sword_trail_instance_reset(SwordTrail *t);
void sword_trail_instance_update(SwordTrail *t, float dt, bool emitting, const float base_world[3], const float tip_world[3]);
void sword_trail_instance_draw(SwordTrail *t, void *viewport);

void sword_trail_init(void);
void sword_trail_reset(void);

/**
 * Update trail aging + optionally emit a new sample this frame.
 *
 * - dt: seconds
 * - emitting: if false, no new samples are added (existing samples still age out)
 * - base_world/tip_world: required only when emitting==true
 */
void sword_trail_update(float dt, bool emitting, const float base_world[3], const float tip_world[3]);

/** Draw the trail (call after 3D render, before UI). */
void sword_trail_draw(void *viewport);
void sword_trail_draw_all(void *viewport);

#endif

