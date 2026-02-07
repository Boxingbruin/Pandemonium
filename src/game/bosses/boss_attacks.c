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
#include "utilities/animation_utility.h"

// Access global character instance
extern Character character;

static inline void boss_attacks_on_player_hit(float damage)
{
    if (damage <= 25.0f) return;

    // Fixed, noticeable impulse (units are world-space and applied in camera right/up).
    animation_utility_set_screen_shake_mag(20.0f);
}

// For some attacks (notably tracking slam), aiming from the boss origin causes
// the weapon to "land" on the player because the sword/hand is offset forward.
// Use the weapon tip position (world XZ) as the aim origin when available.
// We can figure out a better system later.
static inline bool boss_attacks_weapon_tip_world_xz(const Boss *boss, float *outX, float *outZ)
{
    if (!boss || !outX || !outZ) return false;
    if (!boss->skeleton || !boss->modelMat) return false;
    if (boss->handRightBoneIndex < 0) return false;

    T3DSkeleton *sk = (T3DSkeleton*)boss->skeleton;
    const T3DMat4FP *B = &sk->boneMatricesFP[boss->handRightBoneIndex]; // bone in MODEL space
    const T3DMat4FP *M = (const T3DMat4FP*)boss->modelMat;             // model in WORLD space

    // Match collision_system.c: bone-local segment points.
    const float len = 640.0f;
    const float p_tip_local[3] = { -len, 0.0f, 0.0f };

    float p_tip_model[3];
    float p_tip_world[3];
    mat4fp_mul_point_f32_row3_colbasis(B, p_tip_local, p_tip_model);
    mat4fp_mul_point_f32_row3_colbasis(M, p_tip_model, p_tip_world);

    *outX = p_tip_world[0];
    *outZ = p_tip_world[2];
    return true;
}

// static inline void boss_shake_on_window_end(bool windowActiveNow,
//                                             bool *prevWindowActive,
//                                             float damageForThisWindow)
// {
//     // We want the *end* edge: active -> inactive
//     if (*prevWindowActive && !windowActiveNow) {
//         boss_attacks_play_impact_shake(damageForThisWindow);
//     }
//     *prevWindowActive = windowActiveNow;
// }

// Forward declarations
static void boss_attacks_handle_power_jump(Boss* boss, float dt);
static void boss_attacks_handle_combo(Boss* boss, float dt);
static void boss_attacks_handle_combo_starter(Boss* boss, float dt);
static void boss_attacks_handle_roar_stomp(Boss* boss, float dt);
static void boss_attacks_handle_tracking_slam(Boss* boss, float dt);
static void boss_attacks_handle_charge(Boss* boss, float dt);
static void boss_attacks_handle_flip_attack(Boss* boss, float dt);
static void boss_attacks_handle_lunge_starter(Boss* boss, float dt);
static void boss_attacks_handle_stomp(Boss* boss, float dt);
static void boss_attacks_handle_attack1(Boss* boss, float dt);

void boss_attacks_update(Boss* boss, float dt) {
    if (!boss) return;
    
    // Check if boss is in an attack state and update hand collider
    bool isAttackState = (boss->state == BOSS_STATE_POWER_JUMP ||
                        boss->state == BOSS_STATE_COMBO_ATTACK ||
                        boss->state == BOSS_STATE_COMBO_STARTER ||
                        boss->state == BOSS_STATE_TRACKING_SLAM ||
                        boss->state == BOSS_STATE_COMBO_LUNGE ||
                        boss->state == BOSS_STATE_FLIP_ATTACK ||
                        boss->state == BOSS_STATE_LUNGE_STARTER ||
                        boss->state == BOSS_STATE_STOMP ||
                        boss->state == BOSS_STATE_ATTACK1);

    if (!isAttackState) {
        boss->handAttackColliderActive = false;
        boss->sphereAttackColliderActive = false;
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
        case BOSS_STATE_STOMP:
            boss_attacks_handle_stomp(boss, dt);
            break;

        case BOSS_STATE_ATTACK1:
            boss_attacks_handle_attack1(boss, dt);
            break;
        default:
            // Not an attack state, nothing to do
            break;
    }
}


