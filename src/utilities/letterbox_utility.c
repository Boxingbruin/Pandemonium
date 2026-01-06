#include <libdragon.h>
#include <math.h>
#include "letterbox_utility.h"
#include "globals.h"
#include "game_time.h"

// Letterbox configuration
#define LETTERBOX_BAR_HEIGHT (SCREEN_HEIGHT / 12)  // ~20px on 240p
#define LETTERBOX_ANIMATION_DURATION 0.5f  // 0.5 seconds for animation

// Internal state
static LetterboxState letterboxState = LETTERBOX_STATE_HIDDEN;
static float letterboxAnimationTimer = 0.0f;
static float letterboxProgress = 0.0f;  // 0.0 = hidden, 1.0 = fully visible

void letterbox_init(void) {
    letterboxState = LETTERBOX_STATE_HIDDEN;
    letterboxAnimationTimer = 0.0f;
    letterboxProgress = 0.0f;
}

void letterbox_update(void) {
    switch (letterboxState) {
        case LETTERBOX_STATE_SHOWING:
            letterboxAnimationTimer += deltaTime;
            if (letterboxAnimationTimer >= LETTERBOX_ANIMATION_DURATION) {
                letterboxAnimationTimer = LETTERBOX_ANIMATION_DURATION;
                letterboxProgress = 1.0f;
                letterboxState = LETTERBOX_STATE_VISIBLE;
            } else {
                // Smooth ease-in-out interpolation
                float t = letterboxAnimationTimer / LETTERBOX_ANIMATION_DURATION;
                letterboxProgress = t * t * (3.0f - 2.0f * t);  // Smoothstep
            }
            break;
            
        case LETTERBOX_STATE_HIDING:
            letterboxAnimationTimer += deltaTime;
            if (letterboxAnimationTimer >= LETTERBOX_ANIMATION_DURATION) {
                letterboxAnimationTimer = LETTERBOX_ANIMATION_DURATION;
                letterboxProgress = 0.0f;
                letterboxState = LETTERBOX_STATE_HIDDEN;
            } else {
                // Smooth ease-in-out interpolation
                float t = letterboxAnimationTimer / LETTERBOX_ANIMATION_DURATION;
                float smoothT = t * t * (3.0f - 2.0f * t);  // Smoothstep
                letterboxProgress = 1.0f - smoothT;  // Reverse the progress
            }
            break;
            
        case LETTERBOX_STATE_VISIBLE:
        case LETTERBOX_STATE_HIDDEN:
            // No animation needed
            break;
    }
}

void letterbox_show(bool animate) {
    if (letterboxState == LETTERBOX_STATE_VISIBLE) {
        return;  // Already visible
    }
    
    if (animate) {
        letterboxState = LETTERBOX_STATE_SHOWING;
        letterboxAnimationTimer = 0.0f;
        letterboxProgress = 0.0f;
    } else {
        letterboxState = LETTERBOX_STATE_VISIBLE;
        letterboxProgress = 1.0f;
        letterboxAnimationTimer = 0.0f;
    }
}

void letterbox_hide(void) {
    if (letterboxState == LETTERBOX_STATE_HIDDEN) {
        return;  // Already hidden
    }
    
    // If we're currently showing, reverse direction
    if (letterboxState == LETTERBOX_STATE_SHOWING) {
        // Reverse the animation: calculate remaining time based on current progress
        letterboxAnimationTimer = LETTERBOX_ANIMATION_DURATION * (1.0f - letterboxProgress);
    } else {
        letterboxAnimationTimer = 0.0f;
    }
    
    letterboxState = LETTERBOX_STATE_HIDING;
}

bool letterbox_is_visible(void) {
    return letterboxState == LETTERBOX_STATE_VISIBLE || 
           letterboxState == LETTERBOX_STATE_SHOWING ||
           letterboxState == LETTERBOX_STATE_HIDING;
}

bool letterbox_is_animating(void) {
    return letterboxState == LETTERBOX_STATE_SHOWING || 
           letterboxState == LETTERBOX_STATE_HIDING;
}

LetterboxState letterbox_get_state(void) {
    return letterboxState;
}

void letterbox_draw(void) {
    // Don't draw if completely hidden
    if (letterboxState == LETTERBOX_STATE_HIDDEN && letterboxProgress <= 0.0f) {
        return;
    }
    
    // Calculate current bar height based on animation progress
    float currentBarHeight = LETTERBOX_BAR_HEIGHT * letterboxProgress;
    
    // Don't draw if height is effectively zero
    if (currentBarHeight < 0.5f) {
        return;
    }
    
    // Set up rendering state
    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_set_prim_color(RGBA32(0, 0, 0, 255));
    
    // Draw top bar
    // When progress = 1.0: bar is at y=0 to y=LETTERBOX_BAR_HEIGHT
    // When progress = 0.0: bar is at y=-LETTERBOX_BAR_HEIGHT to y=0 (off screen above)
    // The bar moves up as it shrinks
    float topBarStartY = -LETTERBOX_BAR_HEIGHT * (1.0f - letterboxProgress);
    float topBarEndY = topBarStartY + currentBarHeight;
    rdpq_fill_rectangle(0, (int)topBarStartY, SCREEN_WIDTH, (int)topBarEndY);
    
    // Draw bottom bar
    // When progress = 1.0: bar is at y=SCREEN_HEIGHT-LETTERBOX_BAR_HEIGHT to y=SCREEN_HEIGHT
    // When progress = 0.0: bar is at y=SCREEN_HEIGHT to y=SCREEN_HEIGHT+LETTERBOX_BAR_HEIGHT (off screen below)
    // The bar moves down as it shrinks
    float bottomBarStartY = SCREEN_HEIGHT - currentBarHeight;
    float bottomBarEndY = SCREEN_HEIGHT + LETTERBOX_BAR_HEIGHT * (1.0f - letterboxProgress);
    rdpq_fill_rectangle(0, (int)bottomBarStartY, SCREEN_WIDTH, (int)bottomBarEndY);
}

