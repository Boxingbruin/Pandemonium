#ifndef BLOOD_PARTICLES_FX_H
#define BLOOD_PARTICLES_FX_H

#include <t3d/t3d.h>

void blood_particles_fx_init(void);
void blood_particles_fx_cleanup(void);
void blood_particles_fx_reset(void);
void blood_particles_fx_update(float dt);
void blood_particles_fx_draw(T3DViewport *viewport);

void blood_particles_fx_spawn_burst(float x, float y, float z, float strength);

#endif