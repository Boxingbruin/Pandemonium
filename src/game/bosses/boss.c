#include "boss.h"
#include "boss_ai.h"
#include "boss_anim.h"
#include "boss_render.h"
#include "boss_attacks.h"

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>
#include <t3d/t3dmodel.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "game_time.h"
#include "game_math.h"
#include "scene.h"
#include "character.h"
#include "general_utility.h"
#include "globals.h"
#include "utilities/collision_mesh.h"
#include "utilities/sword_trail.h"

// Forward declarations for internal functions
static void boss_apply_intent(Boss* boss, const BossIntent* intent);
static void boss_update_transforms(Boss* boss);
static void boss_update_movement(Boss* boss, float dt);
static inline void boss_update_shadow_mat(Boss* boss);

// Boss structure is defined in boss.h

// Global boss instance
static Boss* g_boss = NULL;

// Shared blob shadow model for the boss
static T3DModel* s_bossShadowModel = NULL;

// Boss shadow tuning
static const float BOSS_SHADOW_GROUND_Y = -1.0f;  // Match roomY floor level
static const float BOSS_SHADOW_Y_OFFSET = 0.2f;        // prevent z-fighting with the ground
static const float BOSS_SHADOW_BASE_ALPHA = 120.0f;    // alpha when on the ground
static const float BOSS_SHADOW_SHRINK_AMOUNT = 0.35f;  // shrink as boss goes up
static const float BOSS_JUMP_REF_HEIGHT = 120.0f;      // reference height for full shrink
static const float BOSS_SHADOW_SIZE_MULT = 3.6f;       // 2x larger than previous

