#ifndef DEV_H
#define DEV_H

#include <stdbool.h>

extern bool showingCollisionMesh;
extern bool debugDraw;

void dev_tools_init(void);
void dev_models_init(void);

void dev_update(void);
void dev_draw_update(T3DViewport *viewport);
void dev_draw_debug_update(T3DViewport *viewport);
void dev_frame_update(void);
void dev_controller_update(void);
void dev_frames_end_update(void);

// Memory debug mode
void dev_draw_memory_debug(void);

// Check if dev menu is currently open
bool dev_menu_is_open(void);

#endif