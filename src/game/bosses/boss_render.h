#ifndef BOSS_RENDER_H
#define BOSS_RENDER_H

#include "boss.h"

// Render module - handles drawing and debug visualization
// Read-only access to Boss state

void boss_render_draw(Boss* boss);
void boss_render_debug(Boss* boss, void* viewport);  // T3DViewport* but avoiding header dependency

#endif // BOSS_RENDER_H


