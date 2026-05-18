#ifndef SCENE_H
#define SCENE_H

#include <stdbool.h>
#include <t3d/t3d.h>
#include <sprite.h>

// CutsceneState now lives with the cutscene manager.
#include "cutscene_manager.h"

typedef enum {
    GAME_STATE_PLAYING,
    GAME_STATE_MENU,
    GAME_STATE_DEAD,
    GAME_STATE_VICTORY,
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

// Boot helpers
// Runs startup logos (skipped in DEV_MODE) and restores display/rdpq state.
// Must be called after audio initialization and before first scene draws.
void scene_boot_logos(void);

// Cutscene state functions. `scene_is_cutscene_active` is a one-line wrapper
// around `cutscene_manager_is_active()` kept for the few callers in
// character.c / boss_render.c / menu_controller.c that already use it.
bool scene_is_cutscene_active(void);
bool scene_is_boss_active(void);

// The A-button sprite is loaded by scene.c because it's reused by the dialog
// "press A" prompt and the title-screen button hint, not just by cutscenes.
// The cutscene manager reads it through this accessor when drawing the skip
// overlay (both for in-engine cutscenes and FMV playback).
sprite_t *scene_get_a_button_sprite(void);

// Game state functions
GameState scene_get_game_state(void);
void scene_set_game_state(GameState state);
bool scene_is_menu_active(void);

// Title helpers
void scene_begin_title_transition(void);

// Dev-only warps (used by the DEV_SCENE_FLOW menu).
// Skip straight into the phase-1 boss fight, or drop the boss to ~42% HP so
// the next hit triggers the phase-2 cutscene chain.
void scene_dev_warp_to_fight(void);
void scene_dev_warp_to_pre_phase2(void);

// Room collision functions
bool scene_check_room_bounds(float posX, float posY, float posZ);

#endif
