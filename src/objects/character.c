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
#include "utilities/sword_trail.h"

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
static int   characterSwordBoneIndex = -1;       // cached bone index for sword/hand
static const float SWORD_LENGTH = 640.0f;        // local-space length of sword capsule segment
static const float SWORD_COLLIDER_RADIUS = 5.0f; // sword collider radius in world units

// Shadow tuning
static const float SHADOW_GROUND_Y = 4.0f;        // Match roomY floor level
static const float SHADOW_BASE_ALPHA = 120.0f;    // alpha when on the ground
static const float SHADOW_SHRINK_AMOUNT = 0.45f;  // 0=no shrink, 0.45 -> 55% size at peak

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
static const float MAX_MOVEMENT_SPEED = 60.0f;
static const float SPEED_BUILDUP_RATE = 1.5f;
static const float SPEED_DECAY_RATE = 4.0f;

static const float ROLL_DURATION = 0.9f;
static const float ROLL_ANIM_SPEED = 1.0f;
static const float ATTACK_DURATION = 0.9f; // (legacy, not used directly)
static const float STRONG_ATTACK_DURATION = 1.2f;
static const float STRONG_ATTACK_HOLD_THRESHOLD = 0.4f;
static const float STRONG_ATTACK_DAMAGE = 20.0f;
static const float STRONG_ATTACK_HIT_START = 0.35f;
static const float STRONG_ATTACK_HIT_END = 0.9f;
static const float JUMP_DURATION = 0.75f; // unused (jump removed)
static const float JUMP_HEIGHT = 40.0f;   // retained for shadow math
static const float ROLL_SPEED = MAX_MOVEMENT_SPEED;
static const float ROLL_STEER_ACCELERATION = 14.0f;
static const float ROLL_FRICTION_SCALE = 0.6f;
static const float STRONG_ATTACK_FRICTION_SCALE = 0.25f; // (currently unused)

// Combo attack tuning - Souls-like windows
static float ATTACK1_DURATION = 0.9f;
static float ATTACK2_DURATION = 0.9f;
static float ATTACK3_DURATION = 0.9f;
static float ATTACK4_DURATION = 1.0f;
static float ATTACK_END_DURATION = 0.6f;

// Souls-like attack windows
static const float ATTACK_QUEUE_OPEN = 0.45f;
static const float ATTACK_QUEUE_CLOSE = 0.90f;
static const float ATTACK_TRANSITION_TIME = 0.92f;
static const float ATTACK_CROSSFADE_DURATION = 0.08f;

static const float ATTACK_FORWARD_IMPULSE = 35.0f;
static const float KNOCKDOWN_DURATION = 0.8f;
static const float KNOCKDOWN_BACK_IMPULSE = 25.0f;
static const float KNOCKDOWN_MAX_STUN_SECONDS = 2.0f;

// Input state tracking
static bool lastBPressed = false;
static bool lastAPressed = false;
static bool leftTriggerHeld = false;
static float leftTriggerHoldTime = 0.0f;

// Input and tuning constants
static const float STICK_MAX = 80.0f;
static const float INPUT_DEADZONE = 0.12f;

// Strafe activation tuning (to avoid accidental strafing on resting sticks)
static const float STRAFE_LATERAL_AXIS_DEADZONE = 0.08f;
static const float STRAFE_ACTIVATION_RATIO     = 0.22f;
static const float STRAFE_DEACTIVATION_RATIO   = 0.12f;

static const float TURN_RATE = 8.0f;
static const float IDLE_THRESHOLD = 0.001f;
static const float WALK_THRESHOLD = 0.03f;
static const float RUN_THRESHOLD = 0.7f;
static const float BLEND_MARGIN = 0.2f;         // (currently unused)
static const float ATTACK_FRICTION_SCALE = 0.3f;

static bool walkThroughFog = false;

// Attack combo state
static int attackComboIndex = 0;        // 0=none, 1..4
static bool attackQueued = false;
static bool attackEnding = false;
static float currentActionDuration = 1.0f;

// Animation selection helpers for lock-on strafing
static bool animLockOnStrafingFlag = false;
static int  animStrafeDirFlag = 0;          // -1 left, +1 right
static float animStrafeBlendRatio = 0.0f;   // 0 run, 1 strafe

static CharacterState prevState = CHAR_STATE_NORMAL;

// Character SFX state
static float footstepTimer = 0.0f;
static const float FOOTSTEP_WALK_INTERVAL = 0.45f;
static const float FOOTSTEP_RUN_INTERVAL  = 0.28f;

/* -----------------------------------------------------------------------------
 * Animation driver state (single source of truth)
 * -------------------------------------------------------------------------- */

// Lock-on blend “drivers”
static int activeMainAnim  = -1; // clip attached to character.skeleton
static int activeBlendAnim = -1; // clip attached to character.skeletonBlend
static int lastBaseAnimLock   = -1;
static int lastStrafeAnimLock = -1;

// Attach/speed caches for currentAnim updates
static int   lastAttachedMain  = -1;
static int   lastAttachedBlend = -1;
static float lastAnimSpeed     = -1.0f;

// Lock-on speed caches
static float lastBaseSpeed   = -1.0f;
static float lastStrafeSpeed = -1.0f;

// Strong-attack upgrade flag
static bool strongAttackUpgradedFlag = false;


static bool hasBlendSnapshot = false;

// Lock-on strafe exit fade (prevents strafe->idle snap)
static float lockonStrafeExitT = 0.0f;
static int   lockonLastDir     = 0;     // last nonzero dir (-1/+1)
static float lockonLastW       = 0.0f;  // last blend weight

// Locomotion crossfade tuning
static const float LOCOMOTION_CROSSFADE_DURATION = 0.10f; // tune 0.06–0.14

// (optional) avoid blending every micro-switch when speed jitters
static const float LOCOMOTION_MIN_SWITCH_INTERVAL = 0.00f; // set to 0.06f if you want
static float locomotionSwitchCooldown = 0.0f;

/* -----------------------------------------------------------------------------
 * Local helpers
 * -------------------------------------------------------------------------- */

static inline void anim_stop_all_except(T3DAnim** set, int count, int keepIdx)
{
    if (!set) return;
    for (int i = 0; i < count; i++) {
        if (!set[i]) continue;
        if (i == keepIdx) continue;
        t3d_anim_set_playing(set[i], false);
    }
}

static inline void anim_stop(T3DAnim** set, int idx)
{
    if (!set) return;
    if (idx < 0 || idx >= character.animationCount) return;
    if (!set[idx]) return;
    t3d_anim_set_playing(set[idx], false);
}

static inline void kill_lockon_drivers(void)
{
    if (activeMainAnim != -1) {
        anim_stop(character.animations, activeMainAnim);
        activeMainAnim = -1;
    }
    if (activeBlendAnim != -1) {
        anim_stop(character.animations, activeBlendAnim);
        activeBlendAnim = -1;
    }

    lastBaseAnimLock   = -1;
    lastStrafeAnimLock = -1;
    lastBaseSpeed      = -1.0f;
    lastStrafeSpeed    = -1.0f;

    // CRITICAL: lock-on path attaches clips directly; invalidate attach/speed caches
    lastAttachedMain  = -1;
    lastAttachedBlend = -1;
    lastAnimSpeed     = -1.0f;

    lockonStrafeExitT = 0.0f;
    lockonLastDir = 0;
    lockonLastW = 0.0f;
}


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

// Copy pose by copying bone matrices (safe-ish cap, same model clone)
static inline void skeleton_copy_pose(T3DSkeleton* dest, T3DSkeleton* src)
{
    if (!dest || !src) return;

    t3d_skeleton_update(src);

    T3DSkeleton* dest_sk = (T3DSkeleton*)dest;
    T3DSkeleton* src_sk  = (T3DSkeleton*)src;

    if (!dest_sk->boneMatricesFP || !src_sk->boneMatricesFP) return;

    const int MAX_BONES = 64;
    for (int i = 0; i < MAX_BONES; i++) {
        dest_sk->boneMatricesFP[i] = src_sk->boneMatricesFP[i];
    }
}

