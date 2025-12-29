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
static void boss_ai_update_movement(Boss* boss, float dt);
static void boss_ai_select_attack(Boss* boss, float dist);
static bool boss_ai_state_is_attack(BossState state);
static void predict_character_position(float *predictedPos, float predictionTime);

// Static state for AI (telegraph, activation tracking, etc.)
static bool bossWasActive = false;
static bool chargeHasPassedPlayer = false;
static float bossStrafeDirection = 1.0f; // 1.0 = right, -1.0 = left

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
    boss->chainSwordCooldown = 0.0f;
    boss->roarStompCooldown = 0.0f;
    boss->trackingSlamCooldown = 0.0f;
    
    // Initialize attack state
    boss->isAttacking = false;
    boss->attackAnimTimer = 0.0f;
    boss->currentAttackHasHit = false;
    boss->currentAttackId = BOSS_ATTACK_COUNT;
    
    bossWasActive = false;
    chargeHasPassedPlayer = false;
}

static bool boss_ai_state_is_attack(BossState state) {
    return state == BOSS_STATE_CHARGE
        || state == BOSS_STATE_ATTACK
        || state == BOSS_STATE_POWER_JUMP
        || state == BOSS_STATE_COMBO_ATTACK
        || state == BOSS_STATE_CHAIN_SWORD
        || state == BOSS_STATE_ROAR_STOMP
        || state == BOSS_STATE_TRACKING_SLAM;
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
            case BOSS_STATE_CHAIN_SWORD:
                predictionTime = 0.8f;
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
    if (boss->chainSwordCooldown > 0.0f) boss->chainSwordCooldown -= dt;
    if (boss->roarStompCooldown > 0.0f) boss->roarStompCooldown -= dt;
    if (boss->trackingSlamCooldown > 0.0f) boss->trackingSlamCooldown -= dt;
    
    if (boss->attackNameDisplayTimer > 0.0f) boss->attackNameDisplayTimer -= dt;
    if (boss->hitMessageTimer > 0.0f) boss->hitMessageTimer -= dt;
}

