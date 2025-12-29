#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>
#include <t3d/t3ddebug.h>
#include <math.h>
#include <stdlib.h>

#include "boss.h"
#include "dev.h"
#include "dev/debug_draw.h"
#include "character.h"

#include "game_time.h"
#include "general_utility.h"

#include "globals.h"
#include "display_utility.h"
#include "simple_collision_utility.h"
#include "game_math.h"
#include "scene.h"
#include "collision_mesh.h"

T3DModel* bossModel;

Boss boss;
static const char* bossDebugSoundName = NULL;
static float bossDebugSoundTimer = 0.0f;
static const float BOSS_SOUND_DISPLAY_DURATION = 2.5f;

static bool bossLowHealthSoundPlayed = false;
static bool bossPowerJumpImpactPlayed = false;
static bool bossRoarImpactSoundPlayed = false;
static bool bossWasActive = false; // tracks rising edge of activation across restarts
static float bossStrafeDirection = 1.0f; // 1.0 = right, -1.0 = left (for animation selection)

static void boss_debug_sound(const char* soundName) {
	if (!soundName) return;
	bossDebugSoundName = soundName;
	bossDebugSoundTimer = BOSS_SOUND_DISPLAY_DURATION;
}

static inline void vec3_to_fixed_world(FixedVec3 *out, float x, float y, float z)
{
	out->v[0] = TO_FIXED(x);
	out->v[1] = TO_FIXED(y);
	out->v[2] = TO_FIXED(z);
}

static SCU_CapsuleFixed boss_make_capsule_fixed(void)
{
	SCU_CapsuleFixed cap;
	// Approximate world endpoints (ignore rotation for simplicity for now)
	float sx = boss.scale[0];
	float ax = boss.capsuleCollider.localCapA.v[0] * sx;
	float ay = boss.capsuleCollider.localCapA.v[1] * sx;
	float az = boss.capsuleCollider.localCapA.v[2] * sx;
	float bx = boss.capsuleCollider.localCapB.v[0] * sx;
	float by = boss.capsuleCollider.localCapB.v[1] * sx;
	float bz = boss.capsuleCollider.localCapB.v[2] * sx;
	vec3_to_fixed_world(&cap.a, boss.pos[0] + ax, boss.pos[1] + ay, boss.pos[2] + az);
	vec3_to_fixed_world(&cap.b, boss.pos[0] + bx, boss.pos[1] + by, boss.pos[2] + bz);
	cap.radius = TO_FIXED(boss.capsuleCollider.radius * sx);
	return cap;
}

static SCU_CapsuleFixed character_make_capsule_fixed(void)
{
	SCU_CapsuleFixed cap;
	float sx = character.scale[0];
	float ax = character.capsuleCollider.localCapA.v[0] * sx;
	float ay = character.capsuleCollider.localCapA.v[1] * sx;
	float az = character.capsuleCollider.localCapA.v[2] * sx;
	float bx = character.capsuleCollider.localCapB.v[0] * sx;
	float by = character.capsuleCollider.localCapB.v[1] * sx;
	float bz = character.capsuleCollider.localCapB.v[2] * sx;
	vec3_to_fixed_world(&cap.a, character.pos[0] + ax, character.pos[1] + ay, character.pos[2] + az);
	vec3_to_fixed_world(&cap.b, character.pos[0] + bx, character.pos[1] + by, character.pos[2] + bz);
	cap.radius = TO_FIXED(character.capsuleCollider.radius * sx);
	return cap;
}

void boss_apply_damage(float amount)
{
	if (amount <= 0.0f) return;
	boss.health -= amount;
	if (boss.health < 0.0f) boss.health = 0.0f;
	boss_debug_sound(amount > 15.0f ? "boss_damage_grunt_02" : "boss_damage_grunt_01");
	if (!bossLowHealthSoundPlayed && boss.health <= boss.maxHealth * 0.25f) {
		bossLowHealthSoundPlayed = true;
		boss_debug_sound("boss_low_health_breathing");
	}
	// printf("[Boss] took %.1f damage (%.1f/%.1f)\n", amount, boss.health, boss.maxHealth);
	t3d_debug_printf(4, 40, "Boss HP: %.0f/%.0f#", boss.health, boss.maxHealth);
	if (boss.health <= 0.0f && scene_get_game_state() == GAME_STATE_PLAYING) {
		scene_set_game_state(GAME_STATE_VICTORY);
	}
	boss.damageFlashTimer = 0.3f;
}

void boss_init(void) 
{
	bossModel = t3d_model_load("rom:/boss/boss.t3dm");

	T3DSkeleton* skeleton = malloc(sizeof(T3DSkeleton));
	*skeleton = t3d_skeleton_create(bossModel);
	
	// Create blend skeleton for animation blending
	// Use false to NOT copy matrices (matches example pattern)
	T3DSkeleton* skeletonBlend = malloc(sizeof(T3DSkeleton));
	*skeletonBlend = t3d_skeleton_clone(skeleton, false);

	T3DAnim** animations = NULL;

	const int animationCount = 7;
	const char* animationNames[] = {
		"Idle1",
		"Walk1",
		"SlowAttack1",
		"StrafeLeft1",
		"StrafeRight1",
		"ComboAttack1",
		"JumpForwardAttack1"
	};
	const bool animationsLooping[] = {
		true,  // Idle - loop
		true,  // Walk - loop
		false, // SlowAttack - one-shot
		true,  // StrafeLeft - loop
		true,  // StrafeRight - loop
		false, // ComboAttack - one-shot
		false  // JumpForward - one-shot
	};

	animations = malloc(animationCount * sizeof(T3DAnim*));
	for (int i = 0; i < animationCount; i++)
	{
		animations[i] = malloc(sizeof(T3DAnim));
		*animations[i] = t3d_anim_create(bossModel, animationNames[i]);
		t3d_anim_set_looping(animations[i], animationsLooping[i]);
		t3d_anim_set_playing(animations[i], false);
		t3d_anim_attach(animations[i], skeleton);
	}

	// Start with idle animation
	if (animationCount > 0) {
		t3d_anim_set_playing(animations[0], true); // Idle animation
	}

	rspq_block_begin();
	t3d_model_draw_skinned(bossModel, skeleton);
	rspq_block_t* dpl = rspq_block_end();

    // Capsule collider for boss-vs-room collision (approx; rotation ignored)
    CapsuleCollider bossCollider = {
        .localCapA = {{0.0f, 0.0f, 0.0f}},
        .localCapB = {{0.0f, 550.0f, 0.0f}},  // Increased height
        .radius = 360.0f                       // Increased radius
    };

	Boss newBoss = {
		.pos = {0.0f, 0.0f, 0.0f},
		.rot = {0.0f, 0.0f, 0.0f},
		// Scale increased to better match scene scale (map is 0.1, character is 0.004)
		.scale = {0.2f, 0.2f, 0.2f},
		.scrollParams = NULL,
		.skeleton = skeleton,
		.skeletonBlend = skeletonBlend,
		.animations = animations,
		.currentAnimation = 0,
		.previousAnimation = -1,
		.animationCount = animationCount,
		.capsuleCollider = bossCollider,
		.modelMat = malloc_uncached(sizeof(T3DMat4FP)),
		.dpl = dpl,
		.visible = true,
		.name = "Destroyer of Worlds",
		.maxHealth = 100.0f,
		.health = 100.0f,
		.phaseIndex = 1,
		.velX = 0.0f,
		.velZ = 0.0f,
		.currentSpeed = 0.0f,
		.turnRate = 8.0f,
		.orbitRadius = 6.0f,
		.stateTimer = 0.0f,
		.attackCooldown = 0.0f,
		.damageFlashTimer = 0.0f,
		.isAttacking = false,
		.attackAnimTimer = 0.0f,
		.blendFactor = 0.0f,
		.blendDuration = 0.5f,  // 500ms blend duration
		.blendTimer = 0.0f,
		.isBlending = false,
		.debugTargetingPos = {0.0f, 0.0f, 0.0f},
		.targetingLocked = false,
		.lockedTargetingPos = {0.0f, 0.0f, 0.0f},
		.targetingUpdateTimer = 0.0f,
		.lastPlayerPos = {0.0f, 0.0f, 0.0f},
		.lastPlayerVel = {0.0f, 0.0f}
	};

	// Initialize the newly allocated matrix before assigning to global
	t3d_mat4fp_identity(newBoss.modelMat);
	boss = newBoss;
	bossWasActive = false;
}

// ==== Update Functions ====

