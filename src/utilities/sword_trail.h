#ifndef SWORD_TRAIL_H
#define SWORD_TRAIL_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Screen-space sword trail ribbon.
 *
 * Rendered as projected 2D triangles (RDP), using world-space base/tip samples.
 * Lightweight: no dynamic Tiny3D mesh needed.
 */

enum { TRAIL_MAX_SAMPLES = 24 };

typedef struct {
    float base[3];
    float tip[3];
    float age;
    bool  valid;
} SwordTrailSample;

typedef struct SwordTrail {
    SwordTrailSample samples[TRAIL_MAX_SAMPLES];
    int count;
    int head;      // newest element index when count>0
    bool inited;
} SwordTrail;

// Global instances (player + boss)
SwordTrail* sword_trail_get_player(void);
SwordTrail* sword_trail_get_boss(void);

// Instance API (use for boss / future trails)
void sword_trail_instance_init(SwordTrail *t);
void sword_trail_instance_reset(SwordTrail *t);
void sword_trail_instance_update(
    SwordTrail *t,
    float dt,
    bool emitting,
    const float base_world[3],
    const float tip_world[3]
);
void sword_trail_instance_draw(SwordTrail *t, void *viewport);

// Back-compat wrappers (player trail)
void sword_trail_init(void);
void sword_trail_reset(void);
void sword_trail_update(float dt, bool emitting, const float base_world[3], const float tip_world[3]);
void sword_trail_draw(void *viewport);
void sword_trail_draw_all(void *viewport);

#endif