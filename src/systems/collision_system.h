#ifndef COLLISION_SYSTEM_H
#define COLLISION_SYSTEM_H

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmodel.h>

void collision_init(void);
void collision_update(void);
void collision_draw(T3DViewport *viewport);

#endif