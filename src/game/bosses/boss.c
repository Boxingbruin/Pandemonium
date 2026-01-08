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
#include "scene.h"
#include "character.h"
#include "general_utility.h"
#include "globals.h"
#include "utilities/collision_mesh.h"

// Forward declarations for internal functions
static void boss_apply_intent(Boss* boss, const BossIntent* intent);
static void boss_update_transforms(Boss* boss);
static void boss_update_movement(Boss* boss, float dt);

// Boss structure is defined in boss.h

// Global boss instance
static Boss* g_boss = NULL;

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
    
    // Static state for strafe direction (persists across frames)
    static float lastStrafeDir = 1.0f; // 1.0 = right, -1.0 = left
    static float strafeDirTimer = 0.0f;
    
    switch (boss->state) {
        case BOSS_STATE_INTRO:
        case BOSS_STATE_NEUTRAL:
            // No movement
            maxSpeed = 0.0f;
            break;
            
        case BOSS_STATE_CHASE:
            // Move toward player (for when far away)
            if (dist > 0.0f) {
                desiredX = dx / dist;
                desiredZ = dz / dist;
            }
            maxSpeed = SPEED_CHASE;
            break;
            
        case BOSS_STATE_STRAFE:
            // Strafing behavior: move perpendicular to direction to character
            // Match character's lateral movement
            if (dist > 0.0f) {
                // Normalize direction to character
                float toCharX = dx / dist;
                float toCharZ = dz / dist;
                
                // Get character's velocity to determine strafe direction
                float charVelX, charVelZ;
                character_get_velocity(&charVelX, &charVelZ);
                
                // Calculate perpendicular directions (left and right relative to boss->character)
                // Left perpendicular: (-toCharZ, toCharX)
                // Right perpendicular: (toCharZ, -toCharX)
                float leftX = -toCharZ;
                float leftZ = toCharX;
                float rightX = toCharZ;
                float rightZ = -toCharX;
                
                // Determine which direction the character is moving laterally
                // Project character velocity onto left/right perpendicular vectors
                float leftDot = charVelX * leftX + charVelZ * leftZ;
                float rightDot = charVelX * rightX + charVelZ * rightZ;
                
                // Choose strafe direction - stick to a direction more strongly
                // Use hysteresis: require strong opposite movement to change direction
                const float STRONG_MOVEMENT_THRESHOLD = 80.0f;
                const float MIN_DIRECTION_TIME = 3.0f;
                const float OPPOSITE_DIRECTION_MULTIPLIER = 2.0f;
                
                strafeDirTimer += dt;
                
                // Only consider changing direction if enough time has passed
                float lateralMovement = fabsf(leftDot) > fabsf(rightDot) ? fabsf(leftDot) : fabsf(rightDot);
                
                if (strafeDirTimer >= MIN_DIRECTION_TIME) {
                    // Determine desired direction based on character movement
                    float desiredDir = 0.0f;
                    if (fabsf(leftDot) > fabsf(rightDot)) {
                        desiredDir = (leftDot > 0.0f) ? -1.0f : 1.0f;
                    } else {
                        desiredDir = (rightDot > 0.0f) ? 1.0f : -1.0f;
                    }
                    
                    // Only change if character is moving strongly in OPPOSITE direction to current strafe
                    if (desiredDir * lastStrafeDir < 0.0f) {
                        // Character moving opposite to current strafe - require very strong movement
                        if (lateralMovement > STRONG_MOVEMENT_THRESHOLD * OPPOSITE_DIRECTION_MULTIPLIER) {
                            lastStrafeDir = desiredDir;
                            strafeDirTimer = 0.0f; // Reset timer after direction change
                        }
                    }
                }
                
                // Apply strafe direction
                if (lastStrafeDir > 0.0f) {
                    desiredX = rightX;
                    desiredZ = rightZ;
                } else {
                    desiredX = leftX;
                    desiredZ = leftZ;
                }
                
                // Blend in some forward movement if character is far away
                if (dist > boss->orbitRadius + 5.0f) {
                    float forwardBlend = fminf(1.0f, (dist - boss->orbitRadius) / 20.0f);
                    desiredX = desiredX * (1.0f - forwardBlend * 0.3f) + toCharX * forwardBlend * 0.3f;
                    desiredZ = desiredZ * (1.0f - forwardBlend * 0.3f) + toCharZ * forwardBlend * 0.3f;
                    // Normalize
                    float len = sqrtf(desiredX * desiredX + desiredZ * desiredZ);
                    if (len > 0.0f) {
                        desiredX /= len;
                        desiredZ /= len;
                    }
                }
            }
            maxSpeed = SPEED_STRAFE;
            break;
            
        case BOSS_STATE_CHARGE:
            // Charge toward locked targeting position
            {
                float targetDx = boss->lockedTargetingPos[0] - boss->pos[0];
                float targetDz = boss->lockedTargetingPos[2] - boss->pos[2];
                float targetDist = sqrtf(targetDx*targetDx + targetDz*targetDz);
                // Stop movement when very close to target to prevent oscillation
                if (targetDist > 10.0f) {
                    desiredX = targetDx / targetDist;
                    desiredZ = targetDz / targetDist;
                    maxSpeed = SPEED_CHARGE;
                } else {
                    // Close enough - stop moving
                    desiredX = 0.0f;
                    desiredZ = 0.0f;
                    maxSpeed = 0.0f;
                }
            }
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
        boss->state != BOSS_STATE_ROAR_STOMP) {
        
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
        // During strafe/chase, smoothly face the character (don't snap)
        extern Character character;
        float faceDx = character.pos[0] - boss->pos[0];
        float faceDz = character.pos[2] - boss->pos[2];
        float targetAngle = -atan2f(-faceDz, faceDx) + T3D_PI; // T3D_PI
        
        // Smoothly rotate toward target angle
        float currentAngle = boss->rot[1];
        float angleDelta = targetAngle - currentAngle;
        
        // Normalize angle delta to [-PI, PI]
        while (angleDelta > T3D_PI) angleDelta -= 2.0f * T3D_PI;
        while (angleDelta < -T3D_PI) angleDelta += 2.0f * T3D_PI;
        
        // Apply smooth rotation with turn rate
        float maxTurnRate = boss->turnRate * dt;
        if (angleDelta > maxTurnRate) angleDelta = maxTurnRate;
        else if (angleDelta < -maxTurnRate) angleDelta = -maxTurnRate;
        
        boss->rot[1] = currentAngle + angleDelta;
    } else if (boss->state == BOSS_STATE_CHARGE) {
        // During charge, smoothly face the locked targeting position
        float faceDx = boss->lockedTargetingPos[0] - boss->pos[0];
        float faceDz = boss->lockedTargetingPos[2] - boss->pos[2];
        float faceDist = sqrtf(faceDx*faceDx + faceDz*faceDz);
        
        // Only rotate if we're not too close to the target (prevents oscillation)
        if (faceDist > 10.0f) {
            float targetAngle = -atan2f(-faceDz, faceDx) + T3D_PI;
            
            // Smoothly rotate toward target angle
            float currentAngle = boss->rot[1];
            float angleDelta = targetAngle - currentAngle;
            
            // Normalize angle delta to [-PI, PI]
            while (angleDelta > T3D_PI) angleDelta -= 2.0f * T3D_PI;
            while (angleDelta < -T3D_PI) angleDelta += 2.0f * T3D_PI;
            
            // Apply smooth rotation with turn rate
            float maxTurnRate = boss->turnRate * dt;
            if (angleDelta > maxTurnRate) angleDelta = maxTurnRate;
            else if (angleDelta < -maxTurnRate) angleDelta = -maxTurnRate;
            
            boss->rot[1] = currentAngle + angleDelta;
        }
        // If too close, stop rotating (keep current rotation)
    } else if (boss->state >= BOSS_STATE_POWER_JUMP && boss->state != BOSS_STATE_CHARGE) {
        // Attack states handle their own rotation
        // (handled by attack handlers, except charge which is handled above)
    } else {
        // Default: face movement direction (for other states)
        float targetAngle = atan2f(-boss->velX, boss->velZ);
        float currentAngle = boss->rot[1];
        float angleDelta = targetAngle - currentAngle;
        while (angleDelta > T3D_PI) angleDelta -= 2.0f * T3D_PI;
        while (angleDelta < -T3D_PI) angleDelta += 2.0f * T3D_PI;
        
        float maxTurnRate = boss->turnRate * dt;
        if (angleDelta > maxTurnRate) angleDelta = maxTurnRate;
        else if (angleDelta < -maxTurnRate) angleDelta = -maxTurnRate;
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
    }
}

