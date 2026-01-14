#ifndef BOSS_H
#define BOSS_H

#include <stdbool.h>
#include <libdragon.h>
#include "general_utility.h"
#include "character.h" // For CapsuleCollider definition

// Shared enums that AI and Anim agree on - must be defined before Boss struct
typedef enum {
    BOSS_STATE_INTRO,
    BOSS_STATE_NEUTRAL,
    BOSS_STATE_CHASE,
    BOSS_STATE_STRAFE,
    BOSS_STATE_RECOVER,
    BOSS_STATE_STAGGER,
    BOSS_STATE_DEAD,
    // Attack-specific states
    BOSS_STATE_CHARGE,
    BOSS_STATE_POWER_JUMP,
    BOSS_STATE_COMBO_ATTACK,
    BOSS_STATE_COMBO_STARTER,
    BOSS_STATE_ROAR_STOMP,
    BOSS_STATE_TRACKING_SLAM,
    BOSS_STATE_FLIP_ATTACK
} BossState;

typedef enum {
    BOSS_ANIM_IDLE = 0,
    BOSS_ANIM_WALK = 1,
    BOSS_ANIM_ATTACK = 2,
    BOSS_ANIM_STRAFE_LEFT = 3,
    BOSS_ANIM_STRAFE_RIGHT = 4,
    BOSS_ANIM_COMBO_ATTACK = 5,
    BOSS_ANIM_JUMP_FORWARD = 6,
    BOSS_ANIM_COMBO_LUNGE = 7,
    BOSS_ANIM_COMBO_STARTER = 8,
    BOSS_ANIM_FLIP_ATTACK = 9,
    BOSS_ANIM_KNEEL = 10,
    BOSS_ANIM_KNEEL_CUTSCENE = 11,
    BOSS_ANIM_COUNT = 12
} BossAnimState;

typedef enum {
    BOSS_ATTACK_CHARGE,
    BOSS_ATTACK_POWER_JUMP,
    BOSS_ATTACK_COMBO,
    BOSS_ATTACK_COMBO_STARTER,
    BOSS_ATTACK_ROAR_STOMP,
    BOSS_ATTACK_TRACKING_SLAM,
    BOSS_ATTACK_FLIP_ATTACK,
    BOSS_ATTACK_COUNT
} BossAttackId;

typedef enum {
    BOSS_ANIM_PRIORITY_LOW = 0,
    BOSS_ANIM_PRIORITY_NORMAL = 1,
    BOSS_ANIM_PRIORITY_HIGH = 2,
    BOSS_ANIM_PRIORITY_CRITICAL = 3  // Death, stagger - always interrupts
} BossAnimPriority;

// Boss structure - modules access fields directly but respect ownership:
// - Animation fields (skeleton, animations, blend state): owned by boss_anim.c
// - AI fields (state, timers, cooldowns): owned by boss_ai.c  
// - Render fields (modelMat, dpl): owned by boss_render.c
typedef struct Boss {
    // Transform
    float pos[3];
    float rot[3];
    float scale[3];
    
    // Model and rendering (owned by boss_render.c)
    void *model;  // T3DModel* (avoiding header dependency)
    void *modelMat;  // T3DMat4FP* 
    void *dpl;  // rspq_block_t*
    bool visible;
    
    // Animation system (owned by boss_anim.c - ONLY boss_anim.c touches these)
    void *skeleton;  // T3DSkeleton*
    void *skeletonBlend;  // T3DSkeleton*
    void **animations;  // T3DAnim**
    int animationCount;

    // Animation state (owned by boss_anim.c)
    int currentAnimation;
    int previousAnimation;
    float blendFactor;
    float blendDuration;
    float blendTimer;
    bool isBlending;
    BossAnimState currentAnimState;
    BossAnimPriority currentPriority;
    int lockFrames;
    
    // Collision
    CapsuleCollider capsuleCollider;
    
    // Hand attack collider (attached to Hand-Right bone)
    int handRightBoneIndex;
    CapsuleCollider handAttackCollider;
    float handAttackColliderWorldPos[3];
    bool handAttackColliderActive;
    
    // Sword model (attached to Hand-Right bone)
    void* swordModel;  // T3DModel*
    void* swordDpl;  // rspq_block_t*
    void* swordMatFP;  // T3DMat4FP*
    
    // Combat stats
    const char *name;
    float maxHealth;
    float health;
    int phaseIndex;
    
    // Movement
    float velX;
    float velZ;
    float currentSpeed;
    float turnRate;
    float orbitRadius;
    
    // AI state (owned by boss_ai.c)
    BossState state;
    float stateTimer;
    float attackCooldown;
    
    // Attack-specific cooldowns
    float powerJumpCooldown;
    float comboCooldown;
    float comboStarterCooldown;
    float roarStompCooldown;
    float trackingSlamCooldown;
    float flipAttackCooldown;
    
    // Attack state tracking
    bool isAttacking;
    float attackAnimTimer;
    bool currentAttackHasHit;
    BossAttackId currentAttackId;
    
    // Combo state
    int comboStep;
    bool comboInterrupted;
    float comboVulnerableTimer;
    
    // Combo starter state
    float swordProjectilePos[3];
    bool swordThrown;
    bool comboStarterSlamHasHit;
    float comboStarterTargetPos[3];
    bool comboStarterCompleted; // Flag to track if combo starter has finished
    
    // Tracking slam state
    float trackingSlamTargetAngle;
    
    // Power jump state
    float powerJumpStartPos[3];
    float powerJumpTargetPos[3];
    float powerJumpHeight;
    
    // Flip attack state
    float flipAttackStartPos[3];
    float flipAttackTargetPos[3];
    float flipAttackHeight;
    
    // Targeting system
    float debugTargetingPos[3];
    bool targetingLocked;
    float lockedTargetingPos[3];
    float targetingUpdateTimer;
    float lastPlayerPos[3];
    float lastPlayerVel[2];
    
    // Visual feedback
    float damageFlashTimer;
    float attackNameDisplayTimer;
    const char* currentAttackName;
    float hitMessageTimer;
    float animationTransitionTimer;
    
    // Pending requests (set by external triggers, read by AI)
    unsigned int pendingRequests;
} Boss;

// Intent/command struct - what AI wants to happen this frame
typedef struct {
    // Animation request
    bool anim_req;
    BossAnimState anim;
    bool force_restart;
    float start_time;
    BossAnimPriority priority;
    
    // Attack request
    bool attack_req;
    BossAttackId attack;
} BossIntent;

// Public API - only what other game code needs
Boss* boss_spawn(void);
void boss_update(Boss* boss);
void boss_draw(Boss* boss);
void boss_draw_ui(Boss* boss, void* viewport);  // T3DViewport* but avoiding header dependency

// Basic getters
float boss_get_hp(const Boss* boss);
float boss_get_max_hp(const Boss* boss);
int boss_get_phase(const Boss* boss);
BossState boss_get_state(const Boss* boss);

// External API to apply damage to the boss
void boss_apply_damage(Boss* boss, float amount);

// Initialization and cleanup
void boss_init(Boss* boss);
void boss_reset(Boss* boss);
void boss_free(Boss* boss);

// Get the global boss instance (for other modules that need it)
Boss* boss_get_instance(void);

#endif // BOSS_H

