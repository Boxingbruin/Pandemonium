#ifndef BOSS_ANIM_H
#define BOSS_ANIM_H

#include "boss.h"

// Animation module - ONLY place that can touch t3d_anim_attach, t3d_skeleton_reset, blend flags, current/prev anim
// This module owns all animation state and tiny3d animation structs

void boss_anim_init(Boss* boss);
void boss_anim_request(Boss* boss, BossAnimState target, float start_time, bool force_restart, BossAnimPriority priority);
void boss_anim_update(Boss* boss);
BossAnimState boss_anim_current(const Boss* boss);

#endif // BOSS_ANIM_H


