#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>
#include <t3d/t3dmath.h>
#include <math.h>

#include "character.h"
#include "camera_controller.h"

#include "joypad_utility.h"
#include "game_time.h"
#include "game_math.h"
#include "globals.h"    

#include "debug_draw.h"
#include "game/bosses/boss.h"
#include "scene.h"
#include "simple_collision_utility.h"
#include "collision_mesh.h"
#include "game_math.h"
#include "display_utility.h"
#include "controllers/audio_controller.h"
#include "scenes/scene_sfx.h"
#include "utilities/general_utility.h"

/*
 Character Controller
 - Responsibilities: input handling, action state (roll/attack/jump), movement + rotation,
     animation selection, and third-person camera follow.
 - Conventions: model forward is +Z at yaw 0, world up is +Y, camera yaw uses `cameraAngleX`.
*/

T3DModel* characterModel;
T3DModel* characterShadowModel;
Character character;

// Sword collider config (player)
static int   characterSwordBoneIndex = -1;     // cached bone index for sword/hand
static const float SWORD_LENGTH = 640.0f;      // local-space length of sword capsule segment
static const float SWORD_COLLIDER_RADIUS = 5.0f; // sword collider radius in world units

// Shadow tuning
static const float SHADOW_GROUND_Y = 4.0f;  // Match roomY floor level
static const float SHADOW_BASE_ALPHA = 120.0f; // alpha when on the ground
static const float SHADOW_SHRINK_AMOUNT = 0.45f; // 0=no shrink, 0.45 -> 55% size at peak

static CharacterState characterState = CHAR_STATE_NORMAL;
static float actionTimer = 0.0f;

// Render yaw offset to align Blender +X forward to world +Z forward
static const float MODEL_YAW_OFFSET = T3D_PI * -0.5f; // -90 degrees

// Movement state
static float movementVelocityX = 0.0f;
static float movementVelocityZ = 0.0f;
static float currentSpeed = 0.0f; // Track current movement speed for animation

static const float MOVEMENT_ACCELERATION = 7.0f;
static const float MOVEMENT_FRICTION = 12.0f;
static const float MAX_MOVEMENT_SPEED = 60.0f;  // Slightly slower movement
static const float SPEED_BUILDUP_RATE = 1.5f;    // How fast speed builds up to run
static const float SPEED_DECAY_RATE = 4.0f;      // How fast speed decays when slowing  

static const float ROLL_DURATION = 0.9f;
static const float ROLL_ANIM_SPEED = 1.0f;
static const float ATTACK_DURATION = 0.9f;
static const float STRONG_ATTACK_DURATION = 1.2f;
static const float STRONG_ATTACK_HOLD_THRESHOLD = 0.4f;
static const float STRONG_ATTACK_DAMAGE = 20.0f;
static const float STRONG_ATTACK_HIT_START = 0.35f;
static const float STRONG_ATTACK_HIT_END = 0.9f;
static const float JUMP_DURATION = 0.75f; // unused (jump removed)
static const float JUMP_HEIGHT = 40.0f;   // retained for shadow math
static const float ROLL_SPEED = MAX_MOVEMENT_SPEED; // Dont think we should speed boost roll, rolling becomes too stronk
static const float ROLL_STEER_ACCELERATION = 14.0f; // steering responsiveness during roll
static const float ROLL_FRICTION_SCALE = 0.6f;      // decay when no input during roll
static const float STRONG_ATTACK_FRICTION_SCALE = 0.25f;

// Combo attack tuning - Souls-like windows
// Durations are now set from actual clip lengths in code
static float ATTACK1_DURATION = 0.9f;  // Will be overwritten with clip length
static float ATTACK2_DURATION = 0.9f;
static float ATTACK3_DURATION = 0.9f;
static float ATTACK4_DURATION = 1.0f;
static float ATTACK_END_DURATION = 0.6f;

// Souls-like attack windows
static const float ATTACK_QUEUE_OPEN = 0.45f;      // Can buffer input starting here
static const float ATTACK_QUEUE_CLOSE = 0.90f;     // Buffer window closes
static const float ATTACK_TRANSITION_TIME = 0.92f;  // Actually switch to next attack here
static const float ATTACK_CROSSFADE_DURATION = 0.08f; // Short crossfade between attacks

static const float ATTACK_FORWARD_IMPULSE = 35.0f;
static const float KNOCKDOWN_DURATION = 0.8f;
static const float KNOCKDOWN_BACK_IMPULSE = 25.0f;

// Input state tracking for edge detection
static bool lastBPressed = false;
static bool lastAPressed = false;
static bool leftTriggerHeld = false;
static float leftTriggerHoldTime = 0.0f;

// Input and tuning constants
static const float STICK_MAX = 80.0f;
static const float INPUT_DEADZONE = 0.12f;
// Strafe activation tuning (to avoid accidental strafing on resting sticks)
static const float STRAFE_LATERAL_AXIS_DEADZONE = 0.08f;   // min |stick.x| to consider sideways intent
static const float STRAFE_ACTIVATION_RATIO     = 0.22f;    // ratio threshold to turn strafe on
static const float STRAFE_DEACTIVATION_RATIO   = 0.12f;    // ratio threshold to turn strafe off (hysteresis)
static const float TURN_RATE = 8.0f;           // rad/sec turn rate cap
static const float IDLE_THRESHOLD = 0.001f;
static const float WALK_THRESHOLD = 0.03f;
static const float RUN_THRESHOLD = 0.7f;
static const float BLEND_MARGIN = 0.2f;        // walk<->run blend zone width
static const float ATTACK_FRICTION_SCALE = 0.3f; // Preserve momentum during attack

static bool walkThroughFog = false;
// Attack combo state
static int attackComboIndex = 0; // 0=none, 1..4
static bool attackQueued = false; // queued next stage of combo
static bool attackEnding = false; // playing AttackNEnd
static float currentActionDuration = 1.0f; // seconds for normalized action timer
// Animation selection helpers for lock-on strafing
static bool animLockOnStrafingFlag = false; // true when lateral motion dominates in lock-on
static int animStrafeDirFlag = 0;           // -1 = left, +1 = right
static float animStrafeBlendRatio = 0.0f;   // 0 = pure run, 1 = pure strafe
static float lastBaseSpeed = -1.0f;
static float lastStrafeSpeed = -1.0f;


static CharacterState prevState = CHAR_STATE_NORMAL;

// Character SFX state
static float footstepTimer = 0.0f;
static const float FOOTSTEP_WALK_INTERVAL = 0.45f;
static const float FOOTSTEP_RUN_INTERVAL  = 0.28f;

static inline void character_play_swing(void) {
    audio_play_scene_sfx_dist(SCENE1_SFX_CHAR_SWING1, 1.0f, 0.0f);
}

static inline int character_random_hit_sfx(void) {
    return SCENE1_SFX_CHAR_ATTACK_HIT1 + (int)(rand_custom_u32() % 6);
}

static inline void character_play_hit(void) {
    audio_play_scene_sfx_dist(character_random_hit_sfx(), 1.0f, 0.0f);
}

static inline void character_play_footstep(bool run) {
    int base = run ? SCENE1_SFX_CHAR_FOOTSTEP_RUN1 : SCENE1_SFX_CHAR_FOOTSTEP_WALK1;
    int idx  = base + (int)(rand_custom_u32() % 4);
    audio_play_scene_sfx_dist(idx, 1.0f, 0.0f);
}

static void character_anim_apply_pose(void)
{
    if (!character.skeleton) return;
    t3d_skeleton_update(character.skeleton);
}

// Helper to copy skeleton pose from source to destination by copying bone matrices
// Both skeletons come from the same model, so they have the same structure
// We update the source skeleton first to ensure it has the current pose, then copy
static inline void skeleton_copy_pose(T3DSkeleton* dest, T3DSkeleton* src) {
    if (!dest || !src) return;
    
    // Ensure source skeleton is updated with current animation state
    t3d_skeleton_update(src);
    
    // Both skeletons are cloned from the same model, so they have identical structure
    // Copy bone matrices directly - use a safe maximum (most characters have < 64 bones)
    T3DSkeleton* dest_sk = (T3DSkeleton*)dest;
    T3DSkeleton* src_sk = (T3DSkeleton*)src;
    
    // Check if bone matrices arrays exist
    if (!dest_sk->boneMatricesFP || !src_sk->boneMatricesFP) return;
    
    // Copy bone matrices - use a conservative limit to avoid accessing invalid memory
    // Most character models have 20-50 bones, so 64 is a safe upper bound
    const int MAX_BONES = 64;
    for (int i = 0; i < MAX_BONES; i++) {
        dest_sk->boneMatricesFP[i] = src_sk->boneMatricesFP[i];
    }
}

// Helper to detect locomotion animations
static inline bool is_locomotion_anim(int a) {
    switch (a) {
        case ANIM_IDLE:
        case ANIM_WALK:
        case ANIM_RUN:
        case ANIM_WALK_BACK:
        case ANIM_RUN_BACK:
        case ANIM_STRAFE_WALK_LEFT:
        case ANIM_STRAFE_WALK_RIGHT:
        case ANIM_STRAFE_RUN_LEFT:
        case ANIM_STRAFE_RUN_RIGHT:
        case ANIM_RUN_END:
            return true;
        default: return false;
    }
}

// Play animation on a specific skeleton using a given anim set
static inline void play_anim_on_skeleton(int animIndex, T3DSkeleton* skel, T3DAnim** animSet, bool looping) {
    if (!animSet || !skel) return;
    if (animIndex < 0 || animIndex >= character.animationCount) return;
    T3DAnim* a = animSet[animIndex];
    if (!a) return;
    t3d_anim_set_looping(a, looping);
    t3d_anim_set_playing(a, true);
}