// Boss state machine
enum { 
	ST_IDLE, ST_CHASE, ST_STRAFE, ST_ORBIT, ST_CHARGE, ST_ATTACK, ST_RECOVER,
	ST_POWER_JUMP, ST_COMBO_ATTACK, ST_CHAIN_SWORD, ST_ROAR_STOMP, ST_TRACKING_SLAM
};
static int bossState = ST_IDLE;
static int bossPrevState = ST_IDLE;

static const char* bossTelegraphName = NULL;
static float bossTelegraphTimer = 0.0f;
static const float BOSS_TELEGRAPH_DURATION = 1.5f;

// Charge attack tracking
static bool chargeHasPassedPlayer = false;
static const char* boss_get_state_name(void) {
	switch (bossState) {
		case ST_IDLE:              return "Idle";
		case ST_CHASE:             return "Chase";
		case ST_STRAFE:            return "Strafe";
		case ST_ORBIT:             return "Orbit";
		case ST_CHARGE:            return "Charge";
		case ST_ATTACK:            return "Attack";
		case ST_RECOVER:           return "Recover";
		case ST_POWER_JUMP:        return "Power Jump";
		case ST_COMBO_ATTACK:      return "Combo Attack";
		case ST_CHAIN_SWORD:       return "Chain Sword";
		case ST_ROAR_STOMP:        return "Roar Stomp";
		case ST_TRACKING_SLAM:     return "Slow Attack";
		default:                   return "Unknown";
	}
}

static bool boss_state_is_attack(int state) {
	return state == ST_CHARGE
		|| state == ST_ATTACK
		|| state == ST_POWER_JUMP
		|| state == ST_COMBO_ATTACK
		|| state == ST_CHAIN_SWORD
		|| state == ST_ROAR_STOMP
		|| state == ST_TRACKING_SLAM;
}

// Helper function to predict where the character will be
static void predict_character_position(float *predictedPos, float predictionTime) {
    // Start with current character position
    predictedPos[0] = character.pos[0];
    predictedPos[1] = character.pos[1];
    predictedPos[2] = character.pos[2];

    // Get character velocity and add prediction
    float velX, velZ;
    character_get_velocity(&velX, &velZ);
    predictedPos[0] += velX * predictionTime;
    predictedPos[2] += velZ * predictionTime;
}

// Advanced targeting system with lag, anticipation, and attack locking
static void boss_update_targeting_system(float dt) {
    // Track player movement for anticipation
    float currentPlayerPos[3] = {character.pos[0], character.pos[1], character.pos[2]};
    float velX, velZ;
    character_get_velocity(&velX, &velZ);

    // Update player tracking data
    boss.lastPlayerVel[0] = velX;
    boss.lastPlayerVel[1] = velZ;

    // Check if we should lock targeting (entering attack state)
    bool shouldLockTargeting = boss_state_is_attack(bossState) && !boss.targetingLocked;

    if (shouldLockTargeting) {
        // Lock the current predicted target position for the attack
        // Use different prediction times based on attack type
        float predictionTime = 0.3f; // Default prediction time
        switch (bossState) {
            case ST_POWER_JUMP:
                predictionTime = 1.0f; // Longer prediction for power jump (long-range attack)
                break;
            case ST_CHAIN_SWORD:
                predictionTime = 0.8f; // Medium prediction for chain sword
                break;
            case ST_CHARGE:
            case ST_TRACKING_SLAM:
            case ST_COMBO_ATTACK:
            case ST_ROAR_STOMP:
            case ST_ATTACK:
                predictionTime = 0.3f; // Short prediction for close/fast attacks
                break;
        }

        predict_character_position(boss.lockedTargetingPos, predictionTime);
        boss.targetingLocked = true;
        boss.targetingUpdateTimer = 0.0f;
    }

    // Check if we should unlock targeting (leaving attack state)
    if (!boss_state_is_attack(bossState) && boss.targetingLocked) {
        boss.targetingLocked = false;
    }

    // Update targeting position based on state
    if (boss.targetingLocked) {
        // During attacks: use locked position (with slight lag for telegraphing)
        boss.debugTargetingPos[0] = boss.lockedTargetingPos[0];
        boss.debugTargetingPos[1] = boss.lockedTargetingPos[1];
        boss.debugTargetingPos[2] = boss.lockedTargetingPos[2];
    } else {
        // During non-attack states: anticipate player movement
        boss.targetingUpdateTimer += dt;

        // Update targeting every 0.1-0.2 seconds for some delay/lag
        if (boss.targetingUpdateTimer >= 0.15f) {
            boss.targetingUpdateTimer = 0.0f;

            // Anticipate where player will be based on current velocity
            float anticipationTime = 0.4f; // Look ahead 0.4 seconds
            predict_character_position(boss.debugTargetingPos, anticipationTime);

            // Store current player position for next update
            boss.lastPlayerPos[0] = currentPlayerPos[0];
            boss.lastPlayerPos[1] = currentPlayerPos[1];
            boss.lastPlayerPos[2] = currentPlayerPos[2];
        }
        // If not updating this frame, keep the current targeting position
    }
}

// Animation control function
void boss_trigger_attack_animation(void) {
    // Stop current animation
    if (boss.currentAnimation >= 0 && boss.currentAnimation < boss.animationCount) {
        t3d_anim_set_playing(boss.animations[boss.currentAnimation], false);
    }
    
    // Play attack animation
    boss.currentAnimation = BOSS_ANIM_ATTACK;
    if (boss.currentAnimation < boss.animationCount) {
        t3d_anim_set_playing(boss.animations[boss.currentAnimation], true);
        t3d_anim_set_time(boss.animations[boss.currentAnimation], 0.0f);
    }
    
    boss.isAttacking = true;
    boss.attackAnimTimer = 0.0f;
}

// Configure and enter the power jump attack state
static void boss_begin_power_jump(void) {
    bossState = ST_POWER_JUMP;
    boss.stateTimer = 0.0f;
    boss.powerJumpCooldown = 12.0f; // 12 second cooldown
    boss.currentAttackHasHit = false;
    
    // Store starting position
    boss.powerJumpStartPos[0] = boss.pos[0];
    boss.powerJumpStartPos[1] = boss.pos[1];
    boss.powerJumpStartPos[2] = boss.pos[2];
    
    // Lock targeting position with 1.0s prediction time (matching power jump prediction)
    predict_character_position(boss.lockedTargetingPos, 1.0f);
    boss.targetingLocked = true;
    
    // Use locked targeting position for attack
    boss.powerJumpTargetPos[0] = boss.lockedTargetingPos[0];
    boss.powerJumpTargetPos[1] = boss.lockedTargetingPos[1];
    boss.powerJumpTargetPos[2] = boss.lockedTargetingPos[2];
    boss.powerJumpHeight = 250.0f + ((float)(rand() % 5));

    boss.currentAttackName = "Power Jump";
    boss.attackNameDisplayTimer = 2.0f;
}

// Attack handler functions
static void boss_select_attack(void) {
    float dx = character.pos[0] - boss.pos[0];
    float dz = character.pos[2] - boss.pos[2];
    float dist = sqrtf(dx*dx + dz*dz);
    
    // Reset hit flag for new attack
    boss.currentAttackHasHit = false;
    
    // Simple distance-based attack selection with cooldown checks
    // When distance is 0-200, prefer close attacks (tracking slam or roar/stomp)
    if (dist <= 200.0f && boss.trackingSlamCooldown <= 0.0f) {
        // Close range - tracking slam
        bossState = ST_TRACKING_SLAM;
        boss.stateTimer = 0.0f;
        boss.trackingSlamCooldown = 8.0f; // 8 second cooldown

        // Set attacking flag to trigger attack animation through state-based system
        boss.isAttacking = true;
        boss.attackAnimTimer = 0.0f;
        boss.animationTransitionTimer = 0.0f;

        // Calculate target angle to player
        float angle = atan2f(-dx, dz);
        boss.trackingSlamTargetAngle = angle;

        boss.currentAttackName = "Slow Attack";
        boss.attackNameDisplayTimer = 2.0f;
    }
    else if (dist <= 200.0f && boss.roarStompCooldown <= 0.0f) {
        // Close range alternative - roar stomp (AOE)
        bossState = ST_ROAR_STOMP;
        boss.stateTimer = 0.0f;
        boss.roarStompCooldown = 6.0f; // 6 second cooldown
        
        boss.currentAttackName = "Roar Stomp";
        boss.attackNameDisplayTimer = 2.0f;
    }
    else if (dist > 200.0f && boss.chainSwordCooldown <= 0.0f) {
        // Long range - chain sword
        bossState = ST_CHAIN_SWORD;
        boss.stateTimer = 0.0f;
        boss.chainSwordCooldown = 10.0f; // 10 second cooldown
        boss.swordThrown = false;
        boss.chainSwordSlamHasHit = false;
        
        // Use locked targeting position for attack
        boss.chainSwordTargetPos[0] = boss.lockedTargetingPos[0];
        boss.chainSwordTargetPos[1] = boss.lockedTargetingPos[1];
        boss.chainSwordTargetPos[2] = boss.lockedTargetingPos[2];
        
        boss.currentAttackName = "Chain Sword";
        boss.attackNameDisplayTimer = 2.0f;
    }
    else if (boss.powerJumpCooldown <= 0.0f && dist >= 400.0f) {
        // Medium range - power jump
        boss_begin_power_jump();
    }
    else if (boss.comboCooldown <= 0.0f && boss.phaseIndex == 2) {
        // Phase 2 only - combo attack
        bossState = ST_COMBO_ATTACK;
        boss.stateTimer = 0.0f;
        boss.comboCooldown = 15.0f; // 15 second cooldown
        boss.comboStep = 0;
        boss.comboInterrupted = false;
        boss.comboVulnerableTimer = 0.0f;
        
        boss.currentAttackName = "Combo Attack";
        boss.attackNameDisplayTimer = 2.0f;
    }
    else if (boss.roarStompCooldown <= 0.0f) {
        // Fallback - roar stomp (AOE)
        bossState = ST_ROAR_STOMP;
        boss.stateTimer = 0.0f;
        boss.roarStompCooldown = 6.0f; // 6 second cooldown
        
        boss.currentAttackName = "Roar Stomp";
        boss.attackNameDisplayTimer = 2.0f;
    }
    else {
        // All attacks on cooldown, return to strafe
        bossState = ST_STRAFE;
        boss.stateTimer = 0.0f;
    }
}

