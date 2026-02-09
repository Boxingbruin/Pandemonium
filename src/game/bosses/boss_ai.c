/*
 * boss_ai.c
 * 
 * AI module - decides intent (states/attacks)
 * Must NOT include tiny3d animation headers
 */

#include <t3d/t3dmath.h>

#include "boss_ai.h"
#include "boss.h"
#include "boss_sfx.h"

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
static float boss_ai_attack_dust_delay_s(BossAttackId id);

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
    boss->trackingSlamCooldown = 0.0f;
    boss->flipAttackCooldown = 0.0f;
    
    // Initialize attack state
    boss->isAttacking = false;
    boss->attackAnimTimer = 0.0f;
    boss->currentAttackHasHit = false;
    boss->currentAttackId = BOSS_ATTACK_COUNT;
    
    boss->comboLungeTracksPlayer = false;
    boss->comboLungeLockedYaw = 0.0f;

    bossWasActive = false;
    bossStrafeDirection = 1.0f;
    strafeDirectionTimer = 0.0f;
    boss->strafeDirection = 1.0f;
}

static bool boss_ai_state_is_attack(BossState state) {
    return state == BOSS_STATE_LUNGE_STARTER
        || state == BOSS_STATE_COMBO_LUNGE
        || state == BOSS_STATE_POWER_JUMP
        || state == BOSS_STATE_COMBO_ATTACK
        || state == BOSS_STATE_COMBO_STARTER
        || state == BOSS_STATE_TRACKING_SLAM
        || state == BOSS_STATE_FLIP_ATTACK
        || state == BOSS_STATE_STOMP
        || state == BOSS_STATE_ATTACK1;
}

static inline float desired_yaw_to_player(const Boss* boss) {
    float dx = character.pos[0] - boss->pos[0];
    float dz = character.pos[2] - boss->pos[2];
    if (dx == 0.0f && dz == 0.0f) return boss->rot[1];
    return -atan2f(-dz, dx) + T3D_PI;
}

// Per-attack dust timing offset (seconds) from the logical impact moment.
// Default matches previous hardcoded behavior (0.20f) but can be tuned per attack.
static float boss_ai_attack_dust_delay_s(BossAttackId id)
{
    switch (id) {
        case BOSS_ATTACK_POWER_JUMP:      return 0.20f;
        case BOSS_ATTACK_TRACKING_SLAM:   return 0.20f;
        case BOSS_ATTACK_STOMP:           return 0.20f;
        case BOSS_ATTACK_FLIP_ATTACK:     return 0.20f;
        case BOSS_ATTACK_COMBO:           return 0.20f;
        case BOSS_ATTACK_COMBO_STARTER:   return 0.20f;
        case BOSS_ATTACK_COMBO_LUNGE:     return 0.20f;
        case BOSS_ATTACK_LUNGE_STARTER:   return 0.20f;
        case BOSS_ATTACK_ATTACK1:         return 0.20f;
        case BOSS_ATTACK_COUNT:
        default:
            return 0.20f;
    }
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
    
    bool shouldLockTargeting =
        boss_ai_state_is_attack(boss->state) &&
        boss->state != BOSS_STATE_COMBO_LUNGE &&
        boss->state != BOSS_STATE_LUNGE_STARTER &&
        !boss->targetingLocked;
    
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
    if (boss->trackingSlamCooldown > 0.0f) boss->trackingSlamCooldown -= dt;
    if (boss->flipAttackCooldown > 0.0f) boss->flipAttackCooldown -= dt;
    
    if (boss->attackNameDisplayTimer > 0.0f) boss->attackNameDisplayTimer -= dt;
    if (boss->hitMessageTimer > 0.0f) boss->hitMessageTimer -= dt;

    if (boss->comboLungeCooldown > 0.0f) boss->comboLungeCooldown -= dt;

    if (boss->stompCooldown > 0.0f)   boss->stompCooldown -= dt;
    if (boss->attack1Cooldown > 0.0f) boss->attack1Cooldown -= dt;
}

