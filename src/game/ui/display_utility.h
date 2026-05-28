
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

// Player stamina bar — green, sits directly below the health bar
void draw_player_stamina_bar(float ratio);

// UI intro animation controls (0.0 = off-screen, 1.0 = fully visible)
void display_utility_set_boss_ui_intro(float progress);
void display_utility_set_player_ui_intro(float progress);

// Snap the boss health bar's recent-damage/heal trail to the current ratio,
// suppressing any leftover trail decay. Call when the HUD has been hidden
// (e.g. through a cutscene) so it doesn't appear to "animate in" on return.
void display_utility_snap_boss_health_trail(float ratio);

void display_utility_solid_black_transition(bool fadeIn, float speed);

#endif