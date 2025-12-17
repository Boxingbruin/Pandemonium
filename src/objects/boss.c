#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>
#include <t3d/t3ddebug.h>
#include <math.h>
#include <stdlib.h>

#include "boss.h"
#include "dev.h"
#include "character.h"

#include "game_time.h"
#include "general_utility.h"

#include "globals.h"
#include "display_utility.h"
#include "simple_collision_utility.h"
#include "game_math.h"
#include "scene.h"

T3DModel* bossModel;

Boss boss;
static const char* bossDebugSoundName = NULL;
static float bossDebugSoundTimer = 0.0f;
static const float BOSS_SOUND_DISPLAY_DURATION = 2.5f;

static bool bossLowHealthSoundPlayed = false;
static bool bossPowerJumpImpactPlayed = false;
static bool bossSecondSlamImpactPlayed = false;
static bool bossRoarImpactSoundPlayed = false;

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
	bossModel = t3d_model_load("rom:/catherine.t3dm");

	T3DSkeleton* skeleton = malloc(sizeof(T3DSkeleton));
	*skeleton = t3d_skeleton_create(bossModel);

	T3DAnim** animations = NULL;

	const int animationCount = 5;
	const char* animationNames[] = {
		"Idle",
		"Walk",
		"Run",
		"Roll",
		"Attack1"
	};
	const bool animationsLooping[] = {
		true, true, true, false, false
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

	CapsuleCollider emptyCollider = {0};  // All fields zeroed

	Boss newBoss = {
		.pos = {0.0f, 0.0f, 0.0f},
		.rot = {0.0f, 0.0f, 0.0f},
		// Start with a scale appropriate for Catherine model; scene may override after init
		.scale = {0.005f, 0.005f, 0.005f},
		.scrollParams = NULL,
		.skeleton = skeleton,
		.animations = animations,
		.currentAnimation = 0,
		.animationCount = animationCount,
		.capsuleCollider = emptyCollider,
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
		.attackAnimTimer = 0.0f
	};

	// Initialize the newly allocated matrix before assigning to global
	t3d_mat4fp_identity(newBoss.modelMat);
	boss = newBoss;
}

// ==== Update Functions ====

// Boss state machine
enum { 
	ST_IDLE, ST_CHASE, ST_ORBIT, ST_CHARGE, ST_ATTACK, ST_RECOVER,
	ST_POWER_JUMP, ST_COMBO_ATTACK, ST_CHAIN_SWORD, ST_ROAR_STOMP, ST_TRACKING_SLAM
};
static int bossState = ST_IDLE;
static int bossPrevState = ST_IDLE;