static void boss_handle_tracking_slam_attack(float dt) {
    // Stationary attack - boss stays in place and tries to hit the character

    // Check for hit during slam
    if (!boss.currentAttackHasHit) {
        float dx = character.pos[0] - boss.pos[0];
        float dz = character.pos[2] - boss.pos[2];
        float dist = sqrtf(dx*dx + dz*dz);

        if (dist < 4.0f) { // Hit radius
            character_apply_damage(25.0f);
            boss.currentAttackHasHit = true;
        }
    }

    // Animation system will handle timing - attack completes when boss.isAttacking becomes false
}

static void boss_handle_chain_sword_attack(float dt) {
    // Keep boss facing the target during the entire attack
    float dx = boss.chainSwordTargetPos[0] - boss.pos[0];
    float dz = boss.chainSwordTargetPos[2] - boss.pos[2];
    // Use same rotation formula as strafe/chase for consistency
    boss.rot[1] = -atan2f(-dz, dx) + T3D_PI;
    
    // Phase 1: Throw sword (0.0 - 0.5s)
    if (!boss.swordThrown && boss.stateTimer < 0.5f) {
        boss_trigger_attack_animation();
    }
    else if (!boss.swordThrown && boss.stateTimer >= 0.5f) {
        // Launch projectile
        boss.swordThrown = true;
        boss.swordProjectilePos[0] = boss.pos[0];
        boss.swordProjectilePos[1] = boss.pos[1] + 2.0f;
        boss.swordProjectilePos[2] = boss.pos[2];
		boss_debug_sound("boss_chain_sword_throw");
		boss_debug_sound("boss_chain_rattle");
    }
    
    // Phase 2: Sword flight (0.5 - 1.5s)
    if (boss.swordThrown && boss.stateTimer < 1.5f && !boss.chainSwordSlamHasHit) {
        // Move sword toward target
        float t = (boss.stateTimer - 0.5f) / 1.0f; // 0 to 1
        boss.swordProjectilePos[0] = boss.pos[0] + (boss.chainSwordTargetPos[0] - boss.pos[0]) * t;
        boss.swordProjectilePos[2] = boss.pos[2] + (boss.chainSwordTargetPos[2] - boss.pos[2]) * t;
        boss.swordProjectilePos[1] = boss.pos[1] + 2.0f + sinf(t * T3D_PI) * 5.0f; // Arc
        
        // Check for hit
        float hitDx = character.pos[0] - boss.swordProjectilePos[0];
        float hitDz = character.pos[2] - boss.swordProjectilePos[2];
        float hitDy = character.pos[1] - boss.swordProjectilePos[1];
        float hitDist = sqrtf(hitDx*hitDx + hitDy*hitDy + hitDz*hitDz);
        
        if (hitDist < 3.0f && !boss.currentAttackHasHit) {
            character_apply_damage(20.0f);
			boss_debug_sound("boss_chain_sword_impact");
			boss_debug_sound("boss_attack_success");
            boss.currentAttackHasHit = true;
            boss.chainSwordSlamHasHit = true;
        }
    }
    
    // Phase 3: Sword slam and pull back (1.5s+)
    if (boss.stateTimer >= 1.5f && !boss.chainSwordSlamHasHit) {
        boss.chainSwordSlamHasHit = true;
		boss_debug_sound("boss_chain_sword_impact");
        // Sword hits ground at target
        boss.swordProjectilePos[0] = boss.chainSwordTargetPos[0];
        boss.swordProjectilePos[1] = boss.chainSwordTargetPos[1];
        boss.swordProjectilePos[2] = boss.chainSwordTargetPos[2];
        
        // Ground impact damage
        float impactDx = character.pos[0] - boss.swordProjectilePos[0];
        float impactDz = character.pos[2] - boss.swordProjectilePos[2];
        float impactDist = sqrtf(impactDx*impactDx + impactDz*impactDz);
        
        if (impactDist < 5.0f && !boss.currentAttackHasHit) {
            character_apply_damage(15.0f);
			boss_debug_sound("boss_attack_success");
            boss.currentAttackHasHit = true;
            // printf("[Boss] Sword ground impact!\n");
        }
    }
    
    // Phase 4: Pull boss toward sword (2.0s - 3.5s)
    if (boss.stateTimer >= 2.0f && boss.stateTimer < 3.5f) {
        // Boss gets pulled toward sword
        float pullDx = boss.swordProjectilePos[0] - boss.pos[0];
        float pullDz = boss.swordProjectilePos[2] - boss.pos[2];
        float pullDist = sqrtf(pullDx*pullDx + pullDz*pullDz);
        
        if (pullDist > 2.0f) {
            float pullSpeed = 200.0f;
            boss.velX = (pullDx / pullDist) * pullSpeed;
            boss.velZ = (pullDz / pullDist) * pullSpeed;
        } else {
            boss.velX *= 0.8f;
            boss.velZ *= 0.8f;
        }
    }
    
    // End attack - ensure clean transition
    if (boss.stateTimer >= 3.5f) {
        // Reset attack state
        boss.swordThrown = false;
        boss.chainSwordSlamHasHit = false;
        boss.velX = 0.0f;
        boss.velZ = 0.0f;
        bossState = ST_STRAFE;
        boss.stateTimer = 0.0f;
    }
}

static void boss_handle_roar_stomp_attack(float dt) {
    // Phase 1: Roar buildup (0.0 - 1.0s)
    if (boss.stateTimer < 1.0f) {
        // Face player - use consistent rotation formula
        float dx = character.pos[0] - boss.pos[0];
        float dz = character.pos[2] - boss.pos[2];
        boss.rot[1] = -atan2f(-dz, dx) + T3D_PI;
        
        if (boss.stateTimer > 0.8f && boss.stateTimer < 0.9f) {
            boss_trigger_attack_animation();
        }
    }
    // Phase 2: Stomp impact (1.0s)
    else if (boss.stateTimer >= 1.0f && boss.stateTimer < 1.1f) {
		if (!bossRoarImpactSoundPlayed) {
			boss_debug_sound("boss_roar_stomp_impact");
			boss_debug_sound("shockwave_rumble");
			bossRoarImpactSoundPlayed = true;
		}
        
        // Calculate damage based on distance (expanding shockwave)
        float dx = character.pos[0] - boss.pos[0];
        float dz = character.pos[2] - boss.pos[2];
        float dist = sqrtf(dx*dx + dz*dz);
        
        float shockwaveRadius = 15.0f;
        if (dist <= shockwaveRadius && !boss.currentAttackHasHit) {
            // Damage decreases with distance
            float damage = 30.0f * (1.0f - (dist / shockwaveRadius));
            character_apply_damage(damage);
			boss_debug_sound("boss_attack_success");
            boss.currentAttackHasHit = true;
            // printf("[Boss] Roar stomp hit for %.1f damage at distance %.1f!\n", damage, dist);
        }
    }
    // Phase 3: Recovery (1.1s+)
    else {
        if (boss.stateTimer > 2.0f) {
            bossState = ST_STRAFE;
            boss.stateTimer = 0.0f;
        }
    }
}

