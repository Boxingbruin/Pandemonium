#ifndef BOSS_H
#define BOSS_H

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>

#include "general_utility.h"
#include "character.h" // For CapsuleCollider definition

// Animation states for boss behavior
typedef enum {
    BOSS_ANIM_IDLE = 0,
    BOSS_ANIM_WALK = 1,
    BOSS_ANIM_ATTACK = 2,
    BOSS_ANIM_STRAFE_LEFT = 3,
    BOSS_ANIM_STRAFE_RIGHT = 4,
    BOSS_ANIM_COMBO_ATTACK = 5,
    BOSS_ANIM_JUMP_FORWARD = 6,
    BOSS_ANIM_COUNT = 7
} BossAnimState;

// Structure for holding boss data
typedef struct {
    T3DModel *model;
    float pos[3];
    float rot[3];
    float scale[3];

    ScrollParams *scrollParams;
    T3DSkeleton *skeleton;
    T3DSkeleton *skeletonBlend;
    T3DAnim **animations;
    int currentAnimation;
    int previousAnimation;
    int animationCount;
    
    // Animation blending state
    float blendFactor;
    float blendDuration;
    float blendTimer;
    bool isBlending;

    CapsuleCollider capsuleCollider;

    T3DMat4FP *modelMat;
    rspq_block_t *dpl;

    bool visible;

    // Boss combat stats and AI state
    const char *name;
    float maxHealth;
    float health;
    int phaseIndex; // 1 or 2

    // Movement and AI
    float velX;
    float velZ;
    float currentSpeed;
    float turnRate;      // radians per second
    float orbitRadius;   // meters in world units

    // Timers
    float stateTimer;
    float attackCooldown;

    // Visual feedback
    float damageFlashTimer;
    
    // Animation state tracking
    bool isAttacking;
    float attackAnimTimer;
    
    // Attack name display
    float attackNameDisplayTimer;
    const char* currentAttackName;
    float hitMessageTimer;       // Debug: show when player hits boss
    
    // Attack-specific cooldowns
    float powerJumpCooldown;
    float comboCooldown;
    float chainSwordCooldown;
    float roarStompCooldown;
    float trackingSlamCooldown;
    
    // Combo state tracking
    int comboStep;  // 0, 1, 2 (which attack in combo)
    bool comboInterrupted;
    float comboVulnerableTimer;
    
    // Chain sword state
    float swordProjectilePos[3];
    bool swordThrown;
    bool chainSwordSlamHasHit;
    float chainSwordTargetPos[3];  // Where sword will land
    
    // Tracking slam state
    float trackingSlamTargetAngle;
    
    // Power jump state
    float powerJumpStartPos[3];
    float powerJumpTargetPos[3];
    float powerJumpHeight;

    // Debug targeting visualization
    float debugTargetingPos[3];  // Current position the boss is targeting/aiming at

    // Advanced targeting system
    bool targetingLocked;        // Whether targeting position is locked for an attack
    float lockedTargetingPos[3]; // Position locked when attack begins
    float targetingUpdateTimer;  // Timer for updating targeting during non-attack states
    float lastPlayerPos[3];      // Last known player position for movement prediction
    float lastPlayerVel[2];      // Last known player velocity for anticipation

    // Hit tracking to prevent multiple damage applications per attack
    bool currentAttackHasHit;    // Track if current attack has already hit

    // Animation transition timer
    float animationTransitionTimer;  // Timer for smooth animation transitions
} Boss;

extern Boss boss;

void boss_init(void);
void boss_reset(void);

void boss_update_position(void);
void boss_update_cutscene(void);

void boss_draw();
void boss_draw_ui(T3DViewport *viewport);
void boss_update();
void boss_delete();

void boss_free(void);

// External API to apply damage to the boss
void boss_apply_damage(float amount);

// Animation control functions
void boss_trigger_attack_animation(void);

#endif