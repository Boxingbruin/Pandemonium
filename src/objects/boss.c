#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>
#include <t3d/t3ddebug.h>
#include <math.h>
#include <stdlib.h>

#include "boss.h"
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
	printf("[Boss] took %.1f damage (%.1f/%.1f)\n", amount, boss.health, boss.maxHealth);
	t3d_debug_printf(4, 40, "Boss HP: %.0f/%.0f#", boss.health, boss.maxHealth);
	boss.damageFlashTimer = 0.3f;
}

void boss_trigger_attack_animation(void)
{
	if (!boss.animations || boss.animationCount <= BOSS_ANIM_ATTACK) return;
	
	// Stop current animation
	if (boss.currentAnimation < boss.animationCount) {
		t3d_anim_set_playing(boss.animations[boss.currentAnimation], false);
	}
	
	// Start attack animation
	boss.currentAnimation = BOSS_ANIM_ATTACK;
	boss.isAttacking = true;
	boss.attackAnimTimer = 0.0f;
	t3d_anim_set_playing(boss.animations[BOSS_ANIM_ATTACK], true);
	t3d_anim_set_time(boss.animations[BOSS_ANIM_ATTACK], 0.0f); // Reset to start
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
enum { ST_IDLE, ST_CHASE, ST_ORBIT, ST_CHARGE, ST_ATTACK, ST_RECOVER };
static int bossState = ST_IDLE;

void boss_update(void) 
{
	// Don't update boss AI during cutscenes, reset state for fresh start
	if (!scene_is_boss_active()) {
		bossState = ST_IDLE;
		boss.stateTimer = 0.0f;
		return;
	}

	// If boss was just activated (scene_is_boss_active became true), start chasing immediately
	static bool wasActive = false;
	bool justActivated = scene_is_boss_active() && !wasActive;
	wasActive = scene_is_boss_active();
	
	if (justActivated && bossState == ST_IDLE) {
		bossState = ST_CHASE;
		boss.stateTimer = 0.0f;
	}

	// Basic AI scaffolding: chase + orbit + simple charge trigger
	float dt = deltaTime;

	// Acquire target from character (centered around their focus height)
	T3DVec3 target = {{ character.pos[0], character.pos[1] + 0.5f, character.pos[2] }};
	float dx = target.v[0] - boss.pos[0];
	float dz = target.v[2] - boss.pos[2];
	float dist = sqrtf(dx*dx + dz*dz);

	// Phase switch at 50% HP
	if (boss.phaseIndex == 1 && boss.health <= boss.maxHealth * 0.5f) {
		boss.phaseIndex = 2;
		printf("[Boss] Phase 2!\n");
		t3d_debug_printf(4, 4, "Phase 2!#");
	}

	// Cooldown tick
	if (boss.attackCooldown > 0.0f) boss.attackCooldown -= dt;
	boss.stateTimer += dt;

	// Movement intent: Option A
	const float ACCEL = 7.0f;
	const float FRICTION = 10.0f;
	const float SPEED_CHASE = boss.phaseIndex == 1 ? 140.0f : 180.0f;
	const float SPEED_ORBIT = boss.phaseIndex == 1 ? 90.0f : 120.0f;
	const float SPEED_CHARGE = boss.phaseIndex == 1 ? 220.0f : 280.0f;
	const float COMBAT_RADIUS = boss.orbitRadius;

	// Enter chase when boss is activated and player in range, or immediately after cutscene
	if (bossState == ST_IDLE) {
		// Start chasing immediately when boss is activated, or when player gets close during idle
		if (dist < 40.0f) { 
			bossState = ST_CHASE; 
			boss.stateTimer = 0.0f; 
		}
	}
	// Transition chase→orbit inside combat radius
	if (bossState == ST_CHASE && dist <= COMBAT_RADIUS + 2.0f) { bossState = ST_ORBIT; boss.stateTimer = 0.0f; }
	// From orbit, sometimes charge or attack
	if (bossState == ST_ORBIT && boss.attackCooldown <= 0.0f) {
		// Prefer charge in phase 2 more often
		float r = (float)(rand() % 100) / 100.0f;
		if (r < (boss.phaseIndex == 2 ? 0.5f : 0.25f)) {
			bossState = ST_CHARGE; boss.stateTimer = 0.0f; boss.attackCooldown = 2.0f;
			printf("[Boss] CHARGE!\n");
			t3d_debug_printf(4, 16, "Boss: CHARGE!#");
		} else {
			bossState = ST_ATTACK; boss.stateTimer = 0.0f; boss.attackCooldown = 1.2f;
			// Trigger attack animation
			boss_trigger_attack_animation();
			printf("[Boss] ATTACK!\n");
			t3d_debug_printf(4, 28, "Boss: ATTACK!#");
		}
	}
	// Charge lasts briefly then recover
	if (bossState == ST_CHARGE && boss.stateTimer > 0.9f) { bossState = ST_RECOVER; boss.stateTimer = 0.0f; }
	if (bossState == ST_ATTACK && boss.stateTimer > 0.7f) { bossState = ST_RECOVER; boss.stateTimer = 0.0f; }
	if (bossState == ST_RECOVER && boss.stateTimer > 0.6f) { bossState = dist > COMBAT_RADIUS ? ST_CHASE : ST_ORBIT; boss.stateTimer = 0.0f; }

	// Melee hit window: during attack mid-frames, check capsule overlap and log
	if (bossState == ST_ATTACK && boss.stateTimer > 0.2f && boss.stateTimer < 0.5f) {
		SCU_CapsuleFixed bc = boss_make_capsule_fixed();
		SCU_CapsuleFixed cc = character_make_capsule_fixed();
		if (scu_fixed_capsule_vs_capsule(&bc, &cc)) {
			// Future: apply damage to player
			printf("[Boss] Landed melee hit!\n");
			t3d_debug_printf(4, 52, "Boss hit!#");
		}
	}

	// Desired velocity per state
	float desiredX = 0.0f, desiredZ = 0.0f, maxSpeed = SPEED_CHASE;
	if (bossState == ST_CHASE) {
		float len = fmaxf(1e-5f, sqrtf(dx*dx + dz*dz));
		desiredX = dx / len; desiredZ = dz / len; maxSpeed = SPEED_CHASE;
	} else if (bossState == ST_ORBIT) {
		// Forward to target, right perpendicular; bias to maintain radius
		float len = fmaxf(1e-5f, sqrtf(dx*dx + dz*dz));
		float fwdX = dx / len, fwdZ = dz / len;
		float rightX = -fwdZ, rightZ = fwdX;
		// Circle around with slight inward push if too far, outward if too close
		float radiusError = dist - COMBAT_RADIUS;
		desiredX = rightX * 0.8f + fwdX * (-radiusError * 0.25f);
		desiredZ = rightZ * 0.8f + fwdZ * (-radiusError * 0.25f);
		maxSpeed = SPEED_ORBIT;
	} else if (bossState == ST_CHARGE) {
		float len = fmaxf(1e-5f, sqrtf(dx*dx + dz*dz));
		desiredX = dx / len; desiredZ = dz / len; maxSpeed = SPEED_CHARGE;
	} else {
		// Idle/Recover: bleed velocity
		desiredX = 0.0f; desiredZ = 0.0f; maxSpeed = 0.0f;
	}

	// Acceleration toward desired
	boss.velX += (desiredX * maxSpeed - boss.velX) * ACCEL * dt;
	boss.velZ += (desiredZ * maxSpeed - boss.velZ) * ACCEL * dt;

	// Friction (reduced during attack to preserve momentum)
	float frictionScale = (bossState == ST_ATTACK) ? 0.3f : 1.0f;
	float k = FRICTION * frictionScale;
	boss.velX *= expf(-k * dt);
	boss.velZ *= expf(-k * dt);

	// Position integrate
	boss.pos[0] += boss.velX * dt;
	boss.pos[2] += boss.velZ * dt;

	// Facing: toward movement for chase/charge; toward player while orbiting
	float targetAngle;
	if (bossState == ST_ORBIT) targetAngle = atan2f(-dx, dz);
	else targetAngle = atan2f(-boss.velX, boss.velZ);
	float currentAngle = boss.rot[1];
	// Clamp turn rate
	float angleDelta = targetAngle - currentAngle;
	while (angleDelta > T3D_PI) angleDelta -= 2.0f * T3D_PI;
	while (angleDelta < -T3D_PI) angleDelta += 2.0f * T3D_PI;
	float maxTurn = boss.turnRate * dt;
	if (angleDelta > maxTurn) angleDelta = maxTurn;
	else if (angleDelta < -maxTurn) angleDelta = -maxTurn;
	boss.rot[1] = currentAngle + angleDelta;

	// TODO: camera interplay hooks (zoom/shake on charge, framing)

	// Update skeleton and matrix
	if (boss.skeleton) {
		// Update attack animation timer
		if (boss.isAttacking) {
			boss.attackAnimTimer += dt;
			// Attack animation duration (similar to character attack duration)
			const float BOSS_ATTACK_DURATION = 0.9f;
			if (boss.attackAnimTimer >= BOSS_ATTACK_DURATION) {
				boss.isAttacking = false;
				boss.attackAnimTimer = 0.0f;
			}
		}
		
		// Update boss animation based on movement (only if not attacking)
		if (!boss.isAttacking) {
			float speed = sqrtf(boss.velX * boss.velX + boss.velZ * boss.velZ);
			BossAnimState targetAnim = BOSS_ANIM_IDLE;
			
			if (speed > 150.0f) {
				targetAnim = BOSS_ANIM_RUN;
			} else if (speed > 30.0f) {
				targetAnim = BOSS_ANIM_WALK;
			}
			
			// Switch animations if needed
			if (boss.currentAnimation != targetAnim) {
				// Stop current animation
				if (boss.animations && boss.currentAnimation < boss.animationCount) {
					t3d_anim_set_playing(boss.animations[boss.currentAnimation], false);
				}
				
				// Start new animation
				boss.currentAnimation = targetAnim;
				if (boss.animations && boss.currentAnimation < boss.animationCount) {
					t3d_anim_set_playing(boss.animations[boss.currentAnimation], true);
				}
			}
		}
		
		// Advance any playing animation
		if (boss.animations && boss.animationCount > 0) {
			for (int i = 0; i < boss.animationCount; i++) {
				if (boss.animations[i]) t3d_anim_update(boss.animations[i], dt);
			}
		}
		t3d_skeleton_update(boss.skeleton);
	}
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
	// Draw simple top health bar
	float ratio = boss.maxHealth > 0.0f ? fmaxf(0.0f, fminf(1.0f, boss.health / boss.maxHealth)) : 0.0f;
	float flash = 0.0f;
	if (boss.damageFlashTimer > 0.0f) {
		flash = fminf(1.0f, boss.damageFlashTimer / 0.3f);
		boss.damageFlashTimer -= deltaTime;
		if (boss.damageFlashTimer < 0.0f) boss.damageFlashTimer = 0.0f;
	}
	draw_boss_health_bar(boss.name, ratio, flash);
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