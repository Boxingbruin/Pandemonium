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
#include "collision_system.h"
#include "game_math.h"
#include "display_utility.h"
#include "controllers/audio_controller.h"
#include "scenes/scene_sfx.h"
#include "utilities/general_utility.h"
#include "utilities/sword_trail.h"
#include "animation_utility.h"

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
static const float MAX_MOVEMENT_SPEED = 50.0f;
static const float SPEED_BUILDUP_RATE = 1.5f;
static const float SPEED_DECAY_RATE = 4.0f;

static const float ROLL_DURATION = 0.9f;
static const float ROLL_DURATION_MOVING = 0.45f; // tune 0.45–0.70
static const float ROLL_MOVING_MAG      = 0.20f; // stick magnitude threshold

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

// Input state tracking
static bool lastBPressed = false;
static bool lastAPressed = false;
static bool leftTriggerHeld = false;
static float leftTriggerHoldTime = 0.0f;

// Input and tuning constants
static const float STICK_MAX = 80.0f;
static const float INPUT_DEADZONE = 0.12f;

// Strafe activation tuning (to avoid accidental strafing on resting sticks)


static const float TURN_RATE = 8.0f;
static const float IDLE_THRESHOLD = 0.1f;
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
static const float FOOTSTEP_WALK_INTERVAL = 0.70f;
static const float FOOTSTEP_RUN_INTERVAL  = 0.40f;

/* -----------------------------------------------------------------------------
 * Animation driver state
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

// Lock-on strafe enter ramp (prevents idle->strafe "pop" when base switches to RUN)
static bool  lockonWasStrafing = false;


// Lock-on strafe enter ramp + base hold (time-based, fixes idle->strafe snap)
static float lockonStrafeEnterT        = 0.0f;
static float lockonStrafeEnterWTarget  = 0.0f;

// Hysteresis to prevent instant strafeWalk<->strafeRun swapping
static bool  lockonStrafeUseRun        = false;


// Tuning
static const float LOCKON_RUN_HI           = 0.78f; // hysteresis high threshold
static const float LOCKON_RUN_LO           = 0.62f; // hysteresis low threshold
static const float LOCKON_STRAFE_EXIT_DUR  = 0.22f;

// ---- Idle settle (fixes speed-based freeze -> snap) ----
static float idleSettleT = 0.0f;


// --- Input intent cached for animation selection ---
static float g_moveIntentFwd = 0.0f;   // -1..+1 (negative = player intends backwards)
static float g_moveIntentMag = 0.0f;   // 0..1

// Regular (non lock-on) walk<->run blend tuning
static const float FREE_WALKRUN_BLEND_START = 0.35f; // speedRatio where run begins to contribute
static const float FREE_WALKRUN_BLEND_END   = 0.85f; // speedRatio where run is full weight

// Playback scaling for regular locomotion (based on true speed)
static const float FREE_MIN_LOCO_SPEED = 0.50f;
static const float FREE_MAX_LOCO_SPEED = 1.00f;

// Strong-hit knockback (displacement-driven)
static bool  strongKnockbackActive   = false;
static float strongKnockbackT        = 0.0f;
static float strongKnockbackPrevDist = 0.0f;
static float strongKnockbackDirX     = 0.0f;
static float strongKnockbackDirZ     = 0.0f;

// Tuning
static const float STRONG_KNOCKBACK_DIST = 50.0f; // world units

// Knockdown timing (seconds)
static const float KNOCKDOWN_TOTAL_TIME_S      = 5.0f;
static const float KNOCKDOWN_BREAKAWAY_MIN_S   = 2.0f;
static const float KNOCKDOWN_BREAKAWAY_MOVE_MAG = 0.30f; // tune 0.20–0.45

// Knockdown runtime (seconds elapsed)
static float knockdownElapsedS = 0.0f;

// uses (KNOCKDOWN_DURATION * 0.5f) as duration

typedef enum {
    KD_BRK_NONE   = 0,
    KD_BRK_ROLL   = 1,
    KD_BRK_ATTACK = 2,
    KD_BRK_MOVE   = 3,
} KnockdownBreakawayReq;

typedef struct {
    float x;
    float y;
    float magnitude;
} StickInput;


#define STRAFEPOSE_MAX_BONES 64

typedef struct {
    bool   valid;
    int    count;
    T3DVec3 pos[STRAFEPOSE_MAX_BONES];
    T3DQuat rot[STRAFEPOSE_MAX_BONES];
    T3DVec3 scl[STRAFEPOSE_MAX_BONES];
} StrafePoseBuf;

// NOTE: no 'g_' prefix — match compiler hints
static StrafePoseBuf strafeFromPose;
static float         strafePoseBlendT   = 0.0f;
static float         strafePoseBlendDur = 0.0f;
static bool          strafePoseBlending = false;


static const float SWING_DELAY_ATTACK1 = 0.25f;
static const float SWING_DELAY_ATTACK2 = 0.35f;
static const float SWING_DELAY_ATTACK3 = 0.8f;
static const float SWING_DELAY_STRONG  = 0.22f;

// Player delayed swing scheduling (boss-style)
static bool  charSwingSfxPlayed = false;
static float charSwingAudioTimer = 0.0f;

/* -----------------------------------------------------------------------------
 * Local helpers
 * -------------------------------------------------------------------------- */

static inline float stick_axis_deadzone(float raw)
{
    float a = raw / STICK_MAX;
    if (a < -1.0f) a = -1.0f;
    if (a >  1.0f) a =  1.0f;

    float ab = fabsf(a);
    if (ab < INPUT_DEADZONE) return 0.0f;

    // remap [deadzone..1] -> [0..1]
    float t = (ab - INPUT_DEADZONE) / (1.0f - INPUT_DEADZONE);
    if (t > 1.0f) t = 1.0f;
    return (a < 0.0f) ? -t : t;
}

static inline void pose_capture(StrafePoseBuf* pb, const T3DSkeleton* sk)
{
    if (!pb || !sk || !sk->bones || !sk->skeletonRef) return;

    int n = sk->skeletonRef->boneCount;
    if (n > STRAFEPOSE_MAX_BONES) n = STRAFEPOSE_MAX_BONES;

    pb->valid = true;
    pb->count = n;

    for (int i = 0; i < n; i++) {
        pb->pos[i] = sk->bones[i].position;
        pb->rot[i] = sk->bones[i].rotation;
        pb->scl[i] = sk->bones[i].scale;
    }
}

static inline void pose_blend_into_skeleton(const StrafePoseBuf* pb,
                                            T3DSkeleton* sk,
                                            float t)
{
    if (!pb || !pb->valid || !sk || !sk->bones || !sk->skeletonRef) return;

    int n = sk->skeletonRef->boneCount;
    if (n > pb->count) n = pb->count;

    for (int i = 0; i < n; i++) {
        T3DBone* b = &sk->bones[i];

        t3d_vec3_lerp(&b->position, &pb->pos[i], &b->position, t);
        t3d_vec3_lerp(&b->scale,    &pb->scl[i], &b->scale,    t);
        t3d_quat_nlerp(&b->rotation, &pb->rot[i], &b->rotation, t);

        b->hasChanged = true;
    }
}

