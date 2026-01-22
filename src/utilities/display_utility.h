
#ifndef DISPLAY_UTILITY_H
#define DISPLAY_UTILITY_H

#include <libdragon.h>

extern surface_t offscreenBuffer;
extern bool startScreenFade;
// Display utility initialization and cleanup
void display_utility_init(void);
void display_utility_cleanup(void);

// Simple boss health bar API
void draw_boss_health_bar(const char *name, float ratio, float flash);

// Simple player health bar API (smaller, bottom-right positioned)
void draw_player_health_bar(const char *name, float ratio, float flash);

// UI intro animation controls (0.0 = off-screen, 1.0 = fully visible)
void display_utility_set_boss_ui_intro(float progress);
void display_utility_set_player_ui_intro(float progress);

void display_utility_solid_black_transition(bool fadeIn, float speed);

#endif