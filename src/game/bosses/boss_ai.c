/*
 * boss_ai.c
 * 
 * AI module - decides intent (states/attacks)
 * Must NOT include tiny3d animation headers
 */

#include "boss_ai.h"
#include "boss.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "game_time.h"
#include "character.h"
#include "scene.h"
#include "simple_collision_utility.h"

// Internal helper functions
static void boss_ai_update_targeting_system(Boss* boss, float dt);
static void boss_ai_update_cooldowns(Boss* boss, float dt);
static void boss_ai_select_attack(Boss* boss, float dist);
static bool boss_ai_state_is_attack(BossState state);
static void predict_character_position(float *predictedPos, float predictionTime);

// Static state for AI (telegraph, activation tracking, etc.)
static bool bossWasActive = false;
static float bossStrafeDirection = 1.0f; // 1.0 = right, -1.0 = left
static float strafeDirectionTimer = 0.0f; // Timer for alternating direction when stationary

// Sound flags shared with attack handlers
bool bossPowerJumpImpactPlayed = false;
bool bossRoarImpactSoundPlayed = false;

void boss_ai_init(Boss* boss) {
    if (!boss) return;
    
    boss->state = BOSS_STATE_INTRO;
    boss->stateTimer = 0.0f;
    boss->attackCooldown = 0.0f;
    
    // Initialize all cooldowns
    boss->powerJumpCooldown = 0.0f;
    boss->comboCooldown = 0.0f;
    boss->comboStarterCooldown = 0.0f;
    boss->roarStompCooldown = 0.0f;
    boss->trackingSlamCooldown = 0.0f;
    boss->flipAttackCooldown = 0.0f;
    
    // Initialize attack state
    boss->isAttacking = false;
    boss->attackAnimTimer = 0.0f;
    boss->currentAttackHasHit = false;
    boss->currentAttackId = BOSS_ATTACK_COUNT;
    
    bossWasActive = false;
}

static bool boss_ai_state_is_attack(BossState state) {
    return state == BOSS_STATE_CHARGE
        || state == BOSS_STATE_POWER_JUMP
        || state == BOSS_STATE_COMBO_ATTACK
        || state == BOSS_STATE_COMBO_STARTER
        || state == BOSS_STATE_ROAR_STOMP
        || state == BOSS_STATE_TRACKING_SLAM
        || state == BOSS_STATE_FLIP_ATTACK;
}

static void predict_character_position(float *predictedPos, float predictionTime) {
    predictedPos[0] = character.pos[0];
    predictedPos[1] = character.pos[1];
    predictedPos[2] = character.pos[2];
    
    float velX, velZ;
    character_get_velocity(&velX, &velZ);
    predictedPos[0] += velX * predictionTime;
    predictedPos[2] += velZ * predictionTime;
}

static void boss_ai_update_targeting_system(Boss* boss, float dt) {
    float currentPlayerPos[3] = {character.pos[0], character.pos[1], character.pos[2]};
    float velX, velZ;
    character_get_velocity(&velX, &velZ);
    
    boss->lastPlayerVel[0] = velX;
    boss->lastPlayerVel[1] = velZ;
    
    bool shouldLockTargeting = boss_ai_state_is_attack(boss->state) && !boss->targetingLocked;
    
    if (shouldLockTargeting) {
        float predictionTime = 0.3f;
        switch (boss->state) {
            case BOSS_STATE_POWER_JUMP:
                predictionTime = 1.0f;
                break;
            case BOSS_STATE_FLIP_ATTACK:
                predictionTime = 0.7f; // Shorter prediction for flip attack
                break;
            case BOSS_STATE_COMBO_STARTER:
                predictionTime = 0.0f; // Combo starter uses current position, not predicted
                break;
            default:
                predictionTime = 0.3f;
                break;
        }
        
        predict_character_position(boss->lockedTargetingPos, predictionTime);
        boss->targetingLocked = true;
        boss->targetingUpdateTimer = 0.0f;
    }
    
    if (!boss_ai_state_is_attack(boss->state) && boss->targetingLocked) {
        boss->targetingLocked = false;
    }
    
    if (boss->targetingLocked) {
        boss->debugTargetingPos[0] = boss->lockedTargetingPos[0];
        boss->debugTargetingPos[1] = boss->lockedTargetingPos[1];
        boss->debugTargetingPos[2] = boss->lockedTargetingPos[2];
    } else {
        boss->targetingUpdateTimer += dt;
        if (boss->targetingUpdateTimer >= 0.15f) {
            boss->targetingUpdateTimer = 0.0f;
            float anticipationTime = 0.4f;
            predict_character_position(boss->debugTargetingPos, anticipationTime);
            boss->lastPlayerPos[0] = currentPlayerPos[0];
            boss->lastPlayerPos[1] = currentPlayerPos[1];
            boss->lastPlayerPos[2] = currentPlayerPos[2];
        }
    }
}

static void boss_ai_update_cooldowns(Boss* boss, float dt) {
    if (boss->attackCooldown > 0.0f) boss->attackCooldown -= dt;
    if (boss->powerJumpCooldown > 0.0f) boss->powerJumpCooldown -= dt;
    if (boss->comboCooldown > 0.0f) boss->comboCooldown -= dt;
    if (boss->comboStarterCooldown > 0.0f) boss->comboStarterCooldown -= dt;
    if (boss->roarStompCooldown > 0.0f) boss->roarStompCooldown -= dt;
    if (boss->trackingSlamCooldown > 0.0f) boss->trackingSlamCooldown -= dt;
    if (boss->flipAttackCooldown > 0.0f) boss->flipAttackCooldown -= dt;
    
    if (boss->attackNameDisplayTimer > 0.0f) boss->attackNameDisplayTimer -= dt;
    if (boss->hitMessageTimer > 0.0f) boss->hitMessageTimer -= dt;
}