static inline float clamp01(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

static inline float smoothstep01(float t) {
    t = clamp01(t);
    return t * t * (3.0f - 2.0f * t);
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

static inline void skeleton_prime_from_pose(T3DSkeleton* skel, const T3DSkeleton* poseSrc)
{
    if (!skel || !poseSrc) return;
    skeleton_copy_pose_bones(skel, poseSrc);
}

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

static inline bool anim_idle_latched(float dt, float velMag, float inputMag, CharacterState state)
{
    if (state != CHAR_STATE_NORMAL) {
        idleSettleT = 0.0f;
        return false;
    }

    // IMPORTANT: stick noise means inputMag is almost never exactly 0.
    // Treat small magnitude as neutral.
    const float INPUT_NEUTRAL_EPS = 0.03f; // tune 0.02–0.06
    const bool inputNeutral = (inputMag <= INPUT_NEUTRAL_EPS);

    if (!inputNeutral) {
        idleSettleT = 0.0f;
        return false;
    }

    // Faster settle so we don't "hang" in slowed locomotion.
    const float SETTLE_TIME = 0.06f; // tune 0.03–0.10

    // Start/continue settling when basically stopped.
    // (Your velocities are in world units/sec-ish.)
    const float VEL_ENTER = 3.0f;  // tune 2.0–4.0
    const float VEL_EXIT  = 5.0f;  // tune 4.0–7.0

    if (velMag <= VEL_ENTER) {
        idleSettleT += dt;
        if (idleSettleT >= SETTLE_TIME) {
            idleSettleT = SETTLE_TIME;
            return true;
        }
    } else if (velMag >= VEL_EXIT) {
        idleSettleT = 0.0f;
    }

    return false;
}

static inline void kill_lockon_drivers(void)
{
    // DO NOT stop animations here.
    // Stopping them causes a "freeze" on the exact frame lock-on blend mode ends,
    // because t3d_anim_update won't write bones when isPlaying==false.

    activeMainAnim  = -1;
    activeBlendAnim = -1;

    lastBaseAnimLock   = -1;
    lastStrafeAnimLock = -1;
    lastBaseSpeed      = -1.0f;
    lastStrafeSpeed    = -1.0f;

    // invalidate attach/speed caches
    lastAttachedMain  = -1;
    lastAttachedBlend = -1;
    lastAnimSpeed     = -1.0f;

    // exit fade
    lockonStrafeExitT = 0.0f;
    lockonLastDir = 0;
    lockonLastW = 0.0f;

    // enter ramp
    lockonStrafeEnterT = 0.0f;
    lockonStrafeEnterWTarget = 0.0f;
    lockonWasStrafing = false;
}


static inline void character_play_swing(void) {
    audio_play_scene_sfx_dist(SCENE1_SFX_CHAR_SWING1, 1.0f, 0.0f);
}

static inline void character_reset_swing_sfx(void)
{
    charSwingSfxPlayed = false;
    charSwingAudioTimer = 0.0f;
}

static inline void character_play_swing_timed(float triggerTime)
{
    // identical structure to boss_play_attack_sfx, but local to character
    if (charSwingSfxPlayed) return;

    if (charSwingAudioTimer >= triggerTime) {
        charSwingSfxPlayed = true;
        charSwingAudioTimer = 0.0f;

        // distance 0.0f like you already do for player
        character_play_swing();
    } else {
        charSwingAudioTimer += deltaTime;
    }
}

static inline float character_get_swing_time(void)
{
    if (characterState == CHAR_STATE_ATTACKING_STRONG) return SWING_DELAY_STRONG;

    // only swing during the actual attack, not the end anim
    if (characterState != CHAR_STATE_ATTACKING || attackEnding) return -1.0f;

    switch (attackComboIndex) {
        case 1: return SWING_DELAY_ATTACK1;
        case 2: return SWING_DELAY_ATTACK2;
        case 3: return SWING_DELAY_ATTACK3;
        default: return SWING_DELAY_ATTACK1;
    }
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

static inline void strong_knockback_update(float dt)
{
    if (!strongKnockbackActive) return;
    if (characterState != CHAR_STATE_KNOCKDOWN) { strongKnockbackActive = false; return; }

    const float dur = KNOCKDOWN_DURATION * 0.5f; // “half the duration”
    if (dur <= 0.0001f) { strongKnockbackActive = false; return; }

    strongKnockbackT += dt;
    float t = strongKnockbackT / dur;
    if (t >= 1.0f) { t = 1.0f; strongKnockbackActive = false; }

    // Smooth displacement 0 -> 50, with velocity easing to 0 by halfway point.
    float eased = smoothstep01(t);
    float dist  = STRONG_KNOCKBACK_DIST * eased;

    float delta = dist - strongKnockbackPrevDist;
    strongKnockbackPrevDist = dist;

    // Apply incremental displacement along knockback direction
    character.pos[0] += strongKnockbackDirX * delta;
    character.pos[2] += strongKnockbackDirZ * delta;
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
    character.healthPotions = 3;
    character.damageFlashTimer = 0.0f;
    character.currentAttackHasHit = false;

    strongAttackUpgradedFlag = false;

    sword_trail_reset();
}

int character_get_health_potion_count(void)
{
    return character.healthPotions;
}

bool character_try_use_health_potion(void)
{
    if (character.healthPotions <= 0) return false;

    // Don't consume if already full (or extremely close to full).
    if (character.maxHealth <= 0.0f) return false;
    if (character.health >= character.maxHealth - 0.01f) return false;

    // Each potion gives a little under half health.
    const float heal = character.maxHealth * 0.45f;
    character.health += heal;
    if (character.health > character.maxHealth) character.health = character.maxHealth;

    character.healthPotions--;
    if (character.healthPotions < 0) character.healthPotions = 0;
    return true;
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

static inline void anim_stop_all_except_two(T3DAnim** set, int count, int keepA, int keepB)
{
    if (!set) return;
    for (int i = 0; i < count; i++) {
        if (!set[i]) continue;
        if (i == keepA || i == keepB) continue;
        t3d_anim_set_playing(set[i], false);
    }
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

    // Shorten roll when moving so we return to locomotion sooner (prevents long slide)
    float mag = stick ? stick->magnitude : 0.0f;
    if (mag >= ROLL_MOVING_MAG) {
        // Scale smoothly: at threshold -> near full duration, at full tilt -> moving duration
        float t = (mag - ROLL_MOVING_MAG) / (1.0f - ROLL_MOVING_MAG);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        currentActionDuration = ROLL_DURATION + (ROLL_DURATION_MOVING - ROLL_DURATION) * t;
    } else {
        currentActionDuration = ROLL_DURATION;
    }

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
        default: return 0.9f;
    }

    if (animIdx >= 0 && animIdx < character.animationCount && character.animations[animIdx]) {
        float len = t3d_anim_get_length(character.animations[animIdx]);
        return (len > 0.0f) ? len : 0.9f;
    }
    return 0.9f;
}




static inline bool try_free_locomotion_speed_blend(float speedRatio, CharacterState state, float dt)
{
    // Only normal locomotion, and ONLY when not lock-on and not doing your other blend systems
    if (state != CHAR_STATE_NORMAL) return false;
    if (cameraLockOnActive) return false;
    if (character.isBlending) return false; // don't fight your existing crossfades
    if (!character.animations || !character.skeleton || !character.skeletonBlend) return false;

    // Not moving => let normal system handle idle etc.
    if (speedRatio <= IDLE_THRESHOLD) return false;

    // Decide forward vs backward (same as your freecam logic)
    bool isBackward = false;
    {
        float yaw = character.rot[1];
        float fwdX = -fm_sinf(yaw);
        float fwdZ =  fm_cosf(yaw);
        float dotForward = movementVelocityX * fwdX + movementVelocityZ * fwdZ;
        isBackward = (dotForward < -0.001f);
    }

    const int walkAnim = isBackward ? ANIM_WALK_BACK : ANIM_WALK;
    const int runAnim  = isBackward ? ANIM_RUN_BACK  : ANIM_RUN;

    if (walkAnim < 0 || runAnim < 0) return false;
    if (walkAnim >= character.animationCount || runAnim >= character.animationCount) return false;
    if (!character.animations[walkAnim] || !character.animations[runAnim]) return false;

    // Run weight from speed (linear)
    float wRun = 0.0f;
    {
        float denom = (FREE_WALKRUN_BLEND_END - FREE_WALKRUN_BLEND_START);
        if (denom <= 0.0001f) denom = 0.0001f;
        wRun = (speedRatio - FREE_WALKRUN_BLEND_START) / denom;
        if (wRun < 0.0f) wRun = 0.0f;
        if (wRun > 1.0f) wRun = 1.0f;
    }

    // Keep ONLY these two alive so nothing else fights the pose
    anim_stop_all_except_two(character.animations, character.animationCount, walkAnim, runAnim);

    T3DAnim* walkA = character.animations[walkAnim];
    T3DAnim* runA  = character.animations[runAnim];

    // ---------------------------------------------------------------------
    // CRITICAL FIX:
    // Always ensure attachments are correct for this mode.
    // "s_freeBlend" was not sufficient because other paths can attach ANIM_RUN
    // to the main skeleton, leaving runA writing into the wrong skeleton.
    // ---------------------------------------------------------------------
    if (lastAttachedMain != walkAnim) {
        t3d_anim_attach(walkA, character.skeleton);
        lastAttachedMain = walkAnim;
    }
    if (lastAttachedBlend != runAnim) {
        t3d_anim_attach(runA, character.skeletonBlend);
        lastAttachedBlend = runAnim;
    }

    // Loop + ensure playing (no restart to avoid pops)
    t3d_anim_set_looping(walkA, true);
    t3d_anim_set_looping(runA,  true);
    if (!walkA->isPlaying) t3d_anim_set_playing(walkA, true);
    if (!runA->isPlaying)  t3d_anim_set_playing(runA,  true);

    // Playback speed based on joystick magnitude (0..1 post-deadzone)
    float stick01 = clamp01(g_moveIntentMag);
    float s = FREE_MIN_LOCO_SPEED + (FREE_MAX_LOCO_SPEED - FREE_MIN_LOCO_SPEED) * stick01;
    t3d_anim_set_speed(walkA, s);
    t3d_anim_set_speed(runA,  s);

    // ---------------------------------------------------------------------
    // Phase-locked update:
    // Walk advances normally. Run is slaved to walk's phase.
    // ---------------------------------------------------------------------
    t3d_anim_update(walkA, dt);

    float walkLen = t3d_anim_get_length(walkA);
    float walkT   = t3d_anim_get_time(walkA);

    float phase = 0.0f;
    if (walkLen > 0.0001f) {
        while (walkT >= walkLen) walkT -= walkLen;
        while (walkT <  0.0f)    walkT += walkLen;
        phase = walkT / walkLen;
    }

    // Prime blend skeleton from walk pose first
    skeleton_copy_pose_bones(character.skeletonBlend, character.skeleton);

    float runLen = t3d_anim_get_length(runA);
    if (runLen > 0.0001f) {
        float runT = phase * runLen;
        while (runT >= runLen) runT -= runLen;
        while (runT <  0.0f)   runT += runLen;
        t3d_anim_set_time(runA, runT);
    }

    // Sample run pose at that time without advancing
    t3d_anim_update(runA, 0.0f);

    // Blend run into walk on main skeleton
    t3d_skeleton_blend(character.skeleton, character.skeleton, character.skeletonBlend, wRun);
    t3d_skeleton_update(character.skeleton);

    // Keep state machine sane
    character.currentAnimation = (wRun >= 0.5f) ? runAnim : walkAnim;

    return true;
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
        character_reset_swing_sfx();

        currentActionDuration = get_attack_duration(1);
        character.currentAttackHasHit = false;

        float yaw = character.rot[1];
        float fx = -fm_sinf(yaw);
        float fz =  fm_cosf(yaw);
        movementVelocityX += fx * ATTACK_FORWARD_IMPULSE;
        movementVelocityZ += fz * ATTACK_FORWARD_IMPULSE;

        character_play_swing_timed(SWING_DELAY_ATTACK1);
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
        character_reset_swing_sfx();
        currentActionDuration = STRONG_ATTACK_DURATION;

        character.currentAttackHasHit = false;
        movementVelocityX = 0.0f;
        movementVelocityZ = 0.0f;

        character_play_swing_timed(SWING_DELAY_STRONG);
    }

    if (characterState == CHAR_STATE_NORMAL) {
        strongAttackUpgradedFlag = false;
    }
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
                default: return ANIM_ATTACK1;
            }
        }
        return ANIM_ATTACK1;
    }

    // In lock-on, locomotion blending is handled separately (try_lockon_locomotion_blend)
    if (cameraLockOnActive && ((animStrafeDirFlag != 0) || (lockonStrafeExitT > 0.0f))) {
        return ANIM_IDLE;
    }

    // ------------------------------------------------------------
    // Backward selection:
    // - FREECAM: use velocity vs facing (original correct behavior)
    // - LOCKON (but not in blend mode): use intent hysteresis (optional safety)
    // ------------------------------------------------------------
    bool isBackward = false;

    if (!cameraLockOnActive) {
        // ORIGINAL: determine if moving backwards relative to facing
        float yaw = character.rot[1];
        float fwdX = -fm_sinf(yaw);
        float fwdZ =  fm_cosf(yaw);
        float dotForward = movementVelocityX * fwdX + movementVelocityZ * fwdZ;
        isBackward = (dotForward < -0.001f);
    } else {
        // If you ever hit this path (lock-on but not in lock-on blend mode),
        // use input-intent gating so sideways doesn't become "walk back".
        const float BACK_INTENT_ENTER = -0.35f; // tune
        const float BACK_INTENT_EXIT  = -0.20f; // tune
        const float INTENT_MIN_MAG    = 0.06f;

        static bool wantBackAnim = false;

        if (g_moveIntentMag < INTENT_MIN_MAG) {
            wantBackAnim = false;
        } else {
            if (!wantBackAnim) {
                if (g_moveIntentFwd <= BACK_INTENT_ENTER) wantBackAnim = true;
            } else {
                if (g_moveIntentFwd >= BACK_INTENT_EXIT) wantBackAnim = false;
            }
        }

        isBackward = wantBackAnim;
    }

    // ------------------------------------------------------------
    // Speed -> clip
    // ------------------------------------------------------------
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

    // We run this path whenever strafing is active OR we are fading out.
    const bool wantLockonBlend = (animStrafeDirFlag != 0) || (lockonStrafeExitT > 0.0f);
    if (!wantLockonBlend) return false;

    // ------------------------------------------------------------
    // Exit fade (time-based)
    // ------------------------------------------------------------
    if (lockonStrafeExitT > 0.0f) {
        const float EXIT_DUR = LOCKON_STRAFE_EXIT_DUR;

        lockonStrafeExitT -= dt;
        if (lockonStrafeExitT < 0.0f) lockonStrafeExitT = 0.0f;

        float t = (EXIT_DUR > 0.0f) ? (lockonStrafeExitT / EXIT_DUR) : 0.0f;
        animStrafeBlendRatio = lockonLastW * t;

        if (animStrafeBlendRatio <= 0.0001f) {
            animStrafeBlendRatio = 0.0f;
            animStrafeDirFlag = 0;
            lockonLastDir = 0;
            lockonLastW = 0.0f;
            lockonStrafeExitT = 0.0f;
        }
    }

    // ------------------------------------------------------------
    // Idle test (velocity-based)
    // ------------------------------------------------------------
    const float LOCKON_IDLE_VEL = 1.8f;
    float velMag = sqrtf(movementVelocityX * movementVelocityX +
                         movementVelocityZ * movementVelocityZ);

    const bool isIdle = (velMag <= LOCKON_IDLE_VEL) || (speedRatio < 0.01f);

    // ------------------------------------------------------------
    // LOCK-ON BASE CLIP SELECTION (intent-gated forward/back)
    // ------------------------------------------------------------
    const float LOCKON_FB_MAG_MIN   = 0.16f;
    const float LOCKON_FB_FWD_MIN   = 0.22f;
    const float LOCKON_FB_FWD_ENTER = 0.28f;
    const float LOCKON_FB_FWD_EXIT  = 0.18f;

    static int lockonFBDir = 0; // -1 back, +1 forward, 0 none

    // ------------------------------------------------------------
    // HACK: "fake diagonal" forward/back component while strafing
    // Only affects the F/B decision logic. Does NOT change baseAnim rules.
    // ------------------------------------------------------------
    static int s_lastRealFBDir = +1; // last meaningful FB direction (default forward)

    // Track last real FB direction when user actually pushes FB meaningfully
    if (!isIdle && fabsf(g_moveIntentFwd) >= LOCKON_FB_FWD_ENTER) {
        s_lastRealFBDir = (g_moveIntentFwd >= 0.0f) ? +1 : -1;
    }

    float fwdForDecision = g_moveIntentFwd;

    // If strafing (left/right) and FB is near zero, inject a small FB
    if (!isIdle && animStrafeDirFlag != 0) {
        const float PURE_STRAFE_FWD_DEADBAND = 0.18f;  // consider this "no FB"
        const float INJECT_FWD              = 0.30f;  // must be > LOCKON_FB_FWD_ENTER (0.28)

        if (fabsf(fwdForDecision) < PURE_STRAFE_FWD_DEADBAND) {
            fwdForDecision = (s_lastRealFBDir > 0) ? +INJECT_FWD : -INJECT_FWD;
        }
    }

    if (isIdle || g_moveIntentMag < LOCKON_FB_MAG_MIN) {
        lockonFBDir = 0;
    } else {
        if (lockonFBDir == 0) {
            if (fwdForDecision >=  LOCKON_FB_FWD_ENTER) lockonFBDir = +1;
            if (fwdForDecision <= -LOCKON_FB_FWD_ENTER) lockonFBDir = -1;
        } else if (lockonFBDir > 0) {
            if (fwdForDecision <=  LOCKON_FB_FWD_EXIT) lockonFBDir = 0;
        } else { // lockonFBDir < 0
            if (fwdForDecision >= -LOCKON_FB_FWD_EXIT) lockonFBDir = 0;
        }

        if (fabsf(fwdForDecision) < LOCKON_FB_FWD_MIN) {
            lockonFBDir = 0;
        }
    }

    // Walk vs run hysteresis
    if (isIdle) {
        lockonStrafeUseRun = false;
    } else {
        if (!lockonStrafeUseRun) {
            if (speedRatio >= LOCKON_RUN_HI) lockonStrafeUseRun = true;
        } else {
            if (speedRatio <= LOCKON_RUN_LO) lockonStrafeUseRun = false;
        }
    }

    int baseAnim = ANIM_IDLE;
    if (!isIdle && lockonFBDir != 0) {
        const bool wantRun = lockonStrafeUseRun;
        if (wantRun) baseAnim = (lockonFBDir > 0) ? ANIM_RUN      : ANIM_RUN_BACK;
        else         baseAnim = (lockonFBDir > 0) ? ANIM_WALK     : ANIM_WALK_BACK;
    }

    // ------------------------------------------------------------
    // Strafe clip selection: always STRAFE_RUN L/R (or idle if not strafing)
    // ------------------------------------------------------------
    int strafeAnim = ANIM_IDLE;
    if (!isIdle && animStrafeDirFlag != 0) {
        const bool right = (animStrafeDirFlag > 0);
        strafeAnim = right ? ANIM_STRAFE_RUN_RIGHT : ANIM_STRAFE_RUN_LEFT;
    }

    // ------------------------------------------------------------
    // CRITICAL FIX:
    // Keep BOTH base + strafe alive. Do NOT stop everything except base.
    // If both are same, this degenerates fine.
    // ------------------------------------------------------------
    anim_stop_all_except_two(character.animations, character.animationCount, baseAnim, strafeAnim);

    const bool baseChanged   = (lastBaseAnimLock   != baseAnim);
    const bool strafeChanged = (lastStrafeAnimLock != strafeAnim);

    // ------------------------------------------------------------
    // Base on main skeleton
    // ------------------------------------------------------------
    if (baseChanged) {
        if (activeMainAnim != -1 && activeMainAnim != baseAnim) {
            anim_stop(character.animations, activeMainAnim);
        }
        activeMainAnim = baseAnim;

        // no restart to avoid pops
        anim_bind_and_play(character.animations, baseAnim, character.skeleton, true, false);

        lastAttachedMain = baseAnim;
        lastBaseAnimLock = baseAnim;
    }

    // Guarantee base is playing
    if (character.animations[baseAnim] && !character.animations[baseAnim]->isPlaying) {
        t3d_anim_set_looping(character.animations[baseAnim], true);
        t3d_anim_set_playing(character.animations[baseAnim], true);
    }

    // ------------------------------------------------------------
    // Strafe on blend skeleton
    // ------------------------------------------------------------
    if (strafeChanged) {
        if (character.skeletonBlend) {
            pose_capture(&strafeFromPose, character.skeletonBlend);
            strafePoseBlendT   = 0.0f;
            strafePoseBlendDur = 0.10f;
            strafePoseBlending = true;
        }

        if (activeBlendAnim != -1 && activeBlendAnim != strafeAnim) {
            anim_stop(character.animations, activeBlendAnim);
        }
        activeBlendAnim = strafeAnim;

        anim_bind_and_play(character.animations, strafeAnim, character.skeletonBlend, true, false);

        lastAttachedBlend  = strafeAnim;
        lastStrafeAnimLock = strafeAnim;
    }

    // Guarantee strafe is playing (prevents “stuck in idle”)
    if (character.animations[strafeAnim] && !character.animations[strafeAnim]->isPlaying) {
        t3d_anim_set_looping(character.animations[strafeAnim], true);
        t3d_anim_set_playing(character.animations[strafeAnim], true);
    }

    // ------------------------------------------------------------
    // Speed scaling
    // ------------------------------------------------------------
    float move01 = isIdle ? 0.0f : fminf(1.0f, fmaxf(0.0f, speedRatio));

    const float MIN_LOCO_SPEED = 0.88f;
    const float MAX_LOCO_SPEED = 1.15f;

    float baseSpeed   = MIN_LOCO_SPEED + (MAX_LOCO_SPEED - MIN_LOCO_SPEED) * move01;
    float strafeSpeed = MIN_LOCO_SPEED + (MAX_LOCO_SPEED - MIN_LOCO_SPEED) * move01;

    if (fabsf(baseSpeed - lastBaseSpeed) > 0.01f) {
        if (character.animations[baseAnim]) t3d_anim_set_speed(character.animations[baseAnim], baseSpeed);
        lastBaseSpeed = baseSpeed;
    }
    if (fabsf(strafeSpeed - lastStrafeSpeed) > 0.01f) {
        if (character.animations[strafeAnim]) t3d_anim_set_speed(character.animations[strafeAnim], strafeSpeed);
        lastStrafeSpeed = strafeSpeed;
    }

    // ------------------------------------------------------------
    // Update order
    // ------------------------------------------------------------
    t3d_anim_update(character.animations[baseAnim], dt);

    // Prime blend skeleton from base pose first (prevents bind-pose on unkeyed bones)
    skeleton_copy_pose_bones(character.skeletonBlend, character.skeleton);

    t3d_anim_update(character.animations[strafeAnim], dt);

    // Crossfade across left/right switches
    if (strafePoseBlending) {
        strafePoseBlendT += dt;
        float t = (strafePoseBlendDur > 0.0f) ? (strafePoseBlendT / strafePoseBlendDur) : 1.0f;
        if (t >= 1.0f) { t = 1.0f; strafePoseBlending = false; }
        t = smoothstep01(t);
        pose_blend_into_skeleton(&strafeFromPose, character.skeletonBlend, t);
    }

    float w = fminf(1.0f, fmaxf(0.0f, animStrafeBlendRatio));
    t3d_skeleton_blend(character.skeleton, character.skeleton, character.skeletonBlend, w);
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
    kill_lockon_drivers();

    anim_stop_all_except(character.animations, character.animationCount, targetAnim);

    character.previousAnimation = character.currentAnimation;
    character.currentAnimation  = targetAnim;

    character.isBlending    = false;
    hasBlendSnapshot        = false;

    // Snapshot FROM pose (current skeleton) before attach resets anything
    if (character.previousAnimation >= 0 &&
        character.previousAnimation < character.animationCount &&
        character.animations[character.previousAnimation] &&
        character.skeleton && character.skeletonBlend)
    {
        skeleton_copy_pose_bones(character.skeletonBlend, character.skeleton);
        hasBlendSnapshot = true;
    }

    // Stop previous clip if any
    if (character.previousAnimation >= 0 && character.animations[character.previousAnimation]) {
        anim_stop(character.animations, character.previousAnimation);
    }

    // Attach + play target on main skeleton
    if (character.animations[targetAnim]) {
        t3d_anim_attach(character.animations[targetAnim], character.skeleton);
        lastAttachedMain = targetAnim;

        // PRIME from snapshot so first update isn't bind pose
        if (hasBlendSnapshot) {
            skeleton_prime_from_pose(character.skeleton, character.skeletonBlend);
        }

        t3d_anim_set_looping(character.animations[targetAnim], false);
        t3d_anim_set_time(character.animations[targetAnim], 0.0f);
        t3d_anim_set_playing(character.animations[targetAnim], true);
    }

    // Start blend
    if (hasBlendSnapshot) {
        character.isBlending    = true;
        character.blendDuration = ATTACK_CROSSFADE_DURATION;
        character.blendTimer    = 0.0f;
        character.blendFactor   = 0.0f;
    }
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