void boss_init(Boss* boss) {
    if (!boss) return;
    
    // Load model
    T3DModel* bossModel = t3d_model_load("rom:/boss/boss_anim.t3dm"); 
    boss->model = bossModel;
    
    // Create skeletons
    T3DSkeleton* skeleton = malloc_uncached(sizeof(T3DSkeleton));
    *skeleton = t3d_skeleton_create(bossModel);
    boss->skeleton = skeleton;
    
    T3DSkeleton* skeletonBlend = malloc_uncached(sizeof(T3DSkeleton));
    *skeletonBlend = t3d_skeleton_clone(skeleton, false);
    boss->skeletonBlend = skeletonBlend;
    
    // Create animations
    const int animationCount = 10;
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
        "FlipAttack1"
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
        false  // FlipAttack - one-shot
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
    
    // Start with idle animation - ensure it's properly set up
    if (animationCount > 0) {
        // Set current animation index
        boss->currentAnimation = 0;
        boss->currentAnimState = BOSS_ANIM_IDLE;
        // Animation is already attached to skeleton from the loop above
        t3d_anim_set_playing(animations[0], true);
    }
    
    // Create display list
    rspq_block_begin();
    t3d_model_draw_skinned(bossModel, skeleton);
    rspq_block_t* dpl = rspq_block_end();
    boss->dpl = dpl;
    
    // Initialize transform
    boss->pos[0] = 0.0f;
    boss->pos[1] = 0.0f;
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
    boss->capsuleCollider.radius = 14.0f;
    
    // Find Hand-Right bone index
    boss->handRightBoneIndex = t3d_skeleton_find_bone(skeleton, "Hand-Right");
    
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
    
    // Initialize combat stats
    boss->name = "Destroyer of Worlds";
    boss->maxHealth = 100.0f;
    boss->health = 100.0f;
    boss->phaseIndex = 1;
    
    // Initialize movement
    boss->velX = 0.0f;
    boss->velZ = 0.0f;
    boss->currentSpeed = 0.0f;
    boss->turnRate = 8.0f;
    boss->orbitRadius = 6.0f;
    
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
    boss->roarStompCooldown = 0.0f;
    boss->trackingSlamCooldown = 0.0f;
    
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
    
    boss->state = BOSS_STATE_INTRO;
    boss->stateTimer = 0.0f;
    boss->health = boss->maxHealth;
    boss->phaseIndex = 1;
    
    // Reset all cooldowns
    boss->attackCooldown = 0.0f;
    boss->powerJumpCooldown = 0.0f;
    boss->comboCooldown = 0.0f;
    boss->comboStarterCooldown = 0.0f;
    boss->roarStompCooldown = 0.0f;
    boss->trackingSlamCooldown = 0.0f;
    
    // Reset combo starter state
    boss->comboStarterCompleted = false;
    
    // Reset attack state
    boss->currentAttackHasHit = false;
    boss->currentAttackName = NULL;
    boss->attackNameDisplayTimer = 0.0f;
    boss->animationTransitionTimer = 0.0f;
    
    // Reset targeting
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
    
    // Reset velocity
    boss->velX = 0.0f;
    boss->velZ = 0.0f;
    
    // Reset animation state
    boss->isAttacking = false;
    boss->attackAnimTimer = 0.0f;
    
    boss_ai_init(boss);
    boss_anim_init(boss);
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
            }
        }
        free(boss->animations);
    }
    
    if (boss->modelMat) {
        rspq_wait();
        free_uncached(boss->modelMat);
    }
    
    if (boss->dpl) {
        rspq_wait();
        rspq_block_free((rspq_block_t*)boss->dpl);
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
}