static void boss_attacks_handle_power_jump(Boss* boss, float dt)
{
    // Frame timings at 30 FPS: 0-41 idle, 41-83 jump+land, 83-136 land+recover
    const float idleDuration    = 41.0f / 25.0f;      // 1.367s
    const float jumpDuration    = 42.0f / 25.0f;      // 1.4s
    const float recoverDuration = 53.0f / 25.0f;      // 1.767s
    const float totalDuration   = idleDuration + jumpDuration + recoverDuration;

    // -----------------------------
    // Tuning
    // -----------------------------
    const float LEAD_TIME = 0.12f;

    // Land "in front" but a bit closer than before
    const float SHORT_FRAC = 0.28f;  // was 0.35
    const float SHORT_MIN  = 20.0f;  // was 30
    const float SHORT_MAX  = 55.0f;  // was 70

    // Air tracking (fast-ish but not perfect)
    const float TRACK_MAX_SPEED  = 100.0f;
    const float TRACK_STRENGTH   = 0.30f;
    const float TRACK_RAMP_IN_T0 = 0.08f;
    const float TRACK_RAMP_IN_T1 = 0.70f;

    // Impact sphere
    const float IMPACT_RADIUS = 70.0f;     // <-- your request
    const float IMPACT_WINDOW = 0.18f;     // slightly wider than 0.1f

    // Local clamp
    #define CLAMPF(x, a, b) ((x) < (a) ? (a) : ((x) > (b) ? (b) : (x)))

    // ------------------------------------------------------------
    // IMPORTANT: on entering POWER_JUMP, clear hit gate
    // (otherwise a previous attack can keep it stuck "already hit")
    // ------------------------------------------------------------
    if (boss->stateTimer - dt < 0.0f) {
        boss->currentAttackHasHit = false;
        boss->sphereAttackColliderActive = false;
    }

    // Phase 1: Idle preparation
    if (boss->stateTimer < idleDuration) {
        float dx = boss->powerJumpTargetPos[0] - boss->pos[0];
        float dz = boss->powerJumpTargetPos[2] - boss->pos[2];
        if (dx != 0.0f || dz != 0.0f) {
            boss->rot[1] = -atan2f(-dz, dx) + T3D_PI;
        }
    }
    // Phase 2: Jump arc
    else if (boss->stateTimer < idleDuration + jumpDuration) {

        float t = (boss->stateTimer - idleDuration) / jumpDuration;
        t = CLAMPF(t, 0.0f, 1.0f);

        // Jump start: pick an initial target once (at phase transition)
        if (boss->stateTimer - dt < idleDuration) {

            float aimX = character.pos[0] + boss->lastPlayerVel[0] * LEAD_TIME;
            float aimZ = character.pos[2] + boss->lastPlayerVel[1] * LEAD_TIME;

            float sx = boss->powerJumpStartPos[0];
            float sz = boss->powerJumpStartPos[2];

            float vx = aimX - sx;
            float vz = aimZ - sz;
            float jumpDist = sqrtf(vx*vx + vz*vz);

            float landShort = CLAMPF(jumpDist * SHORT_FRAC, SHORT_MIN, SHORT_MAX);

            if (jumpDist > 0.001f) {
                float dirX = vx / jumpDist;
                float dirZ = vz / jumpDist;

                boss->powerJumpTargetPos[0] = aimX - dirX * landShort;
                boss->powerJumpTargetPos[2] = aimZ - dirZ * landShort;
            } else {
                boss->powerJumpTargetPos[0] = sx;
                boss->powerJumpTargetPos[2] = sz;
            }

            boss->powerJumpTargetPos[1] = boss->powerJumpStartPos[1];
        }

        // Soft mid-air tracking: nudge target toward the player's "in-front" point
        {
            float ramp = 0.0f;
            if (t <= TRACK_RAMP_IN_T0) ramp = 0.0f;
            else if (t >= TRACK_RAMP_IN_T1) ramp = 1.0f;
            else {
                float u = (t - TRACK_RAMP_IN_T0) / (TRACK_RAMP_IN_T1 - TRACK_RAMP_IN_T0);
                ramp = u*u*(3.0f - 2.0f*u);
            }

            float aimX = character.pos[0] + boss->lastPlayerVel[0] * LEAD_TIME;
            float aimZ = character.pos[2] + boss->lastPlayerVel[1] * LEAD_TIME;

            float sx = boss->powerJumpStartPos[0];
            float sz = boss->powerJumpStartPos[2];

            float vx = aimX - sx;
            float vz = aimZ - sz;
            float jumpDist = sqrtf(vx*vx + vz*vz);

            if (jumpDist > 0.001f) {
                float dirX = vx / jumpDist;
                float dirZ = vz / jumpDist;

                float landShort = CLAMPF(jumpDist * SHORT_FRAC, SHORT_MIN, SHORT_MAX);

                float desiredX = aimX - dirX * landShort;
                float desiredZ = aimZ - dirZ * landShort;

                float dx = desiredX - boss->powerJumpTargetPos[0];
                float dz = desiredZ - boss->powerJumpTargetPos[2];
                float d  = sqrtf(dx*dx + dz*dz);

                if (d > 0.001f) {
                    float maxStep = TRACK_MAX_SPEED * dt * ramp * TRACK_STRENGTH;
                    if (maxStep > d) maxStep = d;

                    boss->powerJumpTargetPos[0] += (dx / d) * maxStep;
                    boss->powerJumpTargetPos[2] += (dz / d) * maxStep;
                }
            }
        }

        // Move along arc
        boss->pos[0] = boss->powerJumpStartPos[0]
                     + (boss->powerJumpTargetPos[0] - boss->powerJumpStartPos[0]) * t;

        boss->pos[2] = boss->powerJumpStartPos[2]
                     + (boss->powerJumpTargetPos[2] - boss->powerJumpStartPos[2]) * t;

        boss->pos[1] = boss->powerJumpStartPos[1]
                     + boss->powerJumpHeight * sinf(t * T3D_PI);

        // Face travel direction
        {
            float dx = boss->powerJumpTargetPos[0] - boss->powerJumpStartPos[0];
            float dz = boss->powerJumpTargetPos[2] - boss->powerJumpStartPos[2];
            if (dx != 0.0f || dz != 0.0f) {
                boss->rot[1] = -atan2f(-dz, dx) + T3D_PI;
            }
        }
    }
    // Phase 3: Landing + recovery
    else if (boss->stateTimer < totalDuration)
    {
        boss->pos[1] = boss->powerJumpStartPos[1];

        // SFX once we land
        if (boss->stateTimer >= idleDuration + jumpDuration) {
            boss_multi_attack_sfx(boss, bossJumpForwardSfx, 2);
        }

        // Impact window
        const float impactT0 = idleDuration + jumpDuration;
        const float impactT1 = impactT0 + IMPACT_WINDOW;

        boss->sphereAttackColliderActive = (boss->stateTimer >= impactT0 && boss->stateTimer < impactT1);

        if (boss->sphereAttackColliderActive && !boss->currentAttackHasHit)
        {
            float dx = character.pos[0] - boss->pos[0];
            float dz = character.pos[2] - boss->pos[2];
            float dist = sqrtf(dx*dx + dz*dz);

            // Sphere radius is now 70
            if (dist <= IMPACT_RADIUS) {
                character_apply_damage(30.0f);
                boss_attacks_on_player_hit(30.0f);
                boss->currentAttackHasHit = true;
            }
        }
    }

    #undef CLAMPF
}

