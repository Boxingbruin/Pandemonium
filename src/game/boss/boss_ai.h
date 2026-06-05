#ifndef BOSS_AI_H
#define BOSS_AI_H

#include "boss.h"
#include "boss_sfx.h"

// AI module - decides intent (states/attacks)
// Must NOT include tiny3d animation headers

void boss_ai_init(Boss* boss);
void boss_ai_update(Boss* boss, BossIntent* out_intent);

// Force the boss into the aerial sword barrage attack right now, bypassing
// range / cooldown / consecutive-use gating. Used by the phase 2 transition
// so phase 2 opens with this attack.
void boss_ai_start_aerial_sword_barrage(Boss* boss);

#endif // BOSS_AI_H