static inline void start_knockdown_breakaway_attack(void)
{
    // Breakaway => transition into ATTACK1 immediately
    characterState = CHAR_STATE_ATTACKING;

    attackComboIndex = 1;
    attackQueued = false;
    attackEnding = false;

    actionTimer = 0.0f;
    character_reset_swing_sfx();
    currentActionDuration = get_attack_duration(1);

    character.currentAttackHasHit = false;

    clear_lockon_strafe_flags_on_action();

    // Same forward impulse you already use
    float yaw = character.rot[1];
    float fx = -fm_sinf(yaw);
    float fz =  fm_cosf(yaw);
    movementVelocityX += fx * ATTACK_FORWARD_IMPULSE;
    movementVelocityZ += fz * ATTACK_FORWARD_IMPULSE;

    // Play the attack anim NOW
    if (ANIM_ATTACK1 >= 0 && ANIM_ATTACK1 < character.animationCount && character.animations[ANIM_ATTACK1]) {
        t3d_anim_set_looping(character.animations[ANIM_ATTACK1], false);
        t3d_anim_set_time(character.animations[ANIM_ATTACK1], 0.0f);
        t3d_anim_set_playing(character.animations[ANIM_ATTACK1], true);
    }

    character_play_swing_timed(SWING_DELAY_ATTACK1);

    // Ensure pose driver switches instantly
    switch_to_action_animation_immediate(ANIM_ATTACK1);
}