static void boss_attacks_handle_combo(Boss* boss, float dt)
{
    // -------------------------
    // Hit windows
    // -------------------------
    const float hitPart1Start = 0.5f;
    const float hitPart1End   = 1.5f;

    const float hitPart2Start = 3.0f;
    const float hitPart2End   = 3.8f;

    // -------------------------
    // Late push movement
    // -------------------------
    const float MOVE_START_TIME = 2.0f;    // when movement begins
    const float MOVE_END_TIME   = 3.5f;    // when movement ends
    const float MOVE_SPEED      = 120.0f;  // units/sec (tune)
    const float MOVE_MAX_DIST   = 200.0f;  // max distance traveled during this window (tune)

    // How far in front of the player we should "land" (short along boss->player line)
    const float LAND_SHORT      = 40.0f;

    // Reset capture if combo rewinds/restarts
    if (boss->stateTimer < MOVE_START_TIME) {
        boss->comboMoveStartCaptured = false;
    }

    // Capture move start position once, at the exact frame we cross MOVE_START_TIME
    if (!boss->comboMoveStartCaptured && boss->stateTimer >= MOVE_START_TIME) {
        boss->comboMoveStartPos[0] = boss->pos[0];
        boss->comboMoveStartPos[1] = boss->pos[1];
        boss->comboMoveStartPos[2] = boss->pos[2];
        boss->comboMoveStartCaptured = true;
    }

    // Move toward a landing point "LAND_SHORT" units before the player (XZ)
    if (boss->stateTimer >= MOVE_START_TIME &&
        boss->stateTimer <  MOVE_END_TIME &&
        boss->comboMoveStartCaptured)
    {
        // how far we've already traveled since move started (XZ)
        float dx0 = boss->pos[0] - boss->comboMoveStartPos[0];
        float dz0 = boss->pos[2] - boss->comboMoveStartPos[2];
        float traveled = sqrtf(dx0*dx0 + dz0*dz0);

        float remaining = MOVE_MAX_DIST - traveled;
        if (remaining > 0.0f) {

            // vector boss -> player
            float toPX = character.pos[0] - boss->pos[0];
            float toPZ = character.pos[2] - boss->pos[2];
            float distToPlayer = sqrtf(toPX*toPX + toPZ*toPZ);

            if (distToPlayer > 0.001f) {
                float dirX = toPX / distToPlayer;
                float dirZ = toPZ / distToPlayer;

                // Landing target = player position minus approach direction * LAND_SHORT
                float targetX = character.pos[0] - dirX * LAND_SHORT;
                float targetZ = character.pos[2] - dirZ * LAND_SHORT;

                // vector boss -> target
                float tx = targetX - boss->pos[0];
                float tz = targetZ - boss->pos[2];
                float distToTarget = sqrtf(tx*tx + tz*tz);

                // If we are basically at/inside target, don't push further
                if (distToTarget > 0.001f) {

                    float step = MOVE_SPEED * dt;

                    // cap by remaining travel budget and distance to target
                    if (step > remaining)     step = remaining;
                    if (step > distToTarget)  step = distToTarget;

                    boss->pos[0] += (tx / distToTarget) * step;
                    boss->pos[2] += (tz / distToTarget) * step;
                }
            }
        }
    }

    // -------------------------
    // Collider on/off + damage
    // -------------------------

    if (boss->stateTimer >= hitPart1End && boss->stateTimer < hitPart2Start) {
        boss->currentAttackHasHit = false;
    }

    if (boss->stateTimer >= hitPart1Start && boss->stateTimer < hitPart1End) {
        boss->handAttackColliderActive = true;
        if (!boss->currentAttackHasHit && bossWeaponCollision) {
            character_apply_damage(15.0f);
            boss_attacks_on_player_hit(18.0f);
            boss->currentAttackHasHit = true;
        }
    }
    else if (boss->stateTimer >= hitPart2Start && boss->stateTimer < hitPart2End) {
        boss->handAttackColliderActive = true;
        if (!boss->currentAttackHasHit && bossWeaponCollision) {
            character_apply_damage(25.0f);
            boss_attacks_on_player_hit(25.0f);
            boss->currentAttackHasHit = true;
        }
    }
    else {
        boss->handAttackColliderActive = false;
    }

    // -------------------------
    // Facing
    // -------------------------
    if (boss->stateTimer < MOVE_END_TIME) {
        float dx = boss->lockedTargetingPos[0] - boss->pos[0];
        float dz = boss->lockedTargetingPos[2] - boss->pos[2];
        boss->rot[1] = -atan2f(-dz, dx) + T3D_PI;
    }

    boss_multi_attack_sfx(boss, bossComboAttack1Sfx, 3);
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
            character_apply_damage(15.0f);
            boss_attacks_on_player_hit(20.0f);
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
            boss_attacks_on_player_hit(15.0f);
            // boss_debug_sound("boss_attack_success");
            boss->currentAttackHasHit = true;
        }
    }
}

