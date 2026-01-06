/*
 * boss_attacks.c
 * 
 * Attack handler module - handles attack-specific logic and mechanics
 * Updates boss state (position, rotation, velocity) during attacks
 * Does NOT modify animation state (handled by boss_anim.c)
 */

#include "boss_attacks.h"
#include "boss.h"

#include <math.h>
#include <stdlib.h>

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3dmath.h>

#include "game_time.h"
#include "character.h"
#include "utilities/collision_mesh.h"
#include "utilities/simple_collision_utility.h"
#include "utilities/game_math.h"

// Access global character instance
extern Character character;

// Forward declarations
static void boss_attacks_handle_power_jump(Boss* boss, float dt);
static void boss_attacks_handle_combo(Boss* boss, float dt);
static void boss_attacks_handle_combo_starter(Boss* boss, float dt);
static void boss_attacks_handle_roar_stomp(Boss* boss, float dt);
static void boss_attacks_handle_tracking_slam(Boss* boss, float dt);
static void boss_attacks_handle_charge(Boss* boss, float dt);
static void boss_attacks_handle_flip_attack(Boss* boss, float dt);
static void boss_update_hand_attack_collider(Boss* boss);

// Sound flags shared with AI module
extern bool bossPowerJumpImpactPlayed;
extern bool bossRoarImpactSoundPlayed;

void boss_attacks_update(Boss* boss, float dt) {
    if (!boss) return;
    
    // Check if boss is in an attack state and update hand collider
    bool isAttackState = (boss->state == BOSS_STATE_POWER_JUMP ||
                          boss->state == BOSS_STATE_COMBO_ATTACK ||
                          boss->state == BOSS_STATE_COMBO_STARTER ||
                          boss->state == BOSS_STATE_ROAR_STOMP ||
                          boss->state == BOSS_STATE_TRACKING_SLAM ||
                          boss->state == BOSS_STATE_CHARGE ||
                          boss->state == BOSS_STATE_FLIP_ATTACK);
    
    // Always update collider position for debugging (even when not attacking)
    if (boss->handRightBoneIndex >= 0) {
        boss_update_hand_attack_collider(boss);
    }
    
    if (isAttackState) {
        boss->handAttackColliderActive = true;
    } else {
        boss->handAttackColliderActive = false;
    }
    
    // Route to appropriate attack handler based on state
    switch (boss->state) {
        case BOSS_STATE_POWER_JUMP:
            boss_attacks_handle_power_jump(boss, dt);
            break;
            
        case BOSS_STATE_COMBO_ATTACK:
            boss_attacks_handle_combo(boss, dt);
            break;
            
        case BOSS_STATE_COMBO_STARTER:
            boss_attacks_handle_combo_starter(boss, dt);
            break;
            
        case BOSS_STATE_ROAR_STOMP:
            boss_attacks_handle_roar_stomp(boss, dt);
            break;
            
        case BOSS_STATE_TRACKING_SLAM:
            boss_attacks_handle_tracking_slam(boss, dt);
            break;
            
        case BOSS_STATE_CHARGE:
            boss_attacks_handle_charge(boss, dt);
            break;
            
        case BOSS_STATE_FLIP_ATTACK:
            boss_attacks_handle_flip_attack(boss, dt);
            break;
            
        default:
            // Not an attack state, nothing to do
            break;
    }
}