static inline void start_knockdown_breakaway_roll(void)
{
    // Breakaway => transition into rolling (invuln maintained by your state check)
    characterState = CHAR_STATE_ROLLING;
    actionTimer = 0.0f;

    // Breakaway roll duration: pick whichever feels right.
    // If you want it snappier, use ROLL_DURATION_MOVING.
    currentActionDuration = ROLL_DURATION_MOVING;

    // Clear lock-on flags so we don't fight locomotion blend
    clear_lockon_strafe_flags_on_action();

    // Optional: kill movement so roll anim reads clean
    movementVelocityX = 0.0f;
    movementVelocityZ = 0.0f;

    // Play roll anim immediately
    if (ANIM_ROLL >= 0 && ANIM_ROLL < character.animationCount && character.animations[ANIM_ROLL]) {
        t3d_anim_set_looping(character.animations[ANIM_ROLL], false);
        t3d_anim_set_time(character.animations[ANIM_ROLL], 0.0f);
        t3d_anim_set_playing(character.animations[ANIM_ROLL], true);
    }

    // Ensure we switch pose driver now (so you see it instantly)
    switch_to_action_animation_immediate(ANIM_ROLL);
}

static inline void progress_action_timers(float dt, KnockdownBreakawayReq breakawayReq)
{
    if (characterState == CHAR_STATE_NORMAL) return;
    if (currentActionDuration <= 0.0001f) currentActionDuration = 1.0f;

    // Default behavior: your "normalized" actionTimer
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
        return;
    }

    if (characterState == CHAR_STATE_ATTACKING) {
        if (!attackEnding && attackQueued &&
            actionTimer >= ATTACK_TRANSITION_TIME &&
            attackComboIndex < 3)
        {
            attackComboIndex++;
            attackQueued = false;

            actionTimer = 0.0f;
            character_reset_swing_sfx();
            currentActionDuration = get_attack_duration(attackComboIndex);
            character.currentAttackHasHit = false;

            // ---- SWING SFX (combo-specific timing) ----
            if (attackComboIndex == 2) {
                character_play_swing_timed(SWING_DELAY_ATTACK2);
            } else if (attackComboIndex == 3) {
                character_play_swing_timed(SWING_DELAY_ATTACK3);
            }

            float yaw = character.rot[1];
            float fx = -fm_sinf(yaw);
            float fz =  fm_cosf(yaw);
            movementVelocityX += fx * ATTACK_FORWARD_IMPULSE;
            movementVelocityZ += fz * ATTACK_FORWARD_IMPULSE;
        } else if (!attackEnding && actionTimer >= 1.0f) {
            if (attackComboIndex <= 3) {
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
        }
        else if (attackEnding && actionTimer >= 1.0f) {
            characterState = CHAR_STATE_NORMAL;
            actionTimer = 0.0f;
            attackComboIndex = 0;
            attackQueued = false;
            attackEnding = false;
        }
        return;
    }

    if (characterState == CHAR_STATE_ATTACKING_STRONG) {
        if (actionTimer >= 1.0f) {
            characterState = CHAR_STATE_NORMAL;
            actionTimer = 0.0f;
        }
        return;
    }

    if (characterState == CHAR_STATE_KNOCKDOWN) {
        // ------------------------------------------------------------
        // Knockdown timing model:
        // - total duration is EXACTLY 5 seconds (unless breakaway)
        // - breakaway allowed only after KNOCKDOWN_BREAKAWAY_MIN_S
        // - knockdown anim is forced-looping so it never "auto-ends"
        // ------------------------------------------------------------
        knockdownElapsedS += dt;

        T3DAnim* kd = NULL;
        if (ANIM_KNOCKDOWN >= 0 && ANIM_KNOCKDOWN < character.animationCount) {
            kd = character.animations[ANIM_KNOCKDOWN];
        }

        if (kd) {
            t3d_anim_set_looping(kd, true);

            if (!kd->isPlaying) {
                t3d_anim_set_time(kd, 0.0f);
                t3d_anim_set_playing(kd, true);
            } else {
                float len = t3d_anim_get_length(kd);
                if (len > 0.0001f) {
                    float t = t3d_anim_get_time(kd);
                    if (t >= len) {
                        while (t >= len) t -= len;
                        t3d_anim_set_time(kd, t);
                    }
                }
            }
        }

        // Breakaway gate
        if (breakawayReq != KD_BRK_NONE && knockdownElapsedS >= KNOCKDOWN_BREAKAWAY_MIN_S) {
            if (kd) t3d_anim_set_looping(kd, false);

            switch (breakawayReq) {
                case KD_BRK_ROLL:
                    start_knockdown_breakaway_roll();
                    return;

                case KD_BRK_ATTACK:
                    start_knockdown_breakaway_attack();
                    return;

                case KD_BRK_MOVE:
                    // Stand up into normal locomotion immediately
                    characterState = CHAR_STATE_NORMAL;
                    actionTimer = 0.0f;
                    knockdownElapsedS = 0.0f;
                    return;

                default:
                    break;
            }
        }

        // Full duration expiry
        if (knockdownElapsedS >= KNOCKDOWN_TOTAL_TIME_S) {
            if (kd) t3d_anim_set_looping(kd, false);

            characterState = CHAR_STATE_NORMAL;
            actionTimer = 0.0f;
            knockdownElapsedS = 0.0f;
            return;
        }

        return;
    }
}

