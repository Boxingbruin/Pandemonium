#include "boss.h"
#include "boss_ai.h"
#include "boss_anim.h"
#include "boss_render.h"
#include "boss_attacks.h"

#include <libdragon.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>
#include <t3d/t3dmodel.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "game_time.h"
#include "game_math.h"
#include "character.h"
#include "globals.h"
#include "../../fx/sword_trail.h"

// Forward declarations for internal functions
static void boss_apply_intent(Boss* boss, const BossIntent* intent);
static void boss_update_transforms(Boss* boss);
static void boss_update_movement(Boss* boss, float dt);
static inline void boss_update_shadow_mat(Boss* boss);

// Global boss instance
static Boss* g_boss = NULL;

// Shared blob shadow model for the boss
static T3DModel* s_bossShadowModel = NULL;

// Boss shadow tuning
static const float BOSS_SHADOW_GROUND_Y = -1.0f;
static const float BOSS_SHADOW_Y_OFFSET = 0.2f;
static const float BOSS_SHADOW_BASE_ALPHA = 120.0f;
static const float BOSS_SHADOW_SHRINK_AMOUNT = 0.35f;
static const float BOSS_JUMP_REF_HEIGHT = 120.0f;
static const float BOSS_SHADOW_SIZE_MULT = 4.05f;

void boss_turn_towards_yaw(Boss *boss, float targetYaw, float maxTurn)
{
    float cur = boss->rot[1];
    float d = wrap_pi(targetYaw - cur);

    if (d >  maxTurn) d =  maxTurn;
    if (d < -maxTurn) d = -maxTurn;

    boss->rot[1] = cur + d;
}

void boss_turn_towards_player(Boss *boss, float dt, float turnScalar)
{
    float dx = character.pos[0] - boss->pos[0];
    float dz = character.pos[2] - boss->pos[2];
    if (dx == 0.0f && dz == 0.0f) return;

    float targetYaw = -atan2f(-dz, dx) + T3D_PI;
    float maxTurn = boss->turnRate * turnScalar * dt;
    boss_turn_towards_yaw(boss, targetYaw, maxTurn);
}

// Sword trail sampling for boss: use the same bone-local segment as the weapon collider.
static inline bool boss_weapon_world_segment(const Boss* boss, float outBase[3], float outTip[3])
{
    if (!boss) return false;
    if (!boss->skeleton || !boss->modelMat) return false;
    if (boss->handRightBoneIndex < 0) return false;

    T3DSkeleton *sk = (T3DSkeleton*)boss->skeleton;
    const T3DMat4FP *B = &sk->boneMatricesFP[boss->handRightBoneIndex];
    const T3DMat4FP *M = (const T3DMat4FP*)boss->modelMat;

    const float p0_local[3] = { 0.0f, 0.0f, 0.0f };
    const float len = 640.0f;
    const float p1_local[3] = { -len, 0.0f, 0.0f };

    float p0_model[3], p1_model[3];
    mat4fp_mul_point_f32_row3_colbasis(B, p0_local, p0_model);
    mat4fp_mul_point_f32_row3_colbasis(B, p1_local, p1_model);

    mat4fp_mul_point_f32_row3_colbasis(M, p0_model, outBase);
    mat4fp_mul_point_f32_row3_colbasis(M, p1_model, outTip);

    return true;
}

bool boss_get_bone_world_pos(const Boss* boss, int boneIndex, T3DVec3 *outWorld)
{
    if (!outWorld) return false;

    *outWorld = (T3DVec3){{0.0f, 0.0f, 0.0f}};

    if (!boss) return false;
    if (!boss->skeleton || !boss->modelMat) return false;
    if (boneIndex < 0) return false;

    T3DSkeleton *skel = (T3DSkeleton*)boss->skeleton;

    const T3DMat4FP *boneMat = &skel->boneMatricesFP[boneIndex];
    const T3DMat4FP *modelMat = (const T3DMat4FP*)boss->modelMat;

    const float boneLocal[3] = {0.0f, 0.0f, 0.0f};

    float boneModel[3];
    float boneWorld[3];

    mat4fp_mul_point_f32_row3_colbasis(boneMat, boneLocal, boneModel);
    mat4fp_mul_point_f32_row3_colbasis(modelMat, boneModel, boneWorld);

    *outWorld = (T3DVec3){{boneWorld[0], boneWorld[1], boneWorld[2]}};

    return true;
}

bool boss_get_head_world_pos(const Boss* boss, T3DVec3 *outWorld)
{
    if (!boss) return false;
    return boss_get_bone_world_pos(boss, boss->headBoneIndex, outWorld);
}

