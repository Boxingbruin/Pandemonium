#ifndef DISPLAY_UTILITY_H
#define DISPLAY_UTILITY_H

#include <libdragon.h>

extern surface_t offscreenBuffer;
extern bool startScreenFade;
extern bool displayBloomEnabled;

void display_utility_init(void);
void display_utility_cleanup(void);

void display_manager_draw_rectangle(int x, int y, int width, int height, color_t color);
void display_utility_solid_black_transition(bool fadeIn, float speed);

void display_utility_set_bloom_enabled(bool enabled);
void display_utility_set_bloom_alpha(uint8_t alpha);
void display_utility_apply_postprocess(surface_t *framebuffer);

#endif