// Update hand attack collider position based on bone transform
static void boss_update_hand_attack_collider(Boss* boss) {
    if (!boss || boss->handRightBoneIndex < 0) return;
    
    T3DSkeleton* skel = (T3DSkeleton*)boss->skeleton;
    if (!skel || !skel->skeletonRef) return;
    
    // Get bone's transform matrix (in model space, updated by animation system)
    T3DMat4FP* boneMat = &skel->boneMatricesFP[boss->handRightBoneIndex];
    
    // Extract position from bone matrix (bone's position in model space)
    // T3DMat4FP is a fixed-point 4x4 matrix stored as 16 int32_t values
    // Try column-major format first: translation at indices 3, 7, 11 (4th element of each of first 3 columns)
    // If that doesn't work, try row-major: indices 12, 13, 14 (last column)
    int32_t* matData = (int32_t*)boneMat;
    
    // Try column-major (most common in graphics APIs)
    float bonePosModelX = FROM_FIXED(matData[3]);
    float bonePosModelY = FROM_FIXED(matData[7]);
    float bonePosModelZ = FROM_FIXED(matData[11]);
    
    // Transform bone position from model space to world space
    // Apply boss's scale first
    float sx = boss->scale[0];
    float sy = boss->scale[1];
    float sz = boss->scale[2];
    
    float scaledX = bonePosModelX * sx;
    float scaledY = bonePosModelY * sy;
    float scaledZ = bonePosModelZ * sz;
    
    // Apply boss's rotation (Euler angles: rot[0]=pitch, rot[1]=yaw, rot[2]=roll)
    // For now, assuming only Y-axis rotation (yaw) as boss typically only rotates around Y
    float yaw = boss->rot[1];
    float cosY = cosf(yaw);
    float sinY = sinf(yaw);
    
    // Rotate around Y axis
    float rotatedX = scaledX * cosY - scaledZ * sinY;
    float rotatedZ = scaledX * sinY + scaledZ * cosY;
    float rotatedY = scaledY; // Y doesn't change with Y-axis rotation
    
    // Apply boss's world position
    boss->handAttackColliderWorldPos[0] = boss->pos[0] + rotatedX;
    boss->handAttackColliderWorldPos[1] = boss->pos[1] + rotatedY;
    boss->handAttackColliderWorldPos[2] = boss->pos[2] + rotatedZ;
}

// Check collision between hand attack collider and character
static bool boss_check_hand_attack_collision(Boss* boss) {
    if (!boss || !boss->handAttackColliderActive || boss->handRightBoneIndex < 0) {
        return false;
    }
    
    // Get character capsule collider in world space
    float sx = character.scale[0];
    float ax = character.capsuleCollider.localCapA.v[0] * sx;
    float ay = character.capsuleCollider.localCapA.v[1] * sx;
    float az = character.capsuleCollider.localCapA.v[2] * sx;
    float bx = character.capsuleCollider.localCapB.v[0] * sx;
    float by = character.capsuleCollider.localCapB.v[1] * sx;
    float bz = character.capsuleCollider.localCapB.v[2] * sx;
    
    float charCapA[3] = {
        character.pos[0] + ax,
        character.pos[1] + ay,
        character.pos[2] + az
    };
    float charCapB[3] = {
        character.pos[0] + bx,
        character.pos[1] + by,
        character.pos[2] + bz
    };
    float charRadius = character.capsuleCollider.radius * sx;
    
    // Get hand collider - use world position as center, calculate endpoints
    // Transform local capsule endpoints through bone and boss transforms
    float handRadius = boss->handAttackCollider.radius * boss->scale[0];
    float handHalfLen = boss->handAttackCollider.localCapB.v[1] * 0.5f * boss->scale[1];
    
    // For now, create capsule along Y axis in local space (simplified)
    // Endpoints in world space
    float handCapA[3] = {
        boss->handAttackColliderWorldPos[0],
        boss->handAttackColliderWorldPos[1] - handHalfLen,
        boss->handAttackColliderWorldPos[2]
    };
    float handCapB[3] = {
        boss->handAttackColliderWorldPos[0],
        boss->handAttackColliderWorldPos[1] + handHalfLen,
        boss->handAttackColliderWorldPos[2]
    };
    
    // Use capsule vs capsule collision
    return scu_capsule_vs_capsule_f(
        handCapA, handCapB, handRadius,
        charCapA, charCapB, charRadius
    );
}