bool boss_get_waist_world_pos(const Boss* boss, T3DVec3 *outWorld)
{
    if (!boss) return false;
    return boss_get_bone_world_pos(boss, boss->waistBoneIndex, outWorld);
}

bool boss_get_lower_leg_left_world_pos(const Boss* boss, T3DVec3 *outWorld)
{
    if (!boss) return false;
    return boss_get_bone_world_pos(boss, boss->lowerLegLeftBoneIndex, outWorld);
}

bool boss_get_lower_leg_right_world_pos(const Boss* boss, T3DVec3 *outWorld)
{
    if (!boss) return false;
    return boss_get_bone_world_pos(boss, boss->lowerLegRightBoneIndex, outWorld);
}

T3DVec3 boss_get_fallback_lock_focus_point(const Boss* boss)
{
    if (!boss) {
        return (T3DVec3){{0.0f, 0.0f, 0.0f}};
    }

    return (T3DVec3){{boss->pos[0], boss->pos[1] + 40.0f, boss->pos[2]}};
}

// Apply intent from AI to animation system
static void boss_apply_intent(Boss* boss, const BossIntent* intent)
{
    if (!intent) return;

    if (intent->anim_req) {
        boss_anim_request(
            boss,
            intent->anim,
            intent->start_time,
            intent->force_restart,
            intent->priority
        );
    }
}

// Update transforms (matrices, hitboxes, bone attachments)
static void boss_update_transforms(Boss* boss)
{
    if (!boss || !boss->modelMat) return;

    T3DMat4FP* mat = (T3DMat4FP*)boss->modelMat;
    t3d_mat4fp_from_srt_euler(mat, boss->scale, boss->rot, boss->pos);

    boss_update_shadow_mat(boss);
}

