#include <libdragon.h>

#include "joypad_utility.h"

joypad_inputs_t joypad;
joypad_buttons_t btn;
joypad_buttons_t rel;

void joypad_utility_init(void)
{
    joypad_init();
    joypad = joypad_get_inputs(JOYPAD_PORT_1);
    btn = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    rel = joypad_get_buttons_released(JOYPAD_PORT_1);
}

void joypad_update(void)
{
    joypad_poll();
    joypad = joypad_get_inputs(JOYPAD_PORT_1);
    btn = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    rel = joypad_get_buttons_released(JOYPAD_PORT_1);
    if(joypad.stick_x < 10 && joypad.stick_x > -10)joypad.stick_x = 0;
    if(joypad.stick_y < 10 && joypad.stick_y > -10)joypad.stick_y = 0;
}