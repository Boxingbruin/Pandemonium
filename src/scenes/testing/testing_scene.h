#ifndef TESTING_SCENE_H
#define TESTING_SCENE_H

#include <t3d/t3d.h>

void testing_scene_init(void);
void testing_scene_reset(void);
void testing_scene_restart(void);
void testing_scene_update(void);
void testing_scene_fixed_update(void);
void testing_scene_draw(T3DViewport *viewport);
void testing_scene_cleanup(void);

#endif