// Update movement and physics
static void boss_update_movement(Boss* boss, float dt)
{
    if (!boss) return;

    extern Character character;

    float dx = character.pos[0] - boss->pos[0];
    float dz = character.pos[2] - boss->pos[2];
    float dist = sqrtf(dx * dx + dz * dz);

    const float ACCEL = 7.0f;
    const float FRICTION = 10.0f;
    const float SPEED_CHASE = 60.0f;
    const float SPEED_ORBIT = boss->phaseIndex == 1 ? 90.0f : 120.0f;
    const float SPEED_STRAFE = boss->phaseIndex == 1 ? 100.0f : 120.0f;

    float desiredX = 0.0f;
    float desiredZ = 0.0f;
    float maxSpeed = 0.0f;

    switch (boss->state) {
        case BOSS_STATE_INTRO:
        case BOSS_STATE_NEUTRAL:
            maxSpeed = 0.0f;
            break;

        case BOSS_STATE_DEAD:
            boss->velX = 0.0f;
            boss->velZ = 0.0f;
            return;

        case BOSS_STATE_CHASE:
            if (dist > 0.0f) {
                desiredX = dx / dist;
                desiredZ = dz / dist;
            }
            maxSpeed = SPEED_CHASE;
            break;

        case BOSS_STATE_STRAFE:
            if (dist > 0.0f) {
                float toCharX = dx / dist;
                float toCharZ = dz / dist;

                float leftX = -toCharZ;
                float leftZ = toCharX;
                float rightX = toCharZ;
                float rightZ = -toCharX;

                if (boss->strafeDirection > 0.0f) {
                    desiredX = rightX;
                    desiredZ = rightZ;
                } else {
                    desiredX = leftX;
                    desiredZ = leftZ;
                }

                if (dist > boss->orbitRadius + 5.0f) {
                    float forwardBlend = fminf(1.0f, (dist - boss->orbitRadius) / 20.0f);

                    desiredX = desiredX * (1.0f - forwardBlend * 0.3f) + toCharX * forwardBlend * 0.3f;
                    desiredZ = desiredZ * (1.0f - forwardBlend * 0.3f) + toCharZ * forwardBlend * 0.3f;

                    float len = sqrtf(desiredX * desiredX + desiredZ * desiredZ);
                    if (len > 0.0f) {
                        desiredX /= len;
                        desiredZ /= len;
                    }
                }
            }
            maxSpeed = SPEED_STRAFE;
            break;

        case BOSS_STATE_RECOVER:
            maxSpeed = SPEED_ORBIT * 0.5f;
            break;

        default:
            break;
    }

    if (boss->state != BOSS_STATE_POWER_JUMP &&
        boss->state != BOSS_STATE_FLIP_ATTACK &&
        boss->state != BOSS_STATE_COMBO_STARTER &&
        boss->state != BOSS_STATE_TRACKING_SLAM &&
        boss->state != BOSS_STATE_COMBO_ATTACK &&
        boss->state != BOSS_STATE_COMBO_LUNGE &&
        boss->state != BOSS_STATE_LUNGE_STARTER &&
        boss->state != BOSS_STATE_STOMP &&
        boss->state != BOSS_STATE_ATTACK1) {

        boss->velX += (desiredX * maxSpeed - boss->velX) * ACCEL * dt;
        boss->velZ += (desiredZ * maxSpeed - boss->velZ) * ACCEL * dt;
    }

    float frictionScale = (boss->state >= BOSS_STATE_POWER_JUMP) ? 0.3f : 1.0f;

    if (boss->state == BOSS_STATE_CHASE) {
        frictionScale = 0.8f;
    }

    float k = FRICTION * frictionScale;

    boss->velX *= expf(-k * dt);
    boss->velZ *= expf(-k * dt);

    float nextX = boss->pos[0] + boss->velX * dt;
    float nextZ = boss->pos[2] + boss->velZ * dt;

    boss->pos[0] = nextX;
    boss->pos[2] = nextZ;

    if (boss->state == BOSS_STATE_STRAFE || boss->state == BOSS_STATE_CHASE) {
        float faceDx = character.pos[0] - boss->pos[0];
        float faceDz = character.pos[2] - boss->pos[2];

        float targetAngle = -atan2f(-faceDz, faceDx) + T3D_PI;

        float currentAngle = boss->rot[1];
        float angleDelta = targetAngle - currentAngle;

        while (angleDelta >  T3D_PI) angleDelta -= 2.0f * T3D_PI;
        while (angleDelta < -T3D_PI) angleDelta += 2.0f * T3D_PI;

        float maxTurnRate = boss->turnRate * dt;

        if (angleDelta >  maxTurnRate) angleDelta =  maxTurnRate;
        if (angleDelta < -maxTurnRate) angleDelta = -maxTurnRate;

        boss->rot[1] = currentAngle + angleDelta;
    } else if (
        boss->state == BOSS_STATE_POWER_JUMP ||
        boss->state == BOSS_STATE_FLIP_ATTACK ||
        boss->state == BOSS_STATE_COMBO_STARTER ||
        boss->state == BOSS_STATE_TRACKING_SLAM ||
        boss->state == BOSS_STATE_COMBO_ATTACK ||
        boss->state == BOSS_STATE_COMBO_LUNGE ||
        boss->state == BOSS_STATE_LUNGE_STARTER ||
        boss->state == BOSS_STATE_STOMP ||
        boss->state == BOSS_STATE_ATTACK1
    ) {
        // Attack states handle their own rotation.
    } else {
        float targetAngle = atan2f(-boss->velX, boss->velZ);
        float currentAngle = boss->rot[1];
        float angleDelta = targetAngle - currentAngle;

        while (angleDelta >  T3D_PI) angleDelta -= 2.0f * T3D_PI;
        while (angleDelta < -T3D_PI) angleDelta += 2.0f * T3D_PI;

        float maxTurnRate = boss->turnRate * dt;

        if (angleDelta >  maxTurnRate) angleDelta =  maxTurnRate;
        if (angleDelta < -maxTurnRate) angleDelta = -maxTurnRate;

        boss->rot[1] = currentAngle + angleDelta;
    }
}

Boss* boss_spawn(void)
{
    if (g_boss) {
        return g_boss;
    }

    g_boss = calloc(1, sizeof(Boss));
    if (!g_boss) return NULL;

    boss_init(g_boss);

    return g_boss;
}

static void boss_clear_combat_runtime(Boss* boss)
{
    if (!boss) return;

    boss->handAttackColliderActive = false;
    boss->sphereAttackColliderActive = false;
    boss->isAttacking = false;
    boss->currentAttackHasHit = false;
    boss->velX = 0.0f;
    boss->velZ = 0.0f;
    boss->currentSpeed = 0.0f;
}

static void boss_request_post_defeat_kneel(Boss* boss)
{
    if (!boss) return;

    boss_anim_request(
        boss,
        BOSS_ANIM_PHASE2_WIN_KNEEL,
        0.0f,
        true,
        BOSS_ANIM_PRIORITY_CRITICAL
    );
}

