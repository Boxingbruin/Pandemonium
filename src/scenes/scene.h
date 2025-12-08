#ifndef SCENE_H
#define SCENE_H

void scene_init(void);
void scene_update(void);
void scene_fixed_update(void);
void scene_draw(T3DViewport *viewport);
void scene_cleanup(void);

#endif