static const char* bossTelegraphName = NULL;
static float bossTelegraphTimer = 0.0f;
static const float BOSS_TELEGRAPH_DURATION = 1.5f;
static const char* boss_get_state_name(void) {
	switch (bossState) {
		case ST_IDLE:          return "Idle";
		case ST_CHASE:         return "Chase";
		case ST_ORBIT:         return "Orbit";
		case ST_CHARGE:        return "Charge";
		case ST_ATTACK:        return "Attack";
		case ST_RECOVER:       return "Recover";
		case ST_POWER_JUMP:    return "Power Jump";
		case ST_COMBO_ATTACK:  return "Combo Attack";
		case ST_CHAIN_SWORD:   return "Chain Sword";
		case ST_ROAR_STOMP:    return "Roar Stomp";
		case ST_TRACKING_SLAM: return "Tracking Slam";
		default:               return "Unknown";
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
    
    // Calculate target with slight prediction
    predict_character_position(boss.powerJumpTargetPos, 0.5f);
    boss.powerJumpHeight = 250.0f + ((float)(rand() % 5));
    
    // Sometimes do a double slam in phase 2
    boss.powerJumpDoSecondSlam = (boss.phaseIndex == 2 && (rand() % 100) < 30);
    
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
    if (dist < 50.0f && boss.trackingSlamCooldown <= 0.0f) {
        // Close range - tracking slam
        bossState = ST_TRACKING_SLAM;
        boss.stateTimer = 0.0f;
        boss.trackingSlamCooldown = 8.0f; // 8 second cooldown
        
        // Variable hold time for unpredictability
        boss.trackingSlamHoldTime = 1.0f + ((float)(rand() % 100) / 100.0f) * 1.5f;
        boss.trackingSlamStartTime = boss.stateTimer;
        
        // Calculate target angle to player
        float angle = atan2f(dx, dz);
        boss.trackingSlamTargetAngle = angle;
        
        boss.currentAttackName = "Tracking Slam";
        boss.attackNameDisplayTimer = 2.0f;
    }
    else if (dist > 50.0f && boss.chainSwordCooldown <= 0.0f) {
        // Long range - chain sword
        bossState = ST_CHAIN_SWORD;
        boss.stateTimer = 0.0f;
        boss.chainSwordCooldown = 10.0f; // 10 second cooldown
        boss.swordThrown = false;
        boss.chainSwordSlamHasHit = false;
        
        // Predict where to throw sword
        predict_character_position(boss.chainSwordTargetPos, 0.8f);
        
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
        // All attacks on cooldown, return to orbit
        bossState = ST_ORBIT;
        boss.stateTimer = 0.0f;
    }
}

static void boss_handle_tracking_slam_attack(float dt) {
    // Phase 1: Hold and track (build up)
    if (boss.stateTimer < boss.trackingSlamHoldTime) {
        // Slowly track the player during buildup
        float dx = character.pos[0] - boss.pos[0];
        float dz = character.pos[2] - boss.pos[2];
        float targetAngle = atan2f(dx, dz);
        
        // Smooth angle interpolation
        float angleDiff = targetAngle - boss.rot[1];
        while (angleDiff > T3D_PI) angleDiff -= 2.0f * T3D_PI;
        while (angleDiff < -T3D_PI) angleDiff += 2.0f * T3D_PI;
        
        boss.rot[1] += angleDiff * 2.0f * dt; // Turn toward player
        boss.trackingSlamTargetAngle = boss.rot[1];
        
        // Visual feedback: charge up effect
    }
    // Phase 2: Slam attack
    else if (boss.stateTimer < boss.trackingSlamHoldTime + 0.3f) {
        // Quick forward slam
        float slamSpeed = 400.0f;
        boss.velX = sinf(boss.trackingSlamTargetAngle) * slamSpeed;
        boss.velZ = cosf(boss.trackingSlamTargetAngle) * slamSpeed;
        
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
    }
    // Phase 3: Recovery
    else {
        // Gradually reduce velocity
        boss.velX *= 0.9f;
        boss.velZ *= 0.9f;
        
        if (boss.stateTimer > boss.trackingSlamHoldTime + 1.5f) {
            bossState = ST_ORBIT;
            boss.stateTimer = 0.0f;
        }
    }
}

static void boss_handle_chain_sword_attack(float dt) {
    // Phase 1: Throw sword (0.0 - 0.5s)
    if (!boss.swordThrown && boss.stateTimer < 0.5f) {
        // Aim at predicted position
        float dx = boss.chainSwordTargetPos[0] - boss.pos[0];
        float dz = boss.chainSwordTargetPos[2] - boss.pos[2];
        float targetAngle = atan2f(dx, dz);
        boss.rot[1] = targetAngle;
        
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
        float dx = character.pos[0] - boss.swordProjectilePos[0];
        float dz = character.pos[2] - boss.swordProjectilePos[2];
        float dy = character.pos[1] - boss.swordProjectilePos[1];
        float dist = sqrtf(dx*dx + dy*dy + dz*dz);
        
        if (dist < 3.0f && !boss.currentAttackHasHit) {
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
        float dx = character.pos[0] - boss.swordProjectilePos[0];
        float dz = character.pos[2] - boss.swordProjectilePos[2];
        float dist = sqrtf(dx*dx + dz*dz);
        
        if (dist < 5.0f && !boss.currentAttackHasHit) {
            character_apply_damage(15.0f);
			boss_debug_sound("boss_attack_success");
            boss.currentAttackHasHit = true;
            // printf("[Boss] Sword ground impact!\n");
        }
    }
    
    // Phase 4: Pull boss toward sword (2.0s+)
    if (boss.stateTimer >= 2.0f && boss.stateTimer < 3.0f) {
        // Boss gets pulled toward sword
        float dx = boss.swordProjectilePos[0] - boss.pos[0];
        float dz = boss.swordProjectilePos[2] - boss.pos[2];
        float dist = sqrtf(dx*dx + dz*dz);
        
        if (dist > 2.0f) {
            float pullSpeed = 200.0f;
            boss.velX = (dx / dist) * pullSpeed;
            boss.velZ = (dz / dist) * pullSpeed;
        } else {
            boss.velX *= 0.8f;
            boss.velZ *= 0.8f;
        }
    }
    
    // End attack
    if (boss.stateTimer > 3.5f) {
        bossState = ST_ORBIT;
        boss.stateTimer = 0.0f;
    }
}

static void boss_handle_roar_stomp_attack(float dt) {
    // Phase 1: Roar buildup (0.0 - 1.0s)
    if (boss.stateTimer < 1.0f) {
        // Face player
        float dx = character.pos[0] - boss.pos[0];
        float dz = character.pos[2] - boss.pos[2];
        boss.rot[1] = atan2f(dx, dz);
        
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
            bossState = ST_ORBIT;
            boss.stateTimer = 0.0f;
        }
    }
}

static void boss_handle_power_jump_attack(float dt) {
    const float jumpDuration = 1.2f;
    const float landDuration = 0.3f;
    const float totalDuration = jumpDuration + landDuration;
    
    // Phase 1: Jump arc (0.0 - 1.2s)
    if (boss.stateTimer < jumpDuration) {
        float t = boss.stateTimer / jumpDuration;
        
        // Smooth arc from start to target
        boss.pos[0] = boss.powerJumpStartPos[0] + (boss.powerJumpTargetPos[0] - boss.powerJumpStartPos[0]) * t;
        boss.pos[2] = boss.powerJumpStartPos[2] + (boss.powerJumpTargetPos[2] - boss.powerJumpStartPos[2]) * t;
        
        // Parabolic height
        boss.pos[1] = boss.powerJumpStartPos[1] + boss.powerJumpHeight * sinf(t * T3D_PI);
        
        // Face movement direction
        float dx = boss.powerJumpTargetPos[0] - boss.powerJumpStartPos[0];
        float dz = boss.powerJumpTargetPos[2] - boss.powerJumpStartPos[2];
        if (dx != 0.0f || dz != 0.0f) {
            boss.rot[1] = atan2f(dx, dz);
        }
    }
    // Phase 2: Landing impact (1.2 - 1.5s)
    else if (boss.stateTimer < totalDuration) {
        // Boss hits ground
        boss.pos[1] = boss.powerJumpStartPos[1];
		if (boss.stateTimer >= jumpDuration && !bossPowerJumpImpactPlayed) {
			boss_debug_sound("boss_power_jump_impact");
			bossPowerJumpImpactPlayed = true;
		}
        
        // Check for impact damage
        if (boss.stateTimer >= jumpDuration && boss.stateTimer < jumpDuration + 0.1f && !boss.currentAttackHasHit) {
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
    // Phase 3: Second jump (if enabled)
    else if (boss.powerJumpDoSecondSlam && boss.stateTimer < totalDuration + 1.5f) {
        float secondT = (boss.stateTimer - totalDuration) / 1.5f;
        
        if (secondT < 1.0f) {
            // Predict new position for second slam
            predict_character_position(boss.powerJumpTargetPos, 0.3f);
            
            boss.pos[0] = boss.pos[0] + (boss.powerJumpTargetPos[0] - boss.pos[0]) * secondT;
            boss.pos[2] = boss.pos[2] + (boss.powerJumpTargetPos[2] - boss.pos[2]) * secondT;
            boss.pos[1] = boss.powerJumpStartPos[1] + (boss.powerJumpHeight * 0.7f) * sinf(secondT * T3D_PI);
        }
        
        // Second impact
        if (boss.stateTimer >= totalDuration + 1.5f - 0.1f && boss.stateTimer < totalDuration + 1.5f) {
			if (!bossSecondSlamImpactPlayed) {
				boss_debug_sound("boss_power_jump_impact");
				bossSecondSlamImpactPlayed = true;
			}
            float dx = character.pos[0] - boss.pos[0];
            float dz = character.pos[2] - boss.pos[2];
            float dist = sqrtf(dx*dx + dz*dz);
            
            if (dist < 6.0f) {
                character_apply_damage(25.0f);
				boss_debug_sound("boss_attack_success");
            }
        }
    }
    // End attack
    else {
        boss.powerJumpDoSecondSlam = false;
        bossState = ST_ORBIT;
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
        boss.rot[1] = atan2f(dx, dz);
        
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
        bossState = ST_ORBIT;
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
    // Match player top speed (~200) so the boss can keep up during chase
    const float SPEED_CHASE = boss.phaseIndex == 1 ? 200.0f : 220.0f;
    const float SPEED_ORBIT = boss.phaseIndex == 1 ? 90.0f : 120.0f;
    const float SPEED_CHARGE = boss.phaseIndex == 1 ? 220.0f : 280.0f;
    
    float desiredX = 0.0f, desiredZ = 0.0f;
    float maxSpeed = 0.0f;
    
    switch (bossState) {
        case ST_IDLE:
            // No movement
            maxSpeed = 0.0f;
            break;
            
        case ST_CHASE:
            // Move toward player
            if (dist > 0.0f) {
                desiredX = dx / dist;
                desiredZ = dz / dist;
            }
            maxSpeed = SPEED_CHASE;
            break;
            
        case ST_ORBIT:
            // Circle around player
            if (dist > 0.0f) {
                float orbitAngle = atan2f(-dx, dz) + T3D_PI * 0.5f; // 90 degrees offset
                desiredX = cosf(orbitAngle);
                desiredZ = sinf(orbitAngle);
            }
            maxSpeed = SPEED_ORBIT;
            break;
            
        case ST_CHARGE:
            // Charge toward player
            if (dist > 0.0f) {
                desiredX = dx / dist;
                desiredZ = dz / dist;
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
    
    // Update position
    boss.pos[0] += boss.velX * dt;
    boss.pos[2] += boss.velZ * dt;
    
    // Update facing direction
    float targetAngle;
    if (bossState == ST_ORBIT) {
        targetAngle = atan2f(-dx, dz); // Face player while orbiting
    } else if (bossState >= ST_POWER_JUMP) {
        // Attack states handle their own rotation
        return;
    } else {
        targetAngle = atan2f(-boss.velX, boss.velZ); // Face movement direction
    }
    
    // Smooth rotation
    float currentAngle = boss.rot[1];
    float angleDelta = targetAngle - currentAngle;
    while (angleDelta > T3D_PI) angleDelta -= 2.0f * T3D_PI;
    while (angleDelta < -T3D_PI) angleDelta += 2.0f * T3D_PI;
    
    float maxTurn = boss.turnRate * dt;
    if (angleDelta > maxTurn) angleDelta = maxTurn;
    else if (angleDelta < -maxTurn) angleDelta = -maxTurn;
    
    boss.rot[1] = currentAngle + angleDelta;
}

static void boss_update_animation_system(float dt) {
    if (!boss.skeleton || !boss.animations) return;
    
    // Update attack animation timer
    if (boss.isAttacking) {
        boss.attackAnimTimer += dt;
        const float BOSS_ATTACK_DURATION = 0.9f;
        if (boss.attackAnimTimer >= BOSS_ATTACK_DURATION) {
            boss.isAttacking = false;
            boss.attackAnimTimer = 0.0f;
        }
    }
    
    // Choose animation based on state and movement
    BossAnimState targetAnim = BOSS_ANIM_IDLE;
    
    if (boss.isAttacking) {
        targetAnim = BOSS_ANIM_ATTACK;
    } else if (bossState == ST_CHASE) {
        // Always run while chasing the player
        targetAnim = BOSS_ANIM_RUN;
    } else {
        float speed = sqrtf(boss.velX * boss.velX + boss.velZ * boss.velZ);
        
        if (speed > 150.0f) {
            targetAnim = BOSS_ANIM_RUN;
        } else if (speed > 30.0f) {
            targetAnim = BOSS_ANIM_WALK;
        }
    }
    
    // Switch animations if needed
    if (boss.currentAnimation != targetAnim) {
        // Stop current animation
        if (boss.currentAnimation >= 0 && boss.currentAnimation < boss.animationCount) {
            t3d_anim_set_playing(boss.animations[boss.currentAnimation], false);
        }
        
        // Start new animation
        boss.currentAnimation = targetAnim;
        if (boss.currentAnimation < boss.animationCount) {
            t3d_anim_set_playing(boss.animations[boss.currentAnimation], true);
        }
    }
    
    // Update all animations
    for (int i = 0; i < boss.animationCount; i++) {
        if (boss.animations[i]) {
            t3d_anim_update(boss.animations[i], dt);
        }
    }
    
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

void boss_update(void) 
{
	// Don't update boss AI during cutscenes, reset state for fresh start
	if (!scene_is_boss_active()) {
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
	static bool wasActive = false;
	bool justActivated = scene_is_boss_active() && !wasActive;
	wasActive = scene_is_boss_active();
	
	if (justActivated && bossState == ST_IDLE) {
		bossState = ST_CHASE;
		boss.stateTimer = 0.0f;
	}

	// Basic AI scaffolding: chase + orbit + simple charge trigger
	// Advance the state timer each frame so time-based transitions fire
	boss.stateTimer += dt;

	// Acquire target from character (centered around their focus height)
	T3DVec3 target = {{ character.pos[0], character.pos[1] + 0.5f, character.pos[2] }};
	float dx = target.v[0] - boss.pos[0];
	float dz = target.v[2] - boss.pos[2];
	float dist = sqrtf(dx*dx + dz*dz);

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
			// Transition to orbit when close enough
			if (dist <= COMBAT_RADIUS + 2.0f) {
				bossState = ST_ORBIT;
				boss.stateTimer = 0.0f;
			}
			break;
			
		case ST_ORBIT:
			// Try to attack when cooldown is ready
			if (boss.attackCooldown <= 0.0f) {
				// Random chance to either charge or use special attack
				float r = (float)(rand() % 100) / 100.0f;
				if (r < 0.3f) { // 30% chance for basic charge
					bossState = ST_CHARGE;
					boss.stateTimer = 0.0f;
					boss.attackCooldown = 2.0f;
					printf("[Boss] CHARGE!\n");
				} else {
					// Use sophisticated attack selection
					boss_select_attack();
				}
			}
			break;
			
		case ST_CHARGE:
			// Charge lasts briefly then recover
			if (boss.stateTimer > 1.0f) {
				bossState = ST_RECOVER;
				boss.stateTimer = 0.0f;
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
				bossState = dist > COMBAT_RADIUS ? ST_CHASE : ST_ORBIT;
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
			break;
	}

	// Track state entry for placeholder SFX triggers
	if (stateBefore != bossState) {
		switch (bossState) {
			case ST_POWER_JUMP:
				bossPowerJumpImpactPlayed = false;
				bossSecondSlamImpactPlayed = false;
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
	bossTelegraphTimer = 0.0f;
	bossTelegraphName = NULL;
	bossDebugSoundName = NULL;
	bossDebugSoundTimer = 0.0f;
	bossLowHealthSoundPlayed = false;
	bossPowerJumpImpactPlayed = false;
	bossSecondSlamImpactPlayed = false;
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
	
	// Reset position to origin or keep current (depending on game design)
	// boss.pos values will be set by the scene initialization
	
	// Reset velocity
	boss.velX = 0.0f;
	boss.velZ = 0.0f;
	
	// Reset animation state
	boss.isAttacking = false;
	boss.attackAnimTimer = 0.0f;
	boss.currentAnimation = BOSS_ANIM_IDLE;
}

void boss_draw_ui()
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
}