static inline void update_actions(const joypad_buttons_t* buttons,
                                  bool leftHeldNow,
                                  bool leftJustPressed,
                                  bool jumpJustPressed,
                                  const StickInput* stick,
                                  KnockdownBreakawayReq breakawayReq,
                                  float dt)
{
    (void)jumpJustPressed;

    // Normal action starts (these won't fire while knocked down anyway)
    try_start_roll(buttons, stick);
    try_start_attack(leftJustPressed);
    upgrade_to_strong_attack(leftHeldNow);

    // Knockdown breakaway handling happens inside progress_action_timers()
    progress_action_timers(dt, breakawayReq);
}

static void switch_to_locomotion_animation(int targetAnim)
{
    kill_lockon_drivers();

    anim_stop_all_except(character.animations, character.animationCount, targetAnim);

    const int fromAnim = character.currentAnimation;

    // Default: no crossfade
    character.isBlending = false;
    hasBlendSnapshot     = false;

    bool  startCrossfade = false;
    float crossfadeDur   = 0.0f;

    // --- Decide if we crossfade ---
    if (is_action_state(prevState)) {
        // your action -> locomotion behavior
        int prevClip = -1;
        if (prevState == CHAR_STATE_ROLLING) prevClip = ANIM_ROLL;
        else if (prevState == CHAR_STATE_KNOCKDOWN) prevClip = ANIM_KNOCKDOWN;
        else if (prevState == CHAR_STATE_ATTACKING || prevState == CHAR_STATE_ATTACKING_STRONG)
            prevClip = character.previousAnimation;

        if (prevClip >= 0 && prevClip < character.animationCount &&
            character.animations[prevClip] && character.skeleton && character.skeletonBlend)
        {
            // Snapshot FROM pose (bones currently on main skeleton)
            skeleton_copy_pose_bones(character.skeletonBlend, character.skeleton);
            hasBlendSnapshot = true;
            startCrossfade   = true;
            crossfadeDur     = 0.12f;
        }
    } else {
        // locomotion -> locomotion crossfade (idle/walk/run/back variants)
        const bool fromIsLocomotion = is_locomotion_anim(fromAnim);
        const bool toIsLocomotion   = is_locomotion_anim(targetAnim);

        if (fromIsLocomotion && toIsLocomotion && character.skeleton && character.skeletonBlend) {
            skeleton_copy_pose_bones(character.skeletonBlend, character.skeleton);
            hasBlendSnapshot = true;
            startCrossfade   = true;
            crossfadeDur     = LOCOMOTION_CROSSFADE_DURATION;
        }
    }

    // Switch current animation id AFTER snapshot
    character.previousAnimation = fromAnim;
    character.currentAnimation  = targetAnim;

    // Attach + play target on main skeleton
    if (character.animations[targetAnim]) {
        t3d_anim_attach(character.animations[targetAnim], character.skeleton);
        lastAttachedMain = targetAnim;

        // CRITICAL: attach() can reset bones -> prime skeleton with FROM pose snapshot
        // so the very first anim_update starts from the previous pose, not bind pose.
        if (startCrossfade && hasBlendSnapshot) {
            skeleton_prime_from_pose(character.skeleton, character.skeletonBlend);
        }

        bool shouldLoop = (targetAnim == ANIM_IDLE || targetAnim == ANIM_IDLE_TITLE || targetAnim == ANIM_WALK ||
                           targetAnim == ANIM_RUN  || targetAnim == ANIM_WALK_BACK ||
                           targetAnim == ANIM_RUN_BACK || targetAnim == ANIM_STRAFE_WALK_LEFT ||
                           targetAnim == ANIM_STRAFE_WALK_RIGHT || targetAnim == ANIM_STRAFE_RUN_LEFT ||
                           targetAnim == ANIM_STRAFE_RUN_RIGHT);

        t3d_anim_set_looping(character.animations[targetAnim], shouldLoop);
        t3d_anim_set_time(character.animations[targetAnim], 0.0f);
        t3d_anim_set_playing(character.animations[targetAnim], true);
    }

    if (startCrossfade && hasBlendSnapshot) {
        character.isBlending    = true;
        character.blendDuration = crossfadeDur;
        character.blendTimer    = 0.0f;
        character.blendFactor   = 0.0f;
    }
}

