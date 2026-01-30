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

T3DVec3 bossWeaponCapA;
T3DVec3 bossWeaponCapB;
float bossWeaponRadius = 1.0f;

bool bossWeaponCollision = false;

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
        boss->pos[0] + boss->capsuleCollider.localCapA.v[0],
        boss->pos[1] + boss->capsuleCollider.localCapA.v[1],
        boss->pos[2] + boss->capsuleCollider.localCapA.v[2],
    }};

    bossCapB = (T3DVec3){{
        boss->pos[0] + boss->capsuleCollider.localCapB.v[0],
        boss->pos[1] + boss->capsuleCollider.localCapB.v[1],
        boss->pos[2] + boss->capsuleCollider.localCapB.v[2],
    }};

    bossRadius = boss->capsuleCollider.radius;

    bossWeaponRadius = 5.0f;
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

    // BOSS
    Boss* boss = boss_get_instance();
    bossCapA = (T3DVec3){{
        boss->pos[0] + boss->capsuleCollider.localCapA.v[0],
        boss->pos[1] + boss->capsuleCollider.localCapA.v[1],
        boss->pos[2] + boss->capsuleCollider.localCapA.v[2],
    }};

    bossCapB = (T3DVec3){{
        boss->pos[0] + boss->capsuleCollider.localCapB.v[0],
        boss->pos[1] + boss->capsuleCollider.localCapB.v[1],
        boss->pos[2] + boss->capsuleCollider.localCapB.v[2],
    }};

    bodyHitboxCollision = scu_capsule_vs_capsule_f(charCapA.v, charCapB.v, charRadius, bossCapA.v, bossCapB.v, bossRadius);

    if(!boss->handAttackColliderActive) return;

    if (!boss || !boss->skeleton || !boss->modelMat) return;
    if (boss->handRightBoneIndex < 0) return;

    T3DSkeleton *sk = (T3DSkeleton*)boss->skeleton;

    const T3DMat4FP *B = &sk->boneMatricesFP[boss->handRightBoneIndex]; // bone in MODEL space
    const T3DMat4FP *M = (const T3DMat4FP*)boss->modelMat;             // model in WORLD space

    // Bone-local points
    const float p0_local[3] = { 0.0f, 0.0f, 0.0f };

    // Choose any axis; you said direction doesn't matter.
    // Try Z first. If it points sideways, swap to X or Y.
    const float len = 640.0f;
    const float p1_local[3] = { -len, 0.0f, 0.0f };

    // 1) bone-local -> MODEL space (apply B)
    float p0_model[3], p1_model[3];
    mat4fp_mul_point_f32_row3_colbasis(B, p0_local, p0_model);
    mat4fp_mul_point_f32_row3_colbasis(B, p1_local, p1_model);

    // 2) MODEL -> WORLD space (apply M)
    float p0_world[3], p1_world[3];
    mat4fp_mul_point_f32_row3_colbasis(M, p0_model, p0_world);
    mat4fp_mul_point_f32_row3_colbasis(M, p1_model, p1_world);

    // 3) Write capsule endpoints (world space)
    
    boss->handAttackCollider.localCapB.v[0] = p1_world[0];
    boss->handAttackCollider.localCapB.v[1] = p1_world[1];
    boss->handAttackCollider.localCapB.v[2] = p1_world[2];

    bossWeaponCapA = (T3DVec3){{
        p0_world[0],
        p0_world[1],
        p0_world[2],
    }};

    bossWeaponCapB = (T3DVec3){{
        p1_world[0],
        p1_world[1],
        p1_world[2],
    }};

    bossWeaponCollision = scu_capsule_vs_capsule_f(bossWeaponCapA.v, bossWeaponCapB.v, bossWeaponRadius, charCapA.v, charCapB.v, charRadius);
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

    Boss* boss = boss_get_instance();

    // Draw boss hand collider
    if(boss->handAttackColliderActive){
        debug_draw_capsule(
            viewport,
            &bossWeaponCapA,
            &bossWeaponCapB,
            bossWeaponRadius,
            DEBUG_COLORS[5]
        );
    }

    if (boss->sphereAttackColliderActive) {
        const float OFFSET = 40.0f;
        float yaw = boss->rot[1];

        float fwdX = cosf(yaw);
        float fwdZ = sinf(yaw);

        T3DVec3 center = {{
            boss->pos[0] - fwdX * OFFSET,
            boss->pos[1],
            boss->pos[2] - fwdZ * OFFSET
        }};

        debug_draw_sphere(
            viewport,
            &center,
            20,
            DEBUG_COLORS[5]
        );
    }
}