static void boss_attacks_handle_combo_starter(Boss* boss, float dt) {
    const float hitStart = 1.0f;
    const float hitEnd   = 2.0f;

    if(boss->stateTimer >= hitStart && boss->stateTimer <  hitEnd)
    {
        boss->handAttackColliderActive  = true;
        if (!boss->currentAttackHasHit && bossWeaponCollision) {
            character_apply_damage(10.0f);
            boss_attacks_on_player_hit(10.0f);
            boss->currentAttackHasHit = true;
        }
    }
    else
    {
        boss->handAttackColliderActive  = false;
    }

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
static void boss_attacks_handle_lunge_starter(Boss* boss, float dt) {
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
            boss_attacks_on_player_hit(damage);
            // boss_debug_sound("boss_attack_success");
            boss->currentAttackHasHit = true;
        }
    }
    // Phase 3: Recovery (1.1s+)
    // End attack - transition handled by AI
    // (AI will check stateTimer > 2.0f and transition to STRAFE)
}

static void boss_attacks_handle_tracking_slam(Boss* boss, float dt) {

    const float hitStart = 2.5f;
    const float hitEnd   = 3.2f;

    if(boss->stateTimer >= hitStart && boss->stateTimer <  hitEnd)
    {
        boss->handAttackColliderActive  = true;
        if (!boss->currentAttackHasHit && bossWeaponCollision) {
            character_apply_damage(30.0f);
            boss_attacks_on_player_hit(30.0f);
            boss->currentAttackHasHit = true;
        }
    }
    else
    {
        boss->handAttackColliderActive  = false;
    }

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

        // Aim from the weapon tip (when available) instead of boss midpoint.
        float originX = boss->pos[0];
        float originZ = boss->pos[2];
        (void)boss_attacks_weapon_tip_world_xz(boss, &originX, &originZ);

        float dx = targetX - originX;
        float dz = targetZ - originZ;

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
}