void boss_set_mode(Boss* boss, BossMode mode)
{
    if (!boss) return;

    boss->mode = mode;

    switch (mode) {
        case BOSS_MODE_INACTIVE:
            boss_clear_combat_runtime(boss);
            break;

        case BOSS_MODE_CINEMATIC:
            boss->visible = true;
            boss_clear_combat_runtime(boss);
            break;

        case BOSS_MODE_COMBAT:
            boss->visible = true;
            boss->handAttackColliderActive = false;
            boss->sphereAttackColliderActive = false;
            boss->currentAttackHasHit = false;
            break;

        case BOSS_MODE_POST_DEFEAT:
            boss->visible = true;
            boss_clear_combat_runtime(boss);
            boss_request_post_defeat_kneel(boss);
            break;

        default:
            boss_clear_combat_runtime(boss);
            boss->mode = BOSS_MODE_INACTIVE;
            break;
    }
}

BossMode boss_get_mode(const Boss* boss)
{
    return boss ? boss->mode : BOSS_MODE_INACTIVE;
}

void boss_activate(Boss* boss)
{
    boss_activate_combat(boss);
}

void boss_activate_combat(Boss* boss)
{
    boss_set_mode(boss, BOSS_MODE_COMBAT);
}

void boss_activate_cinematic(Boss* boss)
{
    boss_set_mode(boss, BOSS_MODE_CINEMATIC);
}

void boss_deactivate(Boss* boss)
{
    boss_set_mode(boss, BOSS_MODE_INACTIVE);
}

bool boss_is_active(const Boss* boss)
{
    return boss && boss->mode != BOSS_MODE_INACTIVE;
}

bool boss_is_combat_active(const Boss* boss)
{
    return boss && boss->mode == BOSS_MODE_COMBAT;
}

bool boss_is_cinematic_active(const Boss* boss)
{
    return boss && boss->mode == BOSS_MODE_CINEMATIC;
}

bool boss_is_post_defeat(const Boss* boss)
{
    return boss && boss->mode == BOSS_MODE_POST_DEFEAT;
}

bool boss_is_interactable(const Boss* boss)
{
    return boss && boss->mode == BOSS_MODE_POST_DEFEAT && boss->state == BOSS_STATE_DEAD;
}

static inline void boss_update_shadow_mat(Boss* boss)
{
    if (!boss || !boss->shadowMat) return;

    float h = boss->pos[1] - BOSS_SHADOW_GROUND_Y;
    if (h < 0.0f) h = 0.0f;

    float t = (BOSS_JUMP_REF_HEIGHT > 0.0f) ? (h / BOSS_JUMP_REF_HEIGHT) : 0.0f;
    if (t > 1.0f) t = 1.0f;

    float shrink = 1.0f - BOSS_SHADOW_SHRINK_AMOUNT * t;

    float shadowPos[3] = {
        boss->pos[0],
        BOSS_SHADOW_GROUND_Y + BOSS_SHADOW_Y_OFFSET,
        boss->pos[2]
    };

    float shadowRot[3] = {
        0.0f,
        0.0f,
        0.0f
    };

    float shadowScale[3] = {
        boss->scale[0] * BOSS_SHADOW_SIZE_MULT * shrink,
        boss->scale[1],
        boss->scale[2] * BOSS_SHADOW_SIZE_MULT * shrink
    };

    t3d_mat4fp_from_srt_euler((T3DMat4FP*)boss->shadowMat, shadowScale, shadowRot, shadowPos);
}

void boss_update(Boss* boss)
{
    if (!boss) return;

    float dt = deltaTime;

    switch (boss->mode) {
        case BOSS_MODE_INACTIVE:
            boss_clear_combat_runtime(boss);
            boss_update_transforms(boss);
            sword_trail_instance_update(sword_trail_get_boss(), dt, false, NULL, NULL);
            return;

        case BOSS_MODE_CINEMATIC:
            boss_clear_combat_runtime(boss);
            boss_update_transforms(boss);

            if (boss->damageFlashTimer > 0.0f) {
                boss->damageFlashTimer -= dt;
                if (boss->damageFlashTimer < 0.0f) boss->damageFlashTimer = 0.0f;
            }

            sword_trail_instance_update(sword_trail_get_boss(), dt, false, NULL, NULL);
            return;

        case BOSS_MODE_POST_DEFEAT:
            boss_clear_combat_runtime(boss);
            boss_anim_update(boss);
            boss_update_transforms(boss);

            if (boss->damageFlashTimer > 0.0f) {
                boss->damageFlashTimer -= dt;
                if (boss->damageFlashTimer < 0.0f) boss->damageFlashTimer = 0.0f;
            }

            sword_trail_instance_update(sword_trail_get_boss(), dt, false, NULL, NULL);
            return;

        case BOSS_MODE_COMBAT:
            break;

        default:
            boss_set_mode(boss, BOSS_MODE_INACTIVE);
            return;
    }

    BossIntent intent = {0};
    boss_ai_update(boss, &intent);

    boss_apply_intent(boss, &intent);

    boss_attacks_update(boss, dt);

    boss_update_movement(boss, dt);

    boss_anim_update(boss);

    boss_update_transforms(boss);

    if (boss->damageFlashTimer > 0.0f) {
        boss->damageFlashTimer -= dt;
        if (boss->damageFlashTimer < 0.0f) boss->damageFlashTimer = 0.0f;
    }

    SwordTrail *trail = sword_trail_get_boss();
    float baseW[3], tipW[3];

    if (boss->handAttackColliderActive && boss_weapon_world_segment(boss, baseW, tipW)) {
        sword_trail_instance_update(trail, dt, true, baseW, tipW);
    } else {
        sword_trail_instance_update(trail, dt, false, NULL, NULL);
    }
}

