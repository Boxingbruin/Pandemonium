#include "game_collision.h"

#include <t3d/t3d.h>
#include <t3d/t3dskeleton.h>

#include "systems/collision_system.h"
#include "game_math.h"
#include "debug_draw.h"   // DEBUG_COLORS
#include "character.h"
#include "game/bosses/boss.h"

// Weapon capsule segment length in bone-local space (matches prior hard-coded values).
#define WEAPON_BONE_LEN   640.0f
#define PLAYER_WEAPON_RADIUS 2.0f
#define BOSS_WEAPON_RADIUS   5.0f

// Build a world-space weapon capsule from a skeleton bone, transforming
// bone-local -> model -> world. Returns the two endpoints.
static void weapon_capsule_from_bone(const T3DSkeleton *sk, const T3DMat4FP *modelMat,
                                     int boneIndex, T3DVec3 *outA, T3DVec3 *outB)
{
    const T3DMat4FP *B = &sk->boneMatricesFP[boneIndex]; // bone in MODEL space
    const T3DMat4FP *M = modelMat;                        // model in WORLD space

    const float p0_local[3] = { 0.0f, 0.0f, 0.0f };
    const float p1_local[3] = { -WEAPON_BONE_LEN, 0.0f, 0.0f };

    float p0_model[3], p1_model[3];
    mat4fp_mul_point_f32_row3_colbasis(B, p0_local, p0_model);
    mat4fp_mul_point_f32_row3_colbasis(B, p1_local, p1_model);

    float p0_world[3], p1_world[3];
    mat4fp_mul_point_f32_row3_colbasis(M, p0_model, p0_world);
    mat4fp_mul_point_f32_row3_colbasis(M, p1_model, p1_world);

    *outA = (T3DVec3){{ p0_world[0], p0_world[1], p0_world[2] }};
    *outB = (T3DVec3){{ p1_world[0], p1_world[1], p1_world[2] }};
}

// ------------------------------------------------------------
// Player (bodyA) callbacks
// ------------------------------------------------------------
static void player_get_capsule(void *ctx, T3DVec3 *outA, T3DVec3 *outB, float *outRadius)
{
    (void)ctx;
    *outA = (T3DVec3){{
        character.pos[0] + character.capsuleCollider.localCapA.v[0],
        character.pos[1] + character.capsuleCollider.localCapA.v[1],
        character.pos[2] + character.capsuleCollider.localCapA.v[2],
    }};
    *outB = (T3DVec3){{
        character.pos[0] + character.capsuleCollider.localCapB.v[0],
        character.pos[1] + character.capsuleCollider.localCapB.v[1],
        character.pos[2] + character.capsuleCollider.localCapB.v[2],
    }};
    *outRadius = character.capsuleCollider.radius;
}

static void player_apply_push(void *ctx, float dx, float dz)
{
    (void)ctx;
    character.pos[0] += dx;
    character.pos[2] += dz;
}

static void player_get_velocity(void *ctx, float *vx, float *vz)
{
    (void)ctx;
    character_get_velocity(vx, vz);
}

static void player_set_velocity(void *ctx, float vx, float vz)
{
    (void)ctx;
    character_set_velocity_xz(vx, vz);
}

static bool player_get_weapon(void *ctx, T3DVec3 *outA, T3DVec3 *outB, float *outRadius)
{
    (void)ctx;
    static int boneIndex = -1;

    if (!character.skeleton || !character.modelMat) return false;
    if (boneIndex < 0) {
        boneIndex = t3d_skeleton_find_bone(character.skeleton, "Hand-Right");
    }
    if (boneIndex < 0) return false;

    weapon_capsule_from_bone(character.skeleton, character.modelMat, boneIndex, outA, outB);
    *outRadius = PLAYER_WEAPON_RADIUS;
    return true;
}

// ------------------------------------------------------------
// Boss (bodyB) callbacks
// ------------------------------------------------------------
static void boss_get_capsule(void *ctx, T3DVec3 *outA, T3DVec3 *outB, float *outRadius)
{
    (void)ctx;
    Boss *boss = boss_get_instance();
    if (!boss) {
        *outA = *outB = (T3DVec3){{0.0f, 0.0f, 0.0f}};
        *outRadius = 0.0f;
        return;
    }
    *outA = (T3DVec3){{
        boss->pos[0] + boss->capsuleCollider.localCapA.v[0],
        boss->pos[1] + boss->capsuleCollider.localCapA.v[1],
        boss->pos[2] + boss->capsuleCollider.localCapA.v[2],
    }};
    *outB = (T3DVec3){{
        boss->pos[0] + boss->capsuleCollider.localCapB.v[0],
        boss->pos[1] + boss->capsuleCollider.localCapB.v[1],
        boss->pos[2] + boss->capsuleCollider.localCapB.v[2],
    }};
    *outRadius = boss->capsuleCollider.radius;
}

static bool boss_get_weapon(void *ctx, T3DVec3 *outA, T3DVec3 *outB, float *outRadius)
{
    (void)ctx;
    Boss *boss = boss_get_instance();
    if (!boss) return false;
    if (!(boss->handAttackColliderActive && boss->skeleton && boss->modelMat &&
          boss->handRightBoneIndex >= 0)) {
        return false;
    }

    weapon_capsule_from_bone((const T3DSkeleton*)boss->skeleton,
                             (const T3DMat4FP*)boss->modelMat,
                             boss->handRightBoneIndex, outA, outB);
    *outRadius = BOSS_WEAPON_RADIUS;
    return true;
}

// ------------------------------------------------------------
// Descriptors + API
// ------------------------------------------------------------
static CollisionBody s_playerBody;
static CollisionBody s_bossBody;

void game_collision_init(void)
{
    s_playerBody = (CollisionBody){
        .ctx                = NULL,
        .get_capsule        = player_get_capsule,
        .apply_push         = player_apply_push,
        .get_velocity_xz    = player_get_velocity,
        .set_velocity_xz    = player_set_velocity,
        .get_weapon_capsule = player_get_weapon,
        .debugColor         = DEBUG_COLORS[1], // green
    };

    s_bossBody = (CollisionBody){
        .ctx                = NULL,
        .get_capsule        = boss_get_capsule,
        .apply_push         = NULL,             // boss is the static pusher
        .get_velocity_xz    = NULL,
        .set_velocity_xz    = NULL,
        .get_weapon_capsule = boss_get_weapon,
        .debugColor         = DEBUG_COLORS[3], // yellow
    };

    collision_init();
}

void game_collision_update(void)
{
    collision_update(&s_playerBody, &s_bossBody);
}

void game_collision_draw(T3DViewport *viewport)
{
    collision_draw(viewport);
}

bool player_weapon_hit_boss(void) { return collision_weapon_a_hit_b(); }
bool boss_weapon_hit_player(void) { return collision_weapon_b_hit_a(); }