static void boss_attacks_handle_charge(Boss* boss, float dt) {
    const float lungeStart = 0.15f;
    const float lungeEnd   = 0.55f;

    if(boss->stateTimer >= lungeStart && boss->stateTimer < lungeEnd + 0.5f)
    {
        boss->handAttackColliderActive  = true;
        //boss->sphereAttackColliderActive = true;
        if (!boss->currentAttackHasHit && bossWeaponCollision) {
            character_apply_damage(15.0f);
            boss_attacks_on_player_hit(15.0f);
            boss->currentAttackHasHit = true;
        }
    }
    else
    {
        boss->handAttackColliderActive  = false;
        //boss->sphereAttackColliderActive = false;
    }

    boss_play_attack_sfx(boss, SCENE1_SFX_BOSS_LUNGE, 0.0f);

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
}

static void boss_attacks_handle_stomp(Boss* boss, float dt)
{
    // Short, nasty close stomp
    const float windupEnd = 2.0f;
    const float impactEnd = 2.2f;

    // Stationary
    boss->velX = 0.0f;
    boss->velZ = 0.0f;

    // Face player
    float dx = character.pos[0] - boss->pos[0];
    float dz = character.pos[2] - boss->pos[2];
    if (dx != 0.0f || dz != 0.0f) {
        boss->rot[1] = -atan2f(-dz, dx) + T3D_PI;
    }

    boss->isAttacking = true;

    boss_play_attack_sfx(boss, SCENE1_SFX_BOSS_LAND2, 2.0f);

    // Impact hit
    if (!boss->currentAttackHasHit &&
        boss->stateTimer >= windupEnd &&
        boss->stateTimer <  impactEnd)
    {
        float px = character.pos[0] - boss->pos[0];
        float pz = character.pos[2] - boss->pos[2];
        float dist = sqrtf(px*px + pz*pz);

        const float radius = 70.0f; // tight radius
        if (dist <= radius) {
            // float damage = 40.0f * (1.0f - (dist / radius)); // falloff
            // if (damage < 6.0f) damage = 6.0f;               // minimum chip
            character_apply_damage(30.0f);
            boss_attacks_on_player_hit(30.0f);
            boss->currentAttackHasHit = true;
        }
    }
}

