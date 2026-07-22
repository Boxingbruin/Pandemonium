#ifndef DUST_PARTICLES_FX_H
#define DUST_PARTICLES_FX_H

#include <stdbool.h>

#include <t3d/t3d.h>

void dust_particles_fx_init(void);
void dust_particles_fx_cleanup(void);
void dust_particles_fx_reset(void);
void dust_particles_fx_update(float dt);
void dust_particles_fx_draw(T3DViewport *viewport);

void dust_particles_fx_spawn_burst(float x, float y, float z, float strength);

void dust_particles_fx_set_ambient_enabled(bool enabled);
bool dust_particles_fx_is_ambient_enabled(void);

void dust_particles_fx_set_ambient_volume(
    float minX, float maxX,
    float minY, float maxY,
    float minZ, float maxZ
);

#endif