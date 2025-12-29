#ifndef MENU_CONTROLLER_H
#define MENU_CONTROLLER_H

#include <stdbool.h>

typedef enum {
    MENU_MAIN,
    MENU_AUDIO
} MenuState;

typedef enum {
    MENU_MAIN_RESUME,
    MENU_MAIN_RESTART,
    MENU_MAIN_SETTINGS,
    MENU_MAIN_EXIT,
    MENU_MAIN_COUNT
} MainMenuOption;

typedef enum {
    MENU_AUDIO_MASTER_VOLUME,
    MENU_AUDIO_MUSIC_VOLUME,
    MENU_AUDIO_SFX_VOLUME,
    MENU_AUDIO_MUTE_TOGGLE,
    MENU_AUDIO_BACK,
    MENU_AUDIO_COUNT
} AudioMenuOption;

void menu_controller_init(void);
void menu_controller_update(void);
void menu_controller_draw(void);
void menu_controller_free(void);

bool menu_controller_is_active(void);
void menu_controller_toggle(void);
void menu_controller_close(void);

#endif