static void boss_handle_power_jump_attack(float dt) {
    // Frame timings at 30 FPS: 0-41 idle, 41-83 jump+land, 83-136 land+recover
    const float idleDuration = 41.0f / 25.0f;      // 1.367s
    const float jumpDuration = 42.0f / 25.0f;      // 1.4s
    const float recoverDuration = 53.0f / 25.0f;   // 1.767s
    const float totalDuration = idleDuration + jumpDuration + recoverDuration;

    // Phase 1: Idle preparation (0.0 - 1.367s)
    if (boss.stateTimer < idleDuration) {
        // Stay in place, face target direction
        float dx = boss.powerJumpTargetPos[0] - boss.pos[0];
        float dz = boss.powerJumpTargetPos[2] - boss.pos[2];
        if (dx != 0.0f || dz != 0.0f) {
            boss.rot[1] = -atan2f(-dz, dx) + T3D_PI;
        }
    }
    // Phase 2: Jump arc (1.367 - 2.767s)
    else if (boss.stateTimer < idleDuration + jumpDuration) {
        float t = (boss.stateTimer - idleDuration) / jumpDuration;
        
        // Smooth arc from start to target
        boss.pos[0] = boss.powerJumpStartPos[0] + (boss.powerJumpTargetPos[0] - boss.powerJumpStartPos[0]) * t;
        boss.pos[2] = boss.powerJumpStartPos[2] + (boss.powerJumpTargetPos[2] - boss.powerJumpStartPos[2]) * t;
        
        // Parabolic height
        boss.pos[1] = boss.powerJumpStartPos[1] + boss.powerJumpHeight * sinf(t * T3D_PI);
        
        // Face movement direction - use consistent rotation formula
        float dx = boss.powerJumpTargetPos[0] - boss.powerJumpStartPos[0];
        float dz = boss.powerJumpTargetPos[2] - boss.powerJumpStartPos[2];
        if (dx != 0.0f || dz != 0.0f) {
            boss.rot[1] = -atan2f(-dz, dx) + T3D_PI;
        }
    }
    // Phase 3: Landing impact + recovery (2.767 - 4.533s)
    else if (boss.stateTimer < totalDuration) {
        // Boss hits ground and recovers
        boss.pos[1] = boss.powerJumpStartPos[1];
		if (boss.stateTimer >= idleDuration + jumpDuration && !bossPowerJumpImpactPlayed) {
			boss_debug_sound("boss_power_jump_impact");
			bossPowerJumpImpactPlayed = true;
		}
        
        // Check for impact damage
        if (boss.stateTimer >= idleDuration + jumpDuration && boss.stateTimer < idleDuration + jumpDuration + 0.1f && !boss.currentAttackHasHit) {
            float dx = character.pos[0] - boss.pos[0];
            float dz = character.pos[2] - boss.pos[2];
            float dist = sqrtf(dx*dx + dz*dz);
            
            if (dist < 6.0f) {
                character_apply_damage(35.0f);
				boss_debug_sound("boss_attack_success");
                boss.currentAttackHasHit = true;
                // printf("[Boss] Power jump impact hit!\n");
            }
        }

        boss_trigger_attack_animation();
    }
    // End attack
    else {
        bossState = ST_STRAFE;
        boss.stateTimer = 0.0f;
    }
}

static void boss_handle_combo_attack(float dt) {
    const float stepDuration = 0.8f; // Each combo step
    const float vulnerableWindow = 0.4f;
    
    int targetStep = (int)(boss.stateTimer / stepDuration);
    if (targetStep != boss.comboStep && targetStep < 3) {
        boss.comboStep = targetStep;
        boss.comboVulnerableTimer = vulnerableWindow;
		const char* comboSound = "boss_combo_sweep";
		if (boss.comboStep == 1) comboSound = "boss_combo_slash";
		else if (boss.comboStep == 2) comboSound = "boss_combo_chop";
		boss_debug_sound(comboSound);
		boss_debug_sound("boss_vulnerable");
        // printf("[Boss] Combo step %d\n", boss.comboStep + 1);
    }
    
    // Update vulnerable timer
    if (boss.comboVulnerableTimer > 0.0f) {
        boss.comboVulnerableTimer -= dt;
    }
    
    // Check for player interrupt during vulnerable window
    if (boss.comboVulnerableTimer > 0.0f && !boss.comboInterrupted) {
        // Check if player is attacking boss during vulnerable window
        float dx = character.pos[0] - boss.pos[0];
        float dz = character.pos[2] - boss.pos[2];
        float dist = sqrtf(dx*dx + dz*dz);
        
        if (dist < 5.0f) {
            // For now, combo is always interruptible when player gets close during vulnerable window
            // In the future, this could check if character is attacking
            boss.comboInterrupted = true;
            boss_apply_damage(10.0f); // Bonus damage for interrupt
            bossState = ST_RECOVER;
            boss.stateTimer = 0.0f;
            // printf("[Boss] Combo interrupted! Bonus damage!\n");
            return;
        }
    }
    
    // Execute combo steps
    if (boss.comboStep == 0) {
        // Step 1: Sweep attack
        float dx = character.pos[0] - boss.pos[0];
        float dz = character.pos[2] - boss.pos[2];
        // Use consistent rotation formula
        boss.rot[1] = -atan2f(-dz, dx) + T3D_PI;
        
        if (boss.stateTimer > 0.5f && boss.stateTimer < 0.7f && !boss.currentAttackHasHit) {
            float dist = sqrtf(dx*dx + dz*dz);
            if (dist < 7.0f) {
                character_apply_damage(15.0f);
				boss_debug_sound("boss_attack_success");
                boss.currentAttackHasHit = true;
                // printf("[Boss] Combo sweep hit!\n");
            }
        }
    }
    else if (boss.comboStep == 1) {
        // Step 2: Forward thrust
        if (boss.stateTimer > stepDuration + 0.5f && boss.stateTimer < stepDuration + 0.7f) {
            float thrustSpeed = 300.0f;
            boss.velX = sinf(boss.rot[1]) * thrustSpeed * dt;
            boss.velZ = cosf(boss.rot[1]) * thrustSpeed * dt;
            
            if (!boss.currentAttackHasHit) {
                float dx = character.pos[0] - boss.pos[0];
                float dz = character.pos[2] - boss.pos[2];
                float dist = sqrtf(dx*dx + dz*dz);
                if (dist < 4.0f) {
                    character_apply_damage(20.0f);
					boss_debug_sound("boss_attack_success");
                    boss.currentAttackHasHit = true;
                    // printf("[Boss] Combo thrust hit!\n");
                }
            }
        }
    }
    else if (boss.comboStep == 2) {
        // Step 3: Overhead slam
        if (boss.stateTimer > stepDuration * 2 + 0.6f && boss.stateTimer < stepDuration * 2 + 0.8f && !boss.currentAttackHasHit) {
            float dx = character.pos[0] - boss.pos[0];
            float dz = character.pos[2] - boss.pos[2];
            float dist = sqrtf(dx*dx + dz*dz);
            
            if (dist < 6.0f) {
                character_apply_damage(30.0f);
				boss_debug_sound("boss_attack_success");
                boss.currentAttackHasHit = true;
                // printf("[Boss] Combo slam hit!\n");
            }
        }
    }
    
    // End combo
    if (boss.stateTimer > stepDuration * 3 + 0.5f) {
        boss.comboStep = 0;
        boss.comboInterrupted = false;
        bossState = ST_STRAFE;
        boss.stateTimer = 0.0f;
    }
}