static void boss_attacks_handle_power_jump(Boss* boss, float dt) {
    // Frame timings at 30 FPS: 0-41 idle, 41-83 jump+land, 83-136 land+recover
    const float idleDuration = 41.0f / 25.0f;      // 1.367s
    const float jumpDuration = 42.0f / 25.0f;      // 1.4s
    const float recoverDuration = 53.0f / 25.0f;   // 1.767s
    const float totalDuration = idleDuration + jumpDuration + recoverDuration;

    // Phase 1: Idle preparation (0.0 - 1.367s)
    if (boss->stateTimer < idleDuration) {
        // Stay in place, face target direction
        float dx = boss->powerJumpTargetPos[0] - boss->pos[0];
        float dz = boss->powerJumpTargetPos[2] - boss->pos[2];
        if (dx != 0.0f || dz != 0.0f) {
            boss->rot[1] = -atan2f(-dz, dx) + 3.14159265359f; // T3D_PI
        }
    }
    // Phase 2: Jump arc (1.367 - 2.767s)
    else if (boss->stateTimer < idleDuration + jumpDuration) {
        float t = (boss->stateTimer - idleDuration) / jumpDuration;
        
        // Smooth arc from start to target
        boss->pos[0] = boss->powerJumpStartPos[0] + (boss->powerJumpTargetPos[0] - boss->powerJumpStartPos[0]) * t;
        boss->pos[2] = boss->powerJumpStartPos[2] + (boss->powerJumpTargetPos[2] - boss->powerJumpStartPos[2]) * t;
        
        // Parabolic height
        boss->pos[1] = boss->powerJumpStartPos[1] + boss->powerJumpHeight * sinf(t * 3.14159265359f);
        
        // Face movement direction - use consistent rotation formula
        float dx = boss->powerJumpTargetPos[0] - boss->powerJumpStartPos[0];
        float dz = boss->powerJumpTargetPos[2] - boss->powerJumpStartPos[2];
        if (dx != 0.0f || dz != 0.0f) {
            boss->rot[1] = -atan2f(-dz, dx) + 3.14159265359f;
        }
    }
    // Phase 3: Landing impact + recovery (2.767 - 4.533s)
    else if (boss->stateTimer < totalDuration) {
        // Boss hits ground and recovers
        boss->pos[1] = boss->powerJumpStartPos[1];
        if (boss->stateTimer >= idleDuration + jumpDuration && !bossPowerJumpImpactPlayed) {
            // Sound would be triggered here - boss_debug_sound("boss_power_jump_impact");
            bossPowerJumpImpactPlayed = true;
        }
        
        // Check for impact damage
        if (boss->stateTimer >= idleDuration + jumpDuration && 
            boss->stateTimer < idleDuration + jumpDuration + 0.1f && 
            !boss->currentAttackHasHit) {
            // Power jump uses ground impact, so use distance check for now
            // (hand collider is in air, ground impact is at boss position)
            float dx = character.pos[0] - boss->pos[0];
            float dz = character.pos[2] - boss->pos[2];
            float dist = sqrtf(dx*dx + dz*dz);
            
            if (dist < 6.0f) {
                character_apply_damage(35.0f);
                // boss_debug_sound("boss_attack_success");
                boss->currentAttackHasHit = true;
            }
        }
    }
    // End attack - transition handled by AI
    // (AI will check stateTimer >= totalDuration and transition to STRAFE)
}

