#ifndef SCREEN_SHAKE_H
#define SCREEN_SHAKE_H

void screen_shake_reset(void);

void screen_shake_update(void);
float screen_shake_get_shake_offset_x(void);
float screen_shake_get_shake_offset_y(void);
void screen_shake_set_shake_mag(float magnitude);

#endif