// Copy pose by copying bone SRT (THIS is what t3d_skeleton_blend uses)
static inline void skeleton_copy_pose_bones(T3DSkeleton* dest, const T3DSkeleton* src)
{
    if (!dest || !src) return;
    if (!dest->bones || !src->bones) return;
    if (!dest->skeletonRef || !src->skeletonRef) return;

    // Assuming both skeletons are clones of the same model skeleton
    const int count = dest->skeletonRef->boneCount;

    for (int i = 0; i < count; i++) {
        dest->bones[i].position = src->bones[i].position;
        dest->bones[i].rotation = src->bones[i].rotation;
        dest->bones[i].scale    = src->bones[i].scale;

        // mark changed so if someone ever updates matrices, they rebuild
        dest->bones[i].hasChanged = true;
    }
}

static inline bool is_locomotion_anim(int a)
{
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
        default:
            return false;
    }
}

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

/* -----------------------------------------------------------------------------
 * Shadow + transform
 * -------------------------------------------------------------------------- */

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

    float h = character.pos[1] - SHADOW_GROUND_Y;
    if (h < 0.0f) h = 0.0f;

    float t = h / JUMP_HEIGHT;
    if (t > 1.0f) t = 1.0f;

    float shrink = 1.0f - SHADOW_SHRINK_AMOUNT * t;

    float shadowPos[3]   = { character.pos[0], SHADOW_GROUND_Y, character.pos[2] };
    float shadowRot[3]   = { 0.0f, 0.0f, 0.0f };
    float shadowScale[3] = {
        character.scale[0] * 2.0f * shrink,
        character.scale[1],
        character.scale[2] * 2.0f * shrink
    };

    t3d_mat4fp_from_srt_euler(character.shadowMat, shadowScale, shadowRot, shadowPos);
}

/* -----------------------------------------------------------------------------
 * Input/movement helpers
 * -------------------------------------------------------------------------- */

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

    animLockOnStrafingFlag = false;
    animStrafeDirFlag = 0;
    animStrafeBlendRatio = 0.0f;

    // Reset lock-on/attach caches
    kill_lockon_drivers();
    lastAttachedMain  = -1;
    lastAttachedBlend = -1;
    lastAnimSpeed     = -1.0f;

    footstepTimer = 0.0f;

    character.health = character.maxHealth;
    character.damageFlashTimer = 0.0f;
    character.currentAttackHasHit = false;

    strongAttackUpgradedFlag = false;

    sword_trail_reset();
}