void boss_draw(Boss* boss)
{
    if (!boss) return;
    boss_render_draw(boss);
}

void boss_draw_ui(Boss* boss, void* viewport)
{
    if (!boss_is_combat_active(boss)) return;
    boss_render_debug(boss, viewport);
}

float boss_get_hp(const Boss* boss)
{
    return boss ? boss->health : 0.0f;
}

float boss_get_max_hp(const Boss* boss)
{
    return boss ? boss->maxHealth : 0.0f;
}

int boss_get_phase(const Boss* boss)
{
    return boss ? boss->phaseIndex : 1;
}

BossState boss_get_state(const Boss* boss)
{
    return boss ? boss->state : BOSS_STATE_INTRO;
}

void boss_apply_damage(Boss* boss, float amount)
{
    if (!boss || amount <= 0.0f) return;
    if (boss->mode != BOSS_MODE_COMBAT) return;

    boss->health -= amount;
    if (boss->health < 0.0f) boss->health = 0.0f;

    boss->damageFlashTimer = 0.25f;

    if (boss->health <= 0.0f) {
        boss->state = BOSS_STATE_DEAD;
        boss->stateTimer = 0.0f;
        boss->isAttacking = false;
        boss->attackAnimTimer = 0.0f;
        boss->handAttackColliderActive = false;
        boss->sphereAttackColliderActive = false;
        boss->velX = 0.0f;
        boss->velZ = 0.0f;
        boss->currentSpeed = 0.0f;
        boss_set_mode(boss, BOSS_MODE_POST_DEFEAT);
    }
}