// Track last used locomotion anims for lock-on blending
static int lastBaseAnimLock = -1;
static int lastStrafeAnimLock = -1;

// Track last attached animations and last speed (forward declarations for early use)
static int lastAttachedMain;
static int lastAttachedBlend;
static float lastAnimSpeed;

// Forward declaration for shadow matrix helper
static inline void character_update_shadow_mat(void);

static void character_finalize_frame(bool updateCamera)
{
    if (updateCamera) {
        character_update_camera();
    }
    float rotAdjusted[3] = { character.rot[0], character.rot[1] + MODEL_YAW_OFFSET, character.rot[2] };
    t3d_mat4fp_from_srt_euler(character.modelMat, character.scale, rotAdjusted, character.pos);
    character_update_shadow_mat();
}

static inline void character_update_shadow_mat(void)
{
    if (!character.shadowMat) return;

    // Height above the ground plane
    float h = character.pos[1] - SHADOW_GROUND_Y;
    if (h < 0.0f) h = 0.0f;

    float t = h / JUMP_HEIGHT;
    if (t > 1.0f) t = 1.0f;

    // Optional: shrink as you rise
    float shrink = 1.0f - SHADOW_SHRINK_AMOUNT * t;

    float shadowPos[3]   = { character.pos[0], SHADOW_GROUND_Y, character.pos[2] };
    float shadowRot[3]   = { 0.0f, 0.0f, 0.0f };
    float shadowScale[3] = { character.scale[0] * 2.0f * shrink, character.scale[1], character.scale[2] * 2.0f * shrink };

    t3d_mat4fp_from_srt_euler(character.shadowMat, shadowScale, shadowRot, shadowPos);
}

// ---- Local helpers ----
typedef struct {
    float x;
    float y;
    float magnitude;
} StickInput;

void character_reset(void)
{
    characterState = CHAR_STATE_NORMAL;
    actionTimer = 0.0f;
    movementVelocityX = 0.0f;
    movementVelocityZ = 0.0f;
    currentSpeed = 0.0f;
    lastBPressed = false;
    lastAPressed = false;
    leftTriggerHeld = false;
    leftTriggerHoldTime = 0.0f;
    character.currentAnimation = 0;
    character.previousAnimation = -1;
    character.isBlending = false;
    character.blendFactor = 0.0f;
    character.blendTimer = 0.0f;
    walkThroughFog = false;
    
    // Reset attach cache
    lastAttachedMain = -1;
    lastAttachedBlend = -1;
    lastAnimSpeed = -1.0f;
    lastBaseSpeed = -1.0f;
    lastStrafeSpeed = -1.0f;

    footstepTimer = 0.0f;

}

void character_reset_button_state(void)
{
    // Sync button state to current state to prevent false "just pressed" events
    // Use globals from joypad_utility
    // B button is for attacks, A button is for roll/dodge
    lastBPressed = btn.b;
    lastAPressed = btn.a;
    leftTriggerHeld = false;
    leftTriggerHoldTime = 0.0f;
}

void character_get_velocity(float* outVelX, float* outVelZ)
{
    if (outVelX) *outVelX = movementVelocityX;
    if (outVelZ) *outVelZ = movementVelocityZ;
}

static inline StickInput normalize_stick(float rawX, float rawY) {
    StickInput s;
    float ix = fmaxf(-1.0f, fminf(1.0f, rawX / STICK_MAX));
    float iy = fmaxf(-1.0f, fminf(1.0f, rawY / STICK_MAX));
    float m = sqrtf(ix * ix + iy * iy);
    m = fminf(1.0f, m);
    if (m < INPUT_DEADZONE) {
        s.x = 0.0f; s.y = 0.0f; s.magnitude = 0.0f; return s;
    }
    float scale = (m - INPUT_DEADZONE) / (1.0f - INPUT_DEADZONE);
    scale = fminf(1.0f, scale);
    if (m > 0.0f) {
        ix = (ix / m) * scale;
        iy = (iy / m) * scale;
        m = scale;
    }
    s.x = ix; s.y = iy; s.magnitude = m; return s;
}

static inline void compute_camera_vectors(float yaw, float* fwdX, float* fwdZ, float* rightX, float* rightZ) {
    *fwdX = -fm_sinf(yaw);
    *fwdZ = -fm_cosf(yaw);
    *rightX = fm_cosf(yaw);
    *rightZ = -fm_sinf(yaw);
}

static inline void compute_desired_velocity(float inputX, float inputY, float yaw, float* outX, float* outZ) {
    float fx, fz, rx, rz;
    compute_camera_vectors(yaw, &fx, &fz, &rx, &rz);
    *outX = fx * inputY + rx * inputX;
    *outZ = fz * inputY + rz * inputX;
}

static inline void compute_desired_velocity_lockon(float inputX, float inputY, const T3DVec3* toTarget, float* outX, float* outZ) {
    // Use the direction toward target as forward, and its perpendicular as right
    // World up is Y; right = normalize(cross(up, forward)) in XZ plane
    float fwdX = toTarget->v[0];
    float fwdZ = toTarget->v[2];
    float len = sqrtf(fwdX*fwdX + fwdZ*fwdZ);
    if (len > 1e-5f) { fwdX /= len; fwdZ /= len; } else { fwdX = 0.0f; fwdZ = 1.0f; }
    // Choose right-hand perpendicular so stick right moves right
    float rightX = -fwdZ;   // perpendicular in XZ (RH coord)
    float rightZ =  fwdX;
    *outX = fwdX * inputY + rightX * inputX;
    *outZ = fwdZ * inputY + rightZ * inputX;
}

// ===== PLEASE DONT USE THE FIXED POINT API =====  
// NOTE: CHECK collision_system.c for a demo
static inline bool attack_hit_test(void) {
    Boss* boss = boss_get_instance();
    if (!boss) {
        return false;  // No boss to hit
    }

    // Boss body capsule (WORLD space)
    const float bossCapA[3] = {
        boss->pos[0] + boss->capsuleCollider.localCapA.v[0],
        boss->pos[1] + boss->capsuleCollider.localCapA.v[1],
        boss->pos[2] + boss->capsuleCollider.localCapA.v[2],
    };
    const float bossCapB[3] = {
        boss->pos[0] + boss->capsuleCollider.localCapB.v[0],
        boss->pos[1] + boss->capsuleCollider.localCapB.v[1],
        boss->pos[2] + boss->capsuleCollider.localCapB.v[2],
    };
    const float bossRadius = boss->capsuleCollider.radius;

    bool attackHit = false;

    // Sword collider derived from bone + model matrices
    if (characterSwordBoneIndex >= 0 && character.skeleton && character.modelMat) {
        T3DSkeleton *sk = (T3DSkeleton*)character.skeleton;
        const T3DMat4FP *B = &sk->boneMatricesFP[characterSwordBoneIndex]; // bone in MODEL space
        const T3DMat4FP *M = (const T3DMat4FP*)character.modelMat;         // model in WORLD space

        // Bone-local segment: hilt (0,0,0) to tip along -X
        const float p0_local[3] = { 0.0f, 0.0f, 0.0f };
        const float p1_local[3] = { -SWORD_LENGTH, 0.0f, 0.0f };

        // bone-local -> MODEL space
        float p0_model[3], p1_model[3];
        mat4fp_mul_point_f32_row3_colbasis(B, p0_local, p0_model);
        mat4fp_mul_point_f32_row3_colbasis(B, p1_local, p1_model);

        // MODEL -> WORLD space
        float p0_world[3], p1_world[3];
        mat4fp_mul_point_f32_row3_colbasis(M, p0_model, p0_world);
        mat4fp_mul_point_f32_row3_colbasis(M, p1_model, p1_world);

        attackHit = scu_capsule_vs_capsule_f(p0_world, p1_world, SWORD_COLLIDER_RADIUS, bossCapA, bossCapB, bossRadius);
    }

    // Fallback: reach check in front of the player to approximate sword length
    if (!attackHit) {
        float yaw = character.rot[1];
        float reachStart = 1.0f;  // start a little in front of the player
        float reachEnd = 2.5f;    // sword reach
        float hitX = character.pos[0] - fm_sinf(yaw) * reachStart;
        float hitZ = character.pos[2] + fm_cosf(yaw) * reachStart;
        float dx = boss->pos[0] - hitX;
        float dz = boss->pos[2] - hitZ;
        float dist = sqrtf(dx * dx + dz * dz);
        float bossRadiusReach = boss->capsuleCollider.radius;
        if (dist <= (reachEnd + bossRadiusReach)) {
            attackHit = true;
        }
    }

    return attackHit;
}

// ---- Action helpers (refactor for clarity) ----
static inline void clear_lockon_strafe_flags_on_action(void) {
    animStrafeDirFlag = 0;
    animLockOnStrafingFlag = false;
    animStrafeBlendRatio = 0.0f;
    lastBaseAnimLock = -1;
    lastStrafeAnimLock = -1;
}

static inline bool can_roll_now(const joypad_buttons_t* buttons, const StickInput* stick) {
    if (!(buttons->a && characterState == CHAR_STATE_NORMAL)) return false;
    // Allow rolling from neutral stick too
    return true;
}

