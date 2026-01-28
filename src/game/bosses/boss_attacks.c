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

#include "systems/collision_system.h"

#include "game_time.h"
#include "character.h"
#include "scene_sfx.h" // TODO: make sfx entity specific

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
static void boss_attacks_handle_lunge_starter(Boss* boss, float dt);

void boss_attacks_update(Boss* boss, float dt) {
    if (!boss) return;
    
    // Check if boss is in an attack state and update hand collider
    bool isAttackState = (boss->state == BOSS_STATE_POWER_JUMP ||
                          boss->state == BOSS_STATE_COMBO_ATTACK ||
                          boss->state == BOSS_STATE_COMBO_STARTER ||
                          boss->state == BOSS_STATE_ROAR_STOMP ||
                          boss->state == BOSS_STATE_TRACKING_SLAM ||
                          boss->state == BOSS_STATE_COMBO_LUNGE ||
                          boss->state == BOSS_STATE_FLIP_ATTACK);
    
    // Always update collider position for debugging (even when not attacking)
    //if (boss->handRightBoneIndex >= 0) {
    //     boss_update_hand_attack_collider(boss);
    // }
    
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
            
        case BOSS_STATE_COMBO_LUNGE:
            boss_attacks_handle_charge(boss, dt);
            break;
            
        case BOSS_STATE_FLIP_ATTACK:
            boss_attacks_handle_flip_attack(boss, dt);
            break;
        case BOSS_STATE_LUNGE_STARTER:
            boss_attacks_handle_lunge_starter(boss, dt);
            break;
        default:
            // Not an attack state, nothing to do
            break;
    }
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
            boss->rot[1] = -atan2f(-dz, dx) + T3D_PI;
        }
    }
    // Phase 2: Jump arc (1.367 - 2.767s)
    else if (boss->stateTimer < idleDuration + jumpDuration) {
        float t = (boss->stateTimer - idleDuration) / jumpDuration;
        
        // Smooth arc from start to target
        boss->pos[0] = boss->powerJumpStartPos[0] + (boss->powerJumpTargetPos[0] - boss->powerJumpStartPos[0]) * t;
        boss->pos[2] = boss->powerJumpStartPos[2] + (boss->powerJumpTargetPos[2] - boss->powerJumpStartPos[2]) * t;
        
        // Parabolic height
        boss->pos[1] = boss->powerJumpStartPos[1] + boss->powerJumpHeight * sinf(t * T3D_PI);
        
        // Face movement direction - use consistent rotation formula
        float dx = boss->powerJumpTargetPos[0] - boss->powerJumpStartPos[0];
        float dz = boss->powerJumpTargetPos[2] - boss->powerJumpStartPos[2];
        if (dx != 0.0f || dz != 0.0f) {
            boss->rot[1] = -atan2f(-dz, dx) + T3D_PI;
        }
    }
    // Phase 3: Landing impact + recovery (2.767 - 4.533s)
    else if (boss->stateTimer < totalDuration) 
    {
        // Boss hits ground and recovers
        boss->pos[1] = boss->powerJumpStartPos[1];

        if (boss->stateTimer >= idleDuration + jumpDuration) 
        {
            boss_multi_attack_sfx(boss, bossJumpForwardSfx, 2);
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
    
    // boss->velX = 0.0f;
    // boss->velZ = 0.0f;

    // Always face the locked target position (lerped player position)
    float dx = boss->lockedTargetingPos[0] - boss->pos[0];
    float dz = boss->lockedTargetingPos[2] - boss->pos[2];
    // Use consistent rotation formula
    boss->rot[1] = -atan2f(-dz, dx) + T3D_PI;
    
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
    
    boss_multi_attack_sfx(boss, bossComboAttack1Sfx, 3);  //(ComboAttack1)
    // Execute combo steps
    if (boss->comboStep == 0) {
        // Step 1: Attack 1

        if (boss->stateTimer > 0.5f && boss->stateTimer < 0.7f && !boss->currentAttackHasHit) {
            if (bossWeaponCollision) {
                character_apply_damage(15.0f);
                boss->currentAttackHasHit = true;
            }
        }
    }
    else if (boss->comboStep == 1) {
        // Step 2: Attack2 into sword lift

        if (boss->stateTimer > stepDuration + 0.5f && boss->stateTimer < stepDuration + 0.7f) {
            // float thrustSpeed = 300.0f;
            // boss->velX = sinf(boss->rot[1]) * thrustSpeed;
            // boss->velZ = cosf(boss->rot[1]) * thrustSpeed;
            
            if (!boss->currentAttackHasHit) {
                if (bossWeaponCollision) {
                    character_apply_damage(20.0f);
                    boss->currentAttackHasHit = true;
                }
            }
        }
    }
    else if (boss->comboStep == 2) {
        // Step 3: Jump sword slam
        if (boss->stateTimer > stepDuration * 2 + 0.6f && 
            boss->stateTimer < stepDuration * 2 + 0.8f && 
            !boss->currentAttackHasHit) 
        {
            if (bossWeaponCollision) {
                character_apply_damage(30.0f);
                boss->currentAttackHasHit = true;
            }
        }
    }
    
    // End combo - transition handled by AI
    // (AI will check stateTimer > stepDuration * 3 + 0.5f and transition to STRAFE)
}

static void boss_attacks_handle_throw(Boss* boss, float dt) // TODO: add after the jam
{
    // Keep boss facing the target during the entire attack
    float dx = boss->comboStarterTargetPos[0] - boss->pos[0];
    float dz = boss->comboStarterTargetPos[2] - boss->pos[2];
    // Use same rotation formula as strafe/chase for consistency
    boss->rot[1] = -atan2f(-dz, dx) + T3D_PI;
    
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
        boss->swordProjectilePos[1] = boss->pos[1] + 2.0f + sinf(t * T3D_PI) * 5.0f; // Arc
        
        // Check for hit
        float hitDx = character.pos[0] - boss->swordProjectilePos[0];
        float hitDz = character.pos[2] - boss->swordProjectilePos[2];
        float hitDy = character.pos[1] - boss->swordProjectilePos[1];
        float hitDist = sqrtf(hitDx*hitDx + hitDy*hitDy + hitDz*hitDz);
        if (hitDist < 3.0f && !boss->currentAttackHasHit) {
            character_apply_damage(20.0f);
            //boss_play_attack_sfx(boss, SCENE1_SFX_BOSS_SMASH2, 0.0f);
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
}

static void boss_attacks_handle_combo_starter(Boss* boss, float dt)
{
    boss_play_attack_sfx(boss, SCENE1_SFX_BOSS_SWING2, 1.0f);

    // Track player yaw during the whole windup
    float dx = character.pos[0] - boss->pos[0];
    float dz = character.pos[2] - boss->pos[2];
    if (dx != 0.0f || dz != 0.0f) {
        boss->rot[1] = -atan2f(-dz, dx) + T3D_PI;
    }

    // Keep boss stationary during combo starter
    boss->velX = 0.0f;
    boss->velZ = 0.0f;

    // (rest of your timing logic unchanged)
    if (boss->stateTimer >= 1.5f && boss->stateTimer < 2.0f) {
        boss->velX = 0.0f;
        boss->velZ = 0.0f;
    }
}


// For distance based lunges, anticipation is added.
static void boss_attacks_handle_lunge_starter(Boss* boss, float dt)
{
    boss->velX = 0.0f;
    boss->velZ = 0.0f;

    // Face player during windup
    float dx = character.pos[0] - boss->pos[0];
    float dz = character.pos[2] - boss->pos[2];
    if (dx != 0.0f || dz != 0.0f) {
        boss->rot[1] = -atan2f(-dz, dx) + T3D_PI;
    }
}

static void boss_attacks_handle_roar_stomp(Boss* boss, float dt) {
    // Phase 1: Roar buildup (0.0 - 1.0s)
    if (boss->stateTimer < 1.0f) {
        // Face player - use consistent rotation formula
        float dx = character.pos[0] - boss->pos[0];
        float dz = character.pos[2] - boss->pos[2];
        boss->rot[1] = -atan2f(-dz, dx) + T3D_PI;
        
        // Animation trigger would be handled by animation system at 0.8-0.9s
    }
    // Phase 2: Stomp impact (1.0s)
    else if (boss->stateTimer >= 1.0f && boss->stateTimer < 1.1f) {

        //boss_play_attack_sfx(boss, SCENE1_SFX_BOSS_LAND1, 0.0f);
        
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

static void boss_attacks_handle_tracking_slam(Boss* boss, float dt)
{
    // Stationary
    boss->velX = 0.0f;
    boss->velZ = 0.0f;

    // Stop tracking once the slam is committed / landing
    // Tune this to match the animation moment where the weapon comes down.
    const float slamLockTime = 2.8f;

    bool allowTracking = (boss->stateTimer < slamLockTime) && !boss->currentAttackHasHit;

    if (allowTracking) {
        const float leadTime = 0.10f;
        float targetX = character.pos[0] + boss->lastPlayerVel[0] * leadTime;
        float targetZ = character.pos[2] + boss->lastPlayerVel[1] * leadTime;

        float dx = targetX - boss->pos[0];
        float dz = targetZ - boss->pos[2];

        if (dx != 0.0f || dz != 0.0f) {
            float targetAngle = -atan2f(-dz, dx) + T3D_PI;

            float currentAngle = boss->rot[1];
            float angleDelta = targetAngle - currentAngle;

            while (angleDelta >  T3D_PI) angleDelta -= 2.0f * T3D_PI;
            while (angleDelta < -T3D_PI) angleDelta += 2.0f * T3D_PI;

            const float trackingTurnScalar = 0.12f; // Adjust this between 0.05 and 0.2
            float maxTurn = boss->turnRate * trackingTurnScalar * dt;

            if (angleDelta >  maxTurn) angleDelta =  maxTurn;
            if (angleDelta < -maxTurn) angleDelta = -maxTurn;

            boss->rot[1] = currentAngle + angleDelta;
        }
    }

    boss_multi_attack_sfx(boss, bossSlowAttackSfx, 2);

    // Hit / land logic (kept as-is)
    if (!boss->currentAttackHasHit) {
        if (bossWeaponCollision) {
            character_apply_damage(25.0f);
            boss->currentAttackHasHit = true;
            // From this frame onward, allowTracking becomes false and yaw stays frozen.
        }
    }
}

// static void boss_attacks_handle_charge(Boss* boss, float dt) 
// {
//     boss_play_attack_sfx(boss, SCENE1_SFX_BOSS_LUNGE, 0.0f);
//     // Charge attack - boss moves toward position behind player
//     // Face the locked targeting position (behind player)
//     float dx = boss->lockedTargetingPos[0] - boss->pos[0];
//     float dz = boss->lockedTargetingPos[2] - boss->pos[2];
//     boss->rot[1] = -atan2f(-dz, dx) + T3D_PI;

//     // Check for hit during charge (check against actual character position for damage)
//     // Hit window: 0.2s to 0.5s into the charge
//     if (boss->stateTimer > 0.2f && boss->stateTimer < 0.5f && !boss->currentAttackHasHit) {
//         if (bossWeaponCollision) {
//             character_apply_damage(15.0f);
//             boss->currentAttackHasHit = true;
//         }
//     }

//     // Movement is handled by boss_update_movement in boss.c
//     // Transition handled by AI when charge completes
// }

static void boss_attacks_handle_charge(Boss* boss, float dt)
{
    boss_play_attack_sfx(boss, SCENE1_SFX_BOSS_LUNGE, 0.0f);

    const float lungeStart = 0.15f;
    const float lungeEnd   = 0.55f;

    const float LUNGE_SPEED_CLOSE = 620.0f;
    const float LUNGE_SPEED_FAR   = 620.0f;

    const float TURN_TRAVEL_CLOSE = 1.25f;  // faster tracking DURING travel (close-range)
    const float TURN_SETTLE       = 0.30f;

    boss->velX = 0.0f;
    boss->velZ = 0.0f;

    const bool inTravel = (boss->stateTimer >= lungeStart && boss->stateTimer <= lungeEnd);

    // --------------------
    // Rotation
    // --------------------
    if (inTravel) {
        if (boss->comboLungeTracksPlayer) {
            // Close-range: track player aggressively DURING movement
            float toX = character.pos[0] - boss->pos[0];
            float toZ = character.pos[2] - boss->pos[2];

            if (toX != 0.0f || toZ != 0.0f) {
                float targetYaw = -atan2f(-toZ, toX) + T3D_PI;

                float cur = boss->rot[1];
                float d = targetYaw - cur;
                while (d >  T3D_PI) d -= 2.0f * T3D_PI;
                while (d < -T3D_PI) d += 2.0f * T3D_PI;

                float maxTurn = boss->turnRate * TURN_TRAVEL_CLOSE * dt;
                if (d >  maxTurn) d =  maxTurn;
                if (d < -maxTurn) d = -maxTurn;

                boss->rot[1] = cur + d;
            }
        } else {
            // Distance-closer: yaw locked during travel
            boss->rot[1] = boss->comboLungeLockedYaw;
        }
    } else {
        // Settle: track player
        float toX = character.pos[0] - boss->pos[0];
        float toZ = character.pos[2] - boss->pos[2];

        if (toX != 0.0f || toZ != 0.0f) {
            float targetYaw = -atan2f(-toZ, toX) + T3D_PI;

            float cur = boss->rot[1];
            float d = targetYaw - cur;
            while (d >  T3D_PI) d -= 2.0f * T3D_PI;
            while (d < -T3D_PI) d += 2.0f * T3D_PI;

            float maxTurn = boss->turnRate * TURN_SETTLE * dt;
            if (d >  maxTurn) d =  maxTurn;
            if (d < -maxTurn) d = -maxTurn;

            boss->rot[1] = cur + d;
        }
    }

    // --------------------
    // Movement (travel only)
    // --------------------
    if (inTravel) {
        float lungeSpeed = boss->comboLungeTracksPlayer ? LUNGE_SPEED_CLOSE : LUNGE_SPEED_FAR;

        float tx = boss->lockedTargetingPos[0] - boss->pos[0];
        float tz = boss->lockedTargetingPos[2] - boss->pos[2];
        float d  = sqrtf(tx*tx + tz*tz);

        if (d > 0.001f) {
            float dirX = tx / d;
            float dirZ = tz / d;

            float speedScale = 1.0f;
            if (d < 20.0f) speedScale = d / 20.0f;

            boss->velX = dirX * lungeSpeed * speedScale;
            boss->velZ = dirZ * lungeSpeed * speedScale;
        }
    }

    if (boss->stateTimer > 0.2f && boss->stateTimer < 0.5f && !boss->currentAttackHasHit) {
        if (bossWeaponCollision) {
            character_apply_damage(15.0f);
            boss->currentAttackHasHit = true;
        }
    }
}

static void boss_attacks_handle_flip_attack(Boss* boss, float dt) {
    // Flip attack: 2s idle, 1s jump arc, 1s recovery
    const float idleDuration = 2.0f;      // 2.0s idle preparation
    const float jumpDuration = 1.0f;      // 1.0s jump arc through air
    const float recoverDuration = 1.0f;   // 1.0s recovery on ground
    const float totalDuration = idleDuration + jumpDuration + recoverDuration;

    boss_multi_attack_sfx(boss, bossFlipAttackSfx, 3);
    
    // Phase 1: Idle preparation (0.0 - 2.0s)
    if (boss->stateTimer < idleDuration) {
        // Stay in place, smoothly face target direction (don't snap)
        float dx = boss->flipAttackTargetPos[0] - boss->pos[0];
        float dz = boss->flipAttackTargetPos[2] - boss->pos[2];
        if (dx != 0.0f || dz != 0.0f) {
            float targetAngle = -atan2f(-dz, dx) + T3D_PI;
            
            // Smoothly rotate toward target angle using boss turn rate
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
    }
    // Phase 2: Jump arc (2.0 - 3.0s)
    else if (boss->stateTimer < idleDuration + jumpDuration) {
        float t = (boss->stateTimer - idleDuration) / jumpDuration;
        
        // Smooth arc from start to target
        boss->pos[0] = boss->flipAttackStartPos[0] + (boss->flipAttackTargetPos[0] - boss->flipAttackStartPos[0]) * t;
        boss->pos[2] = boss->flipAttackStartPos[2] + (boss->flipAttackTargetPos[2] - boss->flipAttackStartPos[2]) * t;
        
        // Parabolic height (lower than power jump)
        boss->pos[1] = boss->flipAttackStartPos[1] + boss->flipAttackHeight * sinf(t * T3D_PI);
        
        // Face movement direction - use consistent rotation formula
        float dx = boss->flipAttackTargetPos[0] - boss->flipAttackStartPos[0];
        float dz = boss->flipAttackTargetPos[2] - boss->flipAttackStartPos[2];
        if (dx != 0.0f || dz != 0.0f) {
            boss->rot[1] = -atan2f(-dz, dx) + T3D_PI;
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
                //boss_play_attack_sfx(boss, SCENE1_SFX_BOSS_LAND1, 0.0f);
                boss->currentAttackHasHit = true;
            }
        }
    }
    // End attack - transition handled by AI
    // (AI will check stateTimer >= totalDuration and transition to STRAFE)
    
}
