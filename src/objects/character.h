#ifndef CHARACTER_H
#define CHARACTER_H

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>

#include "general_utility.h"

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
    T3DAnim **animations;
    T3DAnim **animationsBlend;
    int currentAnimation;
    int animationCount;

    bool hasCollision;
    CapsuleCollider capsuleCollider;

    T3DMat4FP *modelMat;
    rspq_block_t *dpl;

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

void character_draw();
void character_draw_ui();
void character_update();
void character_reset_button_state(void);
void character_delete();

void character_free(void);

// Get character velocity for prediction (used by boss AI)
void character_get_velocity(float* outVelX, float* outVelZ);

// External API to apply damage to the character
void character_apply_damage(float amount);

#endif