static inline void try_start_roll(const joypad_buttons_t* buttons, const StickInput* stick) {
    if (!can_roll_now(buttons, stick)) return;
    characterState = CHAR_STATE_ROLLING;
    actionTimer = 0.0f;
    currentActionDuration = ROLL_DURATION;
    clear_lockon_strafe_flags_on_action();

    // Prime roll animation immediately so timer-based checks see it playing
    if (ANIM_ROLL >= 0 && ANIM_ROLL < character.animationCount && character.animations[ANIM_ROLL]) {
        t3d_anim_set_time(character.animations[ANIM_ROLL], 0.0f);
        t3d_anim_set_playing(character.animations[ANIM_ROLL], true);
    }

    // If stick is neutral, apply an immediate forward impulse so rolls still move
    if (stick->magnitude <= 0.1f) {
        float yaw = character.rot[1];
        float fx = -fm_sinf(yaw);
        float fz =  fm_cosf(yaw);
        movementVelocityX = fx * (ROLL_SPEED * 0.8f);
        movementVelocityZ = fz * (ROLL_SPEED * 0.8f);
    }
}

// Helper to get animation duration
static inline float get_attack_duration(int comboIndex) {
    int animIdx = -1;
    switch (comboIndex) {
        case 1: animIdx = ANIM_ATTACK1; break;
        case 2: animIdx = ANIM_ATTACK2; break;
        case 3: animIdx = ANIM_ATTACK3; break;
        case 4: animIdx = ANIM_ATTACK4; break;
        default: return 0.9f;
    }
    
    if (animIdx >= 0 && animIdx < character.animationCount && character.animations[animIdx]) {
        float len = t3d_anim_get_length(character.animations[animIdx]);
        return (len > 0.0f) ? len : 0.9f;
    }
    return 0.9f;
}

static inline void try_start_attack(bool leftJustPressed) {
    if (!leftJustPressed) return;
    if (characterState == CHAR_STATE_NORMAL) {
        characterState = CHAR_STATE_ATTACKING;
        attackComboIndex = 1;
        attackQueued = false;
        attackEnding = false;
        actionTimer = 0.0f;
        // Use actual clip length
        currentActionDuration = get_attack_duration(1);
        character.currentAttackHasHit = false;
        float yaw = character.rot[1];
        float fx = -fm_sinf(yaw);
        float fz =  fm_cosf(yaw);
        movementVelocityX += fx * ATTACK_FORWARD_IMPULSE;
        movementVelocityZ += fz * ATTACK_FORWARD_IMPULSE;
        character_play_swing();
    } else if (characterState == CHAR_STATE_ATTACKING && !attackEnding) {
        // Queue input only if we're in the window
        if (actionTimer >= ATTACK_QUEUE_OPEN && actionTimer <= ATTACK_QUEUE_CLOSE) {
            attackQueued = true;
        }
    }
}

static bool strongAttackUpgradedFlag = false;
static inline void upgrade_to_strong_attack(bool leftHeldNow) {
    // Charge attack: if player holds A button for STRONG_ATTACK_HOLD_THRESHOLD seconds
    // during the first 30% of a normal attack, upgrade it to a charged attack
    if (characterState == CHAR_STATE_ATTACKING && leftHeldNow &&
        leftTriggerHoldTime >= STRONG_ATTACK_HOLD_THRESHOLD &&
        actionTimer < 0.3f && !attackEnding && !strongAttackUpgradedFlag) {
        strongAttackUpgradedFlag = true;
        characterState = CHAR_STATE_ATTACKING_STRONG;
        attackComboIndex = 1;
        attackQueued = false;
        attackEnding = false;
        actionTimer = 0.0f;
        currentActionDuration = STRONG_ATTACK_DURATION;
        character.currentAttackHasHit = false;
        movementVelocityX = 0.0f;
        movementVelocityZ = 0.0f;
        character_play_swing();
    }
    if (characterState == CHAR_STATE_NORMAL) {
        strongAttackUpgradedFlag = false;
    }
}

static inline void progress_action_timers(float dt) {
    if (characterState == CHAR_STATE_NORMAL) return;
    if (currentActionDuration <= 0.0001f) currentActionDuration = 1.0f;
    actionTimer += dt / currentActionDuration;
    if (actionTimer > 1.0f) actionTimer = 1.0f;

    if (characterState == CHAR_STATE_ROLLING) {
        T3DAnim* rollAnim = NULL;
        if (ANIM_ROLL >= 0 && ANIM_ROLL < character.animationCount) {
            rollAnim = character.animations[ANIM_ROLL];
        }
        // Allow a short grace window for the animation system to bind/start the clip
        if (actionTimer > 0.05f && rollAnim && !rollAnim->isPlaying) {
            characterState = CHAR_STATE_NORMAL;
            actionTimer = 0.0f;
        } else if (actionTimer >= 2.0f) { // failsafe if anim state not available
            characterState = CHAR_STATE_NORMAL;
            actionTimer = 0.0f;
        }
    } else if (characterState == CHAR_STATE_ATTACKING) {
        // SOULS-LIKE: Only advance combo at transition time, not at queue time
        if (!attackEnding && attackQueued && actionTimer >= ATTACK_TRANSITION_TIME && attackComboIndex < 4) {
            attackComboIndex++;
            attackQueued = false;
            actionTimer = 0.0f;
            // Use actual clip length for next attack
            currentActionDuration = get_attack_duration(attackComboIndex);
            character.currentAttackHasHit = false;
            float yaw = character.rot[1];
            float fx = -fm_sinf(yaw);
            float fz =  fm_cosf(yaw);
            movementVelocityX += fx * ATTACK_FORWARD_IMPULSE;
            movementVelocityZ += fz * ATTACK_FORWARD_IMPULSE;
        } else if (!attackEnding && actionTimer >= 1.0f) {
            // Attack finished without queue - go to end animation
            if (attackComboIndex < 4) {
                attackEnding = true;
                actionTimer = 0.0f;
                currentActionDuration = ATTACK_END_DURATION;
            } else {
                characterState = CHAR_STATE_NORMAL;
                actionTimer = 0.0f;
                attackComboIndex = 0;
                attackQueued = false;
                attackEnding = false;
            }
        } else if (attackEnding && actionTimer >= 1.0f) {
            characterState = CHAR_STATE_NORMAL;
            actionTimer = 0.0f;
            attackComboIndex = 0;
            attackQueued = false;
            attackEnding = false;
        }
    } else if (characterState == CHAR_STATE_ATTACKING_STRONG) {
        if (actionTimer >= 1.0f) {
            characterState = CHAR_STATE_NORMAL;
            actionTimer = 0.0f;
        }
    } else if (characterState == CHAR_STATE_KNOCKDOWN) {
        T3DAnim* kd = NULL;
        if (character.currentAnimation == ANIM_KNOCKDOWN &&
            ANIM_KNOCKDOWN >= 0 && ANIM_KNOCKDOWN < character.animationCount) {
            kd = character.animations[ANIM_KNOCKDOWN];
        }
        if (kd && !kd->isPlaying) {
            characterState = CHAR_STATE_NORMAL;
            actionTimer = 0.0f;
        } else if (actionTimer >= 1.5f) {
            characterState = CHAR_STATE_NORMAL;
            actionTimer = 0.0f;
        }
    }
}

static inline void update_actions(const joypad_buttons_t* buttons, bool leftHeldNow, bool leftJustPressed, bool jumpJustPressed, const StickInput* stick, float dt) {
    // Roll
    try_start_roll(buttons, stick);
    // Attacks
    try_start_attack(leftJustPressed);
    upgrade_to_strong_attack(leftHeldNow);
    // Timers and state progression
    progress_action_timers(dt);
}

static inline bool character_is_invulnerable(void) {
    // Rolling grants i-frames so boss attacks can be dodged cleanly
    return characterState == CHAR_STATE_ROLLING;
}

static inline void accelerate_towards(float desiredX, float desiredZ, float maxSpeed, float dt) {
    movementVelocityX += (desiredX * maxSpeed - movementVelocityX) * MOVEMENT_ACCELERATION * dt;
    movementVelocityZ += (desiredZ * maxSpeed - movementVelocityZ) * MOVEMENT_ACCELERATION * dt;
}

static inline void accelerate_towards_with_accel(float desiredX, float desiredZ, float maxSpeed, float accel, float dt) {
    movementVelocityX += (desiredX * maxSpeed - movementVelocityX) * accel * dt;
    movementVelocityZ += (desiredZ * maxSpeed - movementVelocityZ) * accel * dt;
}

static inline void apply_friction(float dt, float scale) {
    float k = MOVEMENT_FRICTION * fmaxf(0.0f, scale);
    movementVelocityX *= expf(-k * dt);
    movementVelocityZ *= expf(-k * dt);
    if (fabsf(movementVelocityX) < 0.001f) movementVelocityX = 0.0f;
    if (fabsf(movementVelocityZ) < 0.001f) movementVelocityZ = 0.0f;
}

static inline void update_yaw_from_velocity(float dt) {
    if (fabsf(movementVelocityX) <= 0.1f && fabsf(movementVelocityZ) <= 0.1f) return;
    float targetAngle = atan2f(-movementVelocityX, movementVelocityZ);
    float currentAngle = character.rot[1];
    while (targetAngle > T3D_PI) targetAngle -= 2.0f * T3D_PI;
    while (targetAngle < -T3D_PI) targetAngle += 2.0f * T3D_PI;
    while (currentAngle > T3D_PI) currentAngle -= 2.0f * T3D_PI;
    while (currentAngle < -T3D_PI) currentAngle += 2.0f * T3D_PI;
    float angleDelta = targetAngle - currentAngle;
    while (angleDelta > T3D_PI) angleDelta -= 2.0f * T3D_PI;
    while (angleDelta < -T3D_PI) angleDelta += 2.0f * T3D_PI;
    float maxTurnRate = TURN_RATE * dt;
    if (angleDelta > maxTurnRate) angleDelta = maxTurnRate;
    else if (angleDelta < -maxTurnRate) angleDelta = -maxTurnRate;
    character.rot[1] = currentAngle + angleDelta;
}

