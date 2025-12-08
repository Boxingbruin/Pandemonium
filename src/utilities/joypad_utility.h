#ifndef JOYPAD_UTILITY_H
#define JOYPAD_UTILITY_H

#include <libdragon.h>

// Declare global variables as extern so they can be accessed from other files
extern joypad_inputs_t joypad;
extern joypad_buttons_t btn;
extern joypad_buttons_t rel;

// Function prototype for joypad update
void joypad_utility_init(void);
void joypad_update(void);

#endif