static void boss_attacks_handle_combo(Boss* boss, float dt) {
    const float stepDuration = 0.8f; // Each combo step
    const float vulnerableWindow = 0.4f;
    
    // Always face the locked target position (lerped player position)
    float dx = boss->lockedTargetingPos[0] - boss->pos[0];
    float dz = boss->lockedTargetingPos[2] - boss->pos[2];
    // Use consistent rotation formula
    boss->rot[1] = -atan2f(-dz, dx) + 3.14159265359f;
    
    int targetStep = (int)(boss->stateTimer / stepDuration);
    if (targetStep != boss->comboStep && targetStep < 3) {
        boss->comboStep = targetStep;
        boss->comboVulnerableTimer = vulnerableWindow;
        // Sound would be triggered here based on step
        // boss_debug_sound(comboSound);
        // boss_debug_sound("boss_vulnerable");
    }
    
    // Update vulnerable timer
    if (boss->comboVulnerableTimer > 0.0f) {
        boss->comboVulnerableTimer -= dt;
    }
    
    // Check for player interrupt during vulnerable window
    if (boss->comboVulnerableTimer > 0.0f && !boss->comboInterrupted) {
        // Check if player is attacking boss during vulnerable window
        float dx2 = character.pos[0] - boss->pos[0];
        float dz2 = character.pos[2] - boss->pos[2];
        float dist = sqrtf(dx2*dx2 + dz2*dz2);
        
        if (dist < 5.0f) {
            // Combo is interruptible when player gets close during vulnerable window
            boss->comboInterrupted = true;
            boss_apply_damage(boss, 10.0f); // Bonus damage for interrupt
            boss->state = BOSS_STATE_RECOVER;
            boss->stateTimer = 0.0f;
            return;
        }
    }
    
    // Execute combo steps
    if (boss->comboStep == 0) {
        // Step 1: Sweep attack
        
        if (boss->stateTimer > 0.5f && boss->stateTimer < 0.7f && !boss->currentAttackHasHit) {
            if (boss_check_hand_attack_collision(boss)) {
                character_apply_damage(15.0f);
                // boss_debug_sound("boss_attack_success");
                boss->currentAttackHasHit = true;
            }
        }
    }
    else if (boss->comboStep == 1) {
        // Step 2: Forward thrust
        if (boss->stateTimer > stepDuration + 0.5f && boss->stateTimer < stepDuration + 0.7f) {
            float thrustSpeed = 300.0f;
            boss->velX = sinf(boss->rot[1]) * thrustSpeed * dt;
            boss->velZ = cosf(boss->rot[1]) * thrustSpeed * dt;
            
            if (!boss->currentAttackHasHit) {
                if (boss_check_hand_attack_collision(boss)) {
                    character_apply_damage(20.0f);
                    // boss_debug_sound("boss_attack_success");
                    boss->currentAttackHasHit = true;
                }
            }
        }
    }
    else if (boss->comboStep == 2) {
        // Step 3: Overhead slam
        if (boss->stateTimer > stepDuration * 2 + 0.6f && 
            boss->stateTimer < stepDuration * 2 + 0.8f && 
            !boss->currentAttackHasHit) {
            if (boss_check_hand_attack_collision(boss)) {
                character_apply_damage(30.0f);
                // boss_debug_sound("boss_attack_success");
                boss->currentAttackHasHit = true;
            }
        }
    }
    
    // End combo - transition handled by AI
    // (AI will check stateTimer > stepDuration * 3 + 0.5f and transition to STRAFE)
}

static void boss_attacks_handle_combo_starter(Boss* boss, float dt) {
    // Keep boss facing the target during the entire attack
    float dx = boss->comboStarterTargetPos[0] - boss->pos[0];
    float dz = boss->comboStarterTargetPos[2] - boss->pos[2];
    // Use same rotation formula as strafe/chase for consistency
    boss->rot[1] = -atan2f(-dz, dx) + 3.14159265359f;
    
    // Phase 1: Throw sword (0.0 - 0.5s)
    if (!boss->swordThrown && boss->stateTimer < 0.5f) {
        // Animation trigger would be handled by animation system
    }
    else if (!boss->swordThrown && boss->stateTimer >= 0.5f) {
        // Launch projectile
        boss->swordThrown = true;
        boss->swordProjectilePos[0] = boss->pos[0];
        boss->swordProjectilePos[1] = boss->pos[1] + 2.0f;
        boss->swordProjectilePos[2] = boss->pos[2];
        // boss_debug_sound("boss_combo_starter_throw");
    }
    
    // Phase 2: Sword flight (0.5 - 1.0s)
    if (boss->swordThrown && boss->stateTimer < 1.0f && !boss->comboStarterSlamHasHit) {
        // Move sword toward target
        float t = (boss->stateTimer - 0.5f) / 0.5f; // 0 to 1 over 0.5s
        boss->swordProjectilePos[0] = boss->pos[0] + (boss->comboStarterTargetPos[0] - boss->pos[0]) * t;
        boss->swordProjectilePos[2] = boss->pos[2] + (boss->comboStarterTargetPos[2] - boss->pos[2]) * t;
        boss->swordProjectilePos[1] = boss->pos[1] + 2.0f + sinf(t * 3.14159265359f) * 5.0f; // Arc
        
        // Check for hit
        float hitDx = character.pos[0] - boss->swordProjectilePos[0];
        float hitDz = character.pos[2] - boss->swordProjectilePos[2];
        float hitDy = character.pos[1] - boss->swordProjectilePos[1];
        float hitDist = sqrtf(hitDx*hitDx + hitDy*hitDy + hitDz*hitDz);
        
        if (hitDist < 3.0f && !boss->currentAttackHasHit) {
            character_apply_damage(20.0f);
            // boss_debug_sound("boss_combo_starter_impact");
            // boss_debug_sound("boss_attack_success");
            boss->currentAttackHasHit = true;
            boss->comboStarterSlamHasHit = true;
        }
    }
    
    // Phase 3: Sword slam (1.0s+)
    if (boss->stateTimer >= 1.0f && !boss->comboStarterSlamHasHit) {
        boss->comboStarterSlamHasHit = true;
        // boss_debug_sound("boss_combo_starter_impact");
        // Sword hits ground at target
        boss->swordProjectilePos[0] = boss->comboStarterTargetPos[0];
        boss->swordProjectilePos[1] = boss->comboStarterTargetPos[1];
        boss->swordProjectilePos[2] = boss->comboStarterTargetPos[2];
        
        // Ground impact damage
        float impactDx = character.pos[0] - boss->swordProjectilePos[0];
        float impactDz = character.pos[2] - boss->swordProjectilePos[2];
        float impactDist = sqrtf(impactDx*impactDx + impactDz*impactDz);
        
        if (impactDist < 5.0f && !boss->currentAttackHasHit) {
            character_apply_damage(15.0f);
            // boss_debug_sound("boss_attack_success");
            boss->currentAttackHasHit = true;
        }
    }
    
    // Phase 4: Boss stays in place (1.5s - 2.0s)
    // Combo starter does not move the boss - only the charge attack moves the boss
    if (boss->stateTimer >= 1.5f && boss->stateTimer < 2.0f) {
        // Ensure boss doesn't move during combo starter
        boss->velX = 0.0f;
        boss->velZ = 0.0f;
    }
    
    // End attack - transition handled by AI
    // (AI will check stateTimer >= 2.0f and transition to charge/combo attack immediately)
}

