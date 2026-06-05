#ifndef SCENE_BOUNDS_H
#define SCENE_BOUNDS_H

#include <stdint.h>

#include <t3d/t3d.h>

#include "../../utilities/simple_collision_utility.h"

const SCU_OBB *scene_bounds_get_room_obbs(void);
int scene_bounds_get_room_obb_count(void);

bool scene_bounds_check_room(float posX, float posY, float posZ);

void scene_bounds_draw_obb_debug(
    T3DViewport *viewport,
    const SCU_OBB *obb,
    float y,
    uint16_t color
);

void scene_bounds_draw_debug(T3DViewport *viewport);

#endif