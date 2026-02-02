#include <libdragon.h>
#include <t3d/t3d.h>
#include <math.h>

#include "character.h"
#include "game/bosses/boss.h"

#include "simple_collision_utility.h"
#include "debug_draw.h"
#include "dev.h"

// ------------------------------------------------------------
// State (debug + collision endpoints)
// ------------------------------------------------------------
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

T3DVec3 charWeaponCapA;
T3DVec3 charWeaponCapB;
float   charWeaponRadius = 2.0f;
bool    charWeaponCollision = false;

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
static inline bool circle_vs_circle_push_xz(
    float ax, float az, float ar,
    float bx, float bz, float br,
    float out_push[3], float out_n[3])
{
    float dx = ax - bx;
    float dz = az - bz;

    float r = ar + br;
    float d2 = dx*dx + dz*dz;

    if (d2 >= r*r) return false;

    float d = sqrtf(d2);

    float nx, nz;
    if (d > 1e-6f) {
        nx = dx / d;
        nz = dz / d;
    } else {
        // perfectly stacked; pick a stable normal
        nx = 1.0f;
        nz = 0.0f;
        d = 0.0f;
    }

    float pen = r - d;

    out_n[0] = nx; out_n[1] = 0.0f; out_n[2] = nz;
    out_push[0] = nx * pen;
    out_push[1] = 0.0f;
    out_push[2] = nz * pen;

    return true;
}

static inline void update_character_capsule_world(void)
{
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

    // NOTE: if you scale the character, do it here (your scene code does this elsewhere)
    charRadius = character.capsuleCollider.radius; // * character.scale[0];
}

static inline bool update_boss_capsule_world(Boss **outBoss)
{
    Boss* boss = boss_get_instance();
    if (!boss) return false;

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

    bossRadius = boss->capsuleCollider.radius; // * boss->scale[0] (if boss has scale)

    *outBoss = boss;
    return true;
}

void collision_init(void)
{
    bodyHitboxCollision = false;
    bossWeaponCollision = false;
    charWeaponCollision = false;

    update_character_capsule_world();

    Boss* boss = NULL;
    if (update_boss_capsule_world(&boss)) {
        bossWeaponRadius = 5.0f;
    }

    // Ensure weapon radius is sane even before first update
    charWeaponRadius = 2.0f;
}

