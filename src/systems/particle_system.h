#ifndef PARTICLE_SYSTEM_H
#define PARTICLE_SYSTEM_H

#include <stdbool.h>

// ------------------------------------------------------------
// TinyPX / TPX particle system.
// ------------------------------------------------------------

void particle_system_init(void);
void particle_system_cleanup(void);
bool particle_system_is_initialized(void);

#endif