static void boss_attacks_handle_roar_stomp(Boss* boss, float dt) {
    // Phase 1: Roar buildup (0.0 - 1.0s)
    if (boss->stateTimer < 1.0f) {
        // Face player - use consistent rotation formula
        float dx = character.pos[0] - boss->pos[0];
        float dz = character.pos[2] - boss->pos[2];
        boss->rot[1] = -atan2f(-dz, dx) + 3.14159265359f;
        
        // Animation trigger would be handled by animation system at 0.8-0.9s
    }
    // Phase 2: Stomp impact (1.0s)
    else if (boss->stateTimer >= 1.0f && boss->stateTimer < 1.1f) {
        if (!bossRoarImpactSoundPlayed) {
            // boss_debug_sound("boss_roar_stomp_impact");
            // boss_debug_sound("shockwave_rumble");
            bossRoarImpactSoundPlayed = true;
        }
        
        // Roar stomp uses shockwave, so use distance check for ground-based attack
        // (hand collider is not appropriate for ground shockwave)
        float dx = character.pos[0] - boss->pos[0];
        float dz = character.pos[2] - boss->pos[2];
        float dist = sqrtf(dx*dx + dz*dz);
        
        float shockwaveRadius = 15.0f;
        if (dist <= shockwaveRadius && !boss->currentAttackHasHit) {
            // Damage decreases with distance
            float damage = 30.0f * (1.0f - (dist / shockwaveRadius));
            character_apply_damage(damage);
            // boss_debug_sound("boss_attack_success");
            boss->currentAttackHasHit = true;
        }
    }
    // Phase 3: Recovery (1.1s+)
    // End attack - transition handled by AI
    // (AI will check stateTimer > 2.0f and transition to STRAFE)
}

static void boss_attacks_handle_tracking_slam(Boss* boss, float dt) {
    // Stationary attack - boss stays in place and tries to hit the character
    // Face the locked target position (predicted player position) instead of current position
    float dx = boss->lockedTargetingPos[0] - boss->pos[0];
    float dz = boss->lockedTargetingPos[2] - boss->pos[2];
    boss->rot[1] = -atan2f(-dz, dx) + 3.14159265359f;

    // Check for hit during slam (check against actual character position for damage)
    if (!boss->currentAttackHasHit) {
        if (boss_check_hand_attack_collision(boss)) {
            character_apply_damage(25.0f);
            boss->currentAttackHasHit = true;
        }
    }

    // Animation system will handle timing - attack completes when boss.isAttacking becomes false
    // Transition handled by AI based on isAttacking flag and animation blend completion
}