static inline void apply_run_end_transition(CharacterState state, float speedRatio, int* targetAnim, bool* runEndActive)
{
    (void)state;
    (void)speedRatio;
    (void)targetAnim;

    // Hard-disabled: RunEnd causes freeze / pop behavior on N64 due to non-looping anim + bone update buffering.
    *runEndActive = false;
}

// static inline void apply_run_end_transition(CharacterState state, float speedRatio, int* targetAnim, bool* runEndActive, float inputMag)
// {
//     // Only normal locomotion, and NEVER in lock-on.
//     if (state != CHAR_STATE_NORMAL || cameraLockOnActive) {
//         *runEndActive = false;
//         return;
//     }

//     // RunEnd is ONLY for "run -> full stop".
//     if (*targetAnim != ANIM_IDLE) {
//         *runEndActive = false;
//         return;
//     }

//     // Require input to be neutral (player is letting go), otherwise this is run->walk, not run->end.
//     // This MUST match your stick noise reality.
//     const float INPUT_NEUTRAL_EPS = 0.03f; // same as idle latch
//     if (inputMag > INPUT_NEUTRAL_EPS) {
//         *runEndActive = false;
//         return;
//     }

//     // Only trigger from forward RUN clip.
//     if (character.currentAnimation != ANIM_RUN) {
//         // Only keep it if we're already in RunEnd.
//         if (character.currentAnimation != ANIM_RUN_END) *runEndActive = false;
//         return;
//     }

//     // Track whether we were "actually running" recently, based on speed being above RUN_THRESHOLD
//     // while input was NOT neutral.
//     static bool  wasRunning = false;
//     static float prevSpeedRatio = 0.0f;

//     if (speedRatio >= RUN_THRESHOLD) {
//         wasRunning = true;
//     }

//     // Deceleration check (must be falling)
//     const bool decelerating = (speedRatio < prevSpeedRatio - 0.001f);
//     prevSpeedRatio = speedRatio;

//     if (!wasRunning || !decelerating) {
//         *runEndActive = false;
//         return;
//     }

