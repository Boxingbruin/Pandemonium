#include "screen_shake.h"
#include "../utilities/general_utility.h"
#include "../utilities/game_time.h"
#include <math.h>

static float shake_magnitude = 0.0f;  // How strong the shake is (units)
static struct { float x, y; } shake_offset = {0.0f, 0.0f}; // Current frame's shake offset
static float shake_accumulator = 0;
static float shake_interval = 0.05f;
static float shake_decay = 12.0f;     // Exponential decay per second (higher = shorter shake)

void screen_shake_reset(void)
{
    // Reset all values
    shake_magnitude = 0.0f;
    shake_offset.x = 0.0f;
    shake_offset.y = 0.0f;
    shake_accumulator = 0;
}

void screen_shake_update(void)
{
    shake_accumulator += deltaTime;

    // Exponential decay so a single impulse shakes briefly and fades out.
    if (shake_magnitude > 0.0f) {
        shake_magnitude *= expf(-shake_decay * deltaTime);
        if (shake_magnitude < 0.01f) {
            shake_magnitude = 0.0f;
            shake_offset.x = 0.0f;
            shake_offset.y = 0.0f;
        }
    }

    if (shake_accumulator >= shake_interval)
    {
        shake_offset.x = ((int32_t)(rand_custom_u32() % 201) - 100) * (shake_magnitude / 100.0f);
        shake_offset.y = ((int32_t)(rand_custom_u32() % 201) - 100) * (shake_magnitude / 100.0f);
        shake_accumulator = 0;
    }
}

void screen_shake_set_shake_mag(float magnitude)
{
    // Treat this as an impulse/trigger: only increase magnitude, decay handles fade-out.
    if (magnitude > shake_magnitude) {
        shake_magnitude = magnitude;
    }
}

float screen_shake_get_shake_offset_x()
{
    return shake_offset.x;
}

float screen_shake_get_shake_offset_y()
{
    return shake_offset.y;
}
