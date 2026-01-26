#ifndef BOSS_SFX_H
#define BOSS_SFX_H

#include "boss.h"
#include "audio_controller.h"

extern MultiSfx bossComboAttack1Sfx[];
extern MultiSfx bossFlipAttackSfx[];
extern MultiSfx bossJumpForwardSfx[];
extern MultiSfx bossSlowAttackSfx[];

void boss_play_attack_sfx(Boss *boss, int sfxIndex, float audioTime);
void boss_multi_attack_sfx(Boss *boss, MultiSfx *sfxList, int sfxCount);
void boss_reset_sfx(void);
#endif