static void boss_attacks_handle_charge(Boss* boss, float dt) {
    // Charge attack - boss moves toward position behind player
    // Face the locked targeting position (behind player)
    float dx = boss->lockedTargetingPos[0] - boss->pos[0];
    float dz = boss->lockedTargetingPos[2] - boss->pos[2];
    boss->rot[1] = -atan2f(-dz, dx) + 3.14159265359f;

    // Check for hit during charge (check against actual character position for damage)
    // Hit window: 0.2s to 0.5s into the charge
    if (boss->stateTimer > 0.2f && boss->stateTimer < 0.5f && !boss->currentAttackHasHit) {
        if (boss_check_hand_attack_collision(boss)) {
            character_apply_damage(15.0f);
            boss->currentAttackHasHit = true;
        }
    }

    // Movement is handled by boss_update_movement in boss.c
    // Transition handled by AI when charge completes
}

static void boss_attacks_handle_flip_attack(Boss* boss, float dt) {
    // Flip attack: 2s idle, 1s jump arc, 1s recovery
    const float idleDuration = 2.0f;      // 2.0s idle preparation
    const float jumpDuration = 1.0f;      // 1.0s jump arc through air
    const float recoverDuration = 1.0f;   // 1.0s recovery on ground
    const float totalDuration = idleDuration + jumpDuration + recoverDuration;

    // Phase 1: Idle preparation (0.0 - 2.0s)
    if (boss->stateTimer < idleDuration) {
        // Stay in place, face target direction
        float dx = boss->flipAttackTargetPos[0] - boss->pos[0];
        float dz = boss->flipAttackTargetPos[2] - boss->pos[2];
        if (dx != 0.0f || dz != 0.0f) {
            boss->rot[1] = -atan2f(-dz, dx) + 3.14159265359f; // T3D_PI
        }
    }
    // Phase 2: Jump arc (2.0 - 3.0s)
    else if (boss->stateTimer < idleDuration + jumpDuration) {
        float t = (boss->stateTimer - idleDuration) / jumpDuration;
        
        // Smooth arc from start to target
        boss->pos[0] = boss->flipAttackStartPos[0] + (boss->flipAttackTargetPos[0] - boss->flipAttackStartPos[0]) * t;
        boss->pos[2] = boss->flipAttackStartPos[2] + (boss->flipAttackTargetPos[2] - boss->flipAttackStartPos[2]) * t;
        
        // Parabolic height (lower than power jump)
        boss->pos[1] = boss->flipAttackStartPos[1] + boss->flipAttackHeight * sinf(t * 3.14159265359f);
        
        // Face movement direction - use consistent rotation formula
        float dx = boss->flipAttackTargetPos[0] - boss->flipAttackStartPos[0];
        float dz = boss->flipAttackTargetPos[2] - boss->flipAttackStartPos[2];
        if (dx != 0.0f || dz != 0.0f) {
            boss->rot[1] = -atan2f(-dz, dx) + 3.14159265359f;
        }
    }
    // Phase 3: Landing impact + recovery (3.0 - 4.0s)
    else if (boss->stateTimer < totalDuration) {
        // Boss hits ground and recovers
        boss->pos[1] = boss->flipAttackStartPos[1];
        
        // Check for impact damage
        if (boss->stateTimer >= idleDuration + jumpDuration && 
            boss->stateTimer < idleDuration + jumpDuration + 0.1f && 
            !boss->currentAttackHasHit) {
            // Flip attack uses ground impact, so use distance check
            float dx = character.pos[0] - boss->pos[0];
            float dz = character.pos[2] - boss->pos[2];
            float dist = sqrtf(dx*dx + dz*dz);
            
            if (dist < 6.0f) {
                character_apply_damage(30.0f); // Slightly less damage than power jump
                // boss_debug_sound("boss_attack_success");
                boss->currentAttackHasHit = true;
            }
        }
    }
    // End attack - transition handled by AI
    // (AI will check stateTimer >= totalDuration and transition to STRAFE)
}