static void boss_ai_select_attack(Boss* boss, float dist) {
    boss->currentAttackHasHit = false;
    
    // Check combo starter first when distance is approximately 75 units
    // This allows combo starter to trigger, which then enables charge and combo attacks
    if (dist >= 70.0f && dist <= 80.0f && boss->comboStarterCooldown <= 0.0f) {
        boss->state = BOSS_STATE_COMBO_STARTER;
        boss->stateTimer = 0.0f;
        boss->comboStarterCooldown = 10.0f;
        boss->swordThrown = false;
        boss->comboStarterSlamHasHit = false;
        boss->comboStarterCompleted = false;
        // Combo starter does not move the boss - ensure velocity is zero
        boss->velX = 0.0f;
        boss->velZ = 0.0f;
        // Combo starter targets where player was at the time (current position, not predicted)
        boss->comboStarterTargetPos[0] = character.pos[0];
        boss->comboStarterTargetPos[1] = character.pos[1];
        boss->comboStarterTargetPos[2] = character.pos[2];
        boss->currentAttackName = "Combo Starter";
        boss->attackNameDisplayTimer = 2.0f;
        boss->currentAttackId = BOSS_ATTACK_COMBO_STARTER;
    }
    // Check flip attack when at medium-long range (closer than power jump)
    else if (boss->flipAttackCooldown <= 0.0f && dist >= 100.0f && dist < 200.0f) {
        boss->state = BOSS_STATE_FLIP_ATTACK;
        boss->stateTimer = 0.0f;
        boss->flipAttackCooldown = 10.0f;
        boss->currentAttackHasHit = false;
        // Clamp position values to prevent denormals
        float startX = boss->pos[0];
        float startY = boss->pos[1];
        float startZ = boss->pos[2];
        if (fabsf(startX) < 1e-6f) startX = 0.0f;
        if (fabsf(startY) < 1e-6f) startY = 0.0f;
        if (fabsf(startZ) < 1e-6f) startZ = 0.0f;
        boss->flipAttackStartPos[0] = startX;
        boss->flipAttackStartPos[1] = startY;
        boss->flipAttackStartPos[2] = startZ;
        predict_character_position(boss->lockedTargetingPos, 0.7f); // Shorter prediction time
        boss->targetingLocked = true;
        float targetX = boss->lockedTargetingPos[0];
        float targetY = boss->lockedTargetingPos[1];
        float targetZ = boss->lockedTargetingPos[2];
        // Clamp target position values
        if (fabsf(targetX) < 1e-6f) targetX = 0.0f;
        if (fabsf(targetY) < 1e-6f) targetY = 0.0f;
        if (fabsf(targetZ) < 1e-6f) targetZ = 0.0f;
        boss->flipAttackTargetPos[0] = targetX;
        boss->flipAttackTargetPos[1] = targetY;
        boss->flipAttackTargetPos[2] = targetZ;
        float height = 10.0f + ((float)(rand() % 10));
        // Clamp height values to prevent denormals
        if (fabsf(height) < 1e-6f) height = 0.0f;
        boss->flipAttackHeight = height;
        boss->currentAttackName = "Flip Attack";
        boss->attackNameDisplayTimer = 2.0f;
        boss->currentAttackId = BOSS_ATTACK_FLIP_ATTACK;
    }
    // Check power jump when at very long range (only when combo starter is on cooldown or at extreme range)
    else if (boss->powerJumpCooldown <= 0.0f && dist >= 250.0f) {
        boss->state = BOSS_STATE_POWER_JUMP;
        boss->stateTimer = 0.0f;
        boss->powerJumpCooldown = 12.0f;
        boss->isAttacking = true;
        boss->attackAnimTimer = 0.0f;
        boss->animationTransitionTimer = 0.0f;
        boss->currentAttackHasHit = false;
        boss->powerJumpStartPos[0] = boss->pos[0];
        boss->powerJumpStartPos[1] = boss->pos[1];
        boss->powerJumpStartPos[2] = boss->pos[2];
        predict_character_position(boss->lockedTargetingPos, 1.0f);
        boss->targetingLocked = true;
        boss->powerJumpTargetPos[0] = boss->lockedTargetingPos[0];
        boss->powerJumpTargetPos[1] = boss->lockedTargetingPos[1];
        boss->powerJumpTargetPos[2] = boss->lockedTargetingPos[2];
        boss->powerJumpHeight = 250.0f + ((float)(rand() % 5));
        boss->currentAttackName = "Power Jump";
        boss->attackNameDisplayTimer = 2.0f;
        boss->currentAttackId = BOSS_ATTACK_POWER_JUMP;
    }
    else if (dist >= 50.0f && dist <= 90.0f && boss->trackingSlamCooldown <= 0.0f) {
        boss->state = BOSS_STATE_TRACKING_SLAM;
        boss->stateTimer = 0.0f;
        boss->trackingSlamCooldown = 8.0f;
        boss->isAttacking = true;
        boss->attackAnimTimer = 0.0f;
        boss->animationTransitionTimer = 0.0f;
        
        // Lock targeting position now (predicted player position)
        // This ensures lockedTargetingPos is set before attack handler runs
        if (!boss->targetingLocked) {
            float predictionTime = 0.3f; // Default prediction for tracking slam
            predict_character_position(boss->lockedTargetingPos, predictionTime);
            boss->targetingLocked = true;
            boss->targetingUpdateTimer = 0.0f;
        }
        
        // Use locked target position (predicted position) for aiming
        float dx = boss->lockedTargetingPos[0] - boss->pos[0];
        float dz = boss->lockedTargetingPos[2] - boss->pos[2];
        float angle = atan2f(-dx, dz);
        boss->trackingSlamTargetAngle = angle;
        
        boss->currentAttackName = "Slow Attack";
        boss->attackNameDisplayTimer = 2.0f;
        boss->currentAttackId = BOSS_ATTACK_TRACKING_SLAM;
    }
    // DISABLED: Roar/stomp attack for testing
    // else if (dist <= 200.0f && boss->roarStompCooldown <= 0.0f) {
    //     boss->state = BOSS_STATE_ROAR_STOMP;
    //     boss->stateTimer = 0.0f;
    //     boss->roarStompCooldown = 6.0f;
    //     boss->currentAttackName = "Roar Stomp";
    //     boss->attackNameDisplayTimer = 2.0f;
    //     boss->currentAttackId = BOSS_ATTACK_ROAR_STOMP;
    // }
    else if (boss->comboCooldown <= 0.0f && boss->comboStarterCompleted) {
        boss->state = BOSS_STATE_COMBO_ATTACK;
        boss->stateTimer = 0.0f;
        boss->comboCooldown = 15.0f;
        boss->comboStep = 0;
        boss->comboInterrupted = false;
        boss->comboVulnerableTimer = 0.0f;
        // ComboAttack target updates slowly with player movement
        boss->lockedTargetingPos[0] = character.pos[0];
        boss->lockedTargetingPos[1] = character.pos[1];
        boss->lockedTargetingPos[2] = character.pos[2];
        boss->targetingLocked = true;
        boss->currentAttackName = "Combo Attack";
        boss->attackNameDisplayTimer = 2.0f;
        boss->currentAttackId = BOSS_ATTACK_COMBO;
    }
    // DISABLED: Roar/stomp attack for testing (fallback case)
    // else if (boss->roarStompCooldown <= 0.0f) {
    //     boss->state = BOSS_STATE_ROAR_STOMP;
    //     boss->stateTimer = 0.0f;
    //     boss->roarStompCooldown = 6.0f;
    //     boss->currentAttackName = "Roar Stomp";
    //     boss->attackNameDisplayTimer = 2.0f;
    //     boss->currentAttackId = BOSS_ATTACK_ROAR_STOMP;
    // }
    else {
        // When under 50 distance, boss should only attack, not chase or strafe
        if (dist < 50.0f) {
            // Force an attack when close - prefer charge if available, otherwise wait for cooldowns
            if (boss->attackCooldown <= 0.0f && dist > 0.0f && dist <= 200.0f && boss->comboStarterCompleted) {
                // Use charge attack
                float dx = character.pos[0] - boss->pos[0];
                float dz = character.pos[2] - boss->pos[2];
                float toPlayerX = dx / dist;
                float toPlayerZ = dz / dist;
                float pastDistance = 20.0f; // Reduced from 50.0f to prevent going too far
                boss->state = BOSS_STATE_CHARGE;
                boss->stateTimer = 0.0f;
                boss->attackCooldown = 2.0f;
                boss->currentAttackId = BOSS_ATTACK_CHARGE;
                boss->currentAttackHasHit = false;
                boss->lockedTargetingPos[0] = character.pos[0] + toPlayerX * pastDistance;
                boss->lockedTargetingPos[1] = character.pos[1];
                boss->lockedTargetingPos[2] = character.pos[2] + toPlayerZ * pastDistance;
                boss->targetingLocked = true;
                boss->targetingUpdateTimer = 0.0f;
            } else if (boss->comboCooldown <= 0.0f && boss->comboStarterCompleted) {
                // Use combo attack
                boss->state = BOSS_STATE_COMBO_ATTACK;
                boss->stateTimer = 0.0f;
                boss->comboCooldown = 15.0f;
                boss->comboStep = 0;
                boss->comboInterrupted = false;
                boss->comboVulnerableTimer = 0.0f;
                boss->lockedTargetingPos[0] = character.pos[0];
                boss->lockedTargetingPos[1] = character.pos[1];
                boss->lockedTargetingPos[2] = character.pos[2];
                boss->targetingLocked = true;
                boss->currentAttackName = "Combo Attack";
                boss->attackNameDisplayTimer = 2.0f;
                boss->currentAttackId = BOSS_ATTACK_COMBO;
            } else if (boss->trackingSlamCooldown <= 0.0f) {
                // Fallback to tracking slam if no other attacks available
                boss->state = BOSS_STATE_TRACKING_SLAM;
                boss->stateTimer = 0.0f;
                boss->trackingSlamCooldown = 8.0f;
                boss->isAttacking = true;
                boss->attackAnimTimer = 0.0f;
                boss->animationTransitionTimer = 0.0f;
                
                // Lock targeting position now (predicted player position)
                if (!boss->targetingLocked) {
                    float predictionTime = 0.3f;
                    predict_character_position(boss->lockedTargetingPos, predictionTime);
                    boss->targetingLocked = true;
                    boss->targetingUpdateTimer = 0.0f;
                }
                
                // Use locked target position for aiming
                float dx = boss->lockedTargetingPos[0] - boss->pos[0];
                float dz = boss->lockedTargetingPos[2] - boss->pos[2];
                float angle = atan2f(-dx, dz);
                boss->trackingSlamTargetAngle = angle;
                
                boss->currentAttackName = "Slow Attack";
                boss->attackNameDisplayTimer = 2.0f;
                boss->currentAttackId = BOSS_ATTACK_TRACKING_SLAM;
            } else {
                // No attack available - use very short recover state to retry quickly
                // This keeps boss aggressive when close without getting stuck
                boss->state = BOSS_STATE_RECOVER;
                boss->stateTimer = 0.0f;
            }
        } else {
            // Only strafe if player is at least 50 units away
            boss->state = BOSS_STATE_STRAFE;
            boss->stateTimer = 0.0f;
        }
    }
}