static void boss_update_movement_and_physics(float dt) {
    // State-specific movement behavior
    float dx = character.pos[0] - boss.pos[0];
    float dz = character.pos[2] - boss.pos[2];
    float dist = sqrtf(dx*dx + dz*dz);
    
    const float ACCEL = 7.0f;
    const float FRICTION = 10.0f;
    // Reduced chase speed to be less aggressive
    const float SPEED_CHASE = 60.0f;
    const float SPEED_ORBIT = boss.phaseIndex == 1 ? 90.0f : 120.0f;
    const float SPEED_CHARGE = boss.phaseIndex == 1 ? 220.0f : 280.0f;
    // Slow strafe speed for Dark Souls-style behavior
    const float SPEED_STRAFE = boss.phaseIndex == 1 ? 100.0f : 120.0f;
    
    float desiredX = 0.0f, desiredZ = 0.0f;
    float maxSpeed = 0.0f;
    
    switch (bossState) {
        case ST_IDLE:
            // No movement
            maxSpeed = 0.0f;
            break;
            
        case ST_CHASE:
            // Move toward player (for when far away)
            if (dist > 0.0f) {
                desiredX = dx / dist;
                desiredZ = dz / dist;
            }
            maxSpeed = SPEED_CHASE;
            break;
            
        case ST_STRAFE:
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
                static float lastStrafeDir = 1.0f; // 1.0 = right, -1.0 = left
                static float strafeDirTimer = 0.0f; // Timer to prevent rapid direction changes
                const float STRONG_MOVEMENT_THRESHOLD = 80.0f; // Very high threshold to change direction
                const float MIN_DIRECTION_TIME = 3.0f; // Must strafe in one direction for at least 3 seconds
                const float OPPOSITE_DIRECTION_MULTIPLIER = 2.0f; // Require 2x movement in opposite direction
                
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
                    // If character moving same direction as strafe, or not moving much, keep current direction
                }
                // Otherwise, maintain lastStrafeDir (stick to current direction until timer expires)
                
                // Apply strafe direction
                if (lastStrafeDir > 0.0f) {
                    desiredX = rightX;
                    desiredZ = rightZ;
                    bossStrafeDirection = 1.0f; // Right
                } else {
                    desiredX = leftX;
                    desiredZ = leftZ;
                    bossStrafeDirection = -1.0f; // Left
                }
                
                // Blend in some forward movement if character is far away
                if (dist > boss.orbitRadius + 5.0f) {
                    float forwardBlend = fminf(1.0f, (dist - boss.orbitRadius) / 20.0f);
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
            
            
        case ST_CHARGE:
            // Charge toward locked targeting position
            {
                float targetDx = boss.lockedTargetingPos[0] - boss.pos[0];
                float targetDz = boss.lockedTargetingPos[2] - boss.pos[2];
                float targetDist = sqrtf(targetDx*targetDx + targetDz*targetDz);
                if (targetDist > 0.0f) {
                    desiredX = targetDx / targetDist;
                    desiredZ = targetDz / targetDist;
                }
            }
            maxSpeed = SPEED_CHARGE;
            break;
            
        case ST_RECOVER:
            // Slow movement
            maxSpeed = SPEED_ORBIT * 0.5f;
            break;
            
        default:
            // Attack states handle their own movement
            break;
    }
    
    // Apply movement for non-attack states
    if (bossState != ST_POWER_JUMP && bossState != ST_CHAIN_SWORD && 
        bossState != ST_TRACKING_SLAM && bossState != ST_COMBO_ATTACK && 
        bossState != ST_ROAR_STOMP) {
        
        boss.velX += (desiredX * maxSpeed - boss.velX) * ACCEL * dt;
        boss.velZ += (desiredZ * maxSpeed - boss.velZ) * ACCEL * dt;
    }
    
    // Apply friction
    float frictionScale = (bossState >= ST_POWER_JUMP) ? 0.3f : 1.0f;
    if (bossState == ST_CHASE) {
        frictionScale = 0.8f; // Keep speed during pursuit to match player pace
    }
    float k = FRICTION * frictionScale;
    boss.velX *= expf(-k * dt);
    boss.velZ *= expf(-k * dt);
    
    // Update position with room-bounds collision (slide by axis)
    float nextX = boss.pos[0] + boss.velX * dt;
    float nextZ = boss.pos[2] + boss.velZ * dt;

    float sx = boss.scale[0];
    // X axis
    if (!collision_mesh_check_bounds_capsule(
            nextX, boss.pos[1], boss.pos[2],
            boss.capsuleCollider.localCapA.v[0], boss.capsuleCollider.localCapA.v[1], boss.capsuleCollider.localCapA.v[2],
            boss.capsuleCollider.localCapB.v[0], boss.capsuleCollider.localCapB.v[1], boss.capsuleCollider.localCapB.v[2],
            boss.capsuleCollider.radius, sx
        )) {
        boss.pos[0] = nextX;
    } else {
        boss.velX = 0.0f;
    }

    // Z axis
    if (!collision_mesh_check_bounds_capsule(
            boss.pos[0], boss.pos[1], nextZ,
            boss.capsuleCollider.localCapA.v[0], boss.capsuleCollider.localCapA.v[1], boss.capsuleCollider.localCapA.v[2],
            boss.capsuleCollider.localCapB.v[0], boss.capsuleCollider.localCapB.v[1], boss.capsuleCollider.localCapB.v[2],
            boss.capsuleCollider.radius, sx
        )) {
        boss.pos[2] = nextZ;
    } else {
        boss.velZ = 0.0f;
    }
    
    // Update facing direction
    float targetAngle;
    if (bossState == ST_STRAFE || bossState == ST_CHASE) {
        // During strafe/chase, always face the character immediately (not movement direction)
        float faceDx = character.pos[0] - boss.pos[0];
        float faceDz = character.pos[2] - boss.pos[2];
        // Use immediate rotation during strafe/chase to ensure boss always faces player
        // Use same formula as attack states
        boss.rot[1] = -atan2f(-faceDz, faceDx) + T3D_PI;
        return; // Skip smooth rotation for strafe/chase
    } else if (bossState == ST_CHARGE) {
        // During charge, face the locked targeting position immediately
        float faceDx = boss.lockedTargetingPos[0] - boss.pos[0];
        float faceDz = boss.lockedTargetingPos[2] - boss.pos[2];
        boss.rot[1] = -atan2f(-faceDz, faceDx) + T3D_PI;
        return; // Skip smooth rotation for charge
    } else if (bossState >= ST_POWER_JUMP) {
        // Attack states handle their own rotation
        return;
    } else {
        // Default: face movement direction (for other states)
        targetAngle = atan2f(-boss.velX, boss.velZ); // Face movement direction
    }
    
    // Smooth rotation for non-strafe/chase states
    float currentAngle = boss.rot[1];
    float angleDelta = targetAngle - currentAngle;
    while (angleDelta > T3D_PI) angleDelta -= 2.0f * T3D_PI;
    while (angleDelta < -T3D_PI) angleDelta += 2.0f * T3D_PI;
    
    // Use clamped turn rate for all states
    float maxTurn = boss.turnRate * dt;
    if (angleDelta > maxTurn) angleDelta = maxTurn;
    else if (angleDelta < -maxTurn) angleDelta = -maxTurn;
    
    boss.rot[1] = currentAngle + angleDelta;
}

