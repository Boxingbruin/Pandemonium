#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>

#include "character.h"
#include "camera_controller.h"

#include "joypad_utility.h"
#include "game_time.h"
#include "globals.h"    

#include "debug_draw.h" // DEV

T3DModel* characterModel;
Character character;  

void character_init(void)
{
    characterModel = t3d_model_load("rom:/knight.t3dm");

    T3DSkeleton* skeleton = NULL;
    T3DAnim** animations = NULL;

	const int animationCount = 0; // Set this to the anim count
	const char** animationNames = {};
	const bool** animationsLooping = {};

    if (animationCount > 0)
    {
        skeleton = malloc(sizeof(T3DSkeleton));
        *skeleton = t3d_skeleton_create(characterModel);

        animations = malloc(animationCount * sizeof(T3DAnim*));
        for (int i = 0; i < animationCount; i++)
        {
            animations[i] = malloc(sizeof(T3DAnim));
            *animations[i] = t3d_anim_create(characterModel, animationNames[i]);
            t3d_anim_set_looping(animations[i], animationsLooping[i]);
            t3d_anim_set_playing(animations[i], false);
            t3d_anim_attach(animations[i], skeleton);
        }
    }

    rspq_block_begin();
    if (skeleton)
        t3d_model_draw_skinned(characterModel, skeleton);
    else
        t3d_model_draw(characterModel);
    rspq_block_t* dpl = rspq_block_end();

    CapsuleCollider collider = {0};  // All fields zeroed for now

    Character newCharacter = {
        .pos = {0.0f, 0.0f, 0.0f},
        .rot = {0.0f, 0.0f, 0.0f},
        .scale = {MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        .scrollParams = NULL,
        .skeleton = skeleton,
        .animations = animations,
        .currentAnimation = 0,
        .animationCount = animationCount,
        .capsuleCollider = collider,
        .modelMat = malloc_uncached(sizeof(T3DMat4FP)),
        .dpl = dpl,
        .visible = true
    };

    t3d_mat4fp_identity(newCharacter.modelMat);

    //Actor create_actor_object(int actorIndex, const T3DModel *model, bool visible, T3DSkeleton *skeleton, T3DAnim **animations, int animationCount);
    character = newCharacter;
}

// ==== Update Functions ====

void character_update(void) 
{
  	t3d_mat4fp_from_srt_euler(character.modelMat, character.scale, character.rot, character.pos);
}

void character_update_position(void) 
{
  	t3d_mat4fp_set_pos(character.modelMat, character.pos);
}

// ==== Drawing Functions ====

void character_draw(void) 
{
	t3d_matrix_set(character.modelMat, true);
	rspq_block_run(character.dpl);
}

void character_delete(void)
{
    rspq_wait();

    t3d_model_free(characterModel);

    free_if_not_null(character.scrollParams);

    if (character.skeleton) 
    {
        t3d_skeleton_destroy(character.skeleton);
        free(character.skeleton);
        character.skeleton = NULL;
    }

    if (character.animations) 
    {
        for (int i = 0; i < character.animationCount; i++) 
        {
        if (character.animations[i]) 
        {
            t3d_anim_destroy(character.animations[i]);
            free(character.animations[i]);
        }
        }
        free(character.animations);
        character.animations = NULL;
    }

    if (character.modelMat) 
    {
        rspq_wait();
        free_uncached(character.modelMat);
        character.modelMat = NULL;
    }

    if (character.dpl) 
    {
        rspq_wait();
        rspq_block_free(character.dpl);
        character.dpl = NULL;
    }
}