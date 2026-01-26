#ifndef BOSS_ATTACKS_H
#define BOSS_ATTACKS_H

#include "boss.h" // For Boss struct
#include "boss_sfx.h"
// Attack handler module - handles attack-specific logic and mechanics
// This module updates boss state (position, rotation, velocity) during attacks
// but does NOT modify animation state (that's handled by boss_anim.c)

void boss_attacks_update(Boss* boss, float dt);

#endif // BOSS_ATTACKS_H


