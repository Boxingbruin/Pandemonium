#ifndef MULTI_SWORD_ATTACKS
#define MULTI_SWORD_ATTACKS

#include <t3d/t3d.h>
#include <stdbool.h>

// Lifecycle
void msa_init(void);
void msa_shutdown(void);

// Main loop
void msa_update(float dt);

// Draws
void msa_draw_visuals(T3DViewport *viewport);
void msa_draw_debug(T3DViewport *viewport);

// Back-compat
void msa_draw(T3DViewport *viewport);

// Runtime controls
void msa_set_enabled(bool enabled);
void msa_set_sword_count(int count);

// Cluster layout knobs
void msa_set_cluster_spacing(float minSpacing, float radius);

// scene floor set it here.
void msa_set_floor_y(float y);

typedef enum {
    MSA_PATTERN_GROUND_SWEEP = 0,
} MsaPattern;

void msa_set_pattern(MsaPattern p);

#endif
