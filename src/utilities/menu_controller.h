#ifndef MENU_CONTROLLER_H
#define MENU_CONTROLLER_H

#include <stdbool.h>

typedef enum {
    // Pause menu root (in-game)
    MENU_MAIN,

    // Title menu root (title screen)
    MENU_TITLE,

    // Shared submenus / screens
    MENU_SETTINGS,
    MENU_AUDIO,
    MENU_VIDEO,
    MENU_CONTROLS,
    MENU_CREDITS,
    MENU_LOAD_GAME,
} MenuState;

typedef enum {
    MENU_MAIN_RESUME,
    MENU_MAIN_RESTART,
    MENU_MAIN_SETTINGS,
    MENU_MAIN_CONTROLS,
    MENU_MAIN_COUNT
} MainMenuOption;

typedef enum {
    MENU_TITLE_CONTINUE,
    MENU_TITLE_LOAD_GAME,
    MENU_TITLE_SETTINGS,
    MENU_TITLE_CREDITS,
    MENU_TITLE_COUNT
} TitleMenuOption;

typedef enum {
    MENU_SETTINGS_AUDIO,
    MENU_SETTINGS_VIDEO,
    MENU_SETTINGS_CONTROLS,
    MENU_SETTINGS_BACK,
    MENU_SETTINGS_COUNT
} SettingsMenuOption;

typedef enum {
    MENU_AUDIO_MASTER_VOLUME,
    MENU_AUDIO_MUSIC_VOLUME,
    MENU_AUDIO_SFX_VOLUME,
    MENU_AUDIO_MUTE_TOGGLE,
    MENU_AUDIO_STEREO_MODE,
    MENU_AUDIO_RUMBLE_TOGGLE,
    MENU_AUDIO_BACK,
    MENU_AUDIO_COUNT
} AudioMenuOption;

typedef enum {
    MENU_VIDEO_ASPECT,
    MENU_VIDEO_UI_OVERSCAN_CALIBRATE,
    MENU_VIDEO_BACK,
    MENU_VIDEO_COUNT
} VideoMenuOption;

typedef enum {
    MENU_LOAD_GAME_SLOT_1,
    MENU_LOAD_GAME_SLOT_2,
    MENU_LOAD_GAME_SLOT_3,
    MENU_LOAD_GAME_BACK,
    MENU_LOAD_GAME_COUNT
} LoadGameMenuOption;

typedef enum {
    MENU_ACTION_NONE = 0,

    // Emitted when the title menu selects Continue/Play or a save slot.
    // The title scene consumes this and starts its transition.
    MENU_ACTION_TITLE_START_GAME,

    // Reserved for later cleanup. For now restart can still call scene_restart()
    // directly from the pause menu if you kept that first-pass behavior.
    MENU_ACTION_RESTART_GAME,
} MenuAction;

void menu_controller_init(void);

/*
 * Guardian / in-game update path.
 * Reads scene_get_game_state() internally for pause/menu behavior.
 */
void menu_controller_update(void);

/*
 * Title-screen update path.
 * Forces title-menu behavior without asking the guardian scene for GameState.
 */
void menu_controller_update_title(void);

/*
 * Returns and clears the latest one-shot menu action.
 */
MenuAction menu_controller_consume_action(void);

void menu_controller_draw(void);
void menu_controller_free(void);

bool menu_controller_is_active(void);
bool menu_controller_is_title_submenu_active(void);
bool menu_controller_is_pause_menu_active(void);

void menu_controller_toggle(void);
void menu_controller_close(void);

#endif