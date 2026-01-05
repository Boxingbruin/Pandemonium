#include <libdragon.h>
#include <t3d/t3d.h>

#include "character.h"
#include "game/bosses/boss.h"

#include "simple_collision_utility.h"
#include "debug_draw.h"
#include "dev.h"

T3DVec3 charCapA;
T3DVec3 charCapB;
float charRadius = 1.0f;
T3DVec3 bossCapA;
T3DVec3 bossCapB;
float bossRadius = 1.0f;

bool bodyHitboxCollision = false;

void collision_init(void)
{
    bodyHitboxCollision = false;

    // CHARACTER
    charCapA = (T3DVec3){{
        character.pos[0] + character.capsuleCollider.localCapA.v[0],
        character.pos[1] + character.capsuleCollider.localCapA.v[1],
        character.pos[2] + character.capsuleCollider.localCapA.v[2],
    }};

    charCapB = (T3DVec3){{
        character.pos[0] + character.capsuleCollider.localCapB.v[0],
        character.pos[1] + character.capsuleCollider.localCapB.v[1],
        character.pos[2] + character.capsuleCollider.localCapB.v[2],
    }};

    charRadius = character.capsuleCollider.radius;

    // BOSS
    Boss* boss = boss_get_instance();
    bossCapA = (T3DVec3){{
        boss->pos[0] + boss->capsuleCollider.localCapA.v[0] * boss->scale[0],
        boss->pos[1] + boss->capsuleCollider.localCapA.v[1] * boss->scale[0],
        boss->pos[2] + boss->capsuleCollider.localCapA.v[2] * boss->scale[0],
    }};

    bossCapB = (T3DVec3){{
        boss->pos[0] + boss->capsuleCollider.localCapB.v[0] * boss->scale[0],
        boss->pos[1] + boss->capsuleCollider.localCapB.v[1] * boss->scale[0],
        boss->pos[2] + boss->capsuleCollider.localCapB.v[2] * boss->scale[0],
    }};

    bossRadius = boss->capsuleCollider.radius * boss->scale[0];
}

void collision_update(void) // Disable viewport after development
{
    // CHARACTER
    charCapA = (T3DVec3){{
        character.pos[0] + character.capsuleCollider.localCapA.v[0],
        character.pos[1] + character.capsuleCollider.localCapA.v[1],
        character.pos[2] + character.capsuleCollider.localCapA.v[2],
    }};

    charCapB = (T3DVec3){{
        character.pos[0] + character.capsuleCollider.localCapB.v[0],
        character.pos[1] + character.capsuleCollider.localCapB.v[1],
        character.pos[2] + character.capsuleCollider.localCapB.v[2],
    }};

    charRadius = character.capsuleCollider.radius;

    // BOSS
    Boss* boss = boss_get_instance();
    bossCapA = (T3DVec3){{
        boss->pos[0] + boss->capsuleCollider.localCapA.v[0] * boss->scale[0],
        boss->pos[1] + boss->capsuleCollider.localCapA.v[1] * boss->scale[0],
        boss->pos[2] + boss->capsuleCollider.localCapA.v[2] * boss->scale[0],
    }};

    bossCapB = (T3DVec3){{
        boss->pos[0] + boss->capsuleCollider.localCapB.v[0] * boss->scale[0],
        boss->pos[1] + boss->capsuleCollider.localCapB.v[1] * boss->scale[0],
        boss->pos[2] + boss->capsuleCollider.localCapB.v[2] * boss->scale[0],
    }};

    bodyHitboxCollision = scu_capsule_vs_capsule_f(charCapA.v, charCapB.v, charRadius, bossCapA.v, bossCapB.v, bossRadius);

    // TODO: Uncomment this when we have a hand attack collider working
    // Draw hand attack collider if bone index is valid (always draw for debugging, use active state for color)
    if (boss->handRightBoneIndex >= 0) {
        //   float handRadius = 300.0f;
        //   float handHalfLen = 600.0f;
        
        //   // Calculate endpoints from center position (capsule along Y axis)
        //   T3DVec3 handCapA = {{
        //       boss->handAttackColliderWorldPos[0],
        //       boss->handAttackColliderWorldPos[1],
        //       boss->handAttackColliderWorldPos[2] + 100.0f,
        //   }};
        //   T3DVec3 handCapB = {{
        //       boss->handAttackColliderWorldPos[0],
        //       boss->handAttackColliderWorldPos[1] + handHalfLen,
        //       boss->handAttackColliderWorldPos[2] + 100.0f,
        //   }};
        
        //   // Draw in red if active, cyan if inactive (for debugging)
        //   uint16_t color = boss->handAttackColliderActive ? DEBUG_COLORS[0] : DEBUG_COLORS[2]; // Red if active, blue if inactive
        //   debug_draw_capsule(viewport, &handCapA, &handCapB, handRadius, color);
    }
}

void collision_draw(T3DViewport *viewport)
{
    if(!debugDraw)
        return;

    rspq_wait();
    debug_draw_capsule(viewport, &charCapA, &charCapB, charRadius, DEBUG_COLORS[1]);
    debug_draw_capsule(viewport, &bossCapA, &bossCapB, bossRadius, DEBUG_COLORS[3]);

    if (bodyHitboxCollision)
    {
        if(debugDraw)
        {
            T3DVec3 mid = {{
                0.5f * (charCapA.v[0] + charCapB.v[0]),
                0.5f * (charCapA.v[1] + charCapB.v[1]),
                0.5f * (charCapA.v[2] + charCapB.v[2]),
            }};
            debug_draw_cross(viewport, &mid, 5.0f, DEBUG_COLORS[0]);
        }
    }
}