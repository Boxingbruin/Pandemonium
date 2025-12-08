#include "animation_utility.h"
#include "general_utility.h"
#include "game_time.h"

static float shake_magnitude = 0.0f;  // How strong the shake is (units)
static struct { float x, y; } shake_offset = {0.0f, 0.0f}; // Current frame's shake offset
static float shake_accumulator = 0;
static float shake_interval = 0.05;

void animation_utility_reset(void)
{
    // Reset all values
    shake_magnitude = 0.0f;
    shake_offset.x = 0.0f;
    shake_offset.y = 0.0f;
    shake_accumulator = 0;
}

float animation_utility_ease_in_out_expo(float t) 
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    if (t < 0.5f)
        return powf(2.0f, 20.0f * t - 10.0f) / 2.0f;
    return (2.0f - powf(2.0f, -20.0f * t + 10.0f)) / 2.0f;
}

void animation_utility_screen_shake_update(void)
{
    shake_accumulator += deltaTime;

    if (shake_accumulator >= shake_interval)
    {
        shake_offset.x = ((int32_t)(rand_custom_u32() % 201) - 100) * (shake_magnitude / 100.0f);
        shake_offset.y = ((int32_t)(rand_custom_u32() % 201) - 100) * (shake_magnitude / 100.0f);
        shake_accumulator = 0;
    }
}

void animation_utility_set_screen_shake_mag(float magnitude)
{
    shake_magnitude = magnitude;
}

float animation_utility_get_shake_offset_x()
{
    return shake_offset.x;
}

float animation_utility_get_shake_offset_y()
{
    return shake_offset.y;
}


void animation_utility_rotate_around_point_xz(float result[3], const float center[3], float radius, float angleRadians) 
{
    result[0] = center[0] + cosf(angleRadians) * radius;
    result[1] = center[1];
    result[2] = center[2] + sinf(angleRadians) * radius;
}