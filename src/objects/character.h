#ifndef CHARACTER_H
#define CHARACTER_H

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>

#include "general_utility.h"

// Animation states - these correspond to the animation indices
typedef enum {
    ANIM_IDLE,
    ANIM_IDLE_TITLE,
    ANIM_WALK,
    ANIM_RUN,
    ANIM_RUN_END,
    ANIM_ROLL,
    ANIM_KNOCKDOWN,
    ANIM_STRAFE_WALK_LEFT,
    ANIM_STRAFE_WALK_RIGHT,
    ANIM_STRAFE_RUN_LEFT,
    ANIM_STRAFE_RUN_RIGHT,
    ANIM_ATTACK1,
    ANIM_ATTACK1_END,
    ANIM_ATTACK2,
    ANIM_ATTACK2_END,
    ANIM_ATTACK3,
    ANIM_ATTACK3_END,
    ANIM_ATTACK4,
    ANIM_FOG_OF_WAR,
    ANIM_ATTACK_CHARGED,
    ANIM_WALK_BACK,
    ANIM_RUN_BACK,
    ANIM_COUNT,
} CharacterAnimState;

// Character state for action mechanics
typedef enum {
    CHAR_STATE_NORMAL,
    CHAR_STATE_ROLLING,
    CHAR_STATE_ATTACKING,
    CHAR_STATE_ATTACKING_STRONG,
    CHAR_STATE_JUMPING,
    CHAR_STATE_TITLE_IDLE,
    CHAR_STATE_FOG_WALK,
    CHAR_STATE_KNOCKDOWN
} CharacterState;

typedef struct {
    T3DVec3 localCapA;
    T3DVec3 localCapB;
    float radius;
} CapsuleCollider;

// Structure for holding character data
typedef struct {
    float pos[3];
    float rot[3];
    float scale[3];

    ScrollParams *scrollParams;
    T3DSkeleton *skeleton;
    T3DSkeleton *skeletonBlend;
    T3DSkeleton *skeletonLocomotion;
    T3DAnim **animations;
    T3DAnim **animationsBlend;
    int currentAnimation;
    int previousAnimation;
    int animationCount;

    // Animation blending state
    float blendFactor;
    float blendDuration;
    float blendTimer;
    bool isBlending;

    bool hasCollision;
    CapsuleCollider capsuleCollider;

    // Matrices
    T3DMat4FP *modelMat;     // character transform
    T3DMat4FP *shadowMat;    // ground-locked shadow transform

    // Display lists
    rspq_block_t *dpl_model;   // skinned character
    rspq_block_t *dpl_shadow;  // shadow blob

    bool visible;

    // Character health and combat stats
    float maxHealth;
    float health;

    // Visual feedback
    float damageFlashTimer;

    // Hit tracking to prevent multiple damage applications per attack
    bool currentAttackHasHit;    // Track if current attack has already hit
} Character;

extern Character character;

void character_init(void);
void character_reset(void);

void character_update_position(void);
void character_update_camera(void);

void character_draw(void);
void character_draw_shadow(void);
void character_draw_ui(void);
void character_update(void);
void character_reset_button_state(void);
void character_delete(void);

void character_free(void);

// Get character velocity for prediction (used by boss AI)
void character_get_velocity(float* outVelX, float* outVelZ);

// External API to apply damage to the character
void character_apply_damage(float amount);

#endif
