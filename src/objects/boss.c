#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>

#include "boss.h"

#include "game_time.h"
#include "general_utility.h"

#include "globals.h"

T3DModel* bossModel;

Boss boss;

void boss_init(void) 
{
	bossModel = t3d_model_load("rom:/boss.t3dm");

	T3DSkeleton* skeleton = NULL;
	T3DAnim** animations = NULL;

	const int animationCount = 0; // Set this to the anim count
	const char** animationNames = {};
	const bool** animationsLooping = {};

	if (animationCount > 0)
	{
		skeleton = malloc(sizeof(T3DSkeleton));
		*skeleton = t3d_skeleton_create(bossModel);

		animations = malloc(animationCount * sizeof(T3DAnim*));
		for (int i = 0; i < animationCount; i++)
		{
			animations[i] = malloc(sizeof(T3DAnim));
			*animations[i] = t3d_anim_create(bossModel, animationNames[i]);
			t3d_anim_set_looping(animations[i], animationsLooping[i]);
			t3d_anim_set_playing(animations[i], false);
			t3d_anim_attach(animations[i], skeleton);
		}
	}

	rspq_block_begin();
	if (skeleton)
		t3d_model_draw_skinned(bossModel, skeleton);
	else
		t3d_model_draw(bossModel);
	rspq_block_t* dpl = rspq_block_end();

	CapsuleCollider emptyCollider = {0};  // All fields zeroed

	Boss newBoss = {
		.pos = {0.0f, 0.0f, 0.0f},
		.rot = {0.0f, 0.0f, 0.0f},
		.scale = {MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
		.scrollParams = NULL,
		.skeleton = skeleton,
		.animations = animations,
		.currentAnimation = 0,
		.animationCount = animationCount,
		.capsuleCollider = emptyCollider,
		.modelMat = malloc_uncached(sizeof(T3DMat4FP)),
		.dpl = dpl,
		.visible = true
	};

	t3d_mat4fp_identity(boss.modelMat);
	boss = newBoss;
}

// ==== Update Functions ====

void boss_update(void) 
{
  	t3d_mat4fp_from_srt_euler(boss.modelMat, boss.scale, boss.rot, boss.pos);
}

void boss_update_position(void) 
{
  	t3d_mat4fp_set_pos(boss.modelMat, boss.pos);
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