static void boss_ai_setup_combo_lunge(Boss* boss, float dist, float dx, float dz)
{
    const float CLOSE_RANGE = 80.0f;
    const float pastDistance = 400.0f;

    // Distance-closer: stop short of the player by this much
    const float STOP_SHORT_DIST = 50.0f;

    // Safety clamps
    if (dist != dist || dist < 0.0f) dist = 0.0f;
    if (dist > 0.0f && dist < 1e-6f) dist = 0.0f;

    if (dist <= CLOSE_RANGE) {
        // Close-range: fixed point through + past the player, yaw can settle later
        boss->comboLungeTracksPlayer = true;

        float toPlayerX = 1.0f, toPlayerZ = 0.0f;
        if (dist > 0.001f) {
            toPlayerX = dx / dist;
            toPlayerZ = dz / dist;
        }

        // Freeze direction once (optional; useful for debugging)
        boss->comboLungeFixedDir[0] = toPlayerX;
        boss->comboLungeFixedDir[1] = toPlayerZ;

        // Fixed past point (doesn't orbit)
        boss->lockedTargetingPos[0] = character.pos[0] + toPlayerX * pastDistance;
        boss->lockedTargetingPos[1] = character.pos[1];
        boss->lockedTargetingPos[2] = character.pos[2] + toPlayerZ * pastDistance;

        // Lock yaw to travel direction (stable during travel)
        boss->comboLungeLockedYaw = -atan2f(-toPlayerZ, toPlayerX) + T3D_PI;
    }
    else
    {
        // Distance-closer: snapshot target, but stop SHORT in front of the player
        boss->comboLungeTracksPlayer = false;

        // Direction from boss to player at start
        float toPlayerX = character.pos[0] - boss->pos[0];
        float toPlayerZ = character.pos[2] - boss->pos[2];
        float d = sqrtf(toPlayerX * toPlayerX + toPlayerZ * toPlayerZ);

        if (d > 0.001f) {
            float dirX = toPlayerX / d;
            float dirZ = toPlayerZ / d;

            // Stop short by 30 units
            boss->lockedTargetingPos[0] = character.pos[0] - dirX * STOP_SHORT_DIST;
            boss->lockedTargetingPos[1] = character.pos[1];
            boss->lockedTargetingPos[2] = character.pos[2] - dirZ * STOP_SHORT_DIST;

            // Lock yaw to travel direction
            boss->comboLungeLockedYaw = -atan2f(-dirZ, dirX) + T3D_PI;
        } else {
            // Degenerate fallback
            boss->lockedTargetingPos[0] = boss->pos[0];
            boss->lockedTargetingPos[1] = boss->pos[1];
            boss->lockedTargetingPos[2] = boss->pos[2];
            boss->comboLungeLockedYaw = boss->rot[1];
        }

        // Snap yaw at start
        //boss->rot[1] = boss->comboLungeLockedYaw;
    }

    boss->targetingLocked = true;
    boss->targetingUpdateTimer = 0.0f;

    boss->currentAttackName = "Combo Lunge";
    boss->attackNameDisplayTimer = 2.0f;
}

static void boss_ai_combo_lunge_helper(Boss* boss, float dist, float dx, float dz)
{
    boss->state = BOSS_STATE_COMBO_LUNGE;
    boss->stateTimer = 0.0f;

    boss->attackCooldown = 2.0f;              // short “don’t instantly spam attacks”
    boss->comboLungeCooldown = 10.0f;         // the real lunge cooldown you want

    boss->currentAttackId = BOSS_ATTACK_COMBO_LUNGE;
    boss->currentAttackHasHit = false;

    boss->isAttacking = true;
    boss->attackAnimTimer = 0.0f;
    boss->animationTransitionTimer = 0.0f;

    boss->velX = 0.0f;
    boss->velZ = 0.0f;

    boss_ai_setup_combo_lunge(boss, dist, dx, dz);
}

