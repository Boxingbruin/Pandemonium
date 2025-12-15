#ifndef SCENE_H
#define SCENE_H

#include <stdbool.h>

void scene_init(void);
void scene_update(void);
void scene_fixed_update(void);
void scene_draw(T3DViewport *viewport);
void scene_cleanup(void);

// Cutscene state functions
bool scene_is_cutscene_active(void);
bool scene_is_boss_active(void);

#endif