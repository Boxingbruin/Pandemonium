#ifndef DISPLAY_UTILITY_H
#define DISPLAY_UTILITY_H

#include <stdbool.h>

#include <libdragon.h>

extern surface_t offscreenBuffer;
extern bool startScreenFade;

// Display utility initialization and cleanup
void display_utility_init(void);
void display_utility_cleanup(void);

// Basic screen-space rectangle helper
void display_manager_draw_rectangle(int x, int y, int width, int height, color_t color);

// Solid black fade transition.
// fadeIn=true fades from black to clear.
// fadeIn=false fades from clear to black.
void display_utility_solid_black_transition(bool fadeIn, float speed);

#endif // DISPLAY_UTILITY_H