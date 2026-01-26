#ifndef BOSS_AI_H
#define BOSS_AI_H

#include "boss.h"
#include "boss_sfx.h"

// AI module - decides intent (states/attacks)
// Must NOT include tiny3d animation headers

void boss_ai_init(Boss* boss);
void boss_ai_update(Boss* boss, BossIntent* out_intent);

#endif // BOSS_AI_H