static void boss_update_animation_system(float dt) {
    if (!boss.skeleton || !boss.animations || !boss.skeletonBlend) return;
    
    // Update attack animation timer
    if (boss.isAttacking) {
        boss.attackAnimTimer += dt;
        // Use longer duration for slow attack to allow animation to complete (155 frames at 60 FPS = 2.58s, doubled = 5.2s)
        const float attackDuration = (bossState == ST_TRACKING_SLAM) ? 6.0f : 0.9f;
        if (boss.attackAnimTimer >= attackDuration) {
            boss.isAttacking = false;
            boss.attackAnimTimer = 0.0f;
        }
    }
    
    // Choose animation based on state and movement
    BossAnimState targetAnim = BOSS_ANIM_IDLE;
    
    // Attack-specific animations
    if (bossState == ST_COMBO_ATTACK) {
        targetAnim = BOSS_ANIM_COMBO_ATTACK;
    } else if (bossState == ST_POWER_JUMP) {
        targetAnim = BOSS_ANIM_JUMP_FORWARD;
    } else if (boss.isAttacking || bossState == ST_CHARGE || bossState == ST_ATTACK || 
               bossState == ST_TRACKING_SLAM || bossState == ST_ROAR_STOMP || bossState == ST_CHAIN_SWORD) {
        targetAnim = BOSS_ANIM_ATTACK;
    } else if (bossState == ST_STRAFE) {
        // Use left or right strafe animation based on direction (swapped - animations are reversed)
        targetAnim = (bossStrafeDirection > 0.0f) ? BOSS_ANIM_STRAFE_LEFT : BOSS_ANIM_STRAFE_RIGHT;
    } else if (bossState == ST_CHASE) {
        // Use walk animation while chasing (no run animation available)
        targetAnim = BOSS_ANIM_WALK;
    } else {
        float speed = sqrtf(boss.velX * boss.velX + boss.velZ * boss.velZ);
        
        if (speed > 30.0f) {
            targetAnim = BOSS_ANIM_WALK;
        }
        // Otherwise stays as BOSS_ANIM_IDLE
    }
    
    // Switch animations if needed - start blending
    if (boss.currentAnimation != targetAnim) {
        // If we're already blending, complete the blend first
        if (boss.isBlending) {
            // Force complete the blend
            boss.blendFactor = 1.0f;
            boss.isBlending = false;
            boss.blendTimer = 0.0f;
            
            // Stop previous animation
            if (boss.previousAnimation >= 0 && boss.previousAnimation < boss.animationCount) {
                t3d_anim_set_playing(boss.animations[boss.previousAnimation], false);
            }
        }
        
        // Store previous animation for blending
        boss.previousAnimation = boss.currentAnimation;
        
        // Start blending if we have a valid previous animation
        if (boss.previousAnimation >= 0 && boss.previousAnimation < boss.animationCount) {
            // Save the current animation time before switching
            float savedTime = 0.0f;
            if (boss.currentAnimation >= 0 && boss.currentAnimation < boss.animationCount && boss.animations[boss.currentAnimation]) {
                savedTime = boss.animations[boss.currentAnimation]->time;
            }
            
            // CRITICAL FIX: Set up blend skeleton to preserve current visual state
            // Reset blend skeleton and attach previous animation (which is still current at this point)
            t3d_skeleton_reset(boss.skeletonBlend);
            t3d_anim_attach(boss.animations[boss.previousAnimation], boss.skeletonBlend);
            t3d_anim_set_playing(boss.animations[boss.previousAnimation], true);
            // Restore the animation time so it continues from where it was
            t3d_anim_set_time(boss.animations[boss.previousAnimation], savedTime);
            
            // Start blending - the blend skeleton will be updated in the normal animation loop
            boss.isBlending = true;
            boss.blendFactor = 0.0f;
            boss.blendTimer = 0.0f; // Start at 0 so blending can begin after blend skeleton is updated
        }
        
        // Start new animation on main skeleton
        // IMPORTANT: We reset the main skeleton here, which will cause a visual snap on the first frame
        // However, blending will smooth this out over the next frames
        boss.currentAnimation = targetAnim;
        if (boss.currentAnimation < boss.animationCount) {
            // Reset main skeleton and attach new animation
            t3d_skeleton_reset(boss.skeleton);
            t3d_anim_attach(boss.animations[boss.currentAnimation], boss.skeleton);
            t3d_anim_set_playing(boss.animations[boss.currentAnimation], true);
            t3d_anim_set_time(boss.animations[boss.currentAnimation], 0.0f);  // Start from beginning
        }
    }
    
    // Update blending
    if (boss.isBlending) {
        boss.blendTimer += dt;
        // Clamp negative timer to 0 (first frame skip)
        if (boss.blendTimer < 0.0f) {
            boss.blendTimer = 0.0f;
            boss.blendFactor = 0.0f;
        } else if (boss.blendTimer >= boss.blendDuration) {
            // Blend complete
            boss.blendFactor = 1.0f;
            boss.isBlending = false;
            boss.blendTimer = 0.0f;
            
            // Stop previous animation
            if (boss.previousAnimation >= 0 && boss.previousAnimation < boss.animationCount) {
                t3d_anim_set_playing(boss.animations[boss.previousAnimation], false);
            }
        } else {
            // Interpolate blend factor
            boss.blendFactor = boss.blendTimer / boss.blendDuration;
        }
    }
    
    // Update all animations (both main and blend animations are updated every frame)
    for (int i = 0; i < boss.animationCount; i++) {
        if (boss.animations[i]) {
            t3d_anim_update(boss.animations[i], dt);
        }
    }
    
    // Update blend skeleton if blending is active
    // This applies the previous animation's pose to the blend skeleton before blending
    if (boss.isBlending && boss.skeletonBlend && 
        boss.previousAnimation >= 0 && boss.previousAnimation < boss.animationCount &&
        boss.animations[boss.previousAnimation] && boss.animations[boss.previousAnimation]->isPlaying) {
        // Update the blend skeleton to apply the previous animation's current pose
        // This must happen after animations are updated but before blending
        t3d_skeleton_update(boss.skeletonBlend);
    }
    
    // Blend skeletons if blending is active (matches example pattern)
    // Note: We blend BEFORE updating the main skeleton
    if (boss.isBlending && boss.skeletonBlend) {
        // Safety check: Only proceed if we have all required components
        bool canBlend = (boss.previousAnimation >= 0 && 
                        boss.previousAnimation < boss.animationCount && 
                        boss.animations[boss.previousAnimation] != NULL &&
                        boss.animations[boss.previousAnimation]->isPlaying &&
                        boss.blendFactor >= 0.0f && 
                        boss.blendFactor <= 1.0f &&
                        boss.blendTimer >= 0.0f); // Allow blending after blend skeleton is updated
        
        if (canBlend) {
            // Blend skeletons: blend from skeletonBlend (old) to skeleton (new), store result in skeleton
            // blendFactor: 0.0 = all old (skeletonBlend), 1.0 = all new (skeleton)
            t3d_skeleton_blend(boss.skeletonBlend, boss.skeleton, boss.skeleton, boss.blendFactor);
        } else {
            // Not safe to blend - disable blending to prevent crashes
            boss.isBlending = false;
            boss.blendFactor = 0.0f;
            boss.blendTimer = 0.0f;
            // Stop previous animation if it was playing
            if (boss.previousAnimation >= 0 && boss.previousAnimation < boss.animationCount &&
                boss.animations[boss.previousAnimation]) {
                t3d_anim_set_playing(boss.animations[boss.previousAnimation], false);
            }
        }
    }
    
    // Update main skeleton (ONLY the main skeleton, never the blend skeleton)
    t3d_skeleton_update(boss.skeleton);
}

static void boss_update_cooldowns(float dt) {
    // Update all attack cooldowns
    if (boss.attackCooldown > 0.0f) boss.attackCooldown -= dt;
    if (boss.powerJumpCooldown > 0.0f) boss.powerJumpCooldown -= dt;
    if (boss.comboCooldown > 0.0f) boss.comboCooldown -= dt;
    if (boss.chainSwordCooldown > 0.0f) boss.chainSwordCooldown -= dt;
    if (boss.roarStompCooldown > 0.0f) boss.roarStompCooldown -= dt;
    if (boss.trackingSlamCooldown > 0.0f) boss.trackingSlamCooldown -= dt;
    
    // Update display timers
    if (boss.attackNameDisplayTimer > 0.0f) boss.attackNameDisplayTimer -= dt;
    if (boss.hitMessageTimer > 0.0f) boss.hitMessageTimer -= dt;
}

