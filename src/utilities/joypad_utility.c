#include <libdragon.h>

#include "joypad_utility.h"
#include "game_time.h"

joypad_inputs_t joypad;
joypad_buttons_t btn;
joypad_buttons_t rel;

static int rumbleFramesRemaining = 0;
static double rumbleStopTimeS = 0.0;
static bool rumbleEnabled = true;

void joypad_utility_init(void)
{
    joypad_init();
    joypad = joypad_get_inputs(JOYPAD_PORT_1);
    btn = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    rel = joypad_get_buttons_released(JOYPAD_PORT_1);
    rumbleFramesRemaining = 0;
    rumbleStopTimeS = 0.0;
    rumbleEnabled = true;

    if (joypad_get_rumble_supported(JOYPAD_PORT_1)) {
        joypad_set_rumble_active(JOYPAD_PORT_1, false);
    }
}

void joypad_update(void)
{
    joypad_poll();
    joypad = joypad_get_inputs(JOYPAD_PORT_1);
    btn = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    rel = joypad_get_buttons_released(JOYPAD_PORT_1);
    if(joypad.stick_x < 10 && joypad.stick_x > -10)joypad.stick_x = 0;
    if(joypad.stick_y < 10 && joypad.stick_y > -10)joypad.stick_y = 0;

    bool shouldRumble = (rumbleFramesRemaining > 0) && (nowS < rumbleStopTimeS) && rumbleEnabled;

    if (!shouldRumble) {
        // Unconditionally drive OFF every frame when we're not supposed to rumble.
        // This catches any case where the stop command was missed (transient disconnect,
        // accessory re-probe, missed frame, etc.).
        if (rumbleFramesRemaining > 0) {
            // Timer just expired
            joypad_rumble_stop();
        } else {
            // Proactively keep motor off (no-op if already off)
            joypad_set_rumble_active(JOYPAD_PORT_1, false);
        }
    }
}

void joypad_rumble_pulse_seconds(float seconds)
{
    if (seconds <= 0.0f) return;
    if (!rumbleEnabled) return;
    if (!joypad_is_connected(JOYPAD_PORT_1)) return;
    if (!joypad_get_rumble_supported(JOYPAD_PORT_1)) return;

    int frames = (int)(seconds * 60.0f + 0.5f);
    if (frames < 1) frames = 1;

    if (frames > rumbleFramesRemaining) {
        rumbleFramesRemaining = frames;
    }

    double stopAt = nowS + (double)seconds;
    if (stopAt > rumbleStopTimeS) {
        rumbleStopTimeS = stopAt;
    }

    joypad_set_rumble_active(JOYPAD_PORT_1, true);
}

void joypad_rumble_stop(void)
{
    rumbleFramesRemaining = 0;
    rumbleStopTimeS = 0.0;

    // Always send OFF — no connection guard here; libdragon handles it safely
    // and a missed stop is the exact bug we are fixing.
    joypad_set_rumble_active(JOYPAD_PORT_1, false);
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