//     // Near-stop gate: only start RunEnd when very close to idle.
//     const float RUN_END_TRIGGER = 0.10f; // tune 0.06–0.14
//     if (!*runEndActive &&
//         speedRatio < RUN_END_TRIGGER &&
//         speedRatio >= IDLE_THRESHOLD)
//     {
//         *targetAnim = ANIM_RUN_END;
//         *runEndActive = true;
//         return;
//     }

//     if (*runEndActive) {
//         *targetAnim = ANIM_RUN_END;
//     }
// }

static inline void update_animations(float speedRatio, CharacterState state, float dt,
                                     float velMag, float inputMag)
{
    if (!character.animations || !character.skeleton || !character.skeletonBlend) return;

    const bool lockonBlendMode = (state == CHAR_STATE_NORMAL &&
                                 cameraLockOnActive &&
                                 ((animStrafeDirFlag != 0) || (lockonStrafeExitT > 0.0f)));

    // Only reset lock-on driver bookkeeping when we EXIT lock-on blend mode,
    // not every single frame we are outside of it.
    static bool wasLockonBlendMode = false;
    if (!lockonBlendMode && wasLockonBlendMode) {
        kill_lockon_drivers();
    }
    wasLockonBlendMode = lockonBlendMode;

    if (try_lockon_locomotion_blend(speedRatio, state, dt)) {
        return;
    }

    // Regular (non lock-on) smooth walk<->run blending by speed
    if (try_free_locomotion_speed_blend(speedRatio, state, dt)) {
        return;
    }

    const bool forceIdle = anim_idle_latched(dt, velMag, inputMag, state);
    int targetAnim = forceIdle ? ANIM_IDLE : get_target_animation(state, speedRatio);

    static bool runEndActive = false;
    apply_run_end_transition(state, speedRatio, &targetAnim, &runEndActive);

    if (targetAnim < 0 || targetAnim >= character.animationCount || !character.animations[targetAnim]) {
        targetAnim = ANIM_IDLE;
    }

    const bool needsSwitch = (character.currentAnimation != targetAnim);

    if (!needsSwitch) {
        ensure_locomotion_playing(state);
    }

    if (needsSwitch) {
        if (is_action_state(state)) switch_to_action_animation(targetAnim);
        else                        switch_to_locomotion_animation(targetAnim);
    }

    // Blend timer
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

    // Drive current anim
    const int cur = character.currentAnimation;
    if (cur >= 0 && cur < character.animationCount && character.animations[cur]) {
        T3DAnim* currentAnim = character.animations[cur];

        if (lastAttachedMain != cur) {
            t3d_anim_attach(currentAnim, character.skeleton);
            lastAttachedMain = cur;
        }

        float animSpeed = 1.0f;

        if (state == CHAR_STATE_NORMAL) {
            if (is_locomotion_anim(cur) && cur != ANIM_IDLE && cur != ANIM_IDLE_TITLE) {
                const float MIN_LOCO_SPEED = 0.88f;
                const float MAX_LOCO_SPEED = 1.15f;
                float s01 = fminf(1.0f, fmaxf(0.0f, speedRatio));
                animSpeed = MIN_LOCO_SPEED + (MAX_LOCO_SPEED - MIN_LOCO_SPEED) * s01;
            } else {
                animSpeed = 1.0f;
            }
        } else if (state == CHAR_STATE_ATTACKING_STRONG) {
            animSpeed = 0.8f;
        } else if (state == CHAR_STATE_ROLLING) {
            animSpeed = ROLL_ANIM_SPEED;
        }

        if (fabsf(animSpeed - lastAnimSpeed) > 0.001f) {
            t3d_anim_set_speed(currentAnim, animSpeed);
            lastAnimSpeed = animSpeed;
        }

        t3d_anim_update(currentAnim, dt);
    }

    if (character.isBlending && hasBlendSnapshot) {
        t3d_skeleton_blend(character.skeleton,
                           character.skeletonBlend,
                           character.skeleton,
                           character.blendFactor);
    }

    t3d_skeleton_update(character.skeleton);
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
        .localCapB = {{0.0f, 16.0f, 0.0f}},
        .radius = 8.0f
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
        .maxHealth = 150.0f,
        .health = 100.0f,
        .healthPotions = 3,
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

        update_animations(0.0f, characterState, deltaTime, 0.0f, 0.0f);

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

        update_animations(animationSpeedRatio, characterState, deltaTime, 0.0f, 0.0f);

        character.pos[0] += movementVelocityX * deltaTime;
        character.pos[2] += movementVelocityZ * deltaTime;

        character_anim_apply_pose();
        character_finalize_frame(false);
        return;
    }

    if (scene_is_cutscene_active()) {
        sword_trail_update(deltaTime, false, NULL, NULL);

        apply_friction(deltaTime, 1.0f);
        update_current_speed(0.0f, deltaTime);
        float animationSpeedRatio = currentSpeed;

        update_animations(animationSpeedRatio, characterState, deltaTime, 0.0f, 0.0f);

        character.pos[0] += movementVelocityX * deltaTime;
        character.pos[2] += movementVelocityZ * deltaTime;

        character_update_camera();
        character_anim_apply_pose();
        character_finalize_frame(false);
        return;
    }

    bool jumpJustPressed = false;

    bool leftJustPressed = (btn.b && !lastBPressed);
    bool aJustPressed    = (btn.a && !lastAPressed);

    lastBPressed = btn.b;
    lastAPressed = btn.a;

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

    StickInput stick = normalize_stick((float)joypad.stick_x, (float)joypad.stick_y);

    float axisX = stick_axis_deadzone((float)joypad.stick_x);
    float axisY = stick_axis_deadzone((float)joypad.stick_y);

    g_moveIntentFwd = axisY;
    g_moveIntentMag = fminf(1.0f, sqrtf(axisX*axisX + axisY*axisY));

    // Build the breakaway request TYPE (so it doesn't always roll)
    KnockdownBreakawayReq breakawayReq = KD_BRK_NONE;
    if (characterState == CHAR_STATE_KNOCKDOWN) {
        if (aJustPressed) {
            breakawayReq = KD_BRK_ROLL;
        } else if (leftJustPressed) {
            breakawayReq = KD_BRK_ATTACK;
        } else if (g_moveIntentMag >= KNOCKDOWN_BREAKAWAY_MOVE_MAG) {
            breakawayReq = KD_BRK_MOVE;
        }
    }

    if (!cameraLockOnActive) {
        animLockOnStrafingFlag = false;
        animStrafeDirFlag = 0;
        animStrafeBlendRatio = 0.0f;
    } else {
        const float STRAFE_NEUTRAL_MAG = 0.02f;
        if (stick.magnitude <= STRAFE_NEUTRAL_MAG) {
            if (animStrafeDirFlag != 0) {
                lockonLastDir = animStrafeDirFlag;
                lockonLastW   = animStrafeBlendRatio;
                lockonStrafeExitT = LOCKON_STRAFE_EXIT_DUR;
            }

            animLockOnStrafingFlag = false;
            animStrafeDirFlag = lockonLastDir;
            animStrafeBlendRatio = lockonLastW;
        }
    }

    update_actions(&btn, leftTriggerHeld, leftJustPressed, jumpJustPressed, &stick, breakawayReq, deltaTime);

    {
        float t = character_get_swing_time();
        if (t >= 0.0f) {
            character_play_swing_timed(t);
        }
    }

    const bool wantsMove = (stick.magnitude > 0.0f);
    const bool wantsStrafeIntent = (cameraLockOnActive && fabsf(axisX) > 0.0f);

    if (characterState != CHAR_STATE_ATTACKING &&
        characterState != CHAR_STATE_ATTACKING_STRONG &&
        characterState != CHAR_STATE_KNOCKDOWN &&
        (wantsMove || wantsStrafeIntent))
    {
        float desiredVelX = 0.0f, desiredVelZ = 0.0f;

        if (wantsMove) {
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
        } else {
            desiredVelX = 0.0f;
            desiredVelZ = 0.0f;
        }

        float currentMaxSpeed = (characterState == CHAR_STATE_ROLLING) ? ROLL_SPEED : MAX_MOVEMENT_SPEED;

        if (characterState == CHAR_STATE_ROLLING) {
            accelerate_towards_with_accel(desiredVelX, desiredVelZ, currentMaxSpeed, ROLL_STEER_ACCELERATION, deltaTime);
        } else {
            accelerate_towards(desiredVelX, desiredVelZ, currentMaxSpeed, deltaTime);
        }

        if (cameraLockOnActive && characterState != CHAR_STATE_ROLLING) {
            float absIntent = fabsf(axisX);

            const float STRAFE_INTENT_ENTER = 0.16f;
            const float STRAFE_INTENT_EXIT  = 0.10f;

            if (!animLockOnStrafingFlag) {
                animLockOnStrafingFlag = (absIntent >= STRAFE_INTENT_ENTER);
            } else {
                animLockOnStrafingFlag = (absIntent >= STRAFE_INTENT_EXIT);
            }

            if (animLockOnStrafingFlag) {
                animStrafeDirFlag = (axisX >= 0.0f) ? +1 : -1;
                animStrafeBlendRatio = fminf(1.0f, absIntent);

                lockonStrafeExitT = 0.0f;
                lockonLastDir = animStrafeDirFlag;
                lockonLastW   = animStrafeBlendRatio;
            } else {
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
    }
    else if (characterState == CHAR_STATE_ATTACKING || characterState == CHAR_STATE_ATTACKING_STRONG)
    {
        float frictionScale = (characterState == CHAR_STATE_ATTACKING_STRONG) ? 1.0f : ATTACK_FRICTION_SCALE;
        apply_friction(deltaTime, frictionScale);

        if (characterState == CHAR_STATE_ATTACKING_STRONG) {
            movementVelocityX = 0.0f;
            movementVelocityZ = 0.0f;
        }

        float hitStart = (characterState == CHAR_STATE_ATTACKING_STRONG) ? STRONG_ATTACK_HIT_START : 0.25f;
        float hitEnd   = (characterState == CHAR_STATE_ATTACKING_STRONG) ? STRONG_ATTACK_HIT_END   : 1.0f;
        float damage = (characterState == CHAR_STATE_ATTACKING_STRONG) ? STRONG_ATTACK_DAMAGE : 5.0f;

        if (actionTimer > hitStart && actionTimer < hitEnd) {
            if (!character.currentAttackHasHit && charWeaponCollision) {
                Boss* boss = boss_get_instance();
                if (boss) {
                    boss_apply_damage(boss, damage);
                }
                character.currentAttackHasHit = true;
                character_play_hit();
            }
        }
    }
    else
    {
        float friction = (characterState == CHAR_STATE_ROLLING) ? ROLL_FRICTION_SCALE : 1.0f;
        apply_friction(deltaTime, friction);
    }

    float animInputMag = fmaxf(stick.magnitude, g_moveIntentMag);

    update_current_speed(animInputMag, deltaTime);

    float velMag = sqrtf(movementVelocityX * movementVelocityX + movementVelocityZ * movementVelocityZ);
    float animationSpeedRatio = fminf(1.0f, velMag / MAX_MOVEMENT_SPEED);

    update_animations(animationSpeedRatio, characterState, deltaTime, velMag, animInputMag);
    prevState = characterState;

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

    strong_knockback_update(deltaTime);

    character.pos[0] += movementVelocityX * deltaTime;
    character.pos[2] += movementVelocityZ * deltaTime;

    character_anim_apply_pose();
    character_finalize_frame(true);

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
        // Lock-on camera:
        // - Keep the same general zoomed-out "character camera" feel
        // - Center view on the lock point
        // - Pull back as the character->target distance grows so the whole character stays visible
        float dx = cameraLockOnTarget.v[0] - character.pos[0];
        float dz = cameraLockOnTarget.v[2] - character.pos[2];
        float distXZ = sqrtf(dx*dx + dz*dz);

        // Prefer horizontal direction to avoid extreme pitch changes when targeting low bones.
        float dirX = 0.0f, dirZ = 1.0f;
        if (distXZ > 0.001f) {
            dirX = dx / distXZ;
            dirZ = dz / distXZ;
        } else {
            // Fallback to character forward if we're right on top of the target.
            float yaw = character.rot[1];
            dirX = -fm_sinf(yaw);
            dirZ =  fm_cosf(yaw);
        }

        // Dynamic pullback based on separation (tuned: keep closer by default).
        // Only start pulling back once we're a bit away from the target, and cap the amount.
        const float pullStart = 120.0f;              // world units (XZ)
        float pull = distXZ - pullStart;
        if (pull < 0.0f) pull = 0.0f;

        float extra = pull * 0.25f;
        if (extra > scaledDistance * 0.60f) extra = scaledDistance * 0.60f;

        float desiredDist = (scaledDistance * 0.90f) + extra;

        desiredCamPos.v[0] = character.pos[0] - dirX * desiredDist;
        // Lower the lock-on camera so the character stays visible while aiming at the lock point.
        // Add a small amount of vertical response to the target height, but keep it subtle.
        float dy = cameraLockOnTarget.v[1] - character.pos[1];
        float yResponse = dy * 0.15f;
        if (yResponse < -6.0f) yResponse = -6.0f;
        if (yResponse >  12.0f) yResponse =  12.0f;
        // Keep the lock-on camera lower than follow mode so the character reads better.
        desiredCamPos.v[1] = character.pos[1] + (scaledHeight * 0.35f) + yResponse;
        desiredCamPos.v[2] = character.pos[2] - dirZ * desiredDist;
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
        // When locked-on, the camera should be centered on the selected lock point.
        desiredTarget = cameraLockOnTarget;
    } else {
        (void)forwardTarget; // currently unused in follow mode; kept for future tuning
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
        return;
    }

    // Only knockdown if the hit is strong enough (>= 20 dmg)
    if (amount >= 20.0f) {
        // Prevent re-triggering knockdown if we are already rolling (invuln) or similar
        if (characterState != CHAR_STATE_ROLLING) {
            characterState = CHAR_STATE_KNOCKDOWN;
            actionTimer = 0.0f;
            currentActionDuration = KNOCKDOWN_DURATION;

            knockdownElapsedS = 0.0f; // start knockdown clock

            // freeze regular movement so knockdown is “clean”
            movementVelocityX = 0.0f;
            movementVelocityZ = 0.0f;

            // Optional: only do displacement knockback on hits strictly > 20
            // (keeps "exactly 20" as knockdown-only, without extra shove)
            if (amount > 20.0f) {
                float yaw = character.rot[1];

                // Back direction
                float bx = fm_sinf(yaw);
                float bz = -fm_cosf(yaw);

                // Normalize just in case
                float len = sqrtf(bx*bx + bz*bz);
                if (len > 0.0001f) { bx /= len; bz /= len; }

                strongKnockbackActive   = true;
                strongKnockbackT        = 0.0f;
                strongKnockbackPrevDist = 0.0f;
                strongKnockbackDirX     = bx;
                strongKnockbackDirZ     = bz;
            } else {
                strongKnockbackActive = false;
            }

            switch_to_action_animation_immediate(ANIM_KNOCKDOWN);
        }
    } else {
        // Non-knockdown hits should not leave knockback running
        audio_play_scene_sfx_dist(SCENE1_SFX_CHAR_UMPH, 1.0f, 0.0f);
        animation_utility_set_screen_shake_mag(10.0f);
        strongKnockbackActive = false;
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