void boss_init(Boss* boss)
{
    if (!boss) return;

    sword_trail_instance_init(sword_trail_get_boss());

    T3DModel* bossModel = t3d_model_load("rom:/boss/boss_anim.t3dm");
    boss->model = bossModel;

    T3DSkeleton* skeleton = malloc(sizeof(T3DSkeleton));
    *skeleton = t3d_skeleton_create(bossModel);
    boss->skeleton = skeleton;

    T3DSkeleton* skeletonBlend = malloc(sizeof(T3DSkeleton));
    *skeletonBlend = t3d_skeleton_clone(skeleton, false);
    boss->skeletonBlend = skeletonBlend;

    const int animationCount = BOSS_ANIM_COUNT;

    const char* animationNames[] = {
        "Idle1",
        "Walk1",
        "SlowAttack1",
        "StrafeLeft1",
        "StrafeRight1",
        "ComboAttack1",
        "JumpForwardAttack1",
        "ComboLunge1",
        "ComboStarter1",
        "FlipAttack1",
        "Phase1Kneel",
        "Phase1KneelCutscene1",
        "LungeStarter1",
        "Attack1",
        "Stomp",
        "WinCollapse",
        "Phase2Collapse",
        "Phase2CollapseIdle",
        "Phase2Reveal",
        "WinKneel"
    };

    const bool animationsLooping[] = {
        true,
        true,
        false,
        true,
        true,
        false,
        false,
        false,
        false,
        false,
        true,
        false,
        false,
        false,
        false,
        false,
        false,
        true,
        false,
        false,
    };

    T3DAnim** animations = malloc_uncached(animationCount * sizeof(T3DAnim*));

    for (int i = 0; i < animationCount; i++) {
        animations[i] = malloc_uncached(sizeof(T3DAnim));
        *animations[i] = t3d_anim_create(bossModel, animationNames[i]);
        t3d_anim_set_looping(animations[i], animationsLooping[i]);
        t3d_anim_set_playing(animations[i], false);
        t3d_anim_attach(animations[i], skeleton);
    }

    boss->animations = (void**)animations;
    boss->animationCount = animationCount;

    boss->currentAnimation = BOSS_ANIM_KNEEL;
    boss->currentAnimState = BOSS_ANIM_KNEEL;
    t3d_anim_set_playing(animations[BOSS_ANIM_KNEEL], true);

    /*
     * boss_render.c records the full custom boss draw lazily as a frozen block
     * at the real draw location, after the scene's live RDP state is active.
     */
    boss->dpl = NULL;

    if (!s_bossShadowModel) {
        s_bossShadowModel = t3d_model_load("rom:/blob_shadow/shadow.t3dm");
    }

    rspq_block_begin();
    t3d_model_draw(s_bossShadowModel);
    rspq_block_t* dpl_shadow = rspq_block_end();
    boss->dpl_shadow = dpl_shadow;

    boss->pos[0] = 0.0f;
    boss->pos[1] = 1.0f;
    boss->pos[2] = 0.0f;

    boss->rot[0] = 0.0f;
    boss->rot[1] = 0.0f;
    boss->rot[2] = 0.0f;

    boss->scale[0] = MODEL_SCALE;
    boss->scale[1] = MODEL_SCALE;
    boss->scale[2] = MODEL_SCALE;

    boss->capsuleCollider.localCapA.v[0] = 0.0f;
    boss->capsuleCollider.localCapA.v[1] = 10.0f;
    boss->capsuleCollider.localCapA.v[2] = 0.0f;

    boss->capsuleCollider.localCapB.v[0] = 0.0f;
    boss->capsuleCollider.localCapB.v[1] = 40.0f;
    boss->capsuleCollider.localCapB.v[2] = 0.0f;

    boss->capsuleCollider.radius = 20.0f;

    boss->handRightBoneIndex = t3d_skeleton_find_bone(skeleton, "Hand-Right");
    boss->waistBoneIndex = t3d_skeleton_find_bone(skeleton, "Waist");
    boss->headBoneIndex = t3d_skeleton_find_bone(skeleton, "Head");
    boss->lowerLegLeftBoneIndex = t3d_skeleton_find_bone(skeleton, "LowerLeg-Left");
    boss->lowerLegRightBoneIndex = t3d_skeleton_find_bone(skeleton, "LowerLeg-Right");

    boss->handAttackCollider.localCapA.v[0] = 0.0f;
    boss->handAttackCollider.localCapA.v[1] = 0.0f;
    boss->handAttackCollider.localCapA.v[2] = 0.0f;

    boss->handAttackCollider.localCapB.v[0] = 0.0f;
    boss->handAttackCollider.localCapB.v[1] = 150.0f;
    boss->handAttackCollider.localCapB.v[2] = 0.0f;

    boss->handAttackCollider.radius = 75.0f;

    boss->handAttackColliderWorldPos[0] = 0.0f;
    boss->handAttackColliderWorldPos[1] = 0.0f;
    boss->handAttackColliderWorldPos[2] = 0.0f;
    boss->handAttackColliderActive = false;

    T3DModel* swordModel = t3d_model_load("rom:/boss/bossSword.t3dm");
    boss->swordModel = swordModel;

    rspq_block_begin();
    t3d_model_draw(swordModel);
    rspq_block_t* swordDpl = rspq_block_end();
    boss->swordDpl = swordDpl;

    T3DMat4FP* swordMatFP = malloc_uncached(sizeof(T3DMat4FP));

    t3d_mat4fp_from_srt_euler(
        swordMatFP,
        (float[3]){1.0f, 1.0f, 1.0f},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, 0.0f, 0.0f}
    );

    boss->swordMatFP = swordMatFP;

    T3DMat4FP* modelMat = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_identity(modelMat);
    boss->modelMat = modelMat;

    T3DMat4FP* shadowMat = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_identity(shadowMat);
    boss->shadowMat = shadowMat;

    boss->name = "Guardian of the Shackled Sun";
    boss->maxHealth = 100.0f;
    boss->health = 100.0f;
    boss->phaseIndex = 1;

    boss->velX = 0.0f;
    boss->velZ = 0.0f;
    boss->currentSpeed = 0.0f;
    boss->turnRate = 8.0f;
    boss->orbitRadius = 6.0f;
    boss->strafeDirection = 1.0f;

    boss->stateTimer = 0.0f;
    boss->attackCooldown = 0.0f;
    boss->damageFlashTimer = 0.0f;
    boss->attackAnimTimer = 0.0f;
    boss->attackNameDisplayTimer = 0.0f;
    boss->hitMessageTimer = 0.0f;
    boss->animationTransitionTimer = 0.0f;
    boss->dustImpactDelayS = 0.0f;

    boss->isAttacking = false;
    boss->currentAttackHasHit = false;
    boss->currentAttackId = BOSS_ATTACK_COUNT;
    boss->currentAttackName = NULL;

    boss->powerJumpCooldown = 0.0f;
    boss->comboCooldown = 0.0f;
    boss->comboStarterCooldown = 0.0f;
    boss->trackingSlamCooldown = 0.0f;
    boss->comboLungeCooldown = 0.0f;
    boss->stompCooldown = 0.0f;
    boss->attack1Cooldown = 0.0f;

    boss->comboStep = 0;
    boss->comboInterrupted = false;
    boss->comboVulnerableTimer = 0.0f;

    boss->swordThrown = false;
    boss->comboStarterSlamHasHit = false;
    boss->comboStarterCompleted = false;

    boss->swordProjectilePos[0] = 0.0f;
    boss->swordProjectilePos[1] = 0.0f;
    boss->swordProjectilePos[2] = 0.0f;

    boss->comboStarterTargetPos[0] = 0.0f;
    boss->comboStarterTargetPos[1] = 0.0f;
    boss->comboStarterTargetPos[2] = 0.0f;

    boss->debugTargetingPos[0] = 0.0f;
    boss->debugTargetingPos[1] = 0.0f;
    boss->debugTargetingPos[2] = 0.0f;

    boss->targetingLocked = false;

    boss->lockedTargetingPos[0] = 0.0f;
    boss->lockedTargetingPos[1] = 0.0f;
    boss->lockedTargetingPos[2] = 0.0f;

    boss->targetingUpdateTimer = 0.0f;

    boss->lastPlayerPos[0] = 0.0f;
    boss->lastPlayerPos[1] = 0.0f;
    boss->lastPlayerPos[2] = 0.0f;

    boss->lastPlayerVel[0] = 0.0f;
    boss->lastPlayerVel[1] = 0.0f;

    boss->flipAttackStartPos[0] = 0.0f;
    boss->flipAttackStartPos[1] = 0.0f;
    boss->flipAttackStartPos[2] = 0.0f;

    boss->flipAttackTargetPos[0] = 0.0f;
    boss->flipAttackTargetPos[1] = 0.0f;
    boss->flipAttackTargetPos[2] = 0.0f;

    boss->flipAttackHeight = 0.0f;
    boss->flipAttackMidReaimed = false;
    boss->flipAttackTravelYaw = boss->rot[1];
    boss->flipAttackPastDist = 0.0f;

    boss->postTurnTimer = 0.0f;
    boss->postTurnDuration = 0.35f;
    boss->postTurnDir = 0;

    boss->visible = true;
    boss->mode = BOSS_MODE_INACTIVE;
    boss->pendingRequests = 0;

    boss_anim_init(boss);
    boss_ai_init(boss);
    boss_draw_init();
}

