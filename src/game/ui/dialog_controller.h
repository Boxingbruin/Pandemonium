#ifndef DIALOG_CONTROLLER_H
#define DIALOG_CONTROLLER_H

#include <libdragon.h>
#include <stdbool.h>

// Dialog controller initialization and cleanup
void dialog_controller_init(void);
void dialog_controller_reset(void);
void dialog_controller_free(void);

// Dialog management
void dialog_controller_speak(const char* text, int style, float activeTime, bool interactable, bool end);
bool dialog_controller_speaking(void);
void dialog_controller_stop_speaking(void);
// Immediately end the current dialog line (so callers can advance to the next line).
void dialog_controller_skip(void);

// Update and draw functions
void dialog_controller_update(void);
void dialog_controller_draw(bool isVertical, int x, int y, int boxWidth, int boxHeight);

#endif