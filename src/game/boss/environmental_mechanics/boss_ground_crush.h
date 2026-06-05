#ifndef BOSS_GROUND_CRUSH_H
#define BOSS_GROUND_CRUSH_H

void boss_ground_crush_init(void);
void boss_ground_crush_reset(void);
void boss_ground_crush_update(float dt);
void boss_ground_crush_draw(void);
void boss_ground_crush_cleanup(void);

void boss_ground_crush_spawn(float x, float y, float z);

#endif