void boss_reset(Boss* boss)
{
    if (!boss) return;

    sword_trail_instance_reset(sword_trail_get_boss());

    boss->pos[0] = 0.0f;
    boss->pos[1] = 1.0f;
    boss->pos[2] = 0.0f;

    boss->rot[0] = 0.0f;
    boss->rot[1] = 0.0f;
    boss->rot[2] = 0.0f;

    boss->scale[0] = MODEL_SCALE;
    boss->scale[1] = MODEL_SCALE;
    boss->scale[2] = MODEL_SCALE;

    boss->state = BOSS_STATE_INTRO;
    boss->health = boss->maxHealth;
    boss->phaseIndex = 1;

    boss->velX = 0.0f;
    boss->velZ = 0.0f;
    boss->currentSpeed = 0.0f;
    boss->turnRate = 8.0f;
    boss->orbitRadius = 6.0f;
    boss->strafeDirection = 1.0f;

    boss->stateTimer = 0.0f;
    boss->attackCooldown = 0.0f;
    boss->damageFlashTimer = 0.0f;
    boss->attackAnimTimer = 0.0f;
    boss->attackNameDisplayTimer = 0.0f;
    boss->hitMessageTimer = 0.0f;
    boss->animationTransitionTimer = 0.0f;
    boss->dustImpactDelayS = 0.0f;

    boss->isAttacking = false;
    boss->currentAttackHasHit = false;
    boss->currentAttackId = BOSS_ATTACK_COUNT;
    boss->currentAttackName = NULL;

    boss->powerJumpCooldown = 0.0f;
    boss->comboCooldown = 0.0f;
    boss->comboStarterCooldown = 0.0f;
    boss->trackingSlamCooldown = 0.0f;
    boss->comboLungeCooldown = 0.0f;
    boss->stompCooldown = 0.0f;
    boss->attack1Cooldown = 0.0f;

    boss->comboStep = 0;
    boss->comboInterrupted = false;
    boss->comboVulnerableTimer = 0.0f;

    boss->swordThrown = false;
    boss->comboStarterSlamHasHit = false;
    boss->comboStarterCompleted = false;

    boss->swordProjectilePos[0] = 0.0f;
    boss->swordProjectilePos[1] = 0.0f;
    boss->swordProjectilePos[2] = 0.0f;

    boss->comboStarterTargetPos[0] = 0.0f;
    boss->comboStarterTargetPos[1] = 0.0f;
    boss->comboStarterTargetPos[2] = 0.0f;

    boss->debugTargetingPos[0] = 0.0f;
    boss->debugTargetingPos[1] = 0.0f;
    boss->debugTargetingPos[2] = 0.0f;

    boss->targetingLocked = false;

    boss->lockedTargetingPos[0] = 0.0f;
    boss->lockedTargetingPos[1] = 0.0f;
    boss->lockedTargetingPos[2] = 0.0f;

    boss->targetingUpdateTimer = 0.0f;

    boss->lastPlayerPos[0] = 0.0f;
    boss->lastPlayerPos[1] = 0.0f;
    boss->lastPlayerPos[2] = 0.0f;

    boss->lastPlayerVel[0] = 0.0f;
    boss->lastPlayerVel[1] = 0.0f;

    boss->flipAttackStartPos[0] = 0.0f;
    boss->flipAttackStartPos[1] = 0.0f;
    boss->flipAttackStartPos[2] = 0.0f;

    boss->flipAttackTargetPos[0] = 0.0f;
    boss->flipAttackTargetPos[1] = 0.0f;
    boss->flipAttackTargetPos[2] = 0.0f;

    boss->flipAttackHeight = 0.0f;
    boss->flipAttackMidReaimed = false;
    boss->flipAttackTravelYaw = boss->rot[1];
    boss->flipAttackPastDist = 0.0f;

    boss->visible = true;
    boss->mode = BOSS_MODE_INACTIVE;
    boss->pendingRequests = 0;

    boss_ai_init(boss);
    boss_anim_init(boss);

    boss_update_transforms(boss);
}