void collision_update(void) // Disable viewport after development
{
    update_character_capsule_world();

    Boss* boss = NULL;
    if (!update_boss_capsule_world(&boss)) {
        bodyHitboxCollision = false;
        bossWeaponCollision = false;
        charWeaponCollision = false;
        return;
    }

    // ------------------------------------------------------------
    // BODY vs BODY: resolve in XZ (stable + cheap)
    // ------------------------------------------------------------
    {
        // midpoints as centers (works even if caps aren't centered on pos)
        float charX = 0.5f * (charCapA.v[0] + charCapB.v[0]);
        float charZ = 0.5f * (charCapA.v[2] + charCapB.v[2]);

        float bossX = 0.5f * (bossCapA.v[0] + bossCapB.v[0]);
        float bossZ = 0.5f * (bossCapA.v[2] + bossCapB.v[2]);

        float push[3], n[3];
        bodyHitboxCollision = circle_vs_circle_push_xz(
            charX, charZ, charRadius,
            bossX, bossZ, bossRadius,
            push, n
        );

        if (bodyHitboxCollision) {
            // push character out
            character.pos[0] += push[0];
            character.pos[2] += push[2];

            // keep our debug capsule in sync this frame
            charCapA.v[0] += push[0]; charCapA.v[2] += push[2];
            charCapB.v[0] += push[0]; charCapB.v[2] += push[2];

            // slide: remove inward velocity component along the normal
            float vx, vz;
            character_get_velocity(&vx, &vz);

            float vn = vx * n[0] + vz * n[2];
            if (vn < 0.0f) {
                vx -= vn * n[0];
                vz -= vn * n[2];
                character_set_velocity_xz(vx, vz);
            }
        }
    }

    // ------------------------------------------------------------
    // BOSS HAND WEAPON collider (debug + hit test)
    // (DO NOT early-return, or we skip character weapon debug)
    // ------------------------------------------------------------
    bossWeaponCollision = false;

    if (boss->handAttackColliderActive &&
        boss->skeleton && boss->modelMat &&
        boss->handRightBoneIndex >= 0)
    {
        T3DSkeleton *sk = (T3DSkeleton*)boss->skeleton;

        const T3DMat4FP *B = &sk->boneMatricesFP[boss->handRightBoneIndex]; // bone in MODEL space
        const T3DMat4FP *M = (const T3DMat4FP*)boss->modelMat;             // model in WORLD space

        // Bone-local points
        const float p0_local[3] = { 0.0f, 0.0f, 0.0f };

        // Capsule segment length in bone-local space
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

        // 3) Store endpoints (WORLD space) for testing + debug draw
        bossWeaponCapA = (T3DVec3){{ p0_world[0], p0_world[1], p0_world[2] }};
        bossWeaponCapB = (T3DVec3){{ p1_world[0], p1_world[1], p1_world[2] }};

        bossWeaponCollision = scu_capsule_vs_capsule_f(
            bossWeaponCapA.v, bossWeaponCapB.v, bossWeaponRadius,
            charCapA.v, charCapB.v, charRadius
        );
    }

    // ------------------------------------------------------------
    // CHARACTER HAND WEAPON collider (debug + hit test)
    // ------------------------------------------------------------
    charWeaponCollision = false;

    {
        // collision_system.c can't see character.c's static bone index,
        // so we find/cache it here.
        static int s_charSwordBoneIndex = -1;

        if (character.skeleton && character.modelMat) {

            if (s_charSwordBoneIndex < 0) {
                s_charSwordBoneIndex = t3d_skeleton_find_bone((T3DSkeleton*)character.skeleton, "Hand-Right");
                // If your bone name differs, change it here.
            }

            if (s_charSwordBoneIndex >= 0) {
                T3DSkeleton *sk = (T3DSkeleton*)character.skeleton;

                const T3DMat4FP *B = &sk->boneMatricesFP[s_charSwordBoneIndex]; // bone in MODEL space
                const T3DMat4FP *M = (const T3DMat4FP*)character.modelMat;      // model in WORLD space

                const float p0_local[3] = { 0.0f, 0.0f, 0.0f };

                // Match your character.c sword values
                const float len = 640.0f;
                const float p1_local[3] = { -len, 0.0f, 0.0f };

                float p0_model[3], p1_model[3];
                mat4fp_mul_point_f32_row3_colbasis(B, p0_local, p0_model);
                mat4fp_mul_point_f32_row3_colbasis(B, p1_local, p1_model);

                float p0_world[3], p1_world[3];
                mat4fp_mul_point_f32_row3_colbasis(M, p0_model, p0_world);
                mat4fp_mul_point_f32_row3_colbasis(M, p1_model, p1_world);

                charWeaponCapA = (T3DVec3){{ p0_world[0], p0_world[1], p0_world[2] }};
                charWeaponCapB = (T3DVec3){{ p1_world[0], p1_world[1], p1_world[2] }};

                // Make sure radius is visible
                charWeaponRadius = 2.0f;

                // Debug collision target: boss body capsule
                charWeaponCollision = scu_capsule_vs_capsule_f(
                    charWeaponCapA.v, charWeaponCapB.v, charWeaponRadius,
                    bossCapA.v, bossCapB.v, bossRadius
                );
            }
        }
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
        T3DVec3 mid = {{
            0.5f * (charCapA.v[0] + charCapB.v[0]),
            0.5f * (charCapA.v[1] + charCapB.v[1]),
            0.5f * (charCapA.v[2] + charCapB.v[2]),
        }};
        debug_draw_cross(viewport, &mid, 5.0f, DEBUG_COLORS[0]);
    }

    Boss* boss = boss_get_instance();
    if (!boss) return;

    // Draw boss hand collider
    if (boss->handAttackColliderActive) {
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

    // Draw player weapon collider (always)
    debug_draw_capsule(
        viewport,
        &charWeaponCapA,
        &charWeaponCapB,
        charWeaponRadius,
        DEBUG_COLORS[5]
    );

    if (charWeaponCollision) {
        T3DVec3 mid = {{
            0.5f * (charWeaponCapA.v[0] + charWeaponCapB.v[0]),
            0.5f * (charWeaponCapA.v[1] + charWeaponCapB.v[1]),
            0.5f * (charWeaponCapA.v[2] + charWeaponCapB.v[2]),
        }};
        debug_draw_cross(viewport, &mid, 6.0f, DEBUG_COLORS[0]);
    }
}