static inline void update_current_speed(float inputMagnitude, float dt) {
    if (inputMagnitude > 0.0f) {
            currentSpeed += SPEED_BUILDUP_RATE * dt; // Increase speed based on input
        currentSpeed = fminf(currentSpeed, inputMagnitude);
    } else {
        currentSpeed -= SPEED_DECAY_RATE * dt;
        currentSpeed = fmaxf(currentSpeed, 0.0f);
    }
}

// Helper function to determine target animation index based on state and speed
static inline int get_target_animation(CharacterState state, float speedRatio) {
    if (state == CHAR_STATE_TITLE_IDLE) {
        return ANIM_IDLE_TITLE;
    }
    if (state == CHAR_STATE_FOG_WALK) {
        return ANIM_FOG_OF_WAR;
    }
    if (state == CHAR_STATE_KNOCKDOWN) {
        return ANIM_KNOCKDOWN;
    }
    if (state == CHAR_STATE_ROLLING) {
        return ANIM_ROLL;
    }
    if (state == CHAR_STATE_ATTACKING || state == CHAR_STATE_ATTACKING_STRONG) {
        if (attackEnding) {
            if (attackComboIndex == 1) return ANIM_ATTACK1_END;
            if (attackComboIndex == 2) return ANIM_ATTACK2_END;
            if (attackComboIndex == 3) return ANIM_ATTACK3_END;
        } else {
            if (state == CHAR_STATE_ATTACKING_STRONG) {
                return ANIM_ATTACK_CHARGED;
            }
            switch (attackComboIndex) {
                case 1: return ANIM_ATTACK1;
                case 2: return ANIM_ATTACK2;
                case 3: return ANIM_ATTACK3;
                case 4: return ANIM_ATTACK4;
                default: return ANIM_ATTACK1;
            }
        }
        return ANIM_ATTACK1;
    }
    // In lock-on, locomotion blending is handled separately; return idle here to avoid hard switching
    if (cameraLockOnActive && animStrafeDirFlag != 0) {
        return ANIM_IDLE;
    }
    // Determine if moving backwards relative to facing
    float yaw = character.rot[1];
    float fwdX = -fm_sinf(yaw);
    float fwdZ =  fm_cosf(yaw);
    float dotForward = movementVelocityX * fwdX + movementVelocityZ * fwdZ;
    bool isBackward = (dotForward < -0.001f);
    // Normal state: select based on speed
    if (speedRatio < IDLE_THRESHOLD) {
        return ANIM_IDLE;
    }
    if (speedRatio < WALK_THRESHOLD) {
        return isBackward ? ANIM_WALK_BACK : ANIM_WALK;
    }
    if (speedRatio < RUN_THRESHOLD) {
        return isBackward ? ANIM_WALK_BACK : ANIM_WALK;
    }
    // For the blend zone and above, use RUN
    return isBackward ? ANIM_RUN_BACK : ANIM_RUN;
}

// ---- Animation helpers (refactor for clarity) ----
static inline bool is_action_state(CharacterState state) {
    return (state == CHAR_STATE_ROLLING) ||
           (state == CHAR_STATE_ATTACKING) ||
           (state == CHAR_STATE_ATTACKING_STRONG) ||
           (state == CHAR_STATE_KNOCKDOWN);
}

// Deterministic per-frame animation application helpers
static int activeMainAnim = -1;
static int activeBlendAnim = -1;

// Track what's currently attached to avoid redundant attach calls
static int lastAttachedMain = -1;
static int lastAttachedBlend = -1;
static float lastAnimSpeed = -1.0f;

static inline void anim_bind_and_play(T3DAnim** set, int idx, T3DSkeleton* skel, bool loop, bool restart)
{
    if (!set || !skel) return;
    if (idx < 0 || idx >= character.animationCount) return;
    T3DAnim* a = set[idx];
    if (!a) return;

    t3d_anim_attach(a, skel);
    t3d_anim_set_looping(a, loop);

    if (restart) {
        t3d_anim_set_time(a, 0.0f);
    } else if (loop) {
        float len = t3d_anim_get_length(a);
        float t   = t3d_anim_get_time(a);
        if (len > 0.0f && t >= len) t3d_anim_set_time(a, 0.0f);
    }

    t3d_anim_set_playing(a, true);
}

static inline void anim_stop(T3DAnim** set, int idx)
{
    if (!set) return;
    if (idx < 0 || idx >= character.animationCount) return;
    if (!set[idx]) return;
    t3d_anim_set_playing(set[idx], false);
}

static inline bool try_lockon_locomotion_blend(float speedRatio, CharacterState state, float dt) {
    if (is_action_state(state)) return false;
    if (character.isBlending) return false;
    if (!(state == CHAR_STATE_NORMAL && cameraLockOnActive && animStrafeDirFlag != 0 && character.animations)) return false;

    float yaw = character.rot[1];
    float fwdX = -fm_sinf(yaw);
    float fwdZ =  fm_cosf(yaw);
    float dotForward = movementVelocityX * fwdX + movementVelocityZ * fwdZ;
    bool isBackward = (dotForward < -0.001f);

    bool running = (speedRatio >= RUN_THRESHOLD);
    int baseAnim = (speedRatio < IDLE_THRESHOLD) ? ANIM_IDLE : (!running ? (isBackward ? ANIM_WALK_BACK : ANIM_WALK)
                                                     : (isBackward ? ANIM_RUN_BACK : ANIM_RUN));
    int strafeAnim = (speedRatio < IDLE_THRESHOLD) ? ANIM_IDLE : (!running ? ((animStrafeDirFlag > 0) ? ANIM_STRAFE_WALK_RIGHT : ANIM_STRAFE_WALK_LEFT)
                                                        : ((animStrafeDirFlag > 0) ? ANIM_STRAFE_RUN_RIGHT : ANIM_STRAFE_RUN_LEFT));

    // *** OPTIMIZATION: Only attach/restart when animations actually change ***
    bool baseChanged = (lastBaseAnimLock != baseAnim);
    bool strafeChanged = (lastStrafeAnimLock != strafeAnim);
    
    if (baseChanged) {
        if (activeMainAnim != -1 && activeMainAnim != baseAnim) {
            anim_stop(character.animations, activeMainAnim);
        }
        activeMainAnim = baseAnim;
        anim_bind_and_play(character.animations, baseAnim, character.skeleton, true, true);
        lastBaseAnimLock = baseAnim;
    }
    
    if (strafeChanged) {
        if (activeBlendAnim != -1 && activeBlendAnim != strafeAnim) {
            anim_stop(character.animations, activeBlendAnim);
        }
        activeBlendAnim = strafeAnim;
        // Use main animations array, but attach to skeletonBlend for blending
        anim_bind_and_play(character.animations, strafeAnim, character.skeletonBlend, true, true);
        lastStrafeAnimLock = strafeAnim;
    }

    // *** OPTIMIZATION: Cache speeds and only set when changed ***
    float baseSpeed = running ? fmaxf(speedRatio * 0.8f + 0.2f, 0.7f) : fmaxf(speedRatio * 2.0f + 0.3f, 0.6f);
    float strafeSpeed = running ? fmaxf(speedRatio * 0.9f + 0.2f, 0.7f) : fmaxf(speedRatio * 1.8f + 0.3f, 0.6f);
    
    static float lastBaseSpeed = -1.0f;
    static float lastStrafeSpeed = -1.0f;
    
    if (fabsf(baseSpeed - lastBaseSpeed) > 0.01f) {
        if (character.animations[baseAnim]) {
            t3d_anim_set_speed(character.animations[baseAnim], baseSpeed);
        }
        lastBaseSpeed = baseSpeed;
    }
    
    if (fabsf(strafeSpeed - lastStrafeSpeed) > 0.01f) {
        if (character.animations[strafeAnim]) {
            t3d_anim_set_speed(character.animations[strafeAnim], strafeSpeed);
        }
        lastStrafeSpeed = strafeSpeed;
    }

    t3d_anim_update(character.animations[baseAnim], dt);
    t3d_anim_update(character.animations[strafeAnim], dt);

    float w = fminf(1.0f, fmaxf(0.0f, animStrafeBlendRatio));
    t3d_skeleton_blend(character.skeleton, character.skeleton, character.skeletonBlend, w);
    character.isBlending = false;
    return true;
}

static inline void ensure_locomotion_playing(CharacterState state) {
    if (state != CHAR_STATE_NORMAL) return;
    if (!is_locomotion_anim(character.currentAnimation)) return;
    int cur = character.currentAnimation;
    if (cur >= 0 && cur < character.animationCount && character.animations[cur]) {
        T3DAnim* ca = character.animations[cur];
        if (!ca->isPlaying) {
            t3d_anim_set_looping(ca, true);
            t3d_anim_set_playing(ca, true);
        }
    }
}

