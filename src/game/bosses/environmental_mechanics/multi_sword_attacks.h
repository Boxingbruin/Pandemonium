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

// Aerial attack support
void msa_spawn_aerial_ring(float centerX, float centerY, float centerZ, float radius, int count);
void msa_update_aerial_ring_pose(float centerX, float centerY, float centerZ, float radius,
                                 float targetX, float targetY, float targetZ);
void msa_fire_aerial_sword(int index, float targetX, float targetY, float targetZ);
bool msa_has_active_aerial_swords(void);
void msa_cleanup_aerial_swords(void);

// Ground sweep attack support (single-cycle, boss-driven)
// Call msa_ground_sweep_start() once to begin; poll msa_ground_sweep_is_done() each frame.
void msa_ground_sweep_start(void);
bool msa_ground_sweep_is_done(void);

#endif