void boss_update(void) {
	// Don't update boss AI during cutscenes, reset state for fresh start
	if (!scene_is_boss_active()) {
		// Ensure activation edge resets when boss is inactive (e.g. during cutscenes/restart)
		bossWasActive = false;
		bossState = ST_IDLE;
		bossPrevState = ST_IDLE;
		bossTelegraphTimer = 0.0f;
		bossTelegraphName = NULL;
		boss.stateTimer = 0.0f;
		return;
	}

	float dt = deltaTime;

	// Decay telegraph timer
	if (bossTelegraphTimer > 0.0f) {
		bossTelegraphTimer -= dt;
		if (bossTelegraphTimer < 0.0f) bossTelegraphTimer = 0.0f;
	}
	if (bossDebugSoundTimer > 0.0f) {
		bossDebugSoundTimer -= dt;
		if (bossDebugSoundTimer < 0.0f) bossDebugSoundTimer = 0.0f;
	}

	int stateBefore = bossState;

	// If boss was just activated (scene_is_boss_active became true), start chasing immediately
	bool justActivated = scene_is_boss_active() && !bossWasActive;
	bossWasActive = scene_is_boss_active();
	
	if (justActivated && bossState == ST_IDLE) {
		bossState = ST_CHASE;
		boss.stateTimer = 0.0f;
		// Ensure blending is disabled when boss first becomes active
		boss.isBlending = false;
		boss.blendFactor = 0.0f;
		boss.blendTimer = 0.0f;
		boss.previousAnimation = -1;
	}

	// Basic AI scaffolding: chase + orbit + simple charge trigger
	// Advance the state timer each frame so time-based transitions fire
	boss.stateTimer += dt;

	// Acquire target from character (centered around their focus height)
	T3DVec3 target = {{ character.pos[0], character.pos[1] + 0.5f, character.pos[2] }};
	float dx = target.v[0] - boss.pos[0];
	float dz = target.v[2] - boss.pos[2];
	float dist = sqrtf(dx*dx + dz*dz);

	// Advanced targeting system with lag, delay, and anticipation
	boss_update_targeting_system(dt);

	// Phase switch at 50% HP
	if (boss.phaseIndex == 1 && boss.health <= boss.maxHealth * 0.5f) {
		boss.phaseIndex = 2;
		// printf("[Boss] Phase 2!\n");
		t3d_debug_printf(4, 4, "Phase 2!#");
		boss_debug_sound("boss_phase_transition");
	}

	// Update all cooldowns
	boss_update_cooldowns(dt);
	
	// State machine logic
	const float COMBAT_RADIUS = boss.orbitRadius;
	
	// State transitions
	switch (bossState) {
		case ST_IDLE:
			// Start chasing when player gets close or boss is activated
			if (dist < 40.0f) {
				bossState = ST_CHASE;
				boss.stateTimer = 0.0f;
			}
			break;
			
		case ST_CHASE:
			// If player is far away, use power jump to quickly close distance
			if (dist >= 400.0f && boss.powerJumpCooldown <= 0.0f) {
				boss_begin_power_jump();
				break;
			}
			// Attack when distance is between 0 and 200 - prefer close attacks
			const float MIN_ATTACK_DISTANCE_CHASE = 0.0f;
			const float MAX_ATTACK_DISTANCE_CHASE = 200.0f; // Attack when within this distance
			if (boss.attackCooldown <= 0.0f && dist >= MIN_ATTACK_DISTANCE_CHASE && dist <= MAX_ATTACK_DISTANCE_CHASE) {
				// When close (0-200), prefer close attacks (tracking slam or roar/stomp)
				// Random chance to either charge or use special attack
				float r = (float)(rand() % 100) / 100.0f;
				if (r < 0.3f) { // 30% chance for basic charge
					bossState = ST_CHARGE;
					boss.stateTimer = 0.0f;
					boss.attackCooldown = 2.0f;
					chargeHasPassedPlayer = false;
					printf("[Boss] CHARGE!\n");
				} else {
					// Use sophisticated attack selection (will prefer close attacks when distance is 0-200)
					boss_select_attack();
				}
			}
			break;
			
		case ST_STRAFE:
			// If player moves too far away, chase them (use higher threshold to prevent rapid switching)
			// But don't immediately chase if we just finished a slow attack (give time for repositioning)
			if (dist > COMBAT_RADIUS + 350.0f && boss.stateTimer > 0.1f) {
				bossState = ST_CHASE;
				boss.stateTimer = 0.0f;
				break;
			}
			// Minimum strafe duration for the "slow dance" - boss must strafe for at least this long
			const float MIN_STRAFE_DURATION = 3.0f; // 3 seconds of strafing before considering attack
			const float MAX_ATTACK_DISTANCE = 100.0f; // Only attack when within this distance
			
			// Select attack when cooldown is ready AND minimum strafe time has passed AND close enough
			if (boss.attackCooldown <= 0.0f && boss.stateTimer >= MIN_STRAFE_DURATION && dist <= MAX_ATTACK_DISTANCE) {
				// Random chance to either charge or use special attack
				float r = (float)(rand() % 100) / 100.0f;
				if (r < 0.3f) { // 30% chance for basic charge
					bossState = ST_CHARGE;
					boss.stateTimer = 0.0f;
					boss.attackCooldown = 2.0f;
					chargeHasPassedPlayer = false;
					printf("[Boss] CHARGE!\n");
				} else {
					// Use sophisticated attack selection
					boss_select_attack();
				}
			}
			break;
			
		case ST_CHARGE:
			// Calculate distance to locked target position (where we're charging toward)
			{
				float targetDx = boss.lockedTargetingPos[0] - boss.pos[0];
				float targetDz = boss.lockedTargetingPos[2] - boss.pos[2];
				float distToTarget = sqrtf(targetDx*targetDx + targetDz*targetDz);

				// Track if we've reached the target point (within 3 units)
				if (!chargeHasPassedPlayer && distToTarget < 3.0f) {
					chargeHasPassedPlayer = true;
				}

				// Continue charging for a bit after reaching the target:
				// - We reached the target (chargeHasPassedPlayer = true)
				// - AND we've charged for at least 1.5 seconds total (minimum charge duration)
				// - OR we've charged for 2.5 seconds (maximum charge duration)
				bool reachedTarget = chargeHasPassedPlayer;
				bool minimumChargeTime = boss.stateTimer >= 1.5f;
				bool maximumChargeTime = boss.stateTimer > 3.0f; // Increased from 5.0f for more controlled duration

				if ((reachedTarget && minimumChargeTime) || maximumChargeTime) {
					bossState = ST_RECOVER;
					boss.stateTimer = 0.0f;
					chargeHasPassedPlayer = false;
				}
			}
			
			// Check for charge hit
			if (boss.stateTimer > 0.2f && boss.stateTimer < 0.5f && !boss.currentAttackHasHit) {
				SCU_CapsuleFixed bc = boss_make_capsule_fixed();
				SCU_CapsuleFixed cc = character_make_capsule_fixed();
				if (scu_fixed_capsule_vs_capsule(&bc, &cc)) {
					character_apply_damage(15.0f);
					boss_debug_sound("boss_attack_success");
					boss.currentAttackHasHit = true;
					// printf("[Boss] Charge hit!\n");
				}
			}
			break;
			
		case ST_ATTACK:
			// Basic attack state (fallback)
			if (boss.stateTimer > 1.2f) {
				bossState = ST_RECOVER;
				boss.stateTimer = 0.0f;
			}
			
			// Check for basic attack hit
			if (boss.stateTimer > 0.2f && boss.stateTimer < 0.5f && !boss.currentAttackHasHit) {
				SCU_CapsuleFixed bc = boss_make_capsule_fixed();
				SCU_CapsuleFixed cc = character_make_capsule_fixed();
				if (scu_fixed_capsule_vs_capsule(&bc, &cc)) {
					character_apply_damage(12.0f);
					boss_debug_sound("boss_basic_attack_hit");
					boss_debug_sound("boss_attack_success");
					boss.currentAttackHasHit = true;
					// printf("[Boss] Basic attack hit!\n");
				}
			}
			break;
			
		case ST_RECOVER:
			// Recovery period
			if (boss.stateTimer > 0.8f) {
				if (dist > COMBAT_RADIUS + 10.0f) {
					bossState = ST_CHASE;
				} else {
					bossState = ST_STRAFE;
				}
				boss.stateTimer = 0.0f;
			}
			break;
			
		// Handle sophisticated attack states
		case ST_POWER_JUMP:
			boss_handle_power_jump_attack(dt);
			break;
			
		case ST_COMBO_ATTACK:
			boss_handle_combo_attack(dt);
			break;
			
		case ST_CHAIN_SWORD:
			boss_handle_chain_sword_attack(dt);
			break;
			
		case ST_ROAR_STOMP:
			boss_handle_roar_stomp_attack(dt);
			break;
			
		case ST_TRACKING_SLAM:
			boss_handle_tracking_slam_attack(dt);
			// Transition to strafe when animation completes
			if (!boss.isAttacking) {
				// Wait for blend to complete before changing state
				boss.animationTransitionTimer += dt;
				if (boss.animationTransitionTimer >= boss.blendDuration) {
					bossState = ST_STRAFE;
					boss.stateTimer = 0.0f;
					boss.animationTransitionTimer = 0.0f;
				}
			}
			break;
	}

	// Track state entry for placeholder SFX triggers
	if (stateBefore != bossState) {
		switch (bossState) {
			case ST_POWER_JUMP:
				bossPowerJumpImpactPlayed = false;
				boss_debug_sound("boss_power_jump_windup");
				break;
			case ST_COMBO_ATTACK:
				boss_debug_sound("boss_combo_sweep");
				break;
			case ST_CHAIN_SWORD:
				boss_debug_sound("boss_chain_sword_throw");
				boss_debug_sound("boss_chain_rattle");
				break;
			case ST_ROAR_STOMP:
				bossRoarImpactSoundPlayed = false;
				boss_debug_sound("boss_roar_buildup");
				break;
			case ST_TRACKING_SLAM:
				boss_debug_sound("boss_tracking_slam_charge");
				break;
			case ST_CHARGE:
				boss_debug_sound("boss_charge_footsteps");
				break;
			case ST_CHASE:
				boss_debug_sound("boss_footstep_heavy");
				break;
			case ST_STRAFE:
				boss_debug_sound("boss_footstep_heavy");
				// Set initial attack cooldown when entering strafe to ensure minimum strafe duration
				// This creates the "slow dance" feeling where boss and character size each other up
				if (boss.attackCooldown <= 0.0f) {
					boss.attackCooldown = 2.0f; // Initial cooldown, combined with MIN_STRAFE_DURATION
				}
				break;
			case ST_IDLE:
				boss_debug_sound("boss_idle_ambient");
				break;
			default:
				break;
		}
	}

	// Detect transitions into attack states to show a telegraph label
	if (stateBefore != bossState && boss_state_is_attack(bossState)) {
		bossTelegraphName = boss_get_state_name();
		bossTelegraphTimer = BOSS_TELEGRAPH_DURATION;
	}
	bossPrevState = bossState;

	// Update movement and physics
	boss_update_movement_and_physics(dt);
	
	// Update animation system
	boss_update_animation_system(dt);
	
	// Update transformation matrix
	t3d_mat4fp_from_srt_euler(boss.modelMat, boss.scale, boss.rot, boss.pos);
}