Boss* boss_get_instance(void)
{
    return g_boss;
}

void boss_free(Boss* boss)
{
    if (!boss) return;

    rspq_wait();

    if (boss->model) {
        t3d_model_free((T3DModel*)boss->model);
    }

    if (boss->skeleton) {
        t3d_skeleton_destroy((T3DSkeleton*)boss->skeleton);
        free(boss->skeleton);
    }

    if (boss->skeletonBlend) {
        t3d_skeleton_destroy((T3DSkeleton*)boss->skeletonBlend);
        free(boss->skeletonBlend);
    }

    if (boss->animations) {
        T3DAnim** anims = (T3DAnim**)boss->animations;

        for (int i = 0; i < boss->animationCount; i++) {
            if (anims[i]) {
                t3d_anim_destroy(anims[i]);
                free_uncached(anims[i]);
                anims[i] = NULL;
            }
        }

        free_uncached(boss->animations);
        boss->animations = NULL;
    }

    if (boss->modelMat) {
        rspq_wait();
        free_uncached(boss->modelMat);
    }

    if (boss->dpl) {
        rspq_wait();
        rspq_block_free((rspq_block_t*)boss->dpl);
    }

    if (boss->dpl_shadow) {
        rspq_wait();
        rspq_block_free((rspq_block_t*)boss->dpl_shadow);
    }

    if (boss->shadowMat) {
        rspq_wait();
        free_uncached((T3DMat4FP*)boss->shadowMat);
    }

    if (boss->swordModel) {
        t3d_model_free((T3DModel*)boss->swordModel);
    }

    if (boss->swordDpl) {
        rspq_wait();
        rspq_block_free((rspq_block_t*)boss->swordDpl);
    }

    if (boss->swordMatFP) {
        rspq_wait();
        free_uncached(boss->swordMatFP);
    }

    if (s_bossShadowModel) {
        t3d_model_free(s_bossShadowModel);
        s_bossShadowModel = NULL;
    }

    if (boss == g_boss) {
        g_boss = NULL;
    }

    free(boss);
}