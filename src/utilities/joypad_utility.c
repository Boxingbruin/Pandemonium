#include <libdragon.h>

#include "joypad_utility.h"
#include "game_time.h"

joypad_inputs_t joypad;
joypad_buttons_t btn;
joypad_buttons_t rel;

static double rumbleStopTimeS = 0.0;
static bool rumbleEnabled = true;

static void drive_rumble(bool active)
{
    if (joypad_get_rumble_supported(JOYPAD_PORT_1)) {
        joypad_set_rumble_active(JOYPAD_PORT_1, active);
    }
}

void joypad_utility_init(void)
{
    joypad_init();
    joypad = joypad_get_inputs(JOYPAD_PORT_1);
    btn = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    rel = joypad_get_buttons_released(JOYPAD_PORT_1);
    rumbleStopTimeS = 0.0;
    rumbleEnabled = true;

    drive_rumble(false);
}

void joypad_update(void)
{
    joypad_poll();
    joypad = joypad_get_inputs(JOYPAD_PORT_1);
    btn = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    rel = joypad_get_buttons_released(JOYPAD_PORT_1);
    if(joypad.stick_x < 10 && joypad.stick_x > -10)joypad.stick_x = 0;
    if(joypad.stick_y < 10 && joypad.stick_y > -10)joypad.stick_y = 0;

    bool shouldRumble = rumbleEnabled && (nowS < rumbleStopTimeS);

    // drive the desired state EVERY frame so the controller and our internal state never diverge
    // matches the per-frame rumble pump used in other N64 titles.
    drive_rumble(shouldRumble);

    if (!shouldRumble) {
        rumbleStopTimeS = 0.0;
    }
}

void joypad_rumble_pulse_seconds(float seconds)
{
    if (seconds <= 0.0f) return;
    if (!rumbleEnabled) return;

    double stopAt = nowS + (double)seconds;
    if (stopAt > rumbleStopTimeS) {
        rumbleStopTimeS = stopAt;
    }
}

void joypad_rumble_stop(void)
{
    rumbleStopTimeS = 0.0;
    drive_rumble(false);
}

void joypad_set_rumble_enabled(bool enabled)
{
    rumbleEnabled = enabled;

    if (!enabled) {
        joypad_rumble_stop();
    }
}

bool joypad_is_rumble_enabled(void)
{
    return rumbleEnabled;
}