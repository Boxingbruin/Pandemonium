#ifndef OPENING_CREDITS_SCENE_H
#define OPENING_CREDITS_SCENE_H

#include <t3d/t3d.h>
#include <stdbool.h>

typedef enum {
    OPENING_CREDITS_SCENE_RESULT_NONE = 0,
    OPENING_CREDITS_SCENE_RESULT_DONE,
} OpeningCreditsSceneResult;

void opening_credits_scene_enter(void);
void opening_credits_scene_exit(void);
void opening_credits_scene_update(void);
void opening_credits_scene_draw(T3DViewport *viewport);

OpeningCreditsSceneResult opening_credits_scene_get_result(void);
void opening_credits_scene_clear_result(void);
bool opening_credits_scene_is_active(void);

#endif