void boss_update_position(void) 
{
	// Update the full transformation matrix with scale, rotation, and position
	t3d_mat4fp_from_srt_euler(boss.modelMat,
		(float[3]){boss.scale[0], boss.scale[1], boss.scale[2]},
		(float[3]){boss.rot[0], boss.rot[1], boss.rot[2]},
		(float[3]){boss.pos[0], boss.pos[1], boss.pos[2]}
	);
}

void boss_update_cutscene(void)
{
	// Keep idle animation/skeleton updated during cutscenes so the boss renders
	boss_update_animation_system(deltaTime);
	boss_update_position();
}

// ==== Drawing Functions ====

void boss_draw(void) 
{
	t3d_matrix_set(boss.modelMat, true);
	rspq_block_run(boss.dpl);
}

void boss_delete(void)
{
	rspq_wait();

	t3d_model_free(bossModel);

	free_if_not_null(boss.scrollParams);

	if (boss.skeleton) 
	{
		t3d_skeleton_destroy(boss.skeleton);
		free(boss.skeleton);
		boss.skeleton = NULL;
	}

	if (boss.skeletonBlend)
	{
		t3d_skeleton_destroy(boss.skeletonBlend);
		free(boss.skeletonBlend);
		boss.skeletonBlend = NULL;
	}

	if (boss.animations) 
	{
		for (int i = 0; i < boss.animationCount; i++) 
		{
		if (boss.animations[i]) 
		{
			t3d_anim_destroy(boss.animations[i]);
			free(boss.animations[i]);
		}
		}
		free(boss.animations);
		boss.animations = NULL;
	}

	if (boss.modelMat) 
	{
		rspq_wait();
		free_uncached(boss.modelMat);
		boss.modelMat = NULL;
	}

	if (boss.dpl) 
	{
		rspq_wait();
		rspq_block_free(boss.dpl);
		boss.dpl = NULL;
	}
}

void boss_reset(void)
{
	// Reset boss to initial state for scene restart
	bossState = ST_IDLE;
	bossPrevState = ST_IDLE;
	bossWasActive = false;
	bossTelegraphTimer = 0.0f;
	bossTelegraphName = NULL;
	bossDebugSoundName = NULL;
	bossDebugSoundTimer = 0.0f;
	bossLowHealthSoundPlayed = false;
	bossPowerJumpImpactPlayed = false;
	bossRoarImpactSoundPlayed = false;
	boss.health = boss.maxHealth;
	boss.phaseIndex = 1;
	boss.stateTimer = 0.0f;
	
	// Reset all cooldowns
	boss.attackCooldown = 0.0f;
	boss.powerJumpCooldown = 0.0f;
	boss.comboCooldown = 0.0f;
	boss.chainSwordCooldown = 0.0f;
	boss.roarStompCooldown = 0.0f;
	boss.trackingSlamCooldown = 0.0f;
	
	// Reset attack state
	boss.currentAttackHasHit = false;
	boss.currentAttackName = NULL;
	boss.attackNameDisplayTimer = 0.0f;
	boss.animationTransitionTimer = 0.0f;
	
	// Reset position to origin or keep current (depending on game design)
	// boss.pos values will be set by the scene initialization

	// Reset targeting system
	boss.debugTargetingPos[0] = 0.0f;
	boss.debugTargetingPos[1] = 0.0f;
	boss.debugTargetingPos[2] = 0.0f;
	boss.targetingLocked = false;
	boss.lockedTargetingPos[0] = 0.0f;
	boss.lockedTargetingPos[1] = 0.0f;
	boss.lockedTargetingPos[2] = 0.0f;
	boss.targetingUpdateTimer = 0.0f;
	boss.lastPlayerPos[0] = 0.0f;
	boss.lastPlayerPos[1] = 0.0f;
	boss.lastPlayerPos[2] = 0.0f;
	boss.lastPlayerVel[0] = 0.0f;
	boss.lastPlayerVel[1] = 0.0f;

	// Reset velocity
	boss.velX = 0.0f;
	boss.velZ = 0.0f;
	
	// Reset animation state
	boss.isAttacking = false;
	boss.attackAnimTimer = 0.0f;
	boss.currentAnimation = BOSS_ANIM_IDLE;
	boss.previousAnimation = -1;
	boss.isBlending = false;
	boss.blendFactor = 0.0f;
	boss.blendTimer = 0.0f;
}

void boss_draw_ui(T3DViewport *viewport)
{
	// Show simple HUD info when the boss is active
	if (boss.health <= 0 || !scene_is_boss_active() || scene_is_cutscene_active()) {
		return;
	}

	// Top health bar (UI pass so it is not affected by 3D fog/lighting)
	float ratio = boss.maxHealth > 0.0f ? fmaxf(0.0f, fminf(1.0f, boss.health / boss.maxHealth)) : 0.0f;
	float flash = 0.0f;
	if (boss.damageFlashTimer > 0.0f) {
		flash = fminf(1.0f, boss.damageFlashTimer / 0.3f);
		boss.damageFlashTimer -= deltaTime;
		if (boss.damageFlashTimer < 0.0f) boss.damageFlashTimer = 0.0f;
	}
	draw_boss_health_bar(boss.name, ratio, flash);

	if (!debugDraw) {
		return;
	}

	// Display the current distance between the player and boss for quick tuning
	float dx = character.pos[0] - boss.pos[0];
	float dy = character.pos[1] - boss.pos[1];
	float dz = character.pos[2] - boss.pos[2];
	float dist = sqrtf(dx * dx + dy * dy + dz * dz);

	// Set text color via prim color (rdpq_textparms_t doesn't carry color)
	rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    int y = 48;
    int listSpacing = 12;
    rdpq_text_printf(NULL, FONT_UNBALANCED, 20, y, "Boss State: %s", boss_get_state_name());
    y += listSpacing;
    rdpq_text_printf(NULL, FONT_UNBALANCED, 20, y, "Boss Dist: %.1f", dist);
    y += listSpacing;
    if (bossTelegraphTimer > 0.0f && bossTelegraphName) {
        rdpq_text_printf(NULL, FONT_UNBALANCED, 20, y, "Next: %s", bossTelegraphName);
        y += listSpacing;
    }
    if (bossDebugSoundTimer > 0.0f && bossDebugSoundName) {
        rdpq_text_printf(NULL, FONT_UNBALANCED, 20, y, "Boss SFX: %s", bossDebugSoundName);
        y += listSpacing;
    }
    
    // Animation blending stats
    if (boss.isBlending) {
        y += listSpacing;
        rdpq_set_prim_color(RGBA32(0x39, 0xBF, 0x1F, 0xFF)); // Green for blending active
        rdpq_text_printf(NULL, FONT_UNBALANCED, 20, y, "Blending: ON");
        y += listSpacing;
        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
        rdpq_text_printf(NULL, FONT_UNBALANCED, 20, y, "Blend Factor: %.2f (%.0f%%)", boss.blendFactor, boss.blendFactor * 100.0f);
        y += listSpacing;
        rdpq_text_printf(NULL, FONT_UNBALANCED, 20, y, "Blend Timer: %.3fs / %.3fs", boss.blendTimer, boss.blendDuration);
        y += listSpacing;
        if (boss.previousAnimation >= 0 && boss.previousAnimation < boss.animationCount) {
            const char* animNames[] = {"Idle", "Walk", "Attack", "StrafeL", "StrafeR", "Combo", "Jump"};
            int prevIdx = (boss.previousAnimation < 7) ? boss.previousAnimation : 0;
            int currIdx = (boss.currentAnimation < 7) ? boss.currentAnimation : 0;
            rdpq_text_printf(NULL, FONT_UNBALANCED, 20, y, "Anim: %d->%d (%s->%s)", 
                boss.previousAnimation, boss.currentAnimation,
                animNames[prevIdx], animNames[currIdx]);
        }
    } else {
        y += listSpacing;
        rdpq_set_prim_color(RGBA32(0x66, 0x66, 0x66, 0xFF)); // Grey for blending inactive
        rdpq_text_printf(NULL, FONT_UNBALANCED, 20, y, "Blending: OFF");
        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    }

    // Draw boss targeting debug visualization
    if (scene_is_boss_active()) {
        T3DVec3 targetPos = {{boss.debugTargetingPos[0], boss.debugTargetingPos[1], boss.debugTargetingPos[2]}};
        // Draw larger orange sphere at targeting position (size 1.5 for better visibility)
        debug_draw_sphere(viewport, &targetPos, 4.0f, DEBUG_COLORS[5]);
        // Also draw a cross for extra visibility
        debug_draw_cross(viewport, &targetPos, 4.0f, DEBUG_COLORS[5]);
    }
}