#ifndef SCENE_H
#define SCENE_H

#include <t3d/t3d.h>

typedef enum {
    GAME_STATE_PLAYING,
    GAME_STATE_MENU,
    GAME_STATE_DEAD,
    GAME_STATE_VICTORY,

    /*
     * Temporary compatibility state.
     *
     * The Guardian scene should no longer enter these states internally,
     * but menu_controller_update_title() still uses GAME_STATE_TITLE as a
     * virtual state to drive title-menu behavior.
     *
     * Later, this should probably move out of scene.h entirely.
     */
    GAME_STATE_TITLE,
    GAME_STATE_TITLE_TRANSITION,

    GAME_STATE_VIDEO
} GameState;

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

// Blood burst at a sword/boss impact point. World-space spawn, screen-space billboards.
// Randomly picks from the available blood sprites (large/medium/tiny).
void scene_spawn_blood_burst(float x, float y, float z, float strength);

// Ground "crush" decal under an impact point (world-space quad, depth-tested).
// Intended for boss slam landings. Auto-expires (~3 seconds).
void scene_spawn_ground_crushed(float x, float z);

// Cutscene state functions.
// Kept for character.c / boss_render.c / menu_controller.c callers.
bool scene_is_cutscene_active(void);
bool scene_is_boss_active(void);

// Game state functions.
GameState scene_get_game_state(void);
void scene_set_game_state(GameState state);
bool scene_is_menu_active(void);

// Dev-only warps used by the DEV_SCENE_FLOW menu.
// Skip straight into the phase-1 boss fight, or drop the boss to ~42% HP so
// the next hit triggers the phase-2 cutscene chain.
void scene_dev_warp_to_fight(void);
void scene_dev_warp_to_pre_phase2(void);

#endif