#ifndef LETTERBOX_UTILITY_H
#define LETTERBOX_UTILITY_H

#include <stdbool.h>

// Letterbox animation states
typedef enum {
    LETTERBOX_STATE_HIDDEN,      // Bars are completely hidden
    LETTERBOX_STATE_SHOWING,     // Bars are animating in (moving into view)
    LETTERBOX_STATE_VISIBLE,     // Bars are fully visible
    LETTERBOX_STATE_HIDING       // Bars are animating out (moving out of view)
} LetterboxState;

// Initialize the letterbox system
void letterbox_init(void);

// Update the letterbox animation (call every frame)
void letterbox_update(void);

// Show the letterbox bars (with optional animation)
void letterbox_show(bool animate);

// Hide the letterbox bars (with animation)
void letterbox_hide(void);

// Check if letterbox is currently visible or animating
bool letterbox_is_visible(void);
bool letterbox_is_animating(void);

// Get current state
LetterboxState letterbox_get_state(void);

// Draw the letterbox bars (call during draw phase)
void letterbox_draw(void);

#endif