void character_reset_button_state(void)
{
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

void character_set_velocity_xz(float vx, float vz)
{
    movementVelocityX = vx;
    movementVelocityZ = vz;
}

static inline StickInput normalize_stick(float rawX, float rawY)
{
    StickInput s;

    float ix = fmaxf(-1.0f, fminf(1.0f, rawX / STICK_MAX));
    float iy = fmaxf(-1.0f, fminf(1.0f, rawY / STICK_MAX));

    float m = sqrtf(ix * ix + iy * iy);
    m = fminf(1.0f, m);

    if (m < INPUT_DEADZONE) {
        s.x = 0.0f; s.y = 0.0f; s.magnitude = 0.0f;
        return s;
    }

    float scale = (m - INPUT_DEADZONE) / (1.0f - INPUT_DEADZONE);
    scale = fminf(1.0f, scale);

    if (m > 0.0f) {
        ix = (ix / m) * scale;
        iy = (iy / m) * scale;
        m = scale;
    }

    s.x = ix;
    s.y = iy;
    s.magnitude = m;
    return s;
}

static inline void compute_camera_vectors(float yaw, float* fwdX, float* fwdZ, float* rightX, float* rightZ)
{
    *fwdX = -fm_sinf(yaw);
    *fwdZ = -fm_cosf(yaw);
    *rightX = fm_cosf(yaw);
    *rightZ = -fm_sinf(yaw);
}

static inline void compute_desired_velocity(float inputX, float inputY, float yaw, float* outX, float* outZ)
{
    float fx, fz, rx, rz;
    compute_camera_vectors(yaw, &fx, &fz, &rx, &rz);
    *outX = fx * inputY + rx * inputX;
    *outZ = fz * inputY + rz * inputX;
}

static inline void compute_desired_velocity_lockon(float inputX, float inputY, const T3DVec3* toTarget, float* outX, float* outZ)
{
    float fwdX = toTarget->v[0];
    float fwdZ = toTarget->v[2];
    float len = sqrtf(fwdX*fwdX + fwdZ*fwdZ);
    if (len > 1e-5f) { fwdX /= len; fwdZ /= len; } else { fwdX = 0.0f; fwdZ = 1.0f; }

    float rightX = -fwdZ;
    float rightZ =  fwdX;

    *outX = fwdX * inputY + rightX * inputX;
    *outZ = fwdZ * inputY + rightZ * inputX;
}

/* -----------------------------------------------------------------------------
 * Sword segment helper (for trail)
 * -------------------------------------------------------------------------- */

static inline bool character_sword_world_segment(float outBase[3], float outTip[3])
{
    if (characterSwordBoneIndex < 0 || !character.skeleton || !character.modelMat) return false;

    T3DSkeleton *sk = (T3DSkeleton*)character.skeleton;
    const T3DMat4FP *B = &sk->boneMatricesFP[characterSwordBoneIndex];
    const T3DMat4FP *M = (const T3DMat4FP*)character.modelMat;

    const float p0_local[3] = { 0.0f, 0.0f, 0.0f };
    const float p1_local[3] = { -SWORD_LENGTH, 0.0f, 0.0f };

    float p0_model[3], p1_model[3];
    mat4fp_mul_point_f32_row3_colbasis(B, p0_local, p0_model);
    mat4fp_mul_point_f32_row3_colbasis(B, p1_local, p1_model);

    mat4fp_mul_point_f32_row3_colbasis(M, p0_model, outBase);
    mat4fp_mul_point_f32_row3_colbasis(M, p1_model, outTip);
    return true;
}

/* -----------------------------------------------------------------------------
 * Combat hit test
 * -------------------------------------------------------------------------- */

// ===== PLEASE DONT USE THE FIXED POINT API =====
// NOTE: CHECK collision_system.c for a demo
static inline bool attack_hit_test(void)
{
    Boss* boss = boss_get_instance();
    if (!boss) return false;

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

    if (characterSwordBoneIndex >= 0 && character.skeleton && character.modelMat) {
        T3DSkeleton *sk = (T3DSkeleton*)character.skeleton;
        const T3DMat4FP *B = &sk->boneMatricesFP[characterSwordBoneIndex];
        const T3DMat4FP *M = (const T3DMat4FP*)character.modelMat;

        const float p0_local[3] = { 0.0f, 0.0f, 0.0f };
        const float p1_local[3] = { -SWORD_LENGTH, 0.0f, 0.0f };

        float p0_model[3], p1_model[3];
        mat4fp_mul_point_f32_row3_colbasis(B, p0_local, p0_model);
        mat4fp_mul_point_f32_row3_colbasis(B, p1_local, p1_model);

        float p0_world[3], p1_world[3];
        mat4fp_mul_point_f32_row3_colbasis(M, p0_model, p0_world);
        mat4fp_mul_point_f32_row3_colbasis(M, p1_model, p1_world);

        attackHit = scu_capsule_vs_capsule_f(p0_world, p1_world, SWORD_COLLIDER_RADIUS, bossCapA, bossCapB, bossRadius);
    }

    if (!attackHit) {
        float yaw = character.rot[1];
        float reachStart = 1.0f;
        float reachEnd = 2.5f;
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

/* -----------------------------------------------------------------------------
 * Actions/state
 * -------------------------------------------------------------------------- */

static inline void clear_lockon_strafe_flags_on_action(void)
{
    animStrafeDirFlag = 0;
    animLockOnStrafingFlag = false;
    animStrafeBlendRatio = 0.0f;

    // If we leave locomotion blending, wipe the lock-on anim identity too
    lastBaseAnimLock = -1;
    lastStrafeAnimLock = -1;
}

static inline bool can_roll_now(const joypad_buttons_t* buttons, const StickInput* stick)
{
    (void)stick;
    if (!(buttons->a && characterState == CHAR_STATE_NORMAL)) return false;
    return true;
}

static inline void try_start_roll(const joypad_buttons_t* buttons, const StickInput* stick)
{
    if (!can_roll_now(buttons, stick)) return;

    characterState = CHAR_STATE_ROLLING;
    actionTimer = 0.0f;
    currentActionDuration = ROLL_DURATION;
    clear_lockon_strafe_flags_on_action();

    if (ANIM_ROLL >= 0 && ANIM_ROLL < character.animationCount && character.animations[ANIM_ROLL]) {
        t3d_anim_set_time(character.animations[ANIM_ROLL], 0.0f);
        t3d_anim_set_playing(character.animations[ANIM_ROLL], true);
    }

    if (stick->magnitude <= 0.1f) {
        float yaw = character.rot[1];
        float fx = -fm_sinf(yaw);
        float fz =  fm_cosf(yaw);
        movementVelocityX = fx * (ROLL_SPEED * 0.8f);
        movementVelocityZ = fz * (ROLL_SPEED * 0.8f);
    }
}

static inline float get_attack_duration(int comboIndex)
{
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

static inline void try_start_attack(bool leftJustPressed)
{
    if (!leftJustPressed) return;

    if (characterState == CHAR_STATE_NORMAL) {
        characterState = CHAR_STATE_ATTACKING;
        attackComboIndex = 1;
        attackQueued = false;
        attackEnding = false;
        actionTimer = 0.0f;

        currentActionDuration = get_attack_duration(1);
        character.currentAttackHasHit = false;

        float yaw = character.rot[1];
        float fx = -fm_sinf(yaw);
        float fz =  fm_cosf(yaw);
        movementVelocityX += fx * ATTACK_FORWARD_IMPULSE;
        movementVelocityZ += fz * ATTACK_FORWARD_IMPULSE;

        character_play_swing();
    } else if (characterState == CHAR_STATE_ATTACKING && !attackEnding) {
        if (actionTimer >= ATTACK_QUEUE_OPEN && actionTimer <= ATTACK_QUEUE_CLOSE) {
            attackQueued = true;
        }
    }
}

static inline void upgrade_to_strong_attack(bool leftHeldNow)
{
    if (characterState == CHAR_STATE_ATTACKING &&
        leftHeldNow &&
        leftTriggerHoldTime >= STRONG_ATTACK_HOLD_THRESHOLD &&
        actionTimer < 0.3f &&
        !attackEnding &&
        !strongAttackUpgradedFlag) {

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

static inline void progress_action_timers(float dt)
{
    if (characterState == CHAR_STATE_NORMAL) return;
    if (currentActionDuration <= 0.0001f) currentActionDuration = 1.0f;

    actionTimer += dt / currentActionDuration;

    if (characterState == CHAR_STATE_ROLLING) {
        T3DAnim* rollAnim = NULL;
        if (ANIM_ROLL >= 0 && ANIM_ROLL < character.animationCount) {
            rollAnim = character.animations[ANIM_ROLL];
        }
        if (actionTimer > 0.05f && rollAnim && !rollAnim->isPlaying) {
            characterState = CHAR_STATE_NORMAL;
            actionTimer = 0.0f;
        } else if (actionTimer >= 2.0f) {
            characterState = CHAR_STATE_NORMAL;
            actionTimer = 0.0f;
        }
    } else if (characterState == CHAR_STATE_ATTACKING) {
        if (!attackEnding && attackQueued && actionTimer >= ATTACK_TRANSITION_TIME && attackComboIndex < 4) {
            attackComboIndex++;
            attackQueued = false;

            actionTimer = 0.0f;
            currentActionDuration = get_attack_duration(attackComboIndex);
            character.currentAttackHasHit = false;

            float yaw = character.rot[1];
            float fx = -fm_sinf(yaw);
            float fz =  fm_cosf(yaw);
            movementVelocityX += fx * ATTACK_FORWARD_IMPULSE;
            movementVelocityZ += fz * ATTACK_FORWARD_IMPULSE;

        } else if (!attackEnding && actionTimer >= 1.0f) {
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
        } else if (actionTimer >= (KNOCKDOWN_MAX_STUN_SECONDS / currentActionDuration)) {
            characterState = CHAR_STATE_NORMAL;
            actionTimer = 0.0f;
        }
    }
}

static inline void update_actions(const joypad_buttons_t* buttons,
                                  bool leftHeldNow,
                                  bool leftJustPressed,
                                  bool jumpJustPressed,
                                  const StickInput* stick,
                                  float dt)
{
    (void)jumpJustPressed;
    try_start_roll(buttons, stick);
    try_start_attack(leftJustPressed);
    upgrade_to_strong_attack(leftHeldNow);
    progress_action_timers(dt);
}

static inline bool character_is_invulnerable(void)
{
    return (characterState == CHAR_STATE_ROLLING) ||
           (characterState == CHAR_STATE_KNOCKDOWN);
}

static inline void accelerate_towards(float desiredX, float desiredZ, float maxSpeed, float dt)
{
    movementVelocityX += (desiredX * maxSpeed - movementVelocityX) * MOVEMENT_ACCELERATION * dt;
    movementVelocityZ += (desiredZ * maxSpeed - movementVelocityZ) * MOVEMENT_ACCELERATION * dt;
}

static inline void accelerate_towards_with_accel(float desiredX, float desiredZ, float maxSpeed, float accel, float dt)
{
    movementVelocityX += (desiredX * maxSpeed - movementVelocityX) * accel * dt;
    movementVelocityZ += (desiredZ * maxSpeed - movementVelocityZ) * accel * dt;
}

static inline void apply_friction(float dt, float scale)
{
    float k = MOVEMENT_FRICTION * fmaxf(0.0f, scale);
    movementVelocityX *= expf(-k * dt);
    movementVelocityZ *= expf(-k * dt);

    if (fabsf(movementVelocityX) < 0.001f) movementVelocityX = 0.0f;
    if (fabsf(movementVelocityZ) < 0.001f) movementVelocityZ = 0.0f;
}

static inline void update_yaw_from_velocity(float dt)
{
    if (fabsf(movementVelocityX) <= 0.1f && fabsf(movementVelocityZ) <= 0.1f) return;

    float targetAngle = atan2f(-movementVelocityX, movementVelocityZ);
    float currentAngle = character.rot[1];

    while (targetAngle >  T3D_PI) targetAngle -= 2.0f * T3D_PI;
    while (targetAngle < -T3D_PI) targetAngle += 2.0f * T3D_PI;
    while (currentAngle >  T3D_PI) currentAngle -= 2.0f * T3D_PI;
    while (currentAngle < -T3D_PI) currentAngle += 2.0f * T3D_PI;

    float angleDelta = targetAngle - currentAngle;
    while (angleDelta >  T3D_PI) angleDelta -= 2.0f * T3D_PI;
    while (angleDelta < -T3D_PI) angleDelta += 2.0f * T3D_PI;

    float maxTurnRate = TURN_RATE * dt;
    if (angleDelta >  maxTurnRate) angleDelta =  maxTurnRate;
    if (angleDelta < -maxTurnRate) angleDelta = -maxTurnRate;

    character.rot[1] = currentAngle + angleDelta;
}

static inline void update_current_speed(float inputMagnitude, float dt)
{
    if (inputMagnitude > 0.0f) {
        currentSpeed += SPEED_BUILDUP_RATE * dt;
        currentSpeed = fminf(currentSpeed, inputMagnitude);
    } else {
        currentSpeed -= SPEED_DECAY_RATE * dt;
        currentSpeed = fmaxf(currentSpeed, 0.0f);
    }
}

/* -----------------------------------------------------------------------------
 * Animation selection + application
 * -------------------------------------------------------------------------- */

static inline int get_target_animation(CharacterState state, float speedRatio)
{
    if (state == CHAR_STATE_DEAD) return ANIM_DEATH;
    if (state == CHAR_STATE_TITLE_IDLE) return ANIM_IDLE_TITLE;
    if (state == CHAR_STATE_FOG_WALK) return ANIM_FOG_OF_WAR;
    if (state == CHAR_STATE_KNOCKDOWN) return ANIM_KNOCKDOWN;
    if (state == CHAR_STATE_ROLLING) return ANIM_ROLL;

    if (state == CHAR_STATE_ATTACKING || state == CHAR_STATE_ATTACKING_STRONG) {
        if (attackEnding) {
            if (attackComboIndex == 1) return ANIM_ATTACK1_END;
            if (attackComboIndex == 2) return ANIM_ATTACK2_END;
            if (attackComboIndex == 3) return ANIM_ATTACK3_END;
        } else {
            if (state == CHAR_STATE_ATTACKING_STRONG) return ANIM_ATTACK_CHARGED;
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

    // In lock-on, locomotion blending is handled separately
    if (cameraLockOnActive && ((animStrafeDirFlag != 0) || (lockonStrafeExitT > 0.0f))) {
        return ANIM_IDLE;
    }

    // Determine if moving backwards relative to facing
    float yaw = character.rot[1];
    float fwdX = -fm_sinf(yaw);
    float fwdZ =  fm_cosf(yaw);
    float dotForward = movementVelocityX * fwdX + movementVelocityZ * fwdZ;
    bool isBackward = (dotForward < -0.001f);

    if (speedRatio < IDLE_THRESHOLD) return ANIM_IDLE;
    if (speedRatio < WALK_THRESHOLD) return isBackward ? ANIM_WALK_BACK : ANIM_WALK;
    if (speedRatio < RUN_THRESHOLD)  return isBackward ? ANIM_WALK_BACK : ANIM_WALK;
    return isBackward ? ANIM_RUN_BACK : ANIM_RUN;
}

static inline bool is_action_state(CharacterState state)
{
    return (state == CHAR_STATE_ROLLING) ||
           (state == CHAR_STATE_ATTACKING) ||
           (state == CHAR_STATE_ATTACKING_STRONG) ||
           (state == CHAR_STATE_KNOCKDOWN) ||
           (state == CHAR_STATE_DEAD);
}

static inline bool try_lockon_locomotion_blend(float speedRatio, CharacterState state, float dt)
{
    if (is_action_state(state)) return false;
    if (character.isBlending) return false;
    if (!(state == CHAR_STATE_NORMAL && cameraLockOnActive && character.animations)) return false;

    // Allow the lock-on blend path to keep running briefly while we fade strafe out.
    const bool wantLockonBlend = (animStrafeDirFlag != 0) || (lockonStrafeExitT > 0.0f);
    if (!wantLockonBlend) return false;

    // Fade-out strafe weight if we're exiting
    if (lockonStrafeExitT > 0.0f) {
        lockonStrafeExitT -= dt;
        if (lockonStrafeExitT < 0.0f) lockonStrafeExitT = 0.0f;

        const float EXIT_DUR = 0.10f; // must match what you set when starting fade
        float t = (EXIT_DUR > 0.0f) ? (lockonStrafeExitT / EXIT_DUR) : 0.0f;
        animStrafeBlendRatio = lockonLastW * t;

        if (animStrafeBlendRatio <= 0.0001f) {
            animStrafeBlendRatio = 0.0f;
            animStrafeDirFlag = 0;
            lockonLastDir = 0;
            lockonLastW = 0.0f;
        }
    }

    // Compute backward relative to facing
    float yaw = character.rot[1];
    float fwdX = -fm_sinf(yaw);
    float fwdZ =  fm_cosf(yaw);
    float dotForward = movementVelocityX * fwdX + movementVelocityZ * fwdZ;
    bool isBackward = (dotForward < -0.001f);

    // Decide whether we're effectively idle (velocity-based)
    const float LOCKON_IDLE_VEL = 1.5f; // tune
    float velMag = sqrtf(movementVelocityX * movementVelocityX +
                         movementVelocityZ * movementVelocityZ);
    const bool isIdle = (velMag <= LOCKON_IDLE_VEL) || (speedRatio < IDLE_THRESHOLD);

    // IMPORTANT: don't swap walk/run clips in lock-on.
    // Use idle when idle, otherwise always use RUN variants and scale speed.
    int baseAnim = isIdle ? ANIM_IDLE
                          : (isBackward ? ANIM_RUN_BACK : ANIM_RUN);

    int strafeAnim;
    if (isIdle || animStrafeDirFlag == 0) {
        strafeAnim = ANIM_IDLE;
    } else {
        strafeAnim = (animStrafeDirFlag > 0) ? ANIM_STRAFE_RUN_RIGHT : ANIM_STRAFE_RUN_LEFT;
    }

    // Keep only baseAnim running on main skeleton
    anim_stop_all_except(character.animations, character.animationCount, baseAnim);

    bool baseChanged   = (lastBaseAnimLock   != baseAnim);
    bool strafeChanged = (lastStrafeAnimLock != strafeAnim);

    if (baseChanged) {
        if (activeMainAnim != -1 && activeMainAnim != baseAnim) {
            anim_stop(character.animations, activeMainAnim);
        }
        activeMainAnim = baseAnim;

        // restart on change is fine here because we eliminated the fast walk/run swapping
        anim_bind_and_play(character.animations, baseAnim, character.skeleton, true, true);

        lastAttachedMain = baseAnim;
        lastBaseAnimLock = baseAnim;
    } else {
        if (character.animations[baseAnim] && !character.animations[baseAnim]->isPlaying) {
            t3d_anim_set_looping(character.animations[baseAnim], true);
            t3d_anim_set_playing(character.animations[baseAnim], true);
        }
    }

    if (strafeChanged) {
        if (activeBlendAnim != -1 && activeBlendAnim != strafeAnim) {
            anim_stop(character.animations, activeBlendAnim);
        }
        activeBlendAnim = strafeAnim;

        anim_bind_and_play(character.animations, strafeAnim, character.skeletonBlend, true, true);

        lastAttachedBlend  = strafeAnim;
        lastStrafeAnimLock = strafeAnim;
    } else {
        if (character.animations[strafeAnim] && !character.animations[strafeAnim]->isPlaying) {
            t3d_anim_set_looping(character.animations[strafeAnim], true);
            t3d_anim_set_playing(character.animations[strafeAnim], true);
        }
    }

    // Speed scaling: makes RUN clip look like WALK at low speed
    float move01 = isIdle ? 0.0f : fminf(1.0f, fmaxf(0.0f, speedRatio));
    float baseSpeed   = fmaxf(move01 * 0.9f + 0.15f, 0.25f);
    float strafeSpeed = fmaxf(move01 * 0.9f + 0.15f, 0.25f);

    if (fabsf(baseSpeed - lastBaseSpeed) > 0.01f) {
        if (character.animations[baseAnim]) t3d_anim_set_speed(character.animations[baseAnim], baseSpeed);
        lastBaseSpeed = baseSpeed;
    }
    if (fabsf(strafeSpeed - lastStrafeSpeed) > 0.01f) {
        if (character.animations[strafeAnim]) t3d_anim_set_speed(character.animations[strafeAnim], strafeSpeed);
        lastStrafeSpeed = strafeSpeed;
    }

    // ---- CRITICAL ORDER FIX (prevents bind/T-pose bleeding) ----
    // 1) Update base anim on main skeleton (drives character.skeleton bones)
    t3d_anim_update(character.animations[baseAnim], dt);

    // 2) PRIME blend skeleton with base pose so any un-keyed bones are NOT bind pose
    skeleton_copy_pose_bones(character.skeletonBlend, character.skeleton);

    // 3) Update strafe anim on blend skeleton (overwrites keyed channels only)
    t3d_anim_update(character.animations[strafeAnim], dt);

    // 4) Blend pose A vs B using weight (now also fades out smoothly)
    float w = fminf(1.0f, fmaxf(0.0f, animStrafeBlendRatio));
    t3d_skeleton_blend(character.skeleton, character.skeleton, character.skeletonBlend, w);

    // 5) Build final matrices once (after pose blend)
    t3d_skeleton_update(character.skeleton);

    return true;
}

static inline void ensure_locomotion_playing(CharacterState state)
{
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

static void switch_to_action_animation(int targetAnim)
{
    // Stop lock-on drivers
    kill_lockon_drivers();

    // Ensure no other clips remain playing
    anim_stop_all_except(character.animations, character.animationCount, targetAnim);

    character.previousAnimation = character.currentAnimation;
    character.currentAnimation  = targetAnim;

    character.isBlending = true;
    character.blendDuration = ATTACK_CROSSFADE_DURATION;
    character.blendTimer = 0.0f;
    character.blendFactor = 0.0f;

    if (character.previousAnimation >= 0 &&
        character.animations[character.previousAnimation] &&
        character.skeleton && character.skeletonBlend)
    {
        // IMPORTANT: snapshot bones (SRT), not matrices
        skeleton_copy_pose_bones(character.skeletonBlend, character.skeleton);
        hasBlendSnapshot = true;
        lastAttachedBlend = character.previousAnimation;
    } else {
        hasBlendSnapshot = false;
    }


    if (character.previousAnimation >= 0 && character.animations[character.previousAnimation]) {
        anim_stop(character.animations, character.previousAnimation);
    }

    anim_bind_and_play(character.animations, targetAnim, character.skeleton, false, true);
    lastAttachedMain = targetAnim;
}

static void switch_to_action_animation_immediate(int targetAnim)
{
    kill_lockon_drivers();

    anim_stop_all_except(character.animations, character.animationCount, targetAnim);

    if (character.previousAnimation >= 0 && character.animations[character.previousAnimation]) {
        anim_stop(character.animations, character.previousAnimation);
    }
    if (character.currentAnimation >= 0 && character.animations[character.currentAnimation]) {
        anim_stop(character.animations, character.currentAnimation);
    }

    character.previousAnimation = character.currentAnimation;
    character.currentAnimation = targetAnim;
    character.isBlending = false;

    anim_bind_and_play(character.animations, targetAnim, character.skeleton, false, true);
    lastAttachedMain = targetAnim;
}

static void switch_to_locomotion_animation(int targetAnim)
{
    // leaving lock-on blending path
    kill_lockon_drivers();

    // stop everything except target
    anim_stop_all_except(character.animations, character.animationCount, targetAnim);

    // Capture what we are coming FROM before we overwrite currentAnimation
    const int fromAnim = character.currentAnimation;

    // Set the new current animation
    character.currentAnimation = targetAnim;

    // Default: no crossfade unless we explicitly start one
    character.isBlending = false;
    hasBlendSnapshot     = false;

    // ------------------------------------------------------------
    // Crossfade sources:
    //  A) Action -> locomotion (your existing behavior)
    //  B) Locomotion -> locomotion (NEW: fixes idle/walk/run snapping)
    // ------------------------------------------------------------

    bool startCrossfade = false;
    float crossfadeDur  = 0.0f;

    if (is_action_state(prevState)) {
        // Action -> locomotion: keep your existing 0.12
        character.previousAnimation = -1;

        int prevClip = -1;
        if (prevState == CHAR_STATE_ROLLING) prevClip = ANIM_ROLL;
        else if (prevState == CHAR_STATE_KNOCKDOWN) prevClip = ANIM_KNOCKDOWN;
        else if (prevState == CHAR_STATE_ATTACKING || prevState == CHAR_STATE_ATTACKING_STRONG)
            prevClip = character.previousAnimation; // might already be set by action switch code

        character.previousAnimation = prevClip;

        if (prevClip >= 0 && prevClip < character.animationCount &&
            character.animations[prevClip] && character.skeleton && character.skeletonBlend)
        {
            // snapshot "from" pose from current skeleton bones
            skeleton_copy_pose_bones(character.skeletonBlend, character.skeleton);
            hasBlendSnapshot = true;
            lastAttachedBlend = prevClip;

            startCrossfade = true;
            crossfadeDur   = 0.12f;
        }
    }
    else {
        // NEW: Locomotion -> locomotion crossfade (idle/walk/run, including back variants)
        const bool fromIsLocomotion = is_locomotion_anim(fromAnim);
        const bool toIsLocomotion   = is_locomotion_anim(targetAnim);

        // optional cooldown to avoid jittery re-crossfades
        if (locomotionSwitchCooldown <= 0.0f &&
            fromIsLocomotion && toIsLocomotion &&
            character.skeleton && character.skeletonBlend)
        {
            skeleton_copy_pose_bones(character.skeletonBlend, character.skeleton);
            hasBlendSnapshot = true;
            lastAttachedBlend = fromAnim;

            startCrossfade = true;
            crossfadeDur   = LOCOMOTION_CROSSFADE_DURATION;

            locomotionSwitchCooldown = LOCOMOTION_MIN_SWITCH_INTERVAL;
        }
    }

    if (startCrossfade) {
        character.isBlending   = true;
        character.blendDuration = crossfadeDur;
        character.blendTimer    = 0.0f;
        character.blendFactor   = 0.0f;
    }

    // Attach + play the target anim on main skeleton
    if (character.animations[targetAnim]) {
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

static inline void apply_run_end_transition(CharacterState state, float speedRatio, int* targetAnim, bool* runEndActive)
{
    // RunEnd should only trigger when we're basically stopping, not when easing to WALK.
    // Use WALK_THRESHOLD (or slightly above) instead of RUN_THRESHOLD.
    const float RUN_END_TRIGGER = WALK_THRESHOLD; // tune: WALK_THRESHOLD .. (WALK_THRESHOLD*1.5f)

    if (!*runEndActive &&
        character.currentAnimation == ANIM_RUN &&
        state == CHAR_STATE_NORMAL &&
        speedRatio < RUN_END_TRIGGER &&
        speedRatio >= IDLE_THRESHOLD)
    {
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

static inline void update_animations(float speedRatio, CharacterState state, float dt)
{
    if (!character.animations || !character.skeleton || !character.skeletonBlend) return;

    // tick optional locomotion switch cooldown
    if (locomotionSwitchCooldown > 0.0f) {
        locomotionSwitchCooldown -= dt;
        if (locomotionSwitchCooldown < 0.0f) locomotionSwitchCooldown = 0.0f;
    }

    const bool lockonBlendMode = (state == CHAR_STATE_NORMAL &&
                                 cameraLockOnActive &&
                                 ((animStrafeDirFlag != 0) || (lockonStrafeExitT > 0.0f)));
    if (!lockonBlendMode) {
        kill_lockon_drivers();
    }

    if (try_lockon_locomotion_blend(speedRatio, state, dt)) {
        return;
    }

    int targetAnim = get_target_animation(state, speedRatio);

    static bool runEndActive = false;
    apply_run_end_transition(state, speedRatio, &targetAnim, &runEndActive);

    if (targetAnim < 0 || targetAnim >= character.animationCount || !character.animations[targetAnim]) {
        targetAnim = ANIM_IDLE;
        runEndActive = false;
    }

    const bool needsSwitch = (character.currentAnimation != targetAnim);

    // Only “rescue” locomotion if we’re NOT switching this frame.
    if (!needsSwitch) {
        ensure_locomotion_playing(state);
    }

    // IMPORTANT: for locomotion crossfade we need to know the old anim, so store it
    const int oldAnim = character.currentAnimation;

    if (needsSwitch) {
        // keep previousAnimation coherent for action transitions too
        character.previousAnimation = oldAnim;

        if (is_action_state(state)) switch_to_action_animation(targetAnim);
        else                        switch_to_locomotion_animation(targetAnim);
    }

    // ---------------------------------------------------------------------
    // BLEND TIMER UPDATE (once per frame, before applying blend)
    // ---------------------------------------------------------------------
    if (character.isBlending && !hasBlendSnapshot) {
        character.isBlending  = false;
        character.blendTimer  = 0.0f;
        character.blendFactor = 0.0f;
    }

    if (character.isBlending) {
        character.blendTimer += dt;

        if (character.blendTimer >= character.blendDuration) {
            character.isBlending  = false;
            character.blendFactor = 1.0f;
            character.blendTimer  = 0.0f;
            hasBlendSnapshot = false;
        } else {
            character.blendFactor = character.blendTimer / character.blendDuration;
        }
    }

    // ---------------------------------------------------------------------
    // Update current animation (drives character.skeleton bone SRT)
    // ---------------------------------------------------------------------
    const int cur = character.currentAnimation;
    if (cur >= 0 && cur < character.animationCount && character.animations[cur]) {
        T3DAnim* currentAnim = character.animations[cur];

        if (lastAttachedMain != cur) {
            t3d_anim_attach(currentAnim, character.skeleton);
            lastAttachedMain = cur;
        }

        float animSpeed = 1.0f;
        if (state == CHAR_STATE_NORMAL) {
            if (cur == ANIM_WALK || cur == ANIM_WALK_BACK) {
                animSpeed = fmaxf(speedRatio * 2.0f + 0.3f, 0.5f);
            } else if (cur == ANIM_RUN || cur == ANIM_RUN_BACK) {
                animSpeed = fmaxf(speedRatio * 0.8f + 0.2f, 0.6f);
            }
        } else if (state == CHAR_STATE_ATTACKING_STRONG) {
            animSpeed = 0.8f;
        } else if (state == CHAR_STATE_ROLLING) {
            animSpeed = ROLL_ANIM_SPEED;
        }

        if (state == CHAR_STATE_TITLE_IDLE && cur == ANIM_IDLE_TITLE) {
            t3d_anim_set_looping(currentAnim, true);
            if (!currentAnim->isPlaying) t3d_anim_set_playing(currentAnim, true);

            float len = t3d_anim_get_length(currentAnim);
            float t   = t3d_anim_get_time(currentAnim);
            if (len > 0.0f && t >= len) t3d_anim_set_time(currentAnim, 0.0f);
        }

        if (fabsf(animSpeed - lastAnimSpeed) > 0.001f) {
            t3d_anim_set_speed(currentAnim, animSpeed);
            lastAnimSpeed = animSpeed;
        }

        t3d_anim_update(currentAnim, dt);
    }

    // ---------------------------------------------------------------------
    // Apply crossfade using the snapshot (skeletonBlend holds "from" pose)
    // ---------------------------------------------------------------------
    if (character.isBlending && hasBlendSnapshot) {
        t3d_skeleton_blend(character.skeleton,
                           character.skeletonBlend,  // from-pose snapshot (bones)
                           character.skeleton,       // to-pose (current anim bones)
                           character.blendFactor);
    }

    // Build matrices ONCE from the final pose (after any blend)
    t3d_skeleton_update(character.skeleton);

    if (runEndActive && character.currentAnimation == ANIM_RUN_END) {
        if (character.animations[ANIM_RUN_END] && !character.animations[ANIM_RUN_END]->isPlaying) {
            runEndActive = false;
        }
    }
}

/* -----------------------------------------------------------------------------
 * Init/update/draw/damage/delete
 * -------------------------------------------------------------------------- */

void character_init(void)
{
    sword_trail_init();

    characterModel = t3d_model_load("rom:/knight/knight.t3dm");
    characterShadowModel = t3d_model_load("rom:/blob_shadow/shadow.t3dm");

    T3DSkeleton* skeleton = malloc(sizeof(T3DSkeleton));
    *skeleton = t3d_skeleton_create(characterModel);

    T3DSkeleton* skeletonBlend = malloc(sizeof(T3DSkeleton));
    *skeletonBlend = t3d_skeleton_clone(skeleton, false);

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
        "RunBackwards",
        "Death"
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
        true,   // RunBackwards
        false   // Death
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

    ATTACK1_DURATION = get_attack_duration(1);
    ATTACK2_DURATION = get_attack_duration(2);
    ATTACK3_DURATION = get_attack_duration(3);
    ATTACK4_DURATION = get_attack_duration(4);

    rspq_block_begin();
    t3d_model_draw_skinned(characterModel, skeleton);
    rspq_block_t* dpl_model = rspq_block_end();

    rspq_block_begin();
    t3d_model_draw(characterShadowModel);
    rspq_block_t* dpl_shadow = rspq_block_end();

    CapsuleCollider collider = {
        .localCapA = {{0.0f, 4.0f, 0.0f}},
        .localCapB = {{0.0f, 13.0f, 0.0f}},
        .radius = 5.0f
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

void character_update(void)
{
    GameState state = scene_get_game_state();

    if (state == GAME_STATE_DEAD) {
        sword_trail_update(deltaTime, false, NULL, NULL);
        movementVelocityX = 0.0f;
        movementVelocityZ = 0.0f;

        if (characterState != CHAR_STATE_DEAD) {
            characterState = CHAR_STATE_DEAD;
            if (character.animations && ANIM_DEATH < character.animationCount && character.animations[ANIM_DEATH]) {
                t3d_anim_set_time(character.animations[ANIM_DEATH], 0.0f);
                t3d_anim_set_playing(character.animations[ANIM_DEATH], true);
            }
        }

        update_animations(0.0f, characterState, deltaTime);

        character_update_camera();
        character_anim_apply_pose();
        character_finalize_frame(false);
        return;
    }

    if (scene_get_game_state() == GAME_STATE_TITLE || scene_get_game_state() == GAME_STATE_TITLE_TRANSITION)
    {
        sword_trail_update(deltaTime, false, NULL, NULL);

        if (scene_get_game_state() == GAME_STATE_TITLE) {
            if (characterState != CHAR_STATE_TITLE_IDLE) {
                characterState = CHAR_STATE_TITLE_IDLE;
                walkThroughFog = false;
                if (character.animations && character.animations[ANIM_IDLE_TITLE]) {
                    t3d_anim_set_time(character.animations[ANIM_IDLE_TITLE], 0.0f);
                    t3d_anim_set_playing(character.animations[ANIM_IDLE_TITLE], true);
                }
            }
        }

        apply_friction(deltaTime, 1.0f);
        update_current_speed(0.0f, deltaTime);
        float animationSpeedRatio = currentSpeed;

        if (scene_get_game_state() == GAME_STATE_TITLE_TRANSITION && walkThroughFog == false)
        {
            characterState = CHAR_STATE_FOG_WALK;
            walkThroughFog = true;

            if (character.animations && character.animations[ANIM_FOG_OF_WAR]) {
                t3d_anim_set_time(character.animations[ANIM_FOG_OF_WAR], 0.0f);
                t3d_anim_set_playing(character.animations[ANIM_FOG_OF_WAR], true);
            }
        }

        update_animations(animationSpeedRatio, characterState, deltaTime);

        float newPosX = character.pos[0] + movementVelocityX * deltaTime;
        float newPosZ = character.pos[2] + movementVelocityZ * deltaTime;

        character.pos[0] = newPosX;
        character.pos[2] = newPosZ;

        character_anim_apply_pose();
        character_finalize_frame(false);
        return;
    }

    if (scene_is_cutscene_active()) {
        sword_trail_update(deltaTime, false, NULL, NULL);

        apply_friction(deltaTime, 1.0f);
        update_current_speed(0.0f, deltaTime);
        float animationSpeedRatio = currentSpeed;

        update_animations(animationSpeedRatio, characterState, deltaTime);

        float newPosX = character.pos[0] + movementVelocityX * deltaTime;
        float newPosZ = character.pos[2] + movementVelocityZ * deltaTime;

        character.pos[0] = newPosX;
        character.pos[2] = newPosZ;

        character_update_camera();
        character_anim_apply_pose();
        character_finalize_frame(false);
        return;
    }

    bool jumpJustPressed = false;

    bool leftJustPressed = btn.b && !lastBPressed;

    if (leftJustPressed) {
        leftTriggerHeld = true;
        leftTriggerHoldTime = 0.0f;
    }

    if (leftTriggerHeld) {
        leftTriggerHoldTime += deltaTime;
    }

    if (rel.b) {
        leftTriggerHeld = false;
        leftTriggerHoldTime = 0.0f;
    }

    lastBPressed = btn.b;

    StickInput stick = normalize_stick((float)joypad.stick_x, (float)joypad.stick_y);

    if (!cameraLockOnActive) {
        animLockOnStrafingFlag = false;
        animStrafeDirFlag = 0;
        animStrafeBlendRatio = 0.0f;
    } else {
        const float STRAFE_NEUTRAL_MAG = 0.02f; // tune 0.02–0.06

        if (stick.magnitude <= STRAFE_NEUTRAL_MAG) {
            // start a short fade-out instead of snapping
            if (animStrafeDirFlag != 0) {
                lockonLastDir = animStrafeDirFlag;
                lockonLastW   = animStrafeBlendRatio;
                lockonStrafeExitT = 0.10f; // tune 0.06–0.14
            }

            animLockOnStrafingFlag = false;
            // keep dir during fade, so lock-on blend path can finish smoothly
            animStrafeDirFlag = lockonLastDir;
            // keep ratio; we'll decay it in the lock-on blend function
            animStrafeBlendRatio = lockonLastW;
        }

    }

    update_actions(&btn, leftTriggerHeld, leftJustPressed, jumpJustPressed, &stick, deltaTime);

    if (characterState != CHAR_STATE_ATTACKING &&
        characterState != CHAR_STATE_ATTACKING_STRONG &&
        characterState != CHAR_STATE_KNOCKDOWN &&
        stick.magnitude > 0.0f) {

        float desiredVelX, desiredVelZ;

        if (cameraLockOnActive) {
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
            accelerate_towards_with_accel(desiredVelX, desiredVelZ, currentMaxSpeed, ROLL_STEER_ACCELERATION, deltaTime);
        } else {
            accelerate_towards(desiredVelX, desiredVelZ, currentMaxSpeed, deltaTime);
        }

        if (cameraLockOnActive && characterState != CHAR_STATE_ROLLING) {
            T3DVec3 toTargetDir = {{
                cameraLockOnTarget.v[0] - character.pos[0],
                0.0f,
                cameraLockOnTarget.v[2] - character.pos[2]
            }};
            t3d_vec3_norm(&toTargetDir);

            T3DVec3 rightDir = {{ -toTargetDir.v[2], 0.0f, toTargetDir.v[0] }};

            float forward = desiredVelX * toTargetDir.v[0] + desiredVelZ * toTargetDir.v[2];
            float lateral = desiredVelX * rightDir.v[0]   + desiredVelZ * rightDir.v[2];

            float sum = fabsf(forward) + fabsf(lateral) + 0.0001f;
            float lateralRatio = fminf(1.0f, fmaxf(0.0f, fabsf(lateral) / sum));

            // HARD neutral band to kill joystick noise
            const float STRAFE_NEUTRAL_X = 0.10f;   // tune: 0.08–0.16
            bool sidewaysEnough = fabsf(stick.x) >= STRAFE_NEUTRAL_X;

            if (!animLockOnStrafingFlag) {
                animLockOnStrafingFlag = sidewaysEnough && (lateralRatio >= STRAFE_ACTIVATION_RATIO);
            } else {
                animLockOnStrafingFlag = sidewaysEnough && (lateralRatio >= STRAFE_DEACTIVATION_RATIO);
            }

            if (animLockOnStrafingFlag) {
                animStrafeBlendRatio = lateralRatio;
                animStrafeDirFlag = (lateral > 0.0f) ? +1 : (lateral < 0.0f ? -1 : 0);
            } else {
                // start exit fade instead of snapping
                if (animStrafeDirFlag != 0) {
                    lockonLastDir = animStrafeDirFlag;
                    lockonLastW   = animStrafeBlendRatio;
                    lockonStrafeExitT = 0.10f;
                }
                animStrafeBlendRatio = lockonLastW;
                animStrafeDirFlag    = lockonLastDir;
            }


            float targetAngle = atan2f(-(cameraLockOnTarget.v[0] - character.pos[0]),
                                       ( cameraLockOnTarget.v[2] - character.pos[2]));
            float currentAngle = character.rot[1];

            while (targetAngle >  T3D_PI) targetAngle -= 2.0f * T3D_PI;
            while (targetAngle < -T3D_PI) targetAngle += 2.0f * T3D_PI;
            while (currentAngle >  T3D_PI) currentAngle -= 2.0f * T3D_PI;
            while (currentAngle < -T3D_PI) currentAngle += 2.0f * T3D_PI;

            float angleDelta = targetAngle - currentAngle;
            while (angleDelta >  T3D_PI) angleDelta -= 2.0f * T3D_PI;
            while (angleDelta < -T3D_PI) angleDelta += 2.0f * T3D_PI;

            float maxTurnRate = TURN_RATE * deltaTime;
            if (angleDelta >  maxTurnRate) angleDelta =  maxTurnRate;
            if (angleDelta < -maxTurnRate) angleDelta = -maxTurnRate;

            character.rot[1] = currentAngle + angleDelta;
        } else {
            update_yaw_from_velocity(deltaTime);
            animLockOnStrafingFlag = false;
            animStrafeDirFlag = 0;
            animStrafeBlendRatio = 0.0f;
        }
    } else if (characterState == CHAR_STATE_ATTACKING || characterState == CHAR_STATE_ATTACKING_STRONG) {
        float frictionScale = (characterState == CHAR_STATE_ATTACKING_STRONG) ? 1.0f : ATTACK_FRICTION_SCALE;
        apply_friction(deltaTime, frictionScale);

        if (characterState == CHAR_STATE_ATTACKING_STRONG) {
            movementVelocityX = 0.0f;
            movementVelocityZ = 0.0f;
        }

        float hitStart = (characterState == CHAR_STATE_ATTACKING_STRONG) ? STRONG_ATTACK_HIT_START : 0.25f;
        float hitEnd   = (characterState == CHAR_STATE_ATTACKING_STRONG) ? STRONG_ATTACK_HIT_END   : 0.55f;
        float damage   = (characterState == CHAR_STATE_ATTACKING_STRONG) ? STRONG_ATTACK_DAMAGE    : 10.0f;
        (void)damage; // you currently hardcode boss_apply_damage(boss, 1000)

        if (actionTimer > hitStart && actionTimer < hitEnd) {
            if (!character.currentAttackHasHit && attack_hit_test()) {
                Boss* boss = boss_get_instance();
                if (boss) {
                    boss_apply_damage(boss, damage);
                }
                character.currentAttackHasHit = true;
                character_play_hit();
            }
        }
    } else {
        float friction = (characterState == CHAR_STATE_ROLLING) ? ROLL_FRICTION_SCALE : 1.0f;
        apply_friction(deltaTime, friction);
    }

    update_current_speed(stick.magnitude, deltaTime);

    float velMag = sqrtf(movementVelocityX * movementVelocityX + movementVelocityZ * movementVelocityZ);
    float animationSpeedRatio = fminf(1.0f, velMag / MAX_MOVEMENT_SPEED);

    update_animations(animationSpeedRatio, characterState, deltaTime);
    prevState = characterState;

    // Footsteps
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

    float newPosX = character.pos[0] + movementVelocityX * deltaTime;
    float newPosZ = character.pos[2] + movementVelocityZ * deltaTime;

    character.pos[0] = newPosX;
    character.pos[2] = newPosZ;

    character_anim_apply_pose();
    character_finalize_frame(true);

    // Sword trail emission
    bool emitting = false;
    if (characterState == CHAR_STATE_ATTACKING && !attackEnding) {
        emitting = (actionTimer >= 0.15f && actionTimer <= 0.75f);
    } else if (characterState == CHAR_STATE_ATTACKING_STRONG) {
        emitting = (actionTimer >= 0.20f && actionTimer <= 0.90f);
    }

    float baseW[3], tipW[3];
    if (emitting && character_sword_world_segment(baseW, tipW)) {
        sword_trail_update(deltaTime, true, baseW, tipW);
    } else {
        sword_trail_update(deltaTime, false, NULL, NULL);
    }
}

void character_update_position(void)
{
    t3d_mat4fp_from_srt_euler(character.modelMat,
        (float[3]){character.scale[0], character.scale[1], character.scale[2]},
        (float[3]){character.rot[0], character.rot[1] + MODEL_YAW_OFFSET, character.rot[2]},
        (float[3]){character.pos[0], character.pos[1], character.pos[2]}
    );
    character_update_shadow_mat();
}

void character_update_camera(void)
{
    static bool lastLockOnActive = false;

    float scaledDistance = cameraDistance * 0.04f;
    float scaledHeight = cameraHeight * 0.03f;

    bool unlockingFromLockOn = lastLockOnActive && !cameraLockOnActive && cameraLockBlend > 0.001f;
    if (unlockingFromLockOn && scaledDistance > 0.0f) {
        T3DVec3 offsetFromCharacter = {{
            characterCamPos.v[0] - character.pos[0],
            characterCamPos.v[1] - character.pos[1],
            characterCamPos.v[2] - character.pos[2]
        }};

        float sinY = (offsetFromCharacter.v[1] - scaledHeight) / scaledDistance;
        if (sinY < -1.0f) sinY = -1.0f;
        if (sinY >  1.0f) sinY =  1.0f;
        cameraAngleY = asinf(sinY);

        float cosY = fm_cosf(cameraAngleY);
        if (cosY < 0.0001f) cosY = 0.0001f;
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

    if (cameraLockOnActive) {
        T3DVec3 toTarget = {{
            cameraLockOnTarget.v[0] - character.pos[0],
            cameraLockOnTarget.v[1] - character.pos[1],
            cameraLockOnTarget.v[2] - character.pos[2]
        }};
        t3d_vec3_norm(&toTarget);

        desiredCamPos.v[0] = character.pos[0] - toTarget.v[0] * scaledDistance;
        desiredCamPos.v[1] = character.pos[1] + (scaledHeight/2);
        desiredCamPos.v[2] = character.pos[2] - toTarget.v[2] * scaledDistance;
    }

    if (deltaTime > 0.0f) {
        vec3_lerp(&characterCamPos, &characterCamPos, &desiredCamPos, cameraLerpSpeed * deltaTime);
    } else {
        characterCamPos = desiredCamPos;
    }

    T3DVec3 followTarget = {{
        character.pos[0],
        character.pos[1] + 15.0f,
        character.pos[2]
    }};

    float yaw = character.rot[1];
    float fwdX = -fm_sinf(yaw);
    float fwdZ =  fm_cosf(yaw);
    T3DVec3 forwardTarget = {{
        character.pos[0] + fwdX * 2.0f,
        character.pos[1] + 1.5f,
        character.pos[2] + fwdZ * 2.0f
    }};

    float blendSpeed = cameraLerpSpeed;
    float targetBlend = cameraLockOnActive ? 1.0f : 0.0f;

    if (deltaTime > 0.0f) {
        float step = blendSpeed * deltaTime;
        if (step > 1.0f) step = 1.0f;
        cameraLockBlend = (1.0f - step) * cameraLockBlend + step * targetBlend;
    } else {
        cameraLockBlend = targetBlend;
    }

    T3DVec3 desiredTarget;
    if (cameraLockOnActive) {
        const float lockBias = 0.35f;
        vec3_lerp(&desiredTarget, &forwardTarget, &cameraLockOnTarget, lockBias);
    } else {
        vec3_lerp(&desiredTarget, &followTarget, &cameraLockOnTarget, cameraLockBlend);
    }

    if (deltaTime > 0.0f) {
        vec3_lerp(&characterCamTarget, &characterCamTarget, &desiredTarget, cameraLerpSpeed * deltaTime);
    } else {
        characterCamTarget = desiredTarget;
    }

    lastLockOnActive = cameraLockOnActive;
}

void character_draw_shadow(void)
{
    if (!character.visible) return;
    if (!character.dpl_shadow || !character.shadowMat) return;

    float h = character.pos[1] - SHADOW_GROUND_Y;
    if (h < 0.0f) h = 0.0f;

    float t = h / JUMP_HEIGHT;
    if (t > 1.0f) t = 1.0f;

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
    float ratio = character.maxHealth > 0.0f
        ? fmaxf(0.0f, fminf(1.0f, character.health / character.maxHealth))
        : 0.0f;

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
    if (character_is_invulnerable()) return;

    character.health -= amount;
    if (character.health < 0.0f) character.health = 0.0f;

    if (character.health <= 0.0f) {
        characterState = CHAR_STATE_DEAD;
        scene_set_game_state(GAME_STATE_DEAD);
    } else {
        if (amount >= 20.0f && characterState != CHAR_STATE_ROLLING) {
            characterState = CHAR_STATE_KNOCKDOWN;
            actionTimer = 0.0f;
            currentActionDuration = KNOCKDOWN_DURATION;

            float yaw = character.rot[1];
            float bx = fm_sinf(yaw);
            float bz = -fm_cosf(yaw);

            movementVelocityX += bx * KNOCKDOWN_BACK_IMPULSE;
            movementVelocityZ += bz * KNOCKDOWN_BACK_IMPULSE;

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

    if (character.skeleton) {
        t3d_skeleton_destroy(character.skeleton);
        free(character.skeleton);
        character.skeleton = NULL;
    }

    if (character.skeletonBlend) {
        t3d_skeleton_destroy(character.skeletonBlend);
        free(character.skeletonBlend);
        character.skeletonBlend = NULL;
    }

    if (character.animations) {
        for (int i = 0; i < character.animationCount; i++) {
            if (character.animations[i]) {
                t3d_anim_destroy(character.animations[i]);
            }
        }
        free(character.animations);
        character.animations = NULL;
    }

    if (character.modelMat) {
        rspq_wait();
        free_uncached(character.modelMat);
        character.modelMat = NULL;
    }

    if (character.shadowMat) {
        rspq_wait();
        free_uncached(character.shadowMat);
        character.shadowMat = NULL;
    }

    if (character.dpl_model) {
        rspq_wait();
        rspq_block_free(character.dpl_model);
        character.dpl_model = NULL;
    }

    if (character.dpl_shadow) {
        rspq_wait();
        rspq_block_free(character.dpl_shadow);
        character.dpl_shadow = NULL;
    }
}