void boss_ai_update(Boss* boss, BossIntent* out_intent) {
    if (!boss || !out_intent) return;
    
    // Initialize intent
    memset(out_intent, 0, sizeof(BossIntent));
    
    // Don't update AI during cutscenes
    if (!scene_is_boss_active()) {
        bossWasActive = false;
        boss->state = BOSS_STATE_INTRO;
        boss->stateTimer = 0.0f;
        // Still output idle animation intent so skeleton has an animation
        out_intent->anim_req = true;
        out_intent->anim = BOSS_ANIM_IDLE;
        out_intent->priority = BOSS_ANIM_PRIORITY_NORMAL;
        out_intent->force_restart = false;
        out_intent->start_time = 0.0f;
        return;
    }
    
    float dt = deltaTime;
    
    // Check for activation
    bool justActivated = scene_is_boss_active() && !bossWasActive;
    bossWasActive = scene_is_boss_active();
    
    if (justActivated && boss->state == BOSS_STATE_INTRO) {
        boss->state = BOSS_STATE_CHASE;
        boss->stateTimer = 0.0f;
    }
    
    // Advance state timer
    boss->stateTimer += dt;
    
    // Get distance to player
    float dx = character.pos[0] - boss->pos[0];
    float dz = character.pos[2] - boss->pos[2];
    float dist = sqrtf(dx*dx + dz*dz);
    // Clamp denormal values to prevent FPU exceptions
    if (dist != dist || dist < 0.0f) dist = 0.0f;  // Check for NaN and negative
    if (dist > 0.0f && dist < 1e-6f) dist = 0.0f;  // Clamp very small denormals
    
    // Update targeting system
    boss_ai_update_targeting_system(boss, dt);
    
    // Phase switch at 50% HP
    if (boss->phaseIndex == 1 && boss->health <= boss->maxHealth * 0.5f) {
        boss->phaseIndex = 2;
    }
    
    // Update cooldowns
    boss_ai_update_cooldowns(boss, dt);
    
    // Check pending requests (e.g., stagger from damage)
    if (boss->pendingRequests & 0x01) {  // BOSS_REQ_STAGGER
        boss->pendingRequests &= ~0x01;  // Clear flag
        boss->state = BOSS_STATE_STAGGER;
        boss->stateTimer = 0.0f;
        out_intent->anim_req = true;
        out_intent->anim = BOSS_ANIM_ATTACK;  // Stagger animation
        out_intent->priority = BOSS_ANIM_PRIORITY_HIGH;
        out_intent->force_restart = true;
        return;
    }
    
    // State machine - determine next state and output intent
    BossState prevState = boss->state;
    const float COMBAT_RADIUS = boss->orbitRadius;
    
    // Maximum time before forcing an attack (prevents boring behavior)
    const float MAX_CHASE_TIME = 6.0f;
    const float MAX_STRAFE_TIME = 5.0f;
    
    switch (boss->state) {
        case BOSS_STATE_INTRO:
        case BOSS_STATE_NEUTRAL:
            // When under 50 distance, boss should only attack, not chase
            // Retry attack selection immediately - don't wait
            if (dist < 50.0f) {
                boss_ai_select_attack(boss, dist);
            } else if (dist >= 50.0f) {
                boss->state = BOSS_STATE_CHASE;
                boss->stateTimer = 0.0f;
            }
            break;
            
        case BOSS_STATE_CHASE:
            // When under 50 distance, boss should only attack, not chase
            if (dist < 50.0f) {
                boss_ai_select_attack(boss, dist);
                break;
            }
            // Force attack if chasing for too long
            if (boss->stateTimer >= MAX_CHASE_TIME) {
                boss_ai_select_attack(boss, dist);
                break;
            }
            // Trigger attack selection when at combo starter range (75 units), flip attack range (100-200), or long range (power jump)
            if ((dist >= 70.0f && dist <= 80.0f) || (dist >= 100.0f && dist < 200.0f) || dist >= 250.0f) {
                boss_ai_select_attack(boss, dist);
                break;
            }
            // Charge attack: only trigger when player is within 200 distance AND combo starter has completed
            if (boss->attackCooldown <= 0.0f && dist > 0.0f && dist <= 200.0f && boss->comboStarterCompleted) {
                float r = (float)(rand() % 100) / 100.0f;
                if (r < 0.5f) { // 50% chance for charge after combo starter
                    boss->state = BOSS_STATE_CHARGE;
                    boss->stateTimer = 0.0f;
                    boss->attackCooldown = 2.0f;
                    boss->currentAttackId = BOSS_ATTACK_CHARGE;
                    boss->currentAttackHasHit = false;
                    // Calculate position past the player (same direction from boss to player, extended further)
                    // Direction from boss to player
                    float toPlayerX = dx / dist;
                    float toPlayerZ = dz / dist;
                    // Position past player: player position plus direction to player (scaled by distance to go past)
                    float pastDistance = 20.0f; // Reduced from 50.0f to prevent going too far // Distance past player to target
                    boss->lockedTargetingPos[0] = character.pos[0] + toPlayerX * pastDistance;
                    boss->lockedTargetingPos[1] = character.pos[1];
                    boss->lockedTargetingPos[2] = character.pos[2] + toPlayerZ * pastDistance;
                    boss->targetingLocked = true;
                    boss->targetingUpdateTimer = 0.0f;
                } else {
                    boss_ai_select_attack(boss, dist);
                }
            }
            break;
            
        case BOSS_STATE_STRAFE:
            // When under 50 distance, boss should only attack, not strafe
            if (dist < 50.0f) {
                boss_ai_select_attack(boss, dist);
                break;
            }
            if (dist > COMBAT_RADIUS + 350.0f && boss->stateTimer > 0.1f) {
                boss->state = BOSS_STATE_CHASE;
                boss->stateTimer = 0.0f;
                break;
            }
            // Force attack if strafing for too long
            if (boss->stateTimer >= MAX_STRAFE_TIME) {
                boss_ai_select_attack(boss, dist);
                break;
            }
            // Trigger attack selection when at combo starter range (75 units) or flip attack range (100-200)
            if ((dist >= 70.0f && dist <= 80.0f) || (dist >= 100.0f && dist < 200.0f)) {
                boss_ai_select_attack(boss, dist);
                break;
            }
            // Charge attack: only trigger when player is within 200 distance AND combo starter has completed
            if (boss->attackCooldown <= 0.0f && boss->stateTimer >= 3.0f && dist > 0.0f && dist <= 200.0f && boss->comboStarterCompleted) {
                float r = (float)(rand() % 100) / 100.0f;
                if (r < 0.5f) { // 50% chance for charge after combo starter
                    boss->state = BOSS_STATE_CHARGE;
                    boss->stateTimer = 0.0f;
                    boss->attackCooldown = 2.0f;
                    boss->currentAttackId = BOSS_ATTACK_CHARGE;
                    boss->currentAttackHasHit = false;
                    // Calculate position past the player (same direction from boss to player, extended further)
                    // Direction from boss to player
                    float toPlayerX = dx / dist;
                    float toPlayerZ = dz / dist;
                    // Position past player: player position plus direction to player (scaled by distance to go past)
                    float pastDistance = 20.0f; // Reduced from 50.0f to prevent going too far // Distance past player to target
                    boss->lockedTargetingPos[0] = character.pos[0] + toPlayerX * pastDistance;
                    boss->lockedTargetingPos[1] = character.pos[1];
                    boss->lockedTargetingPos[2] = character.pos[2] + toPlayerZ * pastDistance;
                    boss->targetingLocked = true;
                    boss->targetingUpdateTimer = 0.0f;
                } else {
                    boss_ai_select_attack(boss, dist);
                }
            }
            break;
            
        case BOSS_STATE_CHARGE:
            {
                // Ensure targeting is locked (in case it wasn't set when entering state)
                if (!boss->targetingLocked) {
                    // Calculate position past the player (same direction from boss to player, extended further)
                    float toPlayerX = dx / dist;
                    float toPlayerZ = dz / dist;
                    float pastDistance = 20.0f; // Reduced from 50.0f to prevent going too far // Distance past player to target
                    boss->lockedTargetingPos[0] = character.pos[0] + toPlayerX * pastDistance;
                    boss->lockedTargetingPos[1] = character.pos[1];
                    boss->lockedTargetingPos[2] = character.pos[2] + toPlayerZ * pastDistance;
                    boss->targetingLocked = true;
                    boss->targetingUpdateTimer = 0.0f;
                }
                
                float targetDx = boss->lockedTargetingPos[0] - boss->pos[0];
                float targetDz = boss->lockedTargetingPos[2] - boss->pos[2];
                float distToTarget = sqrtf(targetDx*targetDx + targetDz*targetDz);
                
                // End charge when: reached target (close enough) AND minimum time elapsed, OR max time exceeded
                bool reachedTarget = distToTarget < 5.0f;
                // Minimum charge time to allow animation to play (2.5 seconds)
                bool minimumChargeTime = boss->stateTimer >= 2.5f;
                bool maximumChargeTime = boss->stateTimer > 5.0f;
                
                if ((reachedTarget && minimumChargeTime) || maximumChargeTime) {
                    // Reset combo starter flag after using charge
                    boss->comboStarterCompleted = false;
                    // Immediately do another attack after charge finishes
                    boss_ai_select_attack(boss, dist);
                }
            }
            break;
            
        case BOSS_STATE_RECOVER:
            // When under 50 distance, chain attacks immediately (no delay)
            // When far away, use a short recovery time
            float recoverTime = (dist < 50.0f) ? 0.0f : 0.3f;
            if (boss->stateTimer > recoverTime) {
                // When under 50 distance, boss should only attack, not chase or strafe
                if (dist < 50.0f) {
                    boss_ai_select_attack(boss, dist);
                } else if (dist > COMBAT_RADIUS + 10.0f) {
                    boss->state = BOSS_STATE_CHASE;
                } else {
                    boss->state = BOSS_STATE_STRAFE;
                }
                boss->stateTimer = 0.0f;
            }
            break;
            
        case BOSS_STATE_STAGGER:
            if (boss->stateTimer > 0.5f) {
                boss->state = BOSS_STATE_RECOVER;
                boss->stateTimer = 0.0f;
            }
            break;
            
        case BOSS_STATE_DEAD:
            // Stay dead
            break;
            
        // Attack states - these handle their own transitions
        case BOSS_STATE_POWER_JUMP:
            // Power jump: Transitions when animation completes (isAttacking becomes false)
            // and animation blend completes
            if (!boss->isAttacking) {
                // Wait for blend to complete before changing state
                boss->animationTransitionTimer += dt;
                if (boss->animationTransitionTimer >= boss->blendDuration) {
                    // Check if we should attack before transitioning to movement states
                    // This prevents a 1-frame STRAFE state that immediately transitions to an attack
                    bool shouldAttack = false;
                    if (dist < 50.0f) {
                        // Close range - always attack
                        shouldAttack = true;
                    } else if ((dist >= 70.0f && dist <= 80.0f && boss->comboStarterCooldown <= 0.0f) ||
                               (dist >= 100.0f && dist < 200.0f && boss->flipAttackCooldown <= 0.0f) ||
                               (dist >= 250.0f && boss->powerJumpCooldown <= 0.0f)) {
                        // In range for an attack and cooldown is ready
                        shouldAttack = true;
                    }
                    
                    if (shouldAttack) {
                        boss_ai_select_attack(boss, dist);
                    } else if (dist >= 50.0f) {
                        boss->state = BOSS_STATE_STRAFE;
                    } else {
                        boss->state = BOSS_STATE_CHASE;
                    }
                    boss->stateTimer = 0.0f;
                    boss->animationTransitionTimer = 0.0f;
                    bossPowerJumpImpactPlayed = false; // Reset for next time
                }
            }
            break;
            
        case BOSS_STATE_FLIP_ATTACK:
            // Flip attack: 3 phases over 4.0s
            // Phase 1: Idle preparation (0.0 - 2.0s)
            // Phase 2: Jump arc (2.0 - 3.0s)
            // Phase 3: Landing impact + recovery (3.0 - 4.0s)
            // End: Transition to strafe (4.0s+)
            {
                const float idleDuration = 2.0f;      // 2.0s idle preparation
                const float jumpDuration = 1.0f;      // 1.0s jump arc through air
                const float recoverDuration = 1.0f;   // 1.0s recovery on ground
                const float totalDuration = idleDuration + jumpDuration + recoverDuration;
                
                // Clamp stateTimer to prevent denormals in comparison
                float clampedTimer = boss->stateTimer;
                if (clampedTimer != clampedTimer || clampedTimer < 0.0f) clampedTimer = 0.0f;
                if (clampedTimer > 0.0f && clampedTimer < 1e-6f) clampedTimer = 0.0f;
                
                if (clampedTimer >= totalDuration) {
                    // When under 50 distance, boss should only attack, not chase or strafe
                    if (dist < 50.0f) {
                        boss_ai_select_attack(boss, dist);
                    } else if (dist >= 50.0f) {
                        boss->state = BOSS_STATE_STRAFE;
                    } else {
                        boss->state = BOSS_STATE_CHASE;
                    }
                    boss->stateTimer = 0.0f;
                }
            }
            break;
            
        case BOSS_STATE_COMBO_ATTACK:
            // Combo attack: 3 steps over 2.9s
            // Each step is 0.8s, plus 0.5s recovery after last step
            // Can be interrupted during vulnerable windows
            // Target updates slowly with player movement
            {
                // Slowly update target position with player movement (lerp)
                const float targetLerpSpeed = 0.1f; // Slow update rate
                boss->lockedTargetingPos[0] += (character.pos[0] - boss->lockedTargetingPos[0]) * targetLerpSpeed;
                boss->lockedTargetingPos[1] = character.pos[1];
                boss->lockedTargetingPos[2] += (character.pos[2] - boss->lockedTargetingPos[2]) * targetLerpSpeed;
                
                const float stepDuration = 0.8f;
                const float totalDuration = stepDuration * 3 + 1.5f; // Increased recovery time to let animation finish
                
                if (boss->comboInterrupted) {
                    // Already transitioned to RECOVER by attack handler
                    // Just reset combo state
                    boss->comboStep = 0;
                    boss->comboInterrupted = false;
                } else if (boss->stateTimer > totalDuration) {
                    boss->comboStep = 0;
                    boss->comboInterrupted = false;
                    // Reset combo starter flag after using charge or combo attack
                    boss->comboStarterCompleted = false;
                    // When under 50 distance, boss should only attack, not chase or strafe
                    if (dist < 50.0f) {
                        boss_ai_select_attack(boss, dist);
                    } else if (dist >= 50.0f) {
                        boss->state = BOSS_STATE_STRAFE;
                    } else {
                        boss->state = BOSS_STATE_CHASE;
                    }
                    boss->stateTimer = 0.0f;
                }
            }
            break;
            
        case BOSS_STATE_COMBO_STARTER:
            // Combo starter attack: 4 phases over 2.0 seconds
            // Phase 1: Throw sword (0.0 - 0.5s)
            // Phase 2: Sword flight (0.5 - 1.0s)
            // Phase 3: Sword slam (1.0s+)
            // Phase 4: Boss stays in place (1.5s - 2.0s)
            // End: Transition to charge/combo attack immediately (2.0s+)
            if (boss->stateTimer >= 2.0f) {
                // Mark combo starter as completed - enables charge and ComboAttack
                boss->comboStarterCompleted = true;
                // Reset attack state
                boss->swordThrown = false;
                boss->comboStarterSlamHasHit = false;
                boss->velX = 0.0f;
                boss->velZ = 0.0f;
                
                // Immediately check for charge or combo attack after combo starter completes
                // Randomly choose between charge and combo attack when both are available
                bool chargeAvailable = (boss->attackCooldown <= 0.0f && dist > 0.0f && dist <= 200.0f);
                bool comboAvailable = (boss->comboCooldown <= 0.0f);
                
                if (chargeAvailable && comboAvailable) {
                    // Both available - randomly choose
                    float r = (float)(rand() % 100) / 100.0f;
                    if (r < 0.5f) {
                        // Choose charge attack
                        boss->state = BOSS_STATE_CHARGE;
                        boss->stateTimer = 0.0f;
                        boss->attackCooldown = 2.0f;
                        boss->currentAttackId = BOSS_ATTACK_CHARGE;
                        boss->currentAttackHasHit = false;
                        // Calculate position past the player
                        float toPlayerX = dx / dist;
                        float toPlayerZ = dz / dist;
                        float pastDistance = 20.0f; // Reduced from 50.0f to prevent going too far
                        boss->lockedTargetingPos[0] = character.pos[0] + toPlayerX * pastDistance;
                        boss->lockedTargetingPos[1] = character.pos[1];
                        boss->lockedTargetingPos[2] = character.pos[2] + toPlayerZ * pastDistance;
                        boss->targetingLocked = true;
                        boss->targetingUpdateTimer = 0.0f;
                        break;
                    } else {
                        // Choose combo attack
                        boss->state = BOSS_STATE_COMBO_ATTACK;
                        boss->stateTimer = 0.0f;
                        boss->comboCooldown = 15.0f;
                        boss->comboStep = 0;
                        boss->comboInterrupted = false;
                        boss->comboVulnerableTimer = 0.0f;
                        boss->lockedTargetingPos[0] = character.pos[0];
                        boss->lockedTargetingPos[1] = character.pos[1];
                        boss->lockedTargetingPos[2] = character.pos[2];
                        boss->targetingLocked = true;
                        boss->currentAttackName = "Combo Attack";
                        boss->attackNameDisplayTimer = 2.0f;
                        boss->currentAttackId = BOSS_ATTACK_COMBO;
                        break;
                    }
                } else if (chargeAvailable) {
                    // Only charge available
                    boss->state = BOSS_STATE_CHARGE;
                    boss->stateTimer = 0.0f;
                    boss->attackCooldown = 2.0f;
                    boss->currentAttackId = BOSS_ATTACK_CHARGE;
                    boss->currentAttackHasHit = false;
                    // Calculate position past the player
                    float toPlayerX = dx / dist;
                    float toPlayerZ = dz / dist;
                    float pastDistance = 20.0f; // Reduced from 50.0f to prevent going too far
                    boss->lockedTargetingPos[0] = character.pos[0] + toPlayerX * pastDistance;
                    boss->lockedTargetingPos[1] = character.pos[1];
                    boss->lockedTargetingPos[2] = character.pos[2] + toPlayerZ * pastDistance;
                    boss->targetingLocked = true;
                    boss->targetingUpdateTimer = 0.0f;
                    break;
                } else if (comboAvailable) {
                    // Only combo available
                    boss->state = BOSS_STATE_COMBO_ATTACK;
                    boss->stateTimer = 0.0f;
                    boss->comboCooldown = 15.0f;
                    boss->comboStep = 0;
                    boss->comboInterrupted = false;
                    boss->comboVulnerableTimer = 0.0f;
                    boss->lockedTargetingPos[0] = character.pos[0];
                    boss->lockedTargetingPos[1] = character.pos[1];
                    boss->lockedTargetingPos[2] = character.pos[2];
                    boss->targetingLocked = true;
                    boss->currentAttackName = "Combo Attack";
                    boss->attackNameDisplayTimer = 2.0f;
                    boss->currentAttackId = BOSS_ATTACK_COMBO;
                    break;
                }
                
                // If no charge/combo available, transition to movement state
                if (dist < 50.0f) {
                    boss_ai_select_attack(boss, dist);
                } else if (dist >= 50.0f) {
                    boss->state = BOSS_STATE_STRAFE;
                } else {
                    boss->state = BOSS_STATE_CHASE;
                }
                boss->stateTimer = 0.0f;
            }
            break;
            
        case BOSS_STATE_ROAR_STOMP:
            // Roar stomp: 3 phases over 2.0s
            // Phase 1: Roar buildup (0.0 - 1.0s)
            // Phase 2: Stomp impact (1.0 - 1.1s)
            // Phase 3: Recovery (1.1s+)
            // End: Transition to strafe (2.0s+)
            if (boss->stateTimer > 2.0f) {
                // When under 50 distance, boss should only attack, not chase or strafe
                if (dist < 50.0f) {
                    boss_ai_select_attack(boss, dist);
                } else if (dist >= 50.0f) {
                    boss->state = BOSS_STATE_STRAFE;
                } else {
                    boss->state = BOSS_STATE_CHASE;
                }
                boss->stateTimer = 0.0f;
                bossRoarImpactSoundPlayed = false; // Reset for next time
            }
            break;
            
        case BOSS_STATE_TRACKING_SLAM:
            // Tracking slam: Stationary attack
            // Transitions when animation completes (isAttacking becomes false)
            // and animation blend completes
            if (!boss->isAttacking) {
                // Wait for blend to complete before changing state
                boss->animationTransitionTimer += dt;
                if (boss->animationTransitionTimer >= boss->blendDuration) {
                    // When under 50 distance, boss should only attack, not chase or strafe
                    if (dist < 50.0f) {
                        boss_ai_select_attack(boss, dist);
                    } else if (dist >= 50.0f) {
                        boss->state = BOSS_STATE_STRAFE;
                    } else {
                        boss->state = BOSS_STATE_CHASE;
                    }
                    boss->stateTimer = 0.0f;
                    boss->animationTransitionTimer = 0.0f;
                }
            }
            break;
            
    }
    
    // Output animation intent based on state
    // Always output an animation request to ensure skeleton has an animation attached
    // For attack states, always output animation request (not just on state change)
    bool isAttackState = boss_ai_state_is_attack(boss->state);
    if (boss->state != prevState || !out_intent->anim_req || isAttackState) {
        out_intent->anim_req = true;
        out_intent->force_restart = false;
        out_intent->start_time = 0.0f;
        out_intent->priority = BOSS_ANIM_PRIORITY_NORMAL;
        
        // Map state to animation
        switch (boss->state) {
            case BOSS_STATE_COMBO_ATTACK:
                out_intent->anim = BOSS_ANIM_COMBO_ATTACK;
                out_intent->priority = BOSS_ANIM_PRIORITY_HIGH;
                // Force restart animation when entering combo attack state (not on every frame)
                if (boss->state != prevState) {
                    out_intent->force_restart = true;
                }
                break;
            case BOSS_STATE_POWER_JUMP:
                out_intent->anim = BOSS_ANIM_JUMP_FORWARD;
                out_intent->priority = BOSS_ANIM_PRIORITY_HIGH;
                break;
            case BOSS_STATE_FLIP_ATTACK:
                out_intent->anim = BOSS_ANIM_FLIP_ATTACK;
                out_intent->priority = BOSS_ANIM_PRIORITY_HIGH;
                break;
            case BOSS_STATE_CHARGE:
                out_intent->anim = BOSS_ANIM_COMBO_LUNGE;
                out_intent->priority = BOSS_ANIM_PRIORITY_HIGH;
                // Force restart animation when entering charge state (not on every frame)
                if (boss->state != prevState) {
                    out_intent->force_restart = true;
                }
                break;
            case BOSS_STATE_TRACKING_SLAM:
            case BOSS_STATE_ROAR_STOMP:
                out_intent->anim = BOSS_ANIM_ATTACK;
                out_intent->priority = BOSS_ANIM_PRIORITY_HIGH;
                break;
            case BOSS_STATE_COMBO_STARTER:
                out_intent->anim = BOSS_ANIM_COMBO_STARTER;
                out_intent->priority = BOSS_ANIM_PRIORITY_HIGH;
                break;
            case BOSS_STATE_STRAFE:
                // Calculate strafe direction based on character movement
                {
                    float dx = character.pos[0] - boss->pos[0];
                    float dz = character.pos[2] - boss->pos[2];
                    float dist = sqrtf(dx*dx + dz*dz);
                    
                    if (dist > 0.0f) {
                        // Normalize direction to character
                        float toCharX = dx / dist;
                        float toCharZ = dz / dist;
                        
                        // Get character's velocity to determine strafe direction
                        float charVelX, charVelZ;
                        character_get_velocity(&charVelX, &charVelZ);
                        
                        // Calculate perpendicular directions
                        float leftX = -toCharZ;
                        float leftZ = toCharX;
                        float rightX = toCharZ;
                        float rightZ = -toCharX;
                        
                        // Project character velocity onto left/right perpendicular vectors
                        float leftDot = charVelX * leftX + charVelZ * leftZ;
                        float rightDot = charVelX * rightX + charVelZ * rightZ;
                        
                        // Update strafe direction based on character's lateral movement
                        // Only update if there's meaningful movement (avoid jitter when stationary)
                        const float MOVEMENT_THRESHOLD = 5.0f;
                        float maxLateralMovement = fmaxf(fabsf(leftDot), fabsf(rightDot));
                        
                        if (maxLateralMovement > MOVEMENT_THRESHOLD) {
                            // Character is moving - follow their lateral movement
                            if (fabsf(leftDot) > fabsf(rightDot)) {
                                // Character moving more in left direction
                                bossStrafeDirection = (leftDot > 0.0f) ? -1.0f : 1.0f;
                            } else {
                                // Character moving more in right direction
                                bossStrafeDirection = (rightDot > 0.0f) ? 1.0f : -1.0f;
                            }
                            strafeDirectionTimer = 0.0f; // Reset timer when character moves
                        } else {
                            // Character is stationary or moving very little
                            // Alternate direction every few seconds to prevent always going right
                            strafeDirectionTimer += dt;
                            const float ALTERNATE_TIME = 3.0f;
                            if (strafeDirectionTimer >= ALTERNATE_TIME) {
                                bossStrafeDirection = -bossStrafeDirection; // Flip direction
                                strafeDirectionTimer = 0.0f;
                            }
                        }
                    }
                }
                // Use left or right strafe based on direction
                out_intent->anim = (bossStrafeDirection > 0.0f) ? BOSS_ANIM_STRAFE_RIGHT : BOSS_ANIM_STRAFE_LEFT;
                break;
            case BOSS_STATE_CHASE:
                out_intent->anim = BOSS_ANIM_WALK;
                break;
            case BOSS_STATE_STAGGER:
                out_intent->anim = BOSS_ANIM_ATTACK;
                out_intent->priority = BOSS_ANIM_PRIORITY_HIGH;
                break;
            case BOSS_STATE_DEAD:
                out_intent->anim = BOSS_ANIM_IDLE;
                out_intent->priority = BOSS_ANIM_PRIORITY_CRITICAL;
                break;
            default:
                out_intent->anim = BOSS_ANIM_IDLE;
                break;
        }
    }
    
    // Output attack request if starting a new attack
    if (boss_ai_state_is_attack(boss->state) && prevState != boss->state) {
        out_intent->attack_req = true;
        out_intent->attack = boss->currentAttackId;
    }
    
    // Sound triggers on state entry (matching old boss code behavior)
    // Note: Sound system integration would go here when available
    if (prevState != boss->state) {
        switch (boss->state) {
            case BOSS_STATE_POWER_JUMP:
                bossPowerJumpImpactPlayed = false;
                // boss_debug_sound("boss_power_jump_windup");
                break;
            case BOSS_STATE_FLIP_ATTACK:
                // boss_debug_sound("boss_flip_attack_windup");
                break;
            case BOSS_STATE_COMBO_ATTACK:
                // boss_debug_sound("boss_combo_sweep");
                break;
            case BOSS_STATE_COMBO_STARTER:
                // boss_debug_sound("boss_combo_starter_throw");
                break;
            case BOSS_STATE_ROAR_STOMP:
                bossRoarImpactSoundPlayed = false;
                // boss_debug_sound("boss_roar_buildup");
                break;
            case BOSS_STATE_TRACKING_SLAM:
                // boss_debug_sound("boss_tracking_slam_charge");
                break;
            case BOSS_STATE_CHARGE:
                // boss_debug_sound("boss_charge_footsteps");
                break;
            case BOSS_STATE_CHASE:
                // boss_debug_sound("boss_footstep_heavy");
                break;
            case BOSS_STATE_STRAFE:
                // boss_debug_sound("boss_footstep_heavy");
                // Set initial attack cooldown when entering strafe to ensure minimum strafe duration
                if (boss->attackCooldown <= 0.0f) {
                    boss->attackCooldown = 2.0f;
                }
                break;
            default:
                break;
        }
    }
}


