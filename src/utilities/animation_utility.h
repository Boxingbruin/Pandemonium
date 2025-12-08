#ifndef ANIMATION_UTILITY_H
#define ANIMATION_UTILITY_H

void animation_utility_reset(void);

void animation_utility_screen_shake_update(void);
float animation_utility_get_shake_offset_x(void);
float animation_utility_get_shake_offset_y(void);
void animation_utility_set_screen_shake_mag(float magnitude);
float animation_utility_ease_in_out_expo(float t);
void animation_utility_rotate_around_point_xz(float result[3], const float center[3], float radius, float angleRadians);

#endif