static void switch_to_action_animation(int targetAnim) {
    // Stop any lock-on locomotion drivers
    if (activeMainAnim != -1) {
        anim_stop(character.animations, activeMainAnim);
        activeMainAnim = -1;
    }
    if (activeBlendAnim != -1) {
        anim_stop(character.animations, activeBlendAnim);
        activeBlendAnim = -1;
    }

    character.previousAnimation = character.currentAnimation;
    character.currentAnimation  = targetAnim;
    character.isBlending = true;
    character.blendDuration = ATTACK_CROSSFADE_DURATION;
    character.blendTimer = 0.0f;
    character.blendFactor = 0.0f;

    // CRITICAL: Snapshot the current skeleton pose into skeletonBlend for blending
    // This captures the previous pose without needing a duplicate animation array
    if (character.previousAnimation >= 0 && 
        character.animations[character.previousAnimation] &&
        character.skeleton && character.skeletonBlend) {
        
        // Snapshot the current skeleton pose into skeletonBlend for blending
        // Update skeleton first to ensure it has the current pose, then copy bone matrices
        t3d_skeleton_update(character.skeleton);
        skeleton_copy_pose(character.skeletonBlend, character.skeleton);
        lastAttachedBlend = character.previousAnimation;
    }

    // Stop previous main clip AFTER we've captured its pose
    if (character.previousAnimation >= 0 && character.animations[character.previousAnimation]) {
        anim_stop(character.animations, character.previousAnimation);
    }

    // Bind and restart target action anim on main skeleton
    anim_bind_and_play(character.animations, targetAnim, character.skeleton, false, true);
    // Mark as needing attach next frame
    lastAttachedMain = targetAnim;
}

// Immediate animation switch for reactive animations (knockdown, hit reactions)
// Skips blending for instant response
static void switch_to_action_animation_immediate(int targetAnim) {
    // Stop any lock-on locomotion drivers
    if (activeMainAnim != -1) {
        anim_stop(character.animations, activeMainAnim);
        activeMainAnim = -1;
    }
    if (activeBlendAnim != -1) {
        anim_stop(character.animations, activeBlendAnim);
        activeBlendAnim = -1;
    }

    // Stop previous animation
    if (character.previousAnimation >= 0 && character.animations[character.previousAnimation]) {
        anim_stop(character.animations, character.previousAnimation);
    }
    if (character.currentAnimation >= 0 && character.animations[character.currentAnimation]) {
        anim_stop(character.animations, character.currentAnimation);
    }

    character.previousAnimation = character.currentAnimation;
    character.currentAnimation = targetAnim;
    character.isBlending = false;  // No blend, immediate switch

    // Bind and restart target action anim on main skeleton
    anim_bind_and_play(character.animations, targetAnim, character.skeleton, false, true);
    // Mark as attached
    lastAttachedMain = targetAnim;
}

static void switch_to_locomotion_animation(int targetAnim) {
    character.currentAnimation = targetAnim;
    // Blend from last action pose into locomotion to avoid any no-pose or hard snap
    if (is_action_state(prevState)) {
        character.isBlending = true;
        character.blendDuration = 0.12f;
        character.blendTimer = 0.0f;
        character.blendFactor = 0.0f;
        // Use the last action clip as previous when available (focus on roll case)
        int prevClip = -1;
        if (prevState == CHAR_STATE_ROLLING) prevClip = ANIM_ROLL;
        else if (prevState == CHAR_STATE_KNOCKDOWN) prevClip = ANIM_KNOCKDOWN;
        // For attack states, prefer the current attack/end clip already selected
        else if (prevState == CHAR_STATE_ATTACKING || prevState == CHAR_STATE_ATTACKING_STRONG) prevClip = character.previousAnimation;
        character.previousAnimation = prevClip;
        if (prevClip >= 0 && prevClip < character.animationCount && character.animations[prevClip] && character.skeleton && character.skeletonBlend) {
            // Snapshot the previous pose into skeletonBlend
            // Update skeleton first to ensure it has the current pose, then copy bone matrices
            t3d_skeleton_update(character.skeleton);
            skeleton_copy_pose(character.skeletonBlend, character.skeleton);
            lastAttachedBlend = prevClip;
        }
    } else {
        character.isBlending = false;
    }

    if (character.animations[targetAnim]) {
        t3d_skeleton_reset(character.skeleton);
        t3d_anim_attach(character.animations[targetAnim], character.skeleton);
        lastAttachedMain = targetAnim;
        bool shouldLoop = (targetAnim == ANIM_IDLE || targetAnim == ANIM_IDLE_TITLE || targetAnim == ANIM_WALK ||
                           targetAnim == ANIM_RUN || targetAnim == ANIM_WALK_BACK ||
                           targetAnim == ANIM_RUN_BACK || targetAnim == ANIM_STRAFE_WALK_LEFT ||
                           targetAnim == ANIM_STRAFE_WALK_RIGHT || targetAnim == ANIM_STRAFE_RUN_LEFT ||
                           targetAnim == ANIM_STRAFE_RUN_RIGHT);
        t3d_anim_set_looping(character.animations[targetAnim], shouldLoop);
        t3d_anim_set_playing(character.animations[targetAnim], true);
    }
}

static inline void apply_run_end_transition(CharacterState state, float speedRatio, int* targetAnim, bool* runEndActive) {
    if (!*runEndActive && character.currentAnimation == ANIM_RUN && state == CHAR_STATE_NORMAL && speedRatio < RUN_THRESHOLD && speedRatio >= IDLE_THRESHOLD) {
        *targetAnim = ANIM_RUN_END;
        *runEndActive = true;
    }
    if (*runEndActive) {
        if (state == CHAR_STATE_NORMAL) {
            if (speedRatio >= IDLE_THRESHOLD) {
                *targetAnim = ANIM_RUN_END;
            } else {
                *runEndActive = false;
            }
        } else {
            *runEndActive = false;
        }
    }
}

static inline void update_animations(float speedRatio, CharacterState state, float dt) {
    if (!character.animations || !character.skeleton || !character.skeletonBlend) return;
    if (try_lockon_locomotion_blend(speedRatio, state, dt)) return;
    
    // Determine target animation
    int targetAnim = get_target_animation(state, speedRatio);

    // RunEnd transition: play end animation once when dropping below run threshold
    static bool runEndActive = false;
    apply_run_end_transition(state, speedRatio, &targetAnim, &runEndActive);

    // Fallback to Idle if target is invalid
    bool targetValid = (targetAnim >= 0 && targetAnim < character.animationCount && character.animations[targetAnim] != NULL);
    if (!targetValid) {
        targetAnim = ANIM_IDLE; // Safe fallback
        if (runEndActive && targetAnim == ANIM_RUN_END) {
            runEndActive = false;
        }
    }

    ensure_locomotion_playing(state);

    // Only attach/switch when actually changing animations
    bool needsSwitch = (character.currentAnimation != targetAnim);
    
    if (needsSwitch) {
        // For action states (attacks, rolls), start immediate transition with blend
        if (is_action_state(state)) {
            switch_to_action_animation(targetAnim);
        } else {
            switch_to_locomotion_animation(targetAnim);
        }
    }
    // REMOVED: The "Action re-trigger" block that was causing restarts mid-attack

    // Update blending if active
    if (character.isBlending) {
        character.blendTimer += dt;
        if (character.blendTimer >= character.blendDuration) {
            // Blend complete
            character.isBlending = false;
            character.blendFactor = 1.0f;
            character.blendTimer = 0.0f;
        } else {
            character.blendFactor = character.blendTimer / character.blendDuration;
        }
    }

    // Update current animation
    if (character.currentAnimation >= 0 && character.animations[character.currentAnimation]) {
        T3DAnim* currentAnim = character.animations[character.currentAnimation];
        
        // Only attach if the animation changed - attaching is expensive!
        if (lastAttachedMain != character.currentAnimation) {
            t3d_anim_attach(currentAnim, character.skeleton);
            lastAttachedMain = character.currentAnimation;
        }
        
        // Set speed based on state and movement
        float animSpeed = 1.0f;
        if (state == CHAR_STATE_NORMAL) {
            if (character.currentAnimation == ANIM_WALK || character.currentAnimation == ANIM_WALK_BACK) {
                animSpeed = fmaxf(speedRatio * 2.0f + 0.3f, 0.5f);
            } else if (character.currentAnimation == ANIM_RUN || character.currentAnimation == ANIM_RUN_BACK) {
                animSpeed = fmaxf(speedRatio * 0.8f + 0.2f, 0.6f);
            }
        } else if (state == CHAR_STATE_ATTACKING_STRONG) {
            animSpeed = 0.8f;
        } else if (state == CHAR_STATE_ROLLING) {
            animSpeed = ROLL_ANIM_SPEED;
        }
        
        // Ensure title idle loops and never stops
        if (state == CHAR_STATE_TITLE_IDLE && character.currentAnimation == ANIM_IDLE_TITLE) {
            t3d_anim_set_looping(currentAnim, true);
            if (!currentAnim->isPlaying) t3d_anim_set_playing(currentAnim, true);
            float len = t3d_anim_get_length(currentAnim);
            float t   = t3d_anim_get_time(currentAnim);
            if (len > 0.0f && t >= len) t3d_anim_set_time(currentAnim, 0.0f);
        }

        // Only set speed when it actually changes - set_speed can be expensive
        if (fabsf(animSpeed - lastAnimSpeed) > 0.001f) {
            t3d_anim_set_speed(currentAnim, animSpeed);
            lastAnimSpeed = animSpeed;
        }
        t3d_anim_update(currentAnim, dt);
    }

    // Blend between previous and current skeletons if blending
    // The previous pose was already captured in skeletonBlend during the transition
    if (character.isBlending && character.skeleton && character.skeletonBlend) {
        // Blend between previous (skeletonBlend) and current (skeleton)
        // t3d_skeleton_blend(dest, skeletonA, skeletonB, factor)
        // factor=0 → 100% A, factor=1 → 100% B
        // We want: factor=0 → 100% previous, factor=1 → 100% current
        // So A=previous (skeletonBlend), B=current (skeleton), factor=blendFactor
        t3d_skeleton_blend(character.skeleton, character.skeletonBlend, character.skeleton, character.blendFactor);
    }

    // Clear RunEnd when it finishes
    if (runEndActive && character.currentAnimation == ANIM_RUN_END) {
        if (character.animations[ANIM_RUN_END] && !character.animations[ANIM_RUN_END]->isPlaying) {
            runEndActive = false;
        }
    }
}