static void boss_attacks_handle_attack1(Boss* boss, float dt)
{
    // Close-range primary slash
    // Keep it snappy so it can be used more often.
    const float hitStart = 0.8f;
    const float hitEnd   = 1.2f;

    if(boss->stateTimer >= hitStart && boss->stateTimer <  hitEnd)
    {
        boss->handAttackColliderActive  = true;
        if (!boss->currentAttackHasHit && bossWeaponCollision) {
            character_apply_damage(10.0f);
            boss_attacks_on_player_hit(10.0f);
            boss->currentAttackHasHit = true;
        }
    }
    else
    {
        boss->handAttackColliderActive  = false;
    }

    // Mostly stationary (optional micro-step forward if you want)
    boss->velX = 0.0f;
    boss->velZ = 0.0f;

    // Face player (slight lead)
    const float leadTime = 0.10f;
    float aimX = character.pos[0] + boss->lastPlayerVel[0] * leadTime;
    float aimZ = character.pos[2] + boss->lastPlayerVel[1] * leadTime;

    float dx = aimX - boss->pos[0];
    float dz = aimZ - boss->pos[2];
    if (dx != 0.0f || dz != 0.0f) {
        boss->rot[1] = -atan2f(-dz, dx) + T3D_PI;
    }

    boss_play_attack_sfx(boss, SCENE1_SFX_BOSS_SWING4, 1.0f);


}

