#ifndef LIGHTNING_FX
#define LIGHTNING_FX

#include <stdint.h>

typedef struct LightningFX LightningFX;

LightningFX* lightning_fx_create(const char* rom_model_path);

void lightning_fx_destroy(LightningFX* fx);
void lightning_fx_strike(LightningFX* fx, float x, float y, float z, float yaw);
void lightning_fx_update(LightningFX* fx, float dt);
void lightning_fx_draw(LightningFX* fx);

#endif