// ==== Update Functions ====

/* Initialize character model, skeletons, animations, and camera. */
void character_init(void)
{
    characterModel = t3d_model_load("rom:/knight/knight.t3dm");
    characterShadowModel = t3d_model_load("rom:/blob_shadow/shadow.t3dm");

    T3DSkeleton* skeleton = malloc(sizeof(T3DSkeleton));
    *skeleton = t3d_skeleton_create(characterModel);

    T3DSkeleton* skeletonBlend = malloc(sizeof(T3DSkeleton));
    *skeletonBlend = t3d_skeleton_clone(skeleton, false);

    // Cache sword/hand bone index for collider attachment (try common names)
    characterSwordBoneIndex = t3d_skeleton_find_bone(skeleton, "Hand-Right");

    const int animationCount = ANIM_COUNT;
    const char* animationNames[] = {
        "Idle",
        "IdleTitle",
        "Walk1",
        "Run",
        "RunEnd",
        "Roll",
        "Knockdown",
        "StrafeWalkLeft",
        "StrafeWalkRight",
        "StrafeRunLeft",
        "StrafeRunRight",
        "Attack1",
        "Attack1End",
        "Attack2",
        "Attack2End",
        "Attack3",
        "Attack3End",
        "Attack4",
        "FogOfWar",
        "AttackCharged",
        "WalkBackwards",
        "RunBackwards"
    };
    const bool animationsLooping[] = {
        true,   // Idle
        true,   // IdleTitle
        true,   // Walk1
        true,   // Run
        false,  // RunEnd
        false,  // Roll
        false,  // Knockdown
        true,   // StrafeWalkLeft
        true,   // StrafeWalkRight
        true,   // StrafeRunLeft
        true,   // StrafeRunRight
        false,  // Attack1
        false,  // Attack1End
        false,  // Attack2
        false,  // Attack2End
        false,  // Attack3
        false,  // Attack3End
        false,  // Attack4
        false,  // FogOfWar
        false,  // AttackCharged
        true,   // WalkBackwards
        true    // RunBackwards
    };

    T3DAnim** animations = malloc_uncached(animationCount * sizeof(T3DAnim*));
    for (int i = 0; i < animationCount; i++) {
        animations[i] = malloc(sizeof(T3DAnim));
        assertf(animations[i], "OOM anim struct %d", i);
        
        *animations[i] = t3d_anim_create(characterModel, animationNames[i]);
        t3d_anim_set_looping(animations[i], animationsLooping[i]);
        t3d_anim_set_playing(animations[i], false);
        t3d_anim_attach(animations[i], skeleton);
    }

    // Initialize attack durations from clip lengths
    ATTACK1_DURATION = get_attack_duration(1);
    ATTACK2_DURATION = get_attack_duration(2);
    ATTACK3_DURATION = get_attack_duration(3);
    ATTACK4_DURATION = get_attack_duration(4);

    // model block (no prim color here; set per-frame in draw for tinting)
    rspq_block_begin();
    t3d_model_draw_skinned(characterModel, skeleton);
    rspq_block_t* dpl_model = rspq_block_end();

    // shadow block (no prim color here; we'll set it per-frame as we need to change the alpha
    rspq_block_begin();
    t3d_model_draw(characterShadowModel);
    rspq_block_t* dpl_shadow = rspq_block_end();

    CapsuleCollider collider = {
        .localCapA = {{0.0f, 4.0f, 0.0f}},    // Bottom at feet
        .localCapB = {{0.0f, 13.0f, 0.0f}},  // Top at head height (reduced)
        .radius = 5.0f                       // Smaller radius for tighter collision
    };

    Character newCharacter = {
        .pos = {0.0f, 0.0f, 0.0f},
        .rot = {0.0f, 0.0f, 0.0f},
        .scale = {MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        .scrollParams = NULL,
        .skeleton = skeleton,
        .skeletonBlend = skeletonBlend,
        .animations = animations,
        .currentAnimation = 0,
        .previousAnimation = -1,
        .animationCount = animationCount,
        .blendFactor = 0.0f,
        .blendDuration = 0.3f,
        .blendTimer = 0.0f,
        .isBlending = false,
        .capsuleCollider = collider,
        .modelMat = malloc_uncached(sizeof(T3DMat4FP)),
        .shadowMat = malloc_uncached(sizeof(T3DMat4FP)),
        .dpl_model = dpl_model,
        .dpl_shadow = dpl_shadow,
        .visible = true,
        .maxHealth = 100.0f,
        .health = 100.0f,
        .damageFlashTimer = 0.0f,
        .currentAttackHasHit = false
    };

    t3d_mat4fp_identity(newCharacter.modelMat);
    t3d_mat4fp_identity(newCharacter.shadowMat);

    character = newCharacter;

    camera_reset_third_person();
    character_update_camera();

    characterState = CHAR_STATE_TITLE_IDLE;
}

/* Main per-frame update: input, actions, movement, rotation, animation, camera. */
void character_update(void) 
{
    GameState state = scene_get_game_state();
    // Halt all player control while in an end state
    if (state == GAME_STATE_DEAD || state == GAME_STATE_VICTORY) {
        movementVelocityX = 0.0f;
        movementVelocityZ = 0.0f;
        character_update_camera();
        character_anim_apply_pose();
        character_finalize_frame(false);
        return;
    }

    if(scene_get_game_state() == GAME_STATE_TITLE || scene_get_game_state() == GAME_STATE_TITLE_TRANSITION)
    {
        apply_friction(deltaTime, 1.0f);
        update_current_speed(0.0f, deltaTime); // No input magnitude during cutscenes
        float animationSpeedRatio = currentSpeed;

        if(scene_get_game_state() == GAME_STATE_TITLE_TRANSITION && walkThroughFog == false)
        {
            t3d_anim_set_playing(character.animations[ANIM_FOG_OF_WAR], true);
            characterState = CHAR_STATE_FOG_WALK;
            walkThroughFog = true;
        }
        else
        {
            update_animations(animationSpeedRatio, characterState, deltaTime);
        }

        //t3d_anim_set_playing(character.animations[ANIM_IDLE], true);

        // Update position with current velocity (with collision check)
        float newPosX = character.pos[0] + movementVelocityX * deltaTime;
        float newPosZ = character.pos[2] + movementVelocityZ * deltaTime;
    
        character.pos[0] = newPosX;
        character.pos[2] = newPosZ;

        character_anim_apply_pose();
        character_finalize_frame(false);
        return;
    }
    // Disable player input during cutscenes & title screen
    if (scene_is_cutscene_active()) {
        // Still update animations and apply friction, but no player input
        apply_friction(deltaTime, 1.0f);
        update_current_speed(0.0f, deltaTime); // No input magnitude during cutscenes
        float animationSpeedRatio = currentSpeed;
        update_animations(animationSpeedRatio, characterState, deltaTime);
        
        // Update position with current velocity (with collision check)
        float newPosX = character.pos[0] + movementVelocityX * deltaTime;
        float newPosZ = character.pos[2] + movementVelocityZ * deltaTime;
        
        //if (!scene_check_room_bounds(newPosX, character.pos[1], newPosZ)) {
            character.pos[0] = newPosX;
            character.pos[2] = newPosZ;
        // } else {
        //     // Stop movement if collision detected
        //     movementVelocityX = 0.0f;
        //     movementVelocityZ = 0.0f;
        // }
        
        character_update_camera();
        character_anim_apply_pose();
        character_finalize_frame(false);
        return;
    }

    // Use globals updated by joypad_update()
    // joypad: stick values; btn: pressed; rel: released

    // Jump removed
    bool jumpJustPressed = false;

    // Handle B button hold time tracking for charge attacks
    bool leftJustPressed = btn.b && !lastBPressed;
    
    if (leftJustPressed) {
        // Button just pressed this frame - start tracking hold time
        leftTriggerHeld = true;
        leftTriggerHoldTime = 0.0f;
    }
    
    if (leftTriggerHeld) {
        // Button is being held - accumulate time
        leftTriggerHoldTime += deltaTime;
    }
    
    if (rel.b) {
        // Button released - stop tracking
        leftTriggerHeld = false;
        leftTriggerHoldTime = 0.0f;
    }
    
    lastBPressed = btn.b;

    StickInput stick = normalize_stick((float)joypad.stick_x, (float)joypad.stick_y);
    update_actions(&btn, leftTriggerHeld, leftJustPressed, jumpJustPressed, &stick, deltaTime);

    if (characterState != CHAR_STATE_ATTACKING && characterState != CHAR_STATE_ATTACKING_STRONG && characterState != CHAR_STATE_KNOCKDOWN && stick.magnitude > 0.0f) {
        float desiredVelX, desiredVelZ;
        if (cameraLockOnActive) {
            // Forward is toward target; strafe left/right orbits around target
            T3DVec3 toTarget = (T3DVec3){{
                cameraLockOnTarget.v[0] - character.pos[0],
                0.0f,
                cameraLockOnTarget.v[2] - character.pos[2]
            }};
            compute_desired_velocity_lockon(stick.x, stick.y, &toTarget, &desiredVelX, &desiredVelZ);
        } else {
            compute_desired_velocity(stick.x, stick.y, cameraAngleX, &desiredVelX, &desiredVelZ);
        }
        float currentMaxSpeed = (characterState == CHAR_STATE_ROLLING) ? ROLL_SPEED : MAX_MOVEMENT_SPEED;
        if (characterState == CHAR_STATE_ROLLING) {
            // More responsive steering during roll
            accelerate_towards_with_accel(desiredVelX, desiredVelZ, currentMaxSpeed, ROLL_STEER_ACCELERATION, deltaTime);
        } else {
            accelerate_towards(desiredVelX, desiredVelZ, currentMaxSpeed, deltaTime);
        }
        if (cameraLockOnActive && characterState != CHAR_STATE_ROLLING) {
            // During lock-on: always face the target while moving (strafe keeps facing boss)
            // BUT when rolling, face the roll direction instead
            // Compute strafe direction relative to target
            T3DVec3 toTargetDir = {{
                cameraLockOnTarget.v[0] - character.pos[0],
                0.0f,
                cameraLockOnTarget.v[2] - character.pos[2]
            }};
            t3d_vec3_norm(&toTargetDir);
            // Right-hand perpendicular in XZ: right = normalize(cross(forward, up))
            T3DVec3 rightDir = {{ -toTargetDir.v[2], 0.0f, toTargetDir.v[0] }}; // perpendicular in XZ
            float forward = desiredVelX * toTargetDir.v[0] + desiredVelZ * toTargetDir.v[2];
            float lateral  = desiredVelX * rightDir.v[0] + desiredVelZ * rightDir.v[2];
            // Compute lateral proportion of motion
            float sum = fabsf(forward) + fabsf(lateral) + 0.0001f;
            float lateralRatio = fminf(1.0f, fmaxf(0.0f, fabsf(lateral) / sum));
            // Require a minimum sideways axis intent to avoid drift-induced strafing
            bool sidewaysEnough = fabsf(stick.x) >= STRAFE_LATERAL_AXIS_DEADZONE;
            // Hysteresis: activate at higher ratio, deactivate at lower
            if (!animLockOnStrafingFlag) {
                animLockOnStrafingFlag = sidewaysEnough && (lateralRatio >= STRAFE_ACTIVATION_RATIO);
            } else {
                animLockOnStrafingFlag = sidewaysEnough && (lateralRatio >= STRAFE_DEACTIVATION_RATIO);
            }
            // Apply strafe flags/blend only when active
            if (animLockOnStrafingFlag) {
                animStrafeBlendRatio = lateralRatio;
                animStrafeDirFlag = (lateral > 0.0f) ? +1 : (lateral < 0.0f ? -1 : 0);
            } else {
                animStrafeBlendRatio = 0.0f;
                animStrafeDirFlag = 0;
            }

            float targetAngle = atan2f(-(cameraLockOnTarget.v[0] - character.pos[0]), (cameraLockOnTarget.v[2] - character.pos[2]));
            float currentAngle = character.rot[1];
            while (targetAngle > T3D_PI) targetAngle -= 2.0f * T3D_PI;
            while (targetAngle < -T3D_PI) targetAngle += 2.0f * T3D_PI;
            while (currentAngle > T3D_PI) currentAngle -= 2.0f * T3D_PI;
            while (currentAngle < -T3D_PI) currentAngle += 2.0f * T3D_PI;
            float angleDelta = targetAngle - currentAngle;
            while (angleDelta > T3D_PI) angleDelta -= 2.0f * T3D_PI;
            while (angleDelta < -T3D_PI) angleDelta += 2.0f * T3D_PI;
            float maxTurnRate = TURN_RATE * deltaTime;
            if (angleDelta > maxTurnRate) angleDelta = maxTurnRate;
            else if (angleDelta < -maxTurnRate) angleDelta = -maxTurnRate;
            character.rot[1] = currentAngle + angleDelta;
        } else {
            // When rolling or not locked-on: always face movement direction
            update_yaw_from_velocity(deltaTime);
            animLockOnStrafingFlag = false;
            animStrafeDirFlag = 0;
            animStrafeBlendRatio = 0.0f;
        }
    } else if (characterState == CHAR_STATE_ATTACKING || characterState == CHAR_STATE_ATTACKING_STRONG) {
        // Charged attack locks movement completely; normal attacks keep some momentum
        float frictionScale = (characterState == CHAR_STATE_ATTACKING_STRONG) ? 1.0f : ATTACK_FRICTION_SCALE;
        apply_friction(deltaTime, frictionScale);

        if (characterState == CHAR_STATE_ATTACKING_STRONG) {
            // Ensure no drift during charged attack
            movementVelocityX = 0.0f;
            movementVelocityZ = 0.0f;
        }

        // Hit window: apply damage to boss on overlap
        float hitStart = (characterState == CHAR_STATE_ATTACKING_STRONG) ? STRONG_ATTACK_HIT_START : 0.25f;
        float hitEnd = (characterState == CHAR_STATE_ATTACKING_STRONG) ? STRONG_ATTACK_HIT_END : 0.55f;
        float damage = (characterState == CHAR_STATE_ATTACKING_STRONG) ? STRONG_ATTACK_DAMAGE : 10.0f;
        if (actionTimer > hitStart && actionTimer < hitEnd) {

            // TODO: CHANGE THIS TO THE FLOAT POINT API PLEASE
            if (!character.currentAttackHasHit && attack_hit_test()) {
                Boss* boss = boss_get_instance();
                if (boss) {
                    boss_apply_damage(boss, damage);
                }
                character.currentAttackHasHit = true; // Mark attack as having hit
                character_play_hit();
            }

        }
    // } else if (characterState == CHAR_STATE_JUMPING) {
    //     // Keep horizontal velocity from the takeoff; no air drag so run speed carries through jump
    //     apply_friction(deltaTime, JUMP_FRICTION_SCALE);
    //     float jumpPhase = fminf(1.0f, actionTimer / JUMP_DURATION);
    //     character.pos[1] = fm_sinf(jumpPhase * T3D_PI) * JUMP_HEIGHT;
    } else {
        // Apply friction when not providing input; include roll-specific decay
        float friction = (characterState == CHAR_STATE_ROLLING) ? ROLL_FRICTION_SCALE : 1.0f;
        apply_friction(deltaTime, friction);
    }

    // if (characterState != CHAR_STATE_JUMPING) {
    //     character.pos[1] = 0.0f;
    // }

    update_current_speed(stick.magnitude, deltaTime);
    // Drive animation speed from actual movement velocity to avoid idle flicker after actions
    float velMag = sqrtf(movementVelocityX * movementVelocityX + movementVelocityZ * movementVelocityZ);
    float animationSpeedRatio = fminf(1.0f, velMag / MAX_MOVEMENT_SPEED);
    update_animations(animationSpeedRatio, characterState, deltaTime);
    prevState = characterState;

    // Footstep SFX for locomotion
    if (characterState == CHAR_STATE_NORMAL) {
        bool isRunning = (animationSpeedRatio >= RUN_THRESHOLD);
        bool isWalking = (!isRunning) && (animationSpeedRatio >= WALK_THRESHOLD);
        if (isRunning || isWalking) {
            float interval = isRunning ? FOOTSTEP_RUN_INTERVAL : FOOTSTEP_WALK_INTERVAL;
            footstepTimer += deltaTime;
            if (footstepTimer >= interval) {
                character_play_footstep(isRunning);
                footstepTimer = 0.0f;
            }
        } else {
            footstepTimer = 0.0f;
        }
    } else {
        footstepTimer = 0.0f;
    }

    // Calculate proposed new position
    float newPosX = character.pos[0] + movementVelocityX * deltaTime;
    float newPosZ = character.pos[2] + movementVelocityZ * deltaTime;
    
    // Check room bounds collision
    // if (scene_check_room_bounds(newPosX, character.pos[1], newPosZ)) {
    //     // Collision detected - try to clamp position
    //     // First try X movement only
    //     if (!scene_check_room_bounds(newPosX, character.pos[1], character.pos[2])) {
    //         character.pos[0] = newPosX;
    //         movementVelocityZ = 0.0f;  // Stop Z movement
    //     }
    //     // Then try Z movement only
    //     else if (!scene_check_room_bounds(character.pos[0], character.pos[1], newPosZ)) {
    //         character.pos[2] = newPosZ;
    //         movementVelocityX = 0.0f;  // Stop X movement
    //     }
    //     // If both cause collision, stop movement
    //     else {
    //         movementVelocityX = 0.0f;
    //         movementVelocityZ = 0.0f;
    //     }
    // } else {
        // No collision, apply movement
        character.pos[0] = newPosX;
        character.pos[2] = newPosZ;
    //}

    character_anim_apply_pose();
    character_finalize_frame(true);
}

void character_update_position(void) 
{
	// Update the full transformation matrix with scale, rotation, and position
    t3d_mat4fp_from_srt_euler(character.modelMat,
        (float[3]){character.scale[0], character.scale[1], character.scale[2]},
        (float[3]){character.rot[0], character.rot[1] + MODEL_YAW_OFFSET, character.rot[2]},
        (float[3]){character.pos[0], character.pos[1], character.pos[2]}
    );
	character_update_shadow_mat();
}

/* Third-person camera follow with smoothing. Distances scaled to character size. */
void character_update_camera(void)
{
    static bool lastLockOnActive = false;

    float scaledDistance = cameraDistance * 0.04f;
    float scaledHeight = cameraHeight * 0.03f;

    // When exiting Z-targeting, adopt the current camera offset as the new manual angle
    bool unlockingFromLockOn = lastLockOnActive && !cameraLockOnActive && cameraLockBlend > 0.001f;
    if (unlockingFromLockOn && scaledDistance > 0.0f) {
        T3DVec3 offsetFromCharacter = {{
            characterCamPos.v[0] - character.pos[0],
            characterCamPos.v[1] - character.pos[1],
            characterCamPos.v[2] - character.pos[2]
        }};

        float sinY = (offsetFromCharacter.v[1] - scaledHeight) / scaledDistance;
        if (sinY < -1.0f) sinY = -1.0f;
        if (sinY > 1.0f) sinY = 1.0f;
        cameraAngleY = asinf(sinY);

        float cosY = fm_cosf(cameraAngleY);
        if (cosY < 0.0001f) cosY = 0.0001f; // avoid division by zero
        cameraAngleX = atan2f(offsetFromCharacter.v[0] / cosY, offsetFromCharacter.v[2] / cosY);

        if (cameraAngleY < cameraMinY) cameraAngleY = cameraMinY;
        if (cameraAngleY > cameraMaxY) cameraAngleY = cameraMaxY;
    }
    
    float cosX = fm_cosf(cameraAngleX);
    float sinX = fm_sinf(cameraAngleX);
    float cosY = fm_cosf(cameraAngleY);
    float sinY = fm_sinf(cameraAngleY);
    
    float offsetX = scaledDistance * sinX * cosY;
    float offsetY = scaledHeight + (scaledDistance * sinY);
    float offsetZ = scaledDistance * cosX * cosY;
    
    T3DVec3 desiredCamPos = {{
        character.pos[0] + offsetX,
        character.pos[1] + offsetY,
        character.pos[2] + offsetZ
    }};

    // For Z-targeting, place camera behind the character relative to the target
    if (cameraLockOnActive) {
        // Direction from character to target
        T3DVec3 toTarget = {{
            cameraLockOnTarget.v[0] - character.pos[0],
            cameraLockOnTarget.v[1] - character.pos[1],
            cameraLockOnTarget.v[2] - character.pos[2]
        }};
        t3d_vec3_norm(&toTarget);

        // Camera sits behind character along opposite of toTarget, plus height
        desiredCamPos.v[0] = character.pos[0] - toTarget.v[0] * scaledDistance;
        desiredCamPos.v[1] = character.pos[1] + (scaledHeight/2);
        desiredCamPos.v[2] = character.pos[2] - toTarget.v[2] * scaledDistance;
    }
    
    if (deltaTime > 0.0f) {
        vec3_lerp(&characterCamPos, &characterCamPos, &desiredCamPos, cameraLerpSpeed * deltaTime);
    } else {
        characterCamPos = desiredCamPos;
    }
    
    // Default follow target (character focus point)
    T3DVec3 followTarget = {{
        character.pos[0],
        character.pos[1] + 15.0f,
        character.pos[2]
    }};

    // Forward-ahead target to keep camera looking behind character when Z-targeting
    float yaw = character.rot[1];
    float fwdX = -fm_sinf(yaw);
    float fwdZ =  fm_cosf(yaw);
    T3DVec3 forwardTarget = {{
        character.pos[0] + fwdX * 2.0f,
        character.pos[1] + 1.5f,
        character.pos[2] + fwdZ * 2.0f
    }};

    // Progress lock blend towards active state
    float blendSpeed = cameraLerpSpeed; // reuse camera smoothing speed
    // Progress blend toward lock-on when active
    float targetBlend = cameraLockOnActive ? 1.0f : 0.0f;
    if (deltaTime > 0.0f)
    {
        // simple exponential-like approach via lerp
        float step = blendSpeed * deltaTime;
        if (step > 1.0f) step = 1.0f;
        cameraLockBlend = (1.0f - step) * cameraLockBlend + step * targetBlend;
    }
    else
    {
        cameraLockBlend = targetBlend;
    }

    // Compute desired target: blend between forward-ahead and lock-on target
    T3DVec3 desiredTarget;
    if (cameraLockOnActive) {
        // Bias keeps camera mostly behind character while still locking-on
        const float lockBias = 0.35f; // 0=ignore lock, 1=full lock
        vec3_lerp(&desiredTarget, &forwardTarget, &cameraLockOnTarget, lockBias);
    } else {
        vec3_lerp(&desiredTarget, &followTarget, &cameraLockOnTarget, cameraLockBlend);
    }

    // Smooth target movement
    if (deltaTime > 0.0f) {
        vec3_lerp(&characterCamTarget, &characterCamTarget, &desiredTarget, cameraLerpSpeed * deltaTime);
    } else {
        characterCamTarget = desiredTarget;
    }

    lastLockOnActive = cameraLockOnActive;
}

// ==== Drawing Functions ====

// Draw only the shadow - should be called in a batched shadow pass with zbuf(true, false)
void character_draw_shadow(void)
{
    if (!character.visible) return;
    if (!character.dpl_shadow || !character.shadowMat) return;

    float h = character.pos[1] - SHADOW_GROUND_Y;
    if (h < 0.0f) h = 0.0f;
    float t = h / JUMP_HEIGHT;
    if (t > 1.0f) t = 1.0f;

    // Smooth fade: (1 - t)^2
    float fade = 1.0f - t;
    fade *= fade;
    uint8_t a = (uint8_t)(SHADOW_BASE_ALPHA * fade);

    if (a > 0) {
        rdpq_set_prim_color(RGBA32(0, 0, 0, a));
        t3d_matrix_set(character.shadowMat, true);
        rspq_block_run(character.dpl_shadow);
    }
}

void character_draw(void) 
{
	if (!character.visible) return;

	// Shadow is now drawn separately via character_draw_shadow() in a batched pass
	// This avoids expensive mode changes per character

	// --- Character ---
    // Set prim color with optional red flash on recent damage
    uint8_t cr = 255, cg = 255, cb = 255, ca = 255;
    if (character.damageFlashTimer > 0.0f) {
        float f = character.damageFlashTimer / 0.3f;
        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;
        cg = (uint8_t)(255.0f * (1.0f - f));
        cb = (uint8_t)(255.0f * (1.0f - f));
    }
    rdpq_set_prim_color(RGBA32(cr, cg, cb, ca));

    t3d_matrix_set(character.modelMat, true);
    rspq_block_run(character.dpl_model);
}

void character_draw_ui(void)
{
	// Draw simple bottom-right health bar
	float ratio = character.maxHealth > 0.0f ? fmaxf(0.0f, fminf(1.0f, character.health / character.maxHealth)) : 0.0f;
	float flash = 0.0f;
	if (character.damageFlashTimer > 0.0f) {
		flash = fminf(1.0f, character.damageFlashTimer / 0.3f);
		character.damageFlashTimer -= deltaTime;
		if (character.damageFlashTimer < 0.0f) character.damageFlashTimer = 0.0f;
	}
	draw_player_health_bar("Player", ratio, flash);
}

void character_apply_damage(float amount)
{
	if (amount <= 0.0f) return;

    // Grant temporary invulnerability while rolling (dodge)
    if (character_is_invulnerable()) {
        return;
    }

    character.health -= amount;
	if (character.health < 0.0f) character.health = 0.0f;
	// printf("[Character] took %.1f damage (%.1f/%.1f)\n", amount, character.health, character.maxHealth);
	if (character.health <= 0.0f) {
		// printf("[Character] HP: %.0f/%.0f - DEFEATED!\n", character.health, character.maxHealth);
		scene_set_game_state(GAME_STATE_DEAD);
    } else {
        // Small knockback when taking significant damage
        if (amount >= 10.0f && characterState != CHAR_STATE_ROLLING) {
            characterState = CHAR_STATE_KNOCKDOWN;
            actionTimer = 0.0f;
            currentActionDuration = KNOCKDOWN_DURATION;
            // backward impulse
            float yaw = character.rot[1];
            float bx = fm_sinf(yaw);  // opposite of forward
            float bz = -fm_cosf(yaw);
            movementVelocityX += bx * KNOCKDOWN_BACK_IMPULSE;
            movementVelocityZ += bz * KNOCKDOWN_BACK_IMPULSE;

            // Use immediate animation switch for instant reaction
            switch_to_action_animation_immediate(ANIM_KNOCKDOWN);
        }
    }
	character.damageFlashTimer = 0.3f;
}

void character_delete(void)
{
    rspq_wait();

    t3d_model_free(characterModel);
    if (characterShadowModel) {
        t3d_model_free(characterShadowModel);
        characterShadowModel = NULL;
    }

    free_if_not_null(character.scrollParams);

    if (character.skeleton) 
    {
        t3d_skeleton_destroy(character.skeleton);
        free(character.skeleton);
        character.skeleton = NULL;
    }

    if (character.skeletonBlend) 
    {
        t3d_skeleton_destroy(character.skeletonBlend);
        free(character.skeletonBlend);
        character.skeletonBlend = NULL;
    }

    if (character.animations) 
    {
        for (int i = 0; i < character.animationCount; i++) 
        {
            if (character.animations[i]) 
            {
                t3d_anim_destroy(character.animations[i]);
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

    if (character.shadowMat)
    {
        rspq_wait();
        free_uncached(character.shadowMat);
        character.shadowMat = NULL;
    }

    if (character.dpl_model)
    {
        rspq_wait();
        rspq_block_free(character.dpl_model);
        character.dpl_model = NULL;
    }

    if (character.dpl_shadow)
    {
        rspq_wait();
        rspq_block_free(character.dpl_shadow);
        character.dpl_shadow = NULL;
    }
}