// Sword trail sampling for boss: use the same bone-local segment as the weapon collider.
static inline bool boss_weapon_world_segment(const Boss* boss, float outBase[3], float outTip[3]) {
    if (!boss) return false;
    if (!boss->skeleton || !boss->modelMat) return false;
    if (boss->handRightBoneIndex < 0) return false;

    T3DSkeleton *sk = (T3DSkeleton*)boss->skeleton;
    const T3DMat4FP *B = &sk->boneMatricesFP[boss->handRightBoneIndex]; // bone in MODEL space
    const T3DMat4FP *M = (const T3DMat4FP*)boss->modelMat;             // model in WORLD space

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

// Apply intent from AI to animation system
static void boss_apply_intent(Boss* boss, const BossIntent* intent) {
    if (!intent) return;
    
    // Apply animation request
    if (intent->anim_req) {
        boss_anim_request(boss, intent->anim, intent->start_time, 
                         intent->force_restart, intent->priority);
    }
    
    // Attack requests are handled by AI state machine
    // Movement/face requests can be handled here if needed
}

// Update transforms (matrices, hitboxes, bone attachments)
static void boss_update_transforms(Boss* boss) {
    if (!boss || !boss->modelMat) return;
    
    // Update transformation matrix
    T3DMat4FP* mat = (T3DMat4FP*)boss->modelMat;
    t3d_mat4fp_from_srt_euler(mat, boss->scale, boss->rot, boss->pos);

    // Update shadow matrix
    boss_update_shadow_mat(boss);
}

// Update movement and physics
static void boss_update_movement(Boss* boss, float dt) {
    if (!boss) return;
    
    // State-specific movement behavior
    extern Character character; // Global character instance
    float dx = character.pos[0] - boss->pos[0];
    float dz = character.pos[2] - boss->pos[2];
    float dist = sqrtf(dx*dx + dz*dz);
    
    const float ACCEL = 7.0f;
    const float FRICTION = 10.0f;
    // Reduced chase speed to be less aggressive
    const float SPEED_CHASE = 60.0f;
    const float SPEED_ORBIT = boss->phaseIndex == 1 ? 90.0f : 120.0f;
    const float SPEED_CHARGE = boss->phaseIndex == 1 ? 220.0f : 280.0f;
    // Slow strafe speed for Dark Souls-style behavior
    const float SPEED_STRAFE = boss->phaseIndex == 1 ? 100.0f : 120.0f;
    
    float desiredX = 0.0f, desiredZ = 0.0f;
    float maxSpeed = 0.0f;
    
    
    
    switch (boss->state) {
        case BOSS_STATE_INTRO:
        case BOSS_STATE_NEUTRAL:
            // No movement
            maxSpeed = 0.0f;
            break;
            
        case BOSS_STATE_DEAD:
            // Fully stop after death (collapse animation should play in place).
            boss->velX = 0.0f;
            boss->velZ = 0.0f;
            return;
            
        case BOSS_STATE_CHASE:
            // Move toward player (for when far away)
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
            // Slow movement
            maxSpeed = SPEED_ORBIT * 0.5f;
            break;
            
        default:
            // Attack states handle their own movement (or no movement)
            break;
    }
    
    // Apply movement for non-attack states
    // Attack states (POWER_JUMP, FLIP_ATTACK, COMBO_STARTER, TRACKING_SLAM, COMBO_ATTACK, ROAR_STOMP) handle their own movement
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
    
    // Apply friction
    float frictionScale = (boss->state >= BOSS_STATE_POWER_JUMP) ? 0.3f : 1.0f;
    if (boss->state == BOSS_STATE_CHASE) {
        frictionScale = 0.8f; // Keep speed during pursuit to match player pace
    }
    float k = FRICTION * frictionScale;
    boss->velX *= expf(-k * dt);
    boss->velZ *= expf(-k * dt);
    
    // Update position with room-bounds collision (slide by axis)
    float nextX = boss->pos[0] + boss->velX * dt;
    float nextZ = boss->pos[2] + boss->velZ * dt;

    float sx = boss->scale[0];
    // X axis
    // if (!collision_mesh_check_bounds_capsule(
    //         nextX, boss->pos[1], boss->pos[2],
    //         boss->capsuleCollider.localCapA.v[0], boss->capsuleCollider.localCapA.v[1], boss->capsuleCollider.localCapA.v[2],
    //         boss->capsuleCollider.localCapB.v[0], boss->capsuleCollider.localCapB.v[1], boss->capsuleCollider.localCapB.v[2],
    //         boss->capsuleCollider.radius, sx
    //     )) {
    boss->pos[0] = nextX;
    // } else {
    //     boss->velX = 0.0f;
    // }

    // // Z axis
    // if (!collision_mesh_check_bounds_capsule(
    //         boss->pos[0], boss->pos[1], nextZ,
    //         boss->capsuleCollider.localCapA.v[0], boss->capsuleCollider.localCapA.v[1], boss->capsuleCollider.localCapA.v[2],
    //         boss->capsuleCollider.localCapB.v[0], boss->capsuleCollider.localCapB.v[1], boss->capsuleCollider.localCapB.v[2],
    //         boss->capsuleCollider.radius, sx
    //     )) {
    boss->pos[2] = nextZ;
    // } else {
    //     boss->velZ = 0.0f;
    // }

    // Update facing direction
    if (boss->state == BOSS_STATE_STRAFE || boss->state == BOSS_STATE_CHASE) {
        // During strafe/chase, smoothly face the character
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
    }
    
    else if (
        boss->state == BOSS_STATE_POWER_JUMP ||
        boss->state == BOSS_STATE_FLIP_ATTACK ||
        boss->state == BOSS_STATE_COMBO_STARTER ||
        boss->state == BOSS_STATE_TRACKING_SLAM ||
        boss->state == BOSS_STATE_COMBO_ATTACK ||
        boss->state == BOSS_STATE_COMBO_LUNGE ||   
        boss->state == BOSS_STATE_LUNGE_STARTER  ||
        boss->state == BOSS_STATE_STOMP ||
        boss->state == BOSS_STATE_ATTACK1
    ) {
        // Attack states: rotation is handled by boss_attacks_* (do nothing here)
    }
    else {
        // Default: face movement direction
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

// Public API implementation
Boss* boss_spawn(void) {
    if (g_boss) {
        return g_boss;  // Already spawned
    }
    
    g_boss = calloc(1, sizeof(Boss));
    if (!g_boss) return NULL;
    
    boss_init(g_boss);
    return g_boss;
}

static inline void boss_update_shadow_mat(Boss* boss)
{
    if (!boss || !boss->shadowMat) return;

    float h = boss->pos[1] - BOSS_SHADOW_GROUND_Y;
    if (h < 0.0f) h = 0.0f;

    float t = (BOSS_JUMP_REF_HEIGHT > 0.0f) ? (h / BOSS_JUMP_REF_HEIGHT) : 0.0f;
    if (t > 1.0f) t = 1.0f;

    float shrink = 1.0f - BOSS_SHADOW_SHRINK_AMOUNT * t;

    float shadowPos[3]   = { boss->pos[0], BOSS_SHADOW_GROUND_Y + BOSS_SHADOW_Y_OFFSET, boss->pos[2] };
    float shadowRot[3]   = { 0.0f, 0.0f, 0.0f };
    float shadowScale[3] = {
        boss->scale[0] * BOSS_SHADOW_SIZE_MULT * shrink,
        boss->scale[1],
        boss->scale[2] * BOSS_SHADOW_SIZE_MULT * shrink
    };

    t3d_mat4fp_from_srt_euler((T3DMat4FP*)boss->shadowMat, shadowScale, shadowRot, shadowPos);
}


// Public API implementation
void boss_update(Boss* boss) {
    if (!boss) return;
    
    float dt = deltaTime;
    
    // Strict update order:
    // 1. AI decides intent
    BossIntent intent = {0};
    boss_ai_update(boss, &intent);
    
    // 2. Apply intent (only place that calls boss_anim_request)
    boss_apply_intent(boss, &intent);
    
    // 3. Attack handlers update (attack-specific logic, position, rotation, velocity)
    boss_attacks_update(boss, dt);
    
    // 4. Movement and physics update (velocity, acceleration, collision)
    boss_update_movement(boss, dt);
    
    // 5. Animation system updates (blend timers, attach clips, update skeleton)
    boss_anim_update(boss);
    
    // 6. Update transforms (matrices, hitboxes, etc.)
    boss_update_transforms(boss);

    // Boss sword trail: emit only while the attack collider is active.
    SwordTrail *trail = sword_trail_get_boss();
    float baseW[3], tipW[3];
    if (boss->handAttackColliderActive && boss_weapon_world_segment(boss, baseW, tipW)) {
        sword_trail_instance_update(trail, dt, true, baseW, tipW);
    } else {
        sword_trail_instance_update(trail, dt, false, NULL, NULL);
    }
    
    //boss_update_weapon_collider_from_hand(boss);
}

void boss_draw(Boss* boss) {
    if (!boss) return;
    boss_render_draw(boss);
}

void boss_draw_ui(Boss* boss, void* viewport) {
    if (!boss) return;
    boss_render_debug(boss, viewport);
}

float boss_get_hp(const Boss* boss) {
    return boss ? boss->health : 0.0f;
}

float boss_get_max_hp(const Boss* boss) {
    return boss ? boss->maxHealth : 0.0f;
}

int boss_get_phase(const Boss* boss) {
    return boss ? boss->phaseIndex : 1;
}

BossState boss_get_state(const Boss* boss) {
    return boss ? boss->state : BOSS_STATE_INTRO;
}

void boss_apply_damage(Boss* boss, float amount) {
    if (!boss || amount <= 0.0f) return;
    
    boss->health -= amount;
    if (boss->health < 0.0f) boss->health = 0.0f;
    
    boss->damageFlashTimer = 0.3f;
    
    // Set pending stagger request if not already dead
    if (boss->health > 0.0f) {
        boss->pendingRequests |= 0x01;  // BOSS_REQ_STAGGER flag
    }
    
    // Check for death
    if (boss->health <= 0.0f) {
        boss->state = BOSS_STATE_DEAD;
        boss->stateTimer = 0.0f;
        boss->isAttacking = false;
        boss->attackAnimTimer = 0.0f;
        boss->handAttackColliderActive = false;
        boss->sphereAttackColliderActive = false;
        boss->velX = 0.0f;
        boss->velZ = 0.0f;
    }
}

void boss_init(Boss* boss) {
    if (!boss) return;

    // Ensure the boss trail starts clean when the boss is created.
    sword_trail_instance_init(sword_trail_get_boss());
    
    // Load model
    T3DModel* bossModel = t3d_model_load("rom:/boss/boss_anim.t3dm"); 
    boss->model = bossModel;
    
    // Create skeletons
    T3DSkeleton* skeleton = malloc(sizeof(T3DSkeleton));
    *skeleton = t3d_skeleton_create(bossModel);
    boss->skeleton = skeleton;
    
    T3DSkeleton* skeletonBlend = malloc(sizeof(T3DSkeleton));
    *skeletonBlend = t3d_skeleton_clone(skeleton, false);
    boss->skeletonBlend = skeletonBlend;
    
    // Create animations
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
        "WinCollapse"
    };
    const bool animationsLooping[] = {
        true,  // Idle - loop
        true,  // Walk - loop
        false, // SlowAttack - one-shot
        true,  // StrafeLeft - loop
        true,  // StrafeRight - loop
        false, // ComboAttack - one-shot
        false, // JumpForward - one-shot
        false, // ComboLunge - one-shot
        false, // ComboStarter - one-shot
        false,  // FlipAttack - one-shot
        true, // Kneel - loop
        false, // Kneel cutscene "FEAR"
        false, // Lunge Starter
        false, // Attack1
        false, // Stomp
        false, // Collapse - one-shot
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
    if (boss->animationCount <= 0 || boss->animationCount > 64) {
        debugf("BAD boss->animationCount = %d\n", boss->animationCount);
        boss->animationCount = BOSS_ANIM_COUNT;
    }
        
    // Set current animation index
    boss->currentAnimation = BOSS_ANIM_KNEEL;
    boss->currentAnimState = BOSS_ANIM_KNEEL;
    // Animation is already attached to skeleton from the loop above
    t3d_anim_set_playing(animations[BOSS_ANIM_KNEEL], true);

    // Create display list
    rspq_block_begin();
    t3d_model_draw_skinned(bossModel, skeleton);
    rspq_block_t* dpl = rspq_block_end();
    boss->dpl = dpl;

    // Ensure shadow model is loaded once
    if (!s_bossShadowModel) {
        s_bossShadowModel = t3d_model_load("rom:/blob_shadow/shadow.t3dm");
    }
    // Create display list for shadow (no prim color here; set per-frame)
    rspq_block_begin();
    t3d_model_draw(s_bossShadowModel);
    rspq_block_t* dpl_shadow = rspq_block_end();
    boss->dpl_shadow = dpl_shadow;
    
    // Initialize transform
    boss->pos[0] = 0.0f;
    boss->pos[1] = 1.0f;
    boss->pos[2] = 0.0f;
    boss->rot[0] = 0.0f;
    boss->rot[1] = 0.0f;
    boss->rot[2] = 0.0f;
    boss->scale[0] = MODEL_SCALE;
    boss->scale[1] = MODEL_SCALE;
    boss->scale[2] = MODEL_SCALE;
    
    // Initialize capsule collider
    boss->capsuleCollider.localCapA.v[0] = 0.0f;
    boss->capsuleCollider.localCapA.v[1] = 10.0f;
    boss->capsuleCollider.localCapA.v[2] = 0.0f;
    boss->capsuleCollider.localCapB.v[0] = 0.0f;
    boss->capsuleCollider.localCapB.v[1] = 40.0f;
    boss->capsuleCollider.localCapB.v[2] = 0.0f;
    boss->capsuleCollider.radius = 20.0f;
    
    // Find Hand-Right bone index
    boss->handRightBoneIndex = t3d_skeleton_find_bone(skeleton, "Hand-Right");
    
    // Find Spine1 bone index (for z-targeting)
    boss->spine1BoneIndex = t3d_skeleton_find_bone(skeleton, "Spine1");
    
    // Initialize hand attack collider (local space, will be updated during attacks)
    boss->handAttackCollider.localCapA.v[0] = 0.0f;
    boss->handAttackCollider.localCapA.v[1] = 0.0f;
    boss->handAttackCollider.localCapA.v[2] = 0.0f;
    boss->handAttackCollider.localCapB.v[0] = 0.0f;
    boss->handAttackCollider.localCapB.v[1] = 150.0f;  // Length along hand
    boss->handAttackCollider.localCapB.v[2] = 0.0f;
    boss->handAttackCollider.radius = 75.0f;  // Radius for hand/fist
    boss->handAttackColliderWorldPos[0] = 0.0f;
    boss->handAttackColliderWorldPos[1] = 0.0f;
    boss->handAttackColliderWorldPos[2] = 0.0f;
    boss->handAttackColliderActive = false;
    
    // Load sword model
    T3DModel* swordModel = t3d_model_load("rom:/boss/bossSword.t3dm");
    boss->swordModel = swordModel;
    
    // Create display list block for sword
    rspq_block_begin();
    t3d_model_draw(swordModel);
    rspq_block_t* swordDpl = rspq_block_end();
    boss->swordDpl = swordDpl;
    
    // Allocate and initialize sword transform matrix (local transform relative to hand bone)
    T3DMat4FP* swordMatFP = malloc_uncached(sizeof(T3DMat4FP));
    // Initialize with identity, will be set properly based on sword model requirements
    // Scale, rotation, and position offset to align sword with hand
    t3d_mat4fp_from_srt_euler(swordMatFP,
        (float[3]){1.0f, 1.0f, 1.0f},  // Scale - may need adjustment
        (float[3]){0.0f, 0.0f, 0.0f},  // Rotation - may need adjustment
        (float[3]){0.0f, 0.0f, 0.0f}   // Position offset - may need adjustment
    );
    boss->swordMatFP = swordMatFP;
    
    // Initialize model matrix
    T3DMat4FP* modelMat = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_identity(modelMat);
    boss->modelMat = modelMat;

    // Initialize shadow matrix
    T3DMat4FP* shadowMat = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_identity(shadowMat);
    boss->shadowMat = shadowMat;
    
    // Initialize combat stats
    boss->name = "Guardian of the Shackled Sun";
    boss->maxHealth = 100.0f;
    boss->health = 100.0f;
    boss->phaseIndex = 1;
    
    // Initialize movement
    boss->velX = 0.0f;
    boss->velZ = 0.0f;
    boss->currentSpeed = 0.0f;
    boss->turnRate = 8.0f;
    boss->orbitRadius = 6.0f;
    boss->strafeDirection = 1.0f;
    
    // Initialize timers
    boss->stateTimer = 0.0f;
    boss->attackCooldown = 0.0f;
    boss->damageFlashTimer = 0.0f;
    boss->attackAnimTimer = 0.0f;
    boss->attackNameDisplayTimer = 0.0f;
    boss->hitMessageTimer = 0.0f;
    boss->animationTransitionTimer = 0.0f;
    
    // Initialize attack state
    boss->isAttacking = false;
    boss->currentAttackHasHit = false;
    boss->currentAttackId = BOSS_ATTACK_COUNT;
    boss->currentAttackName = NULL;
    
    // Initialize cooldowns
    boss->powerJumpCooldown = 0.0f;
    boss->comboCooldown = 0.0f;
    boss->comboStarterCooldown = 0.0f;
    boss->trackingSlamCooldown = 0.0f;
    boss->comboLungeCooldown = 0.0f;
    boss->stompCooldown   = 0.0f;
    boss->attack1Cooldown = 0.0f;

    // Initialize combo state
    boss->comboStep = 0;
    boss->comboInterrupted = false;
    boss->comboVulnerableTimer = 0.0f;
    
    // Initialize combo starter state
    boss->swordThrown = false;
    boss->comboStarterSlamHasHit = false;
    boss->comboStarterCompleted = false;
    boss->swordProjectilePos[0] = 0.0f;
    boss->swordProjectilePos[1] = 0.0f;
    boss->swordProjectilePos[2] = 0.0f;
    boss->comboStarterTargetPos[0] = 0.0f;
    boss->comboStarterTargetPos[1] = 0.0f;
    boss->comboStarterTargetPos[2] = 0.0f;
    
    // Initialize targeting
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
    
    // Initialize flip attack state
    boss->flipAttackStartPos[0] = 0.0f;
    boss->flipAttackStartPos[1] = 0.0f;
    boss->flipAttackStartPos[2] = 0.0f;
    boss->flipAttackTargetPos[0] = 0.0f;
    boss->flipAttackTargetPos[1] = 0.0f;
    boss->flipAttackTargetPos[2] = 0.0f;
    boss->flipAttackHeight = 0.0f;
    boss->flipAttackMidReaimed = false;
    boss->flipAttackTravelYaw = boss->rot[1];
    boss->flipAttackPastDist  = 0.0f;


    boss->visible = true;
    boss->pendingRequests = 0;
    
    // Initialize animation system
    boss_anim_init(boss);
    
    // Initialize AI
    boss_ai_init(boss);

    boss_draw_init();
}

void boss_reset(Boss* boss) {
    if (!boss) return;

    // Clear any lingering trail samples when restarting the fight.
    sword_trail_instance_reset(sword_trail_get_boss());

    // Restore spawn transform first so any derived state uses the correct basis.
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
    
    // Initialize movement
    boss->velX = 0.0f;
    boss->velZ = 0.0f;
    boss->currentSpeed = 0.0f;
    boss->turnRate = 8.0f;
    boss->orbitRadius = 6.0f;
    boss->strafeDirection = 1.0f;
    
    // Initialize timers
    boss->stateTimer = 0.0f;
    boss->attackCooldown = 0.0f;
    boss->damageFlashTimer = 0.0f;
    boss->attackAnimTimer = 0.0f;
    boss->attackNameDisplayTimer = 0.0f;
    boss->hitMessageTimer = 0.0f;
    boss->animationTransitionTimer = 0.0f;
    
    // Initialize attack state
    boss->isAttacking = false;
    boss->currentAttackHasHit = false;
    boss->currentAttackId = BOSS_ATTACK_COUNT;
    boss->currentAttackName = NULL;
    
    // Initialize cooldowns
    boss->powerJumpCooldown = 0.0f;
    boss->comboCooldown = 0.0f;
    boss->comboStarterCooldown = 0.0f;
    boss->trackingSlamCooldown = 0.0f;
    boss->comboLungeCooldown = 0.0f;
    boss->stompCooldown   = 0.0f;
    boss->attack1Cooldown = 0.0f;

    // Initialize combo state
    boss->comboStep = 0;
    boss->comboInterrupted = false;
    boss->comboVulnerableTimer = 0.0f;
    
    // Initialize combo starter state
    boss->swordThrown = false;
    boss->comboStarterSlamHasHit = false;
    boss->comboStarterCompleted = false;
    boss->swordProjectilePos[0] = 0.0f;
    boss->swordProjectilePos[1] = 0.0f;
    boss->swordProjectilePos[2] = 0.0f;
    boss->comboStarterTargetPos[0] = 0.0f;
    boss->comboStarterTargetPos[1] = 0.0f;
    boss->comboStarterTargetPos[2] = 0.0f;
    
    // Initialize targeting
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
    
    // Initialize flip attack state
    boss->flipAttackStartPos[0] = 0.0f;
    boss->flipAttackStartPos[1] = 0.0f;
    boss->flipAttackStartPos[2] = 0.0f;
    boss->flipAttackTargetPos[0] = 0.0f;
    boss->flipAttackTargetPos[1] = 0.0f;
    boss->flipAttackTargetPos[2] = 0.0f;
    boss->flipAttackHeight = 0.0f;
    boss->flipAttackMidReaimed = false;
    boss->flipAttackTravelYaw = boss->rot[1];
    boss->flipAttackPastDist  = 0.0f;

    boss->pendingRequests = 0;
    
    boss_ai_init(boss);
    boss_anim_init(boss);

    // Ensure matrices reflect the reset transform immediately (important before first update).
    boss_update_transforms(boss);
}

// Get the global boss instance
Boss* boss_get_instance(void) {
    return g_boss;
}

void boss_free(Boss* boss) {
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

                // IMPORTANT: anims[i] was allocated with malloc_uncached
                free_uncached(anims[i]);
                anims[i] = NULL;
            }
        }

        // IMPORTANT: the anim pointer array was allocated with malloc_uncached
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
    
    // Cleanup sword resources
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

    // Free shared shadow model if allocated
    if (s_bossShadowModel) {
        t3d_model_free(s_bossShadowModel);
        s_bossShadowModel = NULL;
    }
}
