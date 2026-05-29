#ifndef SCENE_CONTROLLER_H
#define SCENE_CONTROLLER_H

#include <t3d/t3d.h>

typedef enum {
    SCENE_CONTROLLER_SCENE_TITLE = 0,
    SCENE_CONTROLLER_SCENE_GUARDIAN,
} SceneControllerSceneId;

void scene_controller_init(void);
void scene_controller_update(void);
void scene_controller_draw(T3DViewport *viewport);
void scene_controller_restart(void);

SceneControllerSceneId scene_controller_get_active_scene(void);

#endif