// - Stomp: super close (<= 22), highest priority
// - Attack1: close band (50..80) to overlap tracking slam + close-lunge mode
// - Combo starter gated to 60..80 so it doesn't steal 23..49 range
// - Attack1 weighted slightly more frequent than tracking slam + close-lunge

static void boss_ai_select_attack(Boss* boss, float dist) {
    boss->currentAttackHasHit = false;

    // ------------------------------------------------------------
    // NEW: Stomp (super close) and Attack1 (close band)
    // ------------------------------------------------------------
    const float STOMP_RANGE = 30.0f;     // super close
    const float CLOSE_MIN   = 40.0f;     // same as your slam band start
    const float CLOSE_MAX   = 60.0f;     // overlap with close-lunge mode (<= 80)

    // 1) Stomp: highest priority at super close
    if (dist <= STOMP_RANGE && boss->stompCooldown <= 0.0f) {
        boss->state = BOSS_STATE_STOMP;
        boss->stateTimer = 0.0f;

        boss->stompCooldown  = 6.0f;     // tune (not the duration)
        boss->attackCooldown = 1.0f;     // global gate so it can’t instantly chain

        boss->isAttacking = true;
        boss->attackAnimTimer = 0.0f;
        boss->animationTransitionTimer = 0.0f;

        boss->velX = 0.0f;
        boss->velZ = 0.0f;

        boss->currentAttackName = "Stomp";
        boss->attackNameDisplayTimer = 2.0f;
        boss->currentAttackId = BOSS_ATTACK_STOMP;
        return;
    }
    // 2) Attack1: close band, slightly more frequent than tracking slam + close-lunge
    if (dist >= CLOSE_MIN && dist <= CLOSE_MAX) {

        bool attack1Ready = (boss->attack1Cooldown <= 0.0f);
        bool slamReady    = (boss->trackingSlamCooldown <= 0.0f);
        bool lungeReady   = (boss->comboStarterCompleted &&
                             boss->comboLungeCooldown <= 0.0f &&
                             boss->attackCooldown <= 0.0f);

        // weights (Attack1 slightly more frequent than both)
        float wA1    = attack1Ready ? 0.45f : 0.0f;
        float wSlam  = slamReady    ? 0.30f : 0.0f;
        float wLunge = lungeReady   ? 0.25f : 0.0f;

        float sum = wA1 + wSlam + wLunge;

        if (sum > 0.0f) {
            float r = (float)(rand() % 1000) / 1000.0f; // 0..1
            r *= sum;

            if (r < wA1) {
                boss->state = BOSS_STATE_ATTACK1;
                boss->stateTimer = 0.0f;

                boss->attack1Cooldown = 6.0f;   // tune (not the duration)
                boss->attackCooldown  = 1.0f;   // global gate

                boss->isAttacking = true;
                boss->attackAnimTimer = 0.0f;
                boss->animationTransitionTimer = 0.0f;

                boss->velX = 0.0f;
                boss->velZ = 0.0f;

                boss->currentAttackName = "Attack1";
                boss->attackNameDisplayTimer = 2.0f;
                boss->currentAttackId = BOSS_ATTACK_ATTACK1;
                return;
            }

            r -= wA1;

            if (r >= wSlam) {
                // Lunge chosen
                float dx = character.pos[0] - boss->pos[0];
                float dz = character.pos[2] - boss->pos[2];
                boss_ai_combo_lunge_helper(boss, dist, dx, dz);
                return;
            }

            // else: Slam chosen -> fall through to your existing slam block below
        }
    }

    // 3) Combo Starter: close band
    if (dist >= CLOSE_MIN && dist <= CLOSE_MAX && boss->comboStarterCooldown <= 0.0f) {
        boss->state = BOSS_STATE_COMBO_STARTER;
        boss->stateTimer = 0.0f;

        boss->comboStarterCooldown = 5.0f;
        boss->attackCooldown       = 1.0f;

        boss->swordThrown = false;
        boss->comboStarterSlamHasHit = false;
        boss->comboStarterCompleted = false;

        boss->velX = 0.0f;
        boss->velZ = 0.0f;

        boss->comboStarterTargetPos[0] = character.pos[0];
        boss->comboStarterTargetPos[1] = character.pos[1];
        boss->comboStarterTargetPos[2] = character.pos[2];

        boss->isAttacking = true;
        boss->attackAnimTimer = 0.0f;
        boss->animationTransitionTimer = 0.0f;

        boss->currentAttackName = "Combo Starter";
        boss->attackNameDisplayTimer = 2.0f;
        boss->currentAttackId = BOSS_ATTACK_COMBO_STARTER;
        return;
    }


    if (boss->flipAttackCooldown <= 0.0f && dist >= 100.0f && dist < 200.0f) {
        boss->isAttacking = true;
        boss->state = BOSS_STATE_FLIP_ATTACK;
        boss->stateTimer = 0.0f;
        boss->flipAttackCooldown = 10.0f;
        boss->currentAttackHasHit = false;

        boss->flipAttackMidReaimed = false;
        boss->flipAttackTravelYaw = boss->rot[1];
        boss->flipAttackPastDist  = 0.0f;

        float startX = boss->pos[0];
        float startY = boss->pos[1];
        float startZ = boss->pos[2];
        if (fabsf(startX) < 1e-6f) startX = 0.0f;
        if (fabsf(startY) < 1e-6f) startY = 0.0f;
        if (fabsf(startZ) < 1e-6f) startZ = 0.0f;

        boss->flipAttackStartPos[0] = startX;
        boss->flipAttackStartPos[1] = startY;
        boss->flipAttackStartPos[2] = startZ;

        predict_character_position(boss->lockedTargetingPos, 0.7f);
        boss->targetingLocked = true;

        float targetX = boss->lockedTargetingPos[0];
        float targetY = boss->lockedTargetingPos[1];
        float targetZ = boss->lockedTargetingPos[2];

        if (fabsf(targetX) < 1e-6f) targetX = 0.0f;
        if (fabsf(targetY) < 1e-6f) targetY = 0.0f;
        if (fabsf(targetZ) < 1e-6f) targetZ = 0.0f;

        const float FLIP_PAST_DIST = 250.0f;

        float dirX = targetX - startX;
        float dirZ = targetZ - startZ;
        float len  = sqrtf(dirX*dirX + dirZ*dirZ);

        if (len > 0.001f) {
            dirX /= len;
            dirZ /= len;

            boss->flipAttackTargetPos[0] = targetX + dirX * FLIP_PAST_DIST;
            boss->flipAttackTargetPos[1] = targetY;
            boss->flipAttackTargetPos[2] = targetZ + dirZ * FLIP_PAST_DIST;
        } else {
            boss->flipAttackTargetPos[0] = targetX;
            boss->flipAttackTargetPos[1] = targetY;
            boss->flipAttackTargetPos[2] = targetZ;
        }

        boss->flipAttackHeight = 18.0f;

        boss->currentAttackName = "Flip Attack";
        boss->attackNameDisplayTimer = 2.0f;
        boss->currentAttackId = BOSS_ATTACK_FLIP_ATTACK;
    }
    else if (boss->powerJumpCooldown <= 0.0f && dist >= 200.0f) {
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
        boss->trackingSlamCooldown = 15.0f;
        boss->isAttacking = true;
        boss->attackAnimTimer = 0.0f;
        boss->animationTransitionTimer = 0.0f;

        if (!boss->targetingLocked) {
            float predictionTime = 0.3f;
            predict_character_position(boss->lockedTargetingPos, predictionTime);
            boss->targetingLocked = true;
            boss->targetingUpdateTimer = 0.0f;
        }

        float dx = boss->lockedTargetingPos[0] - boss->pos[0];
        float dz = boss->lockedTargetingPos[2] - boss->pos[2];
        float angle = atan2f(-dx, dz);
        boss->trackingSlamTargetAngle = angle;

        boss->currentAttackName = "Slow Attack";
        boss->attackNameDisplayTimer = 2.0f;
        boss->currentAttackId = BOSS_ATTACK_TRACKING_SLAM;
    }
    else if (boss->comboCooldown <= 0.0f && boss->comboStarterCompleted) {
        boss->state = BOSS_STATE_COMBO_ATTACK;
        boss->stateTimer = 0.0f;
        boss->comboCooldown = 10.0f;
        boss->isAttacking = true;
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
    }
    else {
        // ------------------------------------------------------------
        // CLOSE RANGE
        // ------------------------------------------------------------
        if (dist < CLOSE_MIN)
        {
            if (dist <= 22.0f && boss->stompCooldown <= 0.0f) {
                boss->state = BOSS_STATE_STOMP;
                boss->stateTimer = 0.0f;

                boss->stompCooldown  = 6.0f;
                boss->attackCooldown = 0.8f;

                boss->isAttacking = true;
                boss->attackAnimTimer = 0.0f;
                boss->animationTransitionTimer = 0.0f;

                boss->velX = 0.0f;
                boss->velZ = 0.0f;

                boss->currentAttackName = "Stomp";
                boss->attackNameDisplayTimer = 2.0f;
                boss->currentAttackId = BOSS_ATTACK_STOMP;
                return;
            }

            bool starterReady = (boss->comboStarterCooldown <= 0.0f);
            bool comboReady   = (boss->comboCooldown <= 0.0f);          // combo itself
            bool a1Ready      = (boss->attack1Cooldown <= 0.0f);
            bool slamReady    = (boss->trackingSlamCooldown <= 0.0f);

            // weights: ensure starter shows up often enough when close
            float wStarter = starterReady ? 0.40f : 0.0f;
            float wA1      = a1Ready      ? 0.20f : 0.0f;
            float wSlam    = slamReady    ? 0.15f : 0.0f;

            float sum = wStarter + wA1 + wSlam;

            if (sum > 0.0f) {
                float r = ((float)(rand() % 1000) / 1000.0f) * sum;

                if (r < wStarter) {
                    // COMBO STARTER (close-range allowed)
                    boss->state = BOSS_STATE_COMBO_STARTER;
                    boss->stateTimer = 0.0f;

                    boss->comboStarterCooldown = 5.0f;
                    boss->swordThrown = false;
                    boss->comboStarterSlamHasHit = false;
                    boss->comboStarterCompleted = false;

                    boss->velX = 0.0f;
                    boss->velZ = 0.0f;

                    boss->comboStarterTargetPos[0] = character.pos[0];
                    boss->comboStarterTargetPos[1] = character.pos[1];
                    boss->comboStarterTargetPos[2] = character.pos[2];

                    boss->isAttacking = true;
                    boss->attackAnimTimer = 0.0f;
                    boss->animationTransitionTimer = 0.0f;

                    boss->currentAttackName = "Combo Starter";
                    boss->attackNameDisplayTimer = 2.0f;
                    boss->currentAttackId = BOSS_ATTACK_COMBO_STARTER;
                    return;
                }
                r -= wStarter;

                if (r < wA1) {
                    boss->state = BOSS_STATE_ATTACK1;
                    boss->stateTimer = 0.0f;

                    boss->attack1Cooldown = 4.0f;
                    boss->attackCooldown  = 0.8f;

                    boss->isAttacking = true;
                    boss->attackAnimTimer = 0.0f;
                    boss->animationTransitionTimer = 0.0f;

                    boss->velX = 0.0f;
                    boss->velZ = 0.0f;

                    boss->currentAttackName = "Attack1";
                    boss->attackNameDisplayTimer = 2.0f;
                    boss->currentAttackId = BOSS_ATTACK_ATTACK1;
                    return;
                }

                // Slam fallback
                boss->state = BOSS_STATE_TRACKING_SLAM;
                boss->stateTimer = 0.0f;

                boss->trackingSlamCooldown = 15.0f;
                boss->attackCooldown = 0.8f;

                boss->isAttacking = true;
                boss->attackAnimTimer = 0.0f;
                boss->animationTransitionTimer = 0.0f;

                boss->currentAttackName = "Slow Attack";
                boss->attackNameDisplayTimer = 2.0f;
                boss->currentAttackId = BOSS_ATTACK_TRACKING_SLAM;
                return;
            }

            boss->state = BOSS_STATE_STRAFE;
            boss->stateTimer = 0.0f;
            boss->isAttacking = false;
            return;

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
    
    // Phase 2 is now triggered by the scene cutscene system at 40% HP.
    // The cutscene sets phaseIndex = 2 when it ends.
    
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
            // Distance-closer lunge: allowed WITHOUT combo starter, but only when far enough
            if (boss->comboLungeCooldown <= 0.0f && dist >= 80.0f && dist <= 300.0f) {
                boss->state = BOSS_STATE_LUNGE_STARTER;
                boss->stateTimer = 0.0f;

                boss->currentAttackId = BOSS_ATTACK_LUNGE_STARTER;
                boss->currentAttackHasHit = false;

                boss->isAttacking = true;
                boss->attackAnimTimer = 0.0f;
                boss->animationTransitionTimer = 0.0f;

                boss->velX = 0.0f;
                boss->velZ = 0.0f;

                boss->targetingLocked = true;          // prevents other lock logic / debug uses
                boss->targetingUpdateTimer = 0.0f;

                boss->currentAttackName = "Lunge Starter";
                boss->attackNameDisplayTimer = 2.0f;
                break;
            }

            // Force attack if chasing for too long
            if (boss->stateTimer >= MAX_CHASE_TIME) {
                boss_ai_select_attack(boss, dist);
                break;
            }
            // Trigger attack selection when at combo starter range (75 units), flip attack range (100-200), or long range (power jump)
            if ((dist >= 0.0f && dist <= 80.0f) || (dist >= 100.0f && dist < 200.0f) || dist >= 250.0f) {
                boss_ai_select_attack(boss, dist);
                break;
            }
            // Charge past attack: only trigger when player is within 80 distance AND combo starter has completed
            if (boss->comboLungeCooldown <= 0.0f && dist > 0.0f && dist < 80.0f && boss->comboStarterCompleted) {
                float r = (float)(rand() % 100) / 100.0f;
                if (r < 0.5f) { // 50% chance for charge after combo starter
                    boss_ai_combo_lunge_helper(boss, dist, dx, dz);
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
            // Distance-closer lunge: allowed WITHOUT combo starter, but only when far enough
            if (boss->comboLungeCooldown <= 0.0f && dist >= 80.0f && dist <= 300.0f) {
                boss->state = BOSS_STATE_LUNGE_STARTER;
                boss->stateTimer = 0.0f;

                boss->currentAttackId = BOSS_ATTACK_LUNGE_STARTER;
                boss->currentAttackHasHit = false;

                boss->isAttacking = true;
                boss->attackAnimTimer = 0.0f;
                boss->animationTransitionTimer = 0.0f;

                boss->velX = 0.0f;
                boss->velZ = 0.0f;

                boss->targetingLocked = true;          // prevents other lock logic / debug uses
                boss->targetingUpdateTimer = 0.0f;

                boss->currentAttackName = "Lunge Starter";
                boss->attackNameDisplayTimer = 2.0f;
                break;
            }

            // Force attack if strafing for too long
            if (boss->stateTimer >= MAX_STRAFE_TIME) {
                boss_ai_select_attack(boss, dist);
                break;
            }
            // Trigger attack selection when at combo starter range (75 units) or flip attack range (100-200)
            if ((dist >= 0.0f && dist <= 80.0f) || (dist >= 100.0f && dist < 200.0f)) {
                boss_ai_select_attack(boss, dist);
                break;
            }
            // Charge past attack: only trigger when player is within 80 distance AND combo starter has completed
            if (boss->comboLungeCooldown <= 0.0f && boss->stateTimer >= 3.0f && dist > 0.0f && dist < 80.0f && boss->comboStarterCompleted) {
                float r = (float)(rand() % 100) / 100.0f;
                if (r < 0.5f) { // 50% chance for charge after combo starter
                    boss_ai_combo_lunge_helper(boss, dist, dx, dz);
                } else {
                    boss_ai_select_attack(boss, dist);
                }
            }
            break;
            
        case BOSS_STATE_COMBO_LUNGE:
        {
            const float LUNGE_TOTAL = 2.2f;

            // If stateTimer is NaN, force exit immediately
            if (!(boss->stateTimer >= 0.0f)) {
                boss->isAttacking = false;
                boss->comboStarterCompleted = false;
                boss->state = BOSS_STATE_STRAFE;
                boss->stateTimer = 0.0f;
                break;
            }

            if (boss->stateTimer >= LUNGE_TOTAL) {
                boss->comboStarterCompleted = false;
                boss->isAttacking = false;
                boss->animationTransitionTimer = 0.0f;

                // STRAFE here to avoid re-entrant selection weirdness:
                boss->state = BOSS_STATE_STRAFE;
                boss->stateTimer = 0.0f;
                break;
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
                const float recoverDuration = 2.5f;   // 1.0s recovery on ground
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
                const float totalDuration = stepDuration * 3 + 4.5f; // Increased recovery time to let animation finish
                
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
        case BOSS_STATE_LUNGE_STARTER:
            {
                const float starterDuration = 2.5f; // tune to anim length

                // Stay in wind-up; no movement handled here (attacks module will zero vel)
                if (boss->stateTimer >= starterDuration) {
                    float dx = character.pos[0] - boss->pos[0];
                    float dz = character.pos[2] - boss->pos[2];
                    float distNow = sqrtf(dx*dx + dz*dz);

                    // IMPORTANT: this re-evaluates the distance NOW.
                    // If the player rushed in, distNow <= 50 and setup will choose close-range mode.
                    boss_ai_combo_lunge_helper(boss, distNow, dx, dz);
                }
            }
            break;
            case BOSS_STATE_COMBO_STARTER:
                if (boss->stateTimer >= 2.0f) {
                    boss->comboStarterCompleted = true;

                    boss->swordThrown = false;
                    boss->comboStarterSlamHasHit = false;
                    boss->velX = 0.0f;
                    boss->velZ = 0.0f;

                    bool chargeAvailable = (boss->attackCooldown <= 0.0f && dist > 0.0f && dist <= 300.0f);
                    bool comboAvailable  = (boss->comboCooldown <= 0.0f);

                    if (chargeAvailable && comboAvailable) {
                        float r = (float)(rand() % 100) / 100.0f;
                        if (r < 0.5f) {
                            boss_ai_combo_lunge_helper(boss, dist, dx, dz);
                            break;
                        } else {
                            // Choose combo attack
                            boss->state = BOSS_STATE_COMBO_ATTACK;
                            boss->stateTimer = 0.0f;

                            boss->comboCooldown = 10.0f;
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

                            // FIX: ensure combo can actually hit
                            boss->currentAttackHasHit = false;
                            // FIX: keep attack-state consistent
                            boss->isAttacking = true;
                            boss->attackAnimTimer = 0.0f;
                            boss->animationTransitionTimer = 0.0f;

                            break;
                        }
                    }
                    else if (chargeAvailable) {
                        boss_ai_combo_lunge_helper(boss, dist, dx, dz);
                        break;
                    }
                    else if (comboAvailable) {
                        // Only combo available
                        boss->state = BOSS_STATE_COMBO_ATTACK;
                        boss->stateTimer = 0.0f;

                        boss->comboCooldown = 10.0f;
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

                        // FIX: ensure combo can actually hit
                        boss->currentAttackHasHit = false;
                        // FIX: keep attack-state consistent
                        boss->isAttacking = true;
                        boss->attackAnimTimer = 0.0f;
                        boss->animationTransitionTimer = 0.0f;

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

                    // FIX: leaving attack state
                    boss->isAttacking = false;
                    boss->currentAttackHasHit = false;

                    boss->stateTimer = 0.0f;
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
            case BOSS_STATE_STOMP:
                if (boss->stateTimer >= 3.0f) {
                    if (dist < 50.0f) boss_ai_select_attack(boss, dist);
                    else boss->state = BOSS_STATE_STRAFE;
                    boss->stateTimer = 0.0f;
                }
                break;

            case BOSS_STATE_ATTACK1:
                // short slash duration
                if (boss->stateTimer >= 2.0f) {
                    if (dist < 50.0f) boss_ai_select_attack(boss, dist);
                    else boss->state = BOSS_STATE_STRAFE;
                    boss->stateTimer = 0.0f;
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
            case BOSS_STATE_COMBO_LUNGE:
                out_intent->anim = BOSS_ANIM_COMBO_LUNGE;
                out_intent->priority = BOSS_ANIM_PRIORITY_HIGH;
                // Force restart animation when entering charge state (not on every frame)
                if (boss->state != prevState) {
                    out_intent->force_restart = true;
                }
                break;
            case BOSS_STATE_LUNGE_STARTER:
                out_intent->anim = BOSS_ANIM_LUNGE_STARTER;
                out_intent->priority = BOSS_ANIM_PRIORITY_HIGH;
                if (boss->state != prevState) out_intent->force_restart = true;
                break;
            case BOSS_STATE_TRACKING_SLAM:
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
                            // Alternate direction every few seconds
                            strafeDirectionTimer += dt;
                            const float ALTERNATE_TIME = 3.0f;
                            if (strafeDirectionTimer >= ALTERNATE_TIME) {
                                bossStrafeDirection = -bossStrafeDirection; // Flip direction
                                strafeDirectionTimer = 0.0f;
                            }
                        }
                    }
                }
                boss->strafeDirection = bossStrafeDirection;
                out_intent->anim = (boss->strafeDirection > 0.0f) ? BOSS_ANIM_STRAFE_RIGHT : BOSS_ANIM_STRAFE_LEFT;
                // Set initial attack cooldown when entering strafe to ensure minimum strafe duration
                if (boss->attackCooldown <= 0.0f) {
                    boss->attackCooldown = 2.0f;
                }

                break;
            case BOSS_STATE_CHASE:
                out_intent->anim = BOSS_ANIM_WALK;
                break;
            case BOSS_STATE_STAGGER:
                out_intent->anim = BOSS_ANIM_ATTACK;
                out_intent->priority = BOSS_ANIM_PRIORITY_HIGH;
                break;
            case BOSS_STATE_DEAD:
                // Play collapse once (non-looping) when dead.
                out_intent->anim = BOSS_ANIM_COLLAPSE;
                out_intent->priority = BOSS_ANIM_PRIORITY_CRITICAL;
                // Force the first request after death even if state was set externally.
                out_intent->force_restart = (boss->currentAnimState != BOSS_ANIM_COLLAPSE);
                break;
            case BOSS_STATE_STOMP:
                out_intent->anim = BOSS_ANIM_STOMP1;
                out_intent->priority = BOSS_ANIM_PRIORITY_HIGH;
                if (boss->state != prevState) out_intent->force_restart = true;
                break;
            case BOSS_STATE_ATTACK1:
                out_intent->anim = BOSS_ANIM_ATTACK1;
                out_intent->priority = BOSS_ANIM_PRIORITY_HIGH;
                if (boss->state != prevState) out_intent->force_restart = true;
                break;
            default:
                out_intent->anim = BOSS_ANIM_IDLE;
                break;
        }
    }
    
    // Output attack request if starting a new attack
    if (boss_ai_state_is_attack(boss->state) && prevState != boss->state) {
        boss->dustImpactDelayS = boss_ai_attack_dust_delay_s(boss->currentAttackId);
        out_intent->attack_req = true;
        out_intent->attack = boss->currentAttackId;
    }
    
    // Sound triggers on state entry (matching old boss code behavior)
    // Note: Sound system integration would go here when available
    if (prevState != boss->state) 
    {
        boss_reset_sfx();
    }
}


