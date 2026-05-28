#ifndef GAME_COLLISION_H
#define GAME_COLLISION_H

#include <t3d/t3d.h>

// Game-side wiring for the generic engine collision system. Owns the player and
// boss CollisionBody descriptors (capsule + weapon-bone math) and maps the
// engine's neutral bodyA/bodyB results back to game semantics.
//
// Convention: bodyA = player (dynamic, gets pushed), bodyB = boss (static).

void game_collision_init(void);
void game_collision_update(void);
void game_collision_draw(T3DViewport *viewport);

// Per-frame hit queries (valid after game_collision_update()):
bool player_weapon_hit_boss(void);
bool boss_weapon_hit_player(void);

#endif