static void boss_ai_select_attack(Boss* boss, float dist) {
    boss->currentAttackHasHit = false;
    
    if (dist <= 200.0f && boss->trackingSlamCooldown <= 0.0f) {
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
    else if (dist <= 200.0f && boss->roarStompCooldown <= 0.0f) {
        boss->state = BOSS_STATE_ROAR_STOMP;
        boss->stateTimer = 0.0f;
        boss->roarStompCooldown = 6.0f;
        boss->currentAttackName = "Roar Stomp";
        boss->attackNameDisplayTimer = 2.0f;
        boss->currentAttackId = BOSS_ATTACK_ROAR_STOMP;
    }
    else if (dist > 200.0f && boss->chainSwordCooldown <= 0.0f) {
        boss->state = BOSS_STATE_CHAIN_SWORD;
        boss->stateTimer = 0.0f;
        boss->chainSwordCooldown = 10.0f;
        boss->swordThrown = false;
        boss->chainSwordSlamHasHit = false;
        boss->chainSwordTargetPos[0] = boss->lockedTargetingPos[0];
        boss->chainSwordTargetPos[1] = boss->lockedTargetingPos[1];
        boss->chainSwordTargetPos[2] = boss->lockedTargetingPos[2];
        boss->currentAttackName = "Chain Sword";
        boss->attackNameDisplayTimer = 2.0f;
        boss->currentAttackId = BOSS_ATTACK_CHAIN_SWORD;
    }
    else if (boss->powerJumpCooldown <= 0.0f && dist >= 400.0f) {
        boss->state = BOSS_STATE_POWER_JUMP;
        boss->stateTimer = 0.0f;
        boss->powerJumpCooldown = 12.0f;
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
    else if (boss->comboCooldown <= 0.0f && boss->phaseIndex == 2) {
        boss->state = BOSS_STATE_COMBO_ATTACK;
        boss->stateTimer = 0.0f;
        boss->comboCooldown = 15.0f;
        boss->comboStep = 0;
        boss->comboInterrupted = false;
        boss->comboVulnerableTimer = 0.0f;
        boss->currentAttackName = "Combo Attack";
        boss->attackNameDisplayTimer = 2.0f;
        boss->currentAttackId = BOSS_ATTACK_COMBO;
    }
    else if (boss->roarStompCooldown <= 0.0f) {
        boss->state = BOSS_STATE_ROAR_STOMP;
        boss->stateTimer = 0.0f;
        boss->roarStompCooldown = 6.0f;
        boss->currentAttackName = "Roar Stomp";
        boss->attackNameDisplayTimer = 2.0f;
        boss->currentAttackId = BOSS_ATTACK_ROAR_STOMP;
    }
    else {
        boss->state = BOSS_STATE_STRAFE;
        boss->stateTimer = 0.0f;
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
    
    switch (boss->state) {
        case BOSS_STATE_INTRO:
        case BOSS_STATE_NEUTRAL:
            if (dist < 40.0f) {
                boss->state = BOSS_STATE_CHASE;
                boss->stateTimer = 0.0f;
            }
            break;
            
        case BOSS_STATE_CHASE:
            if (dist >= 400.0f && boss->powerJumpCooldown <= 0.0f) {
                boss_ai_select_attack(boss, dist);
                break;
            }
            if (boss->attackCooldown <= 0.0f && dist >= 0.0f && dist <= 200.0f) {
                float r = (float)(rand() % 100) / 100.0f;
                if (r < 0.3f) {
                    boss->state = BOSS_STATE_CHARGE;
                    boss->stateTimer = 0.0f;
                    boss->attackCooldown = 2.0f;
                    chargeHasPassedPlayer = false;
                    boss->currentAttackId = BOSS_ATTACK_CHARGE;
                } else {
                    boss_ai_select_attack(boss, dist);
                }
            }
            break;
            
        case BOSS_STATE_STRAFE:
            if (dist > COMBAT_RADIUS + 350.0f && boss->stateTimer > 0.1f) {
                boss->state = BOSS_STATE_CHASE;
                boss->stateTimer = 0.0f;
                break;
            }
            if (boss->attackCooldown <= 0.0f && boss->stateTimer >= 3.0f && dist <= 100.0f) {
                float r = (float)(rand() % 100) / 100.0f;
                if (r < 0.3f) {
                    boss->state = BOSS_STATE_CHARGE;
                    boss->stateTimer = 0.0f;
                    boss->attackCooldown = 2.0f;
                    chargeHasPassedPlayer = false;
                    boss->currentAttackId = BOSS_ATTACK_CHARGE;
                } else {
                    boss_ai_select_attack(boss, dist);
                }
            }
            break;
            
        case BOSS_STATE_CHARGE:
            {
                float targetDx = boss->lockedTargetingPos[0] - boss->pos[0];
                float targetDz = boss->lockedTargetingPos[2] - boss->pos[2];
                float distToTarget = sqrtf(targetDx*targetDx + targetDz*targetDz);
                
                if (!chargeHasPassedPlayer && distToTarget < 3.0f) {
                    chargeHasPassedPlayer = true;
                }
                
                bool reachedTarget = chargeHasPassedPlayer;
                bool minimumChargeTime = boss->stateTimer >= 1.5f;
                bool maximumChargeTime = boss->stateTimer > 3.0f;
                
                if ((reachedTarget && minimumChargeTime) || maximumChargeTime) {
                    boss->state = BOSS_STATE_RECOVER;
                    boss->stateTimer = 0.0f;
                    chargeHasPassedPlayer = false;
                }
            }
            break;
            
        case BOSS_STATE_RECOVER:
            if (boss->stateTimer > 0.8f) {
                if (dist > COMBAT_RADIUS + 10.0f) {
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
            // Power jump: 3 phases over 4.533s
            // Phase 1: Idle preparation (0.0 - 1.367s)
            // Phase 2: Jump arc (1.367 - 2.767s)
            // Phase 3: Landing impact + recovery (2.767 - 4.533s)
            // End: Transition to strafe (4.533s+)
            {
                const float idleDuration = 41.0f / 25.0f;      // 1.367s
                const float jumpDuration = 42.0f / 25.0f;      // 1.4s
                const float recoverDuration = 53.0f / 25.0f;   // 1.767s
                const float totalDuration = idleDuration + jumpDuration + recoverDuration;
                
                if (boss->stateTimer >= totalDuration) {
                    boss->state = BOSS_STATE_STRAFE;
                    boss->stateTimer = 0.0f;
                    bossPowerJumpImpactPlayed = false; // Reset for next time
                }
            }
            break;
            
        case BOSS_STATE_COMBO_ATTACK:
            // Combo attack: 3 steps over 2.9s
            // Each step is 0.8s, plus 0.5s recovery after last step
            // Can be interrupted during vulnerable windows
            {
                const float stepDuration = 0.8f;
                const float totalDuration = stepDuration * 3 + 0.5f;
                
                if (boss->comboInterrupted) {
                    // Already transitioned to RECOVER by attack handler
                    // Just reset combo state
                    boss->comboStep = 0;
                    boss->comboInterrupted = false;
                } else if (boss->stateTimer > totalDuration) {
                    boss->comboStep = 0;
                    boss->comboInterrupted = false;
                    boss->state = BOSS_STATE_STRAFE;
                    boss->stateTimer = 0.0f;
                }
            }
            break;
            
        case BOSS_STATE_CHAIN_SWORD:
            // Chain sword attack: 4 phases over 3.5 seconds
            // Phase 1: Throw sword (0.0 - 0.5s)
            // Phase 2: Sword flight (0.5 - 1.5s)
            // Phase 3: Sword slam (1.5s+)
            // Phase 4: Pull boss toward sword (2.0s - 3.5s)
            // End: Transition to strafe (3.5s+)
            if (boss->stateTimer >= 3.5f) {
                // Reset attack state
                boss->swordThrown = false;
                boss->chainSwordSlamHasHit = false;
                boss->velX = 0.0f;
                boss->velZ = 0.0f;
                boss->state = BOSS_STATE_STRAFE;
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
                boss->state = BOSS_STATE_STRAFE;
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
                    boss->state = BOSS_STATE_STRAFE;
                    boss->stateTimer = 0.0f;
                    boss->animationTransitionTimer = 0.0f;
                }
            }
            break;
            
        case BOSS_STATE_ATTACK:
            // Generic attack transitions handled elsewhere
            break;
    }
    
    // Output animation intent based on state
    // Always output an animation request to ensure skeleton has an animation attached
    if (boss->state != prevState || !out_intent->anim_req) {
        out_intent->anim_req = true;
        out_intent->force_restart = false;
        out_intent->start_time = 0.0f;
        out_intent->priority = BOSS_ANIM_PRIORITY_NORMAL;
        
        // Map state to animation
        switch (boss->state) {
            case BOSS_STATE_COMBO_ATTACK:
                out_intent->anim = BOSS_ANIM_COMBO_ATTACK;
                out_intent->priority = BOSS_ANIM_PRIORITY_HIGH;
                break;
            case BOSS_STATE_POWER_JUMP:
                out_intent->anim = BOSS_ANIM_JUMP_FORWARD;
                out_intent->priority = BOSS_ANIM_PRIORITY_HIGH;
                break;
            case BOSS_STATE_ATTACK:
            case BOSS_STATE_CHARGE:
            case BOSS_STATE_TRACKING_SLAM:
            case BOSS_STATE_ROAR_STOMP:
            case BOSS_STATE_CHAIN_SWORD:
                out_intent->anim = BOSS_ANIM_ATTACK;
                out_intent->priority = BOSS_ANIM_PRIORITY_HIGH;
                break;
            case BOSS_STATE_STRAFE:
                // Use left or right strafe based on direction
                out_intent->anim = (bossStrafeDirection > 0.0f) ? BOSS_ANIM_STRAFE_LEFT : BOSS_ANIM_STRAFE_RIGHT;
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
            case BOSS_STATE_COMBO_ATTACK:
                // boss_debug_sound("boss_combo_sweep");
                break;
            case BOSS_STATE_CHAIN_SWORD:
                // boss_debug_sound("boss_chain_sword_throw");
                // boss_debug_sound("boss_chain_rattle");
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

// Movement update (simplified - full implementation would be in boss_update_movement)
static void boss_ai_update_movement(Boss* boss, float dt) {
    // Movement logic is complex and involves collision, so it's kept in boss.c
    // This is a placeholder for future refactoring
}

