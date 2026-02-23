#ifndef LIGHTNING_FX
#define LIGHTNING_FX

#include <stdbool.h>

void lightning_fx_system_init(const char* rom_model_path);
void lightning_fx_system_shutdown(void);

void lightning_fx_system_update(float dt);
void lightning_fx_system_draw(void);

void lightning_fx_system_ring_enable(bool enabled);
void lightning_fx_system_ring_config(float rMin, float rMax, float y,
                                     float minIntervalSec, float maxIntervalSec);

//  manual strike
void lightning_fx_system_strike(float x, float y, float z, float yaw);

// bnw trigger
bool lightning_fx_system_is_lit(void);

#endif