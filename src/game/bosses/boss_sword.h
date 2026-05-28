#ifndef BOSS_SWORD_H
#define BOSS_SWORD_H

#include <stdbool.h>
#include "boss.h"

// Boss sword module — owns a dedicated sword model (bossSword.t3dm) that the
// boss holds and throws. This is separate from the boss model's own baked
// sword (z_sword1), which is left completely untouched. The module handles:
//   - attaching the sword to the hand bone (ATTACHED)
//   - the in-flight / stuck draw, with world-scale derived from the hand bone
//   - the chain strip between the boss's hand and the sword
//
// The throw choreography itself (windup jump, flight lerp, reel-in, slam) lives
// in boss_attacks.c; it drives the sword purely through the transition helpers
// below (release / land / reattach) plus the shared Boss fields.

// Resource lifecycle.
void boss_sword_init(Boss* boss);   // load model/dpl, alloc matrices, default visual state
void boss_sword_reset(Boss* boss);  // reset visual state only (mode=ATTACHED, no spin)
void boss_sword_free(Boss* boss);   // free model/dpl/matrices

// Bone-attached draw. Call from boss_render_draw (inside the boss's matrix
// slot) — draws our sword model in the boss's hand while it's ATTACHED.
void boss_sword_draw(Boss* boss);

// Thrown-sword draw. Call from the scene at the ROOT matrix level (next to
// msa_draw_visuals, after the boss's matrix slot is popped) — draws the
// in-flight / stuck sword and the chain strip in world space.
void boss_sword_draw_thrown(Boss* boss);

// True while the sword is in the hand (used by AI to gate re-throwing).
bool boss_sword_is_attached(const Boss* boss);

// Flying sword's world-space blade segment (along the flight direction), for
// the in-flight hitbox. Returns false when not in flight.
bool boss_sword_flight_segment(const Boss* boss, float outBase[3], float outTip[3]);

// Flying sword's trail segment (perpendicular to flight so the trail ribbon has
// width). Returns false when not in flight.
bool boss_sword_flight_trail_segment(const Boss* boss, float outBase[3], float outTip[3]);

// Throw transitions, driven by the attack handler:
void boss_sword_release(Boss* boss);   // capture hand-world pos, go in-flight
void boss_sword_land(Boss* boss);      // snap to target, hide
void boss_sword_reattach(Boss* boss);  // return to the hand

#endif // BOSS_SWORD_H
