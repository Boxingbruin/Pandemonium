#ifndef BUTTON_PROMPT_UTILITY_H
#define BUTTON_PROMPT_UTILITY_H

#include <stdbool.h>

void button_prompt_init(void);
void button_prompt_cleanup(void);

bool button_prompt_has_a_button(void);

int button_prompt_a_icon_width(float targetPx);
int button_prompt_a_icon_height(float targetPx);

void button_prompt_draw_a_icon(int x, int y, float targetPx);
void button_prompt_draw_a_icon_centered(int centerX, int centerY, float targetPx);

void button_prompt_draw_skip_bottom_right(void);

#endif