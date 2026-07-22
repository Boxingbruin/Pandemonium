#ifndef ROOM_DUST_FX_H
#define ROOM_DUST_FX_H

#include <stdbool.h>
#include <t3d/t3d.h>

void room_dust_fx_init(void);
void room_dust_fx_cleanup(void);
void room_dust_fx_reset(void);

void room_dust_fx_set_enabled(bool enabled);
bool room_dust_fx_is_enabled(void);

void room_dust_fx_update(float dt);
void room_dust_fx_draw(T3DViewport *viewport);

#endif