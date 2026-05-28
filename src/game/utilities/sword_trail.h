#ifndef SWORD_TRAIL_H
#define SWORD_TRAIL_H

#include <stdint.h>
#include <stdbool.h>

#define TRAIL_MAX_SAMPLES 64

typedef struct {
    float base[3];
    float tip[3];
    float age;
    bool  valid;
} SwordTrailSample;

typedef struct {
    SwordTrailSample samples[TRAIL_MAX_SAMPLES];
    int count;
    int head;
    bool inited;

    // --- per-instance tuning (NEW) ---
    float lifetime_sec;        // how long samples live (main "length" control)
    float min_sample_dist;     // sampling density (smaller = denser = more expensive)
    float subdiv_dist;         // smoothness (smaller = smoother = more triangles)
    int   subdiv_max;
    float emit_accum;

    uint8_t max_alpha;
    uint8_t color_r, color_g, color_b;
} SwordTrail;

SwordTrail* sword_trail_get_player(void);
SwordTrail* sword_trail_get_boss(void);

void sword_trail_instance_init(SwordTrail *t);
void sword_trail_instance_reset(SwordTrail *t);

void sword_trail_instance_update(SwordTrail *t, float dt, bool emitting,
                                const float base_world[3], const float tip_world[3]);

void sword_trail_instance_draw(SwordTrail *t, void *viewport);

// --- setters (NEW) ---
void sword_trail_instance_set_lifetime(SwordTrail *t, float seconds);
void sword_trail_instance_set_min_sample_dist(SwordTrail *t, float dist);
void sword_trail_instance_set_subdiv(SwordTrail *t, float subdiv_dist, int subdiv_max);
void sword_trail_instance_set_color(SwordTrail *t, uint8_t r, uint8_t g, uint8_t b, uint8_t max_alpha);

// Back-compat wrappers if you want to keep them
void sword_trail_init(void);
void sword_trail_reset(void);
void sword_trail_update(float dt, bool emitting, const float base_world[3], const float tip_world[3]);
void sword_trail_draw(void *viewport);
void sword_trail_draw_all(void *viewport);

#endif