static void boss_attacks_handle_flip_attack(Boss* boss, float dt)
{
    const float hitStart = 2.0f;
    const float hitEnd   = 4.0f;

    const float sphereDamageWindow1On  = 2.0f;
    const float sphereDamageWindow1Off = 2.5f;

    const float sphereDamageWindow2On  = 3.0f;
    const float sphereDamageWindow2Off = 3.2f;

    const float sphereDamageWindow3On  = 3.5f;
    const float sphereDamageWindow3Off = 4.2f;

    // ------------------------------------------------------------
    // Radial damage sphere config (matches your debug sphere)
    // ------------------------------------------------------------
    const float SPHERE_OFFSET = 40.0f;   // same as your debug
    const float SPHERE_RADIUS = 20.0f;   // same as your debug

    // Per-window damage
    const float DMG_W1 = 12.0f;
    const float DMG_W2 = 20.0f;
    const float DMG_W3 = 30.0f;

    // Determine which damage window we're currently in (0 = none, 1..3 = active window)
    int sphereWindow = 0;
    if (boss->stateTimer >= sphereDamageWindow1On && boss->stateTimer < sphereDamageWindow1Off) sphereWindow = 1;
    else if (boss->stateTimer >= sphereDamageWindow2On && boss->stateTimer < sphereDamageWindow2Off) sphereWindow = 2;
    else if (boss->stateTimer >= sphereDamageWindow3On && boss->stateTimer < sphereDamageWindow3Off) sphereWindow = 3;

    // Active flag for visuals / debugging
    boss->sphereAttackColliderActive = (sphereWindow != 0);

    // Pick damage for the current window
    float sphereDamage = 0.0f;
    if      (sphereWindow == 1) sphereDamage = DMG_W1;
    else if (sphereWindow == 2) sphereDamage = DMG_W2;
    else if (sphereWindow == 3) sphereDamage = DMG_W3;

    // Reset hit gate between windows so each window can hit once
    // NOTE: static assumes single boss; move to Boss struct if needed
    static int lastSphereWindow = 0;
    if (sphereWindow != lastSphereWindow) {
        boss->currentAttackHasHit = false;
        lastSphereWindow = sphereWindow;
    }

    // Apply radial damage once per window
    if (sphereWindow != 0 && sphereDamage > 0.0f && !boss->currentAttackHasHit) {

        float yaw  = boss->rot[1];
        float fwdX = cosf(yaw);
        float fwdZ = sinf(yaw);

        // Matches your debug (pos - fwd * OFFSET)
        float cx = boss->pos[0] - fwdX * SPHERE_OFFSET;
        float cz = boss->pos[2] - fwdZ * SPHERE_OFFSET;

        float dxs = character.pos[0] - cx;
        float dzs = character.pos[2] - cz;
        float dist = sqrtf(dxs*dxs + dzs*dzs);

        if (dist <= SPHERE_RADIUS) {
            character_apply_damage(sphereDamage);
            boss_attacks_on_player_hit(sphereDamage);
            boss->currentAttackHasHit = true;
        }
    }

    // ------------------------------------------------------------
    // Existing hand hit windows
    // ------------------------------------------------------------
    if(boss->stateTimer >= hitStart && boss->stateTimer < hitEnd)
    {
        boss->handAttackColliderActive  = true;
        if (!boss->currentAttackHasHit && bossWeaponCollision) {
            character_apply_damage(10.0f);
            boss_attacks_on_player_hit(10.0f);
            boss->currentAttackHasHit = true;
        }
    }
    else
    {
        boss->handAttackColliderActive  = false;
    }

    const float idleDuration    = 2.0f;
    const float jumpDuration    = 1.5f;
    const float recoverDuration = 0.5f;
    const float totalDuration   = idleDuration + jumpDuration + recoverDuration;

    boss_multi_attack_sfx(boss, bossFlipAttackSfx, 3);

    // --------------------------------
    // Phase 1: Idle / windup
    // --------------------------------
    if (boss->stateTimer < idleDuration) {

        float dx = character.pos[0] - boss->pos[0];
        float dz = character.pos[2] - boss->pos[2];

        if (dx != 0.0f || dz != 0.0f) {
            float targetYaw = -atan2f(-dz, dx) + T3D_PI;

            float cur = boss->rot[1];
            float d = targetYaw - cur;
            while (d >  T3D_PI) d -= 2.0f * T3D_PI;
            while (d < -T3D_PI) d += 2.0f * T3D_PI;

            float maxTurn = boss->turnRate * dt;
            if (d >  maxTurn) d =  maxTurn;
            if (d < -maxTurn) d = -maxTurn;

            boss->rot[1] = cur + d;
        }
    }

    // --------------------------------
    // Phase 2: Jump arc
    // --------------------------------
    else if (boss->stateTimer < idleDuration + jumpDuration) {

        // === Jump start: compute initial travel ===
        if (boss->stateTimer - dt < idleDuration) {

            const float leadTime = 0.25f;

            float aimX = character.pos[0] + boss->lastPlayerVel[0] * leadTime;
            float aimZ = character.pos[2] + boss->lastPlayerVel[1] * leadTime;

            float sx = boss->flipAttackStartPos[0];
            float sz = boss->flipAttackStartPos[2];

            float dirX = aimX - sx;
            float dirZ = aimZ - sz;
            float len  = sqrtf(dirX*dirX + dirZ*dirZ);

            float past = len * 0.25f;
            if (past < 20.0f)  past = 20.0f;
            if (past > 60.0f)  past = 60.0f;

            boss->flipAttackPastDist = past;
            boss->flipAttackMidReaimed = false;

            if (len > 0.001f) {
                dirX /= len;
                dirZ /= len;

                boss->flipAttackTravelYaw =
                    -atan2f(-dirZ, dirX) + T3D_PI;

                boss->flipAttackTargetPos[0] = aimX + dirX * past;
                boss->flipAttackTargetPos[1] = boss->flipAttackStartPos[1];
                boss->flipAttackTargetPos[2] = aimZ + dirZ * past;
            } else {
                boss->flipAttackTravelYaw = boss->rot[1];
            }
        }

        float t = (boss->stateTimer - idleDuration) / jumpDuration;

        // === Move along arc ===
        boss->pos[0] = boss->flipAttackStartPos[0]
                     + (boss->flipAttackTargetPos[0] - boss->flipAttackStartPos[0]) * t;

        boss->pos[2] = boss->flipAttackStartPos[2]
                     + (boss->flipAttackTargetPos[2] - boss->flipAttackStartPos[2]) * t;

        // boss->pos[1] = boss->flipAttackStartPos[1]
        //              + boss->flipAttackHeight * sinf(t * T3D_PI);

        float mdx = boss->flipAttackTargetPos[0] - boss->flipAttackStartPos[0];
        float mdz = boss->flipAttackTargetPos[2] - boss->flipAttackStartPos[2];
        if (mdx != 0.0f || mdz != 0.0f) {
            boss->rot[1] = -atan2f(-mdz, mdx) + T3D_PI;
        }
    }
}
