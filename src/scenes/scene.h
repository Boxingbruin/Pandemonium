#ifndef SCENE_H
#define SCENE_H

#include <stdbool.h>
#include <t3d/t3d.h>

typedef enum {
    GAME_STATE_PLAYING,
    GAME_STATE_MENU,
    GAME_STATE_DEAD,
    GAME_STATE_VICTORY,
    GAME_STATE_TITLE,
    GAME_STATE_TITLE_TRANSITION,
    GAME_STATE_VIDEO
} GameState;

// Cutscene state management
typedef enum {
    CUTSCENE_NONE,
    CUTSCENE_PHASE1_INTRO,
    CUTSCENE_PHASE1_CHAIN_CLOSEUP,
    CUTSCENE_PHASE1_SWORDS_CLOSEUP,
    CUTSCENE_PHASE1_FILLER,
    CUTSCENE_PHASE1_LOYALTY,
    CUTSCENE_PHASE1_FEAR,
    CUTSCENE_PHASE1_BREAK_CHAINS,
    CUTSCENE_PHASE1_FACE_ZOOM_OUT,
    CUTSCENE_PHASE1_INTRO_END,
    CUTSCENE_POST_BOSS_RESTORED
} CutsceneState;

typedef enum {
    VIDEO_PREROLL_NONE = 0,
    VIDEO_PREROLL_FADING_TO_BLACK,
    VIDEO_PREROLL_BLACK_HOLD,
} VideoPrerollState;

void scene_init(void);
void scene_reset(void);
void scene_restart(void);
void scene_update(void);
void scene_fixed_update(void);
void scene_draw(T3DViewport *viewport);
void scene_cleanup(void);

// Simple world-space dust burst (screen-space rendered).
// Intended for impacts/landings. `strength` is a loose scalar (1.0 = normal).
void scene_spawn_dust_burst(float x, float y, float z, float strength);

// Boot helpers
// Runs startup logos (skipped in DEV_MODE) and restores display/rdpq state.
// Must be called after audio initialization and before first scene draws.
void scene_boot_logos(void);

// Cutscene state functions
bool scene_is_cutscene_active(void);
bool scene_is_boss_active(void);

// Game state functions
GameState scene_get_game_state(void);
void scene_set_game_state(GameState state);
bool scene_is_menu_active(void);

// Title helpers
void scene_begin_title_transition(void);

// Room collision functions
bool scene_check_room_bounds(float posX, float posY, float posZ);

#endif