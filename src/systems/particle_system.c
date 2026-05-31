#include "particle_system.h"

#include <stdbool.h>
#include <t3d/tpx.h>

static bool s_particleSystemInitialized = false;

void particle_system_init(void)
{
    if (s_particleSystemInitialized) {
        return;
    }

    tpx_init((TPXInitParams){});

    s_particleSystemInitialized = true;
}

void particle_system_cleanup(void)
{
    if (!s_particleSystemInitialized) {
        return;
    }

    tpx_destroy();

    s_particleSystemInitialized = false;
}

bool particle_system_is_initialized(void)
{
    return s_particleSystemInitialized;
}