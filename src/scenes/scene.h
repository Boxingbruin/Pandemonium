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
    CUTSCENE_PHASE1_INTRO_END
} CutsceneState;

typedef enum Scene1Sfx
{
    SCENE1_SFX_TITLE_WALK = 0,

    SCENE1_SFX_BOSS_SWING1, 
    SCENE1_SFX_BOSS_SWING2, 
    SCENE1_SFX_BOSS_SWING3, 
    SCENE1_SFX_BOSS_SWING4, 
    SCENE1_SFX_BOSS_SMASH1, 
    SCENE1_SFX_BOSS_SMASH2, 
    SCENE1_SFX_BOSS_SMASH3, 
    SCENE1_SFX_BOSS_LUNGE, 
    SCENE1_SFX_BOSS_LAND1, 
    SCENE1_SFX_BOSS_LAND2,

    SCENE1_SFX_COUNT
} Scene1Sfx; // TODO: Change this from a generic name to a scene specific name.

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

// Room collision functions
bool scene_check_room_bounds(float posX, float posY, float posZ);

#endif