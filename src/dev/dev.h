#ifndef DEV_H
#define DEV_H

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

#endif