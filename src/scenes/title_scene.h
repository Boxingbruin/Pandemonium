#ifndef TITLE_SCENE_H
#define TITLE_SCENE_H

#include <stdbool.h>
#include <t3d/t3d.h>

typedef enum {
    TITLE_SCENE_RESULT_NONE = 0,
    TITLE_SCENE_RESULT_START_GUARDIAN_INTRO,
} TitleSceneResult;

void title_scene_enter(void);
void title_scene_exit(void);
void title_scene_update(void);
void title_scene_draw(T3DViewport *viewport);

void title_scene_begin_transition(void);

TitleSceneResult title_scene_get_result(void);
void title_scene_clear_result(void);

bool title_scene_is_active(void);
bool title_scene_is_transitioning(void);

#endif