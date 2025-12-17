#ifndef SCENE_H
#define SCENE_H

#include <stdbool.h>
#include <t3d/t3d.h>

typedef enum {
    GAME_STATE_PLAYING,
    GAME_STATE_MENU,
    GAME_STATE_DEAD,
    GAME_STATE_VICTORY
} GameState;

void scene_init(void);
void scene_reset(void);
void scene_restart(void);
void scene_update(void);
void scene_fixed_update(void);
void scene_draw(T3DViewport *viewport);
void scene_cleanup(void);

// Cutscene state functions
bool scene_is_cutscene_active(void);
bool scene_is_boss_active(void);

// Game state functions
GameState scene_get_game_state(void);
void scene_set_game_state(GameState state);
bool scene_is_menu_active(void);

#endif