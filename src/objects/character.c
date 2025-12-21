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
#include "boss.h"
#include "scene.h"
#include "simple_collision_utility.h"
#include "game_math.h"
#include "display_utility.h"

/*
 Character Controller
 - Responsibilities: input handling, action state (roll/attack/jump), movement + rotation,
     animation selection, and third-person camera follow.
 - Conventions: model forward is +Z at yaw 0, world up is +Y, camera yaw uses `cameraAngleX`.
*/

T3DModel* characterModel;
Character character;

// Animation states - these correspond to the animation indices
typedef enum {
    ANIM_IDLE = 0,
    ANIM_WALK = 1, 
    ANIM_RUN = 2,
    ANIM_ROLL = 3,
    ANIM_ATTACK = 4,
    ANIM_COUNT = 5
} CharacterAnimState;

// Character state for action mechanics
typedef enum {
    CHAR_STATE_NORMAL,
    CHAR_STATE_ROLLING,
    CHAR_STATE_ATTACKING,
    CHAR_STATE_ATTACKING_STRONG,
    CHAR_STATE_JUMPING
} CharacterState;

static CharacterState characterState = CHAR_STATE_NORMAL;
static float actionTimer = 0.0f;
static const float ROLL_DURATION = 0.9f;
static const float ATTACK_DURATION = 0.9f;
static const float STRONG_ATTACK_DURATION = 1.2f;
static const float STRONG_ATTACK_HOLD_THRESHOLD = 0.4f;
static const float STRONG_ATTACK_DAMAGE = 20.0f;
static const float STRONG_ATTACK_HIT_START = 0.35f;
static const float STRONG_ATTACK_HIT_END = 0.9f;
static const float JUMP_DURATION = 0.75f;
static const float JUMP_HEIGHT = 40.0f;
static const float ROLL_SPEED = 250.0f; // Speed boost during roll
static const float STRONG_ATTACK_FRICTION_SCALE = 0.25f;
static const float JUMP_FRICTION_SCALE = 0.0f;   // Preserve run speed while in the air
static const float ROLL_JUMP_WINDOW_START = 0.35f; // fraction of roll duration
static const float ROLL_JUMP_WINDOW_END   = 0.85f; // fraction of roll duration

// Movement state
static float movementVelocityX = 0.0f;
static float movementVelocityZ = 0.0f;
static float currentSpeed = 0.0f; // Track current movement speed for animation

// Input state tracking for edge detection
static bool lastBPressed = false;
static bool lastLPressed = false;
static bool leftTriggerHeld = false;
static float leftTriggerHoldTime = 0.0f;

void character_reset(void)
{
    characterState = CHAR_STATE_NORMAL;
    actionTimer = 0.0f;
    movementVelocityX = 0.0f;
    movementVelocityZ = 0.0f;
    currentSpeed = 0.0f;
    lastBPressed = false;
    lastLPressed = false;
    leftTriggerHeld = false;
    leftTriggerHoldTime = 0.0f;
    character.pos[1] = 0.0f;
}

void character_reset_button_state(void)
{
    // Sync button state to current state to prevent false "just pressed" events
    joypad_buttons_t buttons = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    lastBPressed = buttons.b;
    lastLPressed = buttons.l;
    leftTriggerHeld = false;
    leftTriggerHoldTime = 0.0f;
}

void character_get_velocity(float* outVelX, float* outVelZ)
{
    if (outVelX) *outVelX = movementVelocityX;
    if (outVelZ) *outVelZ = movementVelocityZ;
}

static const float MOVEMENT_ACCELERATION = 7.0f;
static const float MOVEMENT_FRICTION = 12.0f;
static const float MAX_MOVEMENT_SPEED = 200.0f;  // Much faster movement
static const float SPEED_BUILDUP_RATE = 1.5f;    // How fast speed builds up to run
static const float SPEED_DECAY_RATE = 4.0f;      // How fast speed decays when slowing  

// Input and tuning constants
static const float STICK_MAX = 80.0f;
static const float INPUT_DEADZONE = 0.12f;
static const float TURN_RATE = 8.0f;           // rad/sec turn rate cap
static const float IDLE_THRESHOLD = 0.001f;
static const float WALK_THRESHOLD = 0.03f;
static const float RUN_THRESHOLD = 0.7f;
static const float BLEND_MARGIN = 0.2f;        // walk<->run blend zone width
static const float ATTACK_FRICTION_SCALE = 0.3f; // Preserve momentum during attack

// Animation names for recreation (used to reset action animations)
static const char* kAnimNames[ANIM_COUNT] = {
    "Idle", "Walk", "Run", "Roll", "Attack1"
};
static CharacterState prevState = CHAR_STATE_NORMAL;

// ---- Local helpers ----
typedef struct {
    float x;
    float y;
    float magnitude;
} StickInput;

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

static inline bool attack_hit_test(void) {
    SCU_CapsuleFixed cc;
    float sx = character.scale[0];
    float ax = character.capsuleCollider.localCapA.v[0] * sx;
    float ay = character.capsuleCollider.localCapA.v[1] * sx;
    float az = character.capsuleCollider.localCapA.v[2] * sx;
    float bx = character.capsuleCollider.localCapB.v[0] * sx;
    float by = character.capsuleCollider.localCapB.v[1] * sx;
    float bz = character.capsuleCollider.localCapB.v[2] * sx;
    cc.a.v[0] = TO_FIXED(character.pos[0] + ax);
    cc.a.v[1] = TO_FIXED(character.pos[1] + ay);
    cc.a.v[2] = TO_FIXED(character.pos[2] + az);
    cc.b.v[0] = TO_FIXED(character.pos[0] + bx);
    cc.b.v[1] = TO_FIXED(character.pos[1] + by);
    cc.b.v[2] = TO_FIXED(character.pos[2] + bz);
    cc.radius = TO_FIXED(character.capsuleCollider.radius * sx);

    SCU_CapsuleFixed bc;
    float bs = boss.scale[0];
    float bax = boss.capsuleCollider.localCapA.v[0] * bs;
    float bay = boss.capsuleCollider.localCapA.v[1] * bs;
    float baz = boss.capsuleCollider.localCapA.v[2] * bs;
    float bbx = boss.capsuleCollider.localCapB.v[0] * bs;
    float bby = boss.capsuleCollider.localCapB.v[1] * bs;
    float bbz = boss.capsuleCollider.localCapB.v[2] * bs;
    bc.a.v[0] = TO_FIXED(boss.pos[0] + bax);
    bc.a.v[1] = TO_FIXED(boss.pos[1] + bay);
    bc.a.v[2] = TO_FIXED(boss.pos[2] + baz);
    bc.b.v[0] = TO_FIXED(boss.pos[0] + bbx);
    bc.b.v[1] = TO_FIXED(boss.pos[1] + bby);
    bc.b.v[2] = TO_FIXED(boss.pos[2] + bbz);
    bc.radius = TO_FIXED(boss.capsuleCollider.radius * bs);

    bool attackHit = scu_fixed_capsule_vs_capsule(&cc, &bc);

    // Fallback: reach check in front of the player to approximate sword length
    if (!attackHit) {
        float yaw = character.rot[1];
        float reachStart = 1.0f;  // start a little in front of the player
        float reachEnd = 2.5f;    // sword reach
        float hitX = character.pos[0] - fm_sinf(yaw) * reachStart;
        float hitZ = character.pos[2] + fm_cosf(yaw) * reachStart;
        float dx = boss.pos[0] - hitX;
        float dz = boss.pos[2] - hitZ;
        float dist = sqrtf(dx * dx + dz * dz);
        float bossRadius = boss.capsuleCollider.radius * boss.scale[0];
        if (dist <= (reachEnd + bossRadius)) {
            attackHit = true;
        }
    }

    return attackHit;
}

static inline void update_actions(const joypad_buttons_t* buttons, bool leftHeldNow, bool leftJustPressed, bool jumpJustPressed, float inputMagnitude, float dt) {
    if (buttons->a && characterState == CHAR_STATE_NORMAL && inputMagnitude > 0.1f) {
        characterState = CHAR_STATE_ROLLING;
        actionTimer = 0.0f;
    }
    if (jumpJustPressed) {
        if (characterState == CHAR_STATE_NORMAL) {
            characterState = CHAR_STATE_JUMPING;
            actionTimer = 0.0f;
        } else if (characterState == CHAR_STATE_ROLLING) {
            float rollPhase = (ROLL_DURATION > 0.0f) ? (actionTimer / ROLL_DURATION) : 0.0f;
            if (rollPhase >= ROLL_JUMP_WINDOW_START && rollPhase <= ROLL_JUMP_WINDOW_END) {
                characterState = CHAR_STATE_JUMPING;
                actionTimer = 0.0f;
            }
        }
    }
    if (leftJustPressed && characterState == CHAR_STATE_NORMAL) {
        characterState = CHAR_STATE_ATTACKING;
        actionTimer = 0.0f;
        character.currentAttackHasHit = false; // Reset hit flag for new attack
    } else if (characterState == CHAR_STATE_ATTACKING && leftHeldNow && leftTriggerHoldTime >= STRONG_ATTACK_HOLD_THRESHOLD) {
        // Upgrade a light attack to a strong attack if the trigger is held
        characterState = CHAR_STATE_ATTACKING_STRONG;
        actionTimer = 0.0f;
        character.currentAttackHasHit = false;
    }
    if (characterState != CHAR_STATE_NORMAL) {
        actionTimer += dt;
        if (characterState == CHAR_STATE_ROLLING) {
            if (actionTimer >= ROLL_DURATION) {
                characterState = CHAR_STATE_NORMAL;
                actionTimer = 0.0f;
            }
        } else if (characterState == CHAR_STATE_ATTACKING && actionTimer >= ATTACK_DURATION) {
            characterState = CHAR_STATE_NORMAL;
            actionTimer = 0.0f;
        } else if (characterState == CHAR_STATE_ATTACKING_STRONG && actionTimer >= STRONG_ATTACK_DURATION) {
            characterState = CHAR_STATE_NORMAL;
            actionTimer = 0.0f;
        } else if (characterState == CHAR_STATE_JUMPING && actionTimer >= JUMP_DURATION) {
            characterState = CHAR_STATE_NORMAL;
            actionTimer = 0.0f;
            character.pos[1] = 0.0f; // Land back on the ground plane
        }
    }
}

static inline bool character_is_invulnerable(void) {
    // Rolling grants i-frames so boss attacks can be dodged cleanly
    return characterState == CHAR_STATE_ROLLING;
}

static inline void accelerate_towards(float desiredX, float desiredZ, float maxSpeed, float dt) {
    movementVelocityX += (desiredX * maxSpeed - movementVelocityX) * MOVEMENT_ACCELERATION * dt;
    movementVelocityZ += (desiredZ * maxSpeed - movementVelocityZ) * MOVEMENT_ACCELERATION * dt;
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
        currentSpeed += SPEED_BUILDUP_RATE * dt;
        currentSpeed = fminf(currentSpeed, inputMagnitude);
    } else {
        currentSpeed -= SPEED_DECAY_RATE * dt;
        currentSpeed = fmaxf(currentSpeed, 0.0f);
    }
}

static inline void update_animations(float speedRatio, CharacterState state, float dt) {
    if (!character.animations) return;
    for (int i = 0; i < character.animationCount; i++) {
        if (character.animations[i]) t3d_anim_set_playing(character.animations[i], false);
    }
    // On entering an action, recreate the animation to guarantee a fresh start
    if (state == CHAR_STATE_ROLLING && prevState != CHAR_STATE_ROLLING) {
        if (character.animations[ANIM_ROLL]) {
            t3d_anim_destroy(character.animations[ANIM_ROLL]);
            free(character.animations[ANIM_ROLL]);
        }
        character.animations[ANIM_ROLL] = malloc(sizeof(T3DAnim));
        *character.animations[ANIM_ROLL] = t3d_anim_create(characterModel, kAnimNames[ANIM_ROLL]);
        t3d_anim_set_looping(character.animations[ANIM_ROLL], false);
        t3d_anim_set_playing(character.animations[ANIM_ROLL], true);
        t3d_anim_attach(character.animations[ANIM_ROLL], character.skeleton);
    }
    if ((state == CHAR_STATE_ATTACKING || state == CHAR_STATE_ATTACKING_STRONG) && prevState != state) {
        if (character.animations[ANIM_ATTACK]) {
            t3d_anim_destroy(character.animations[ANIM_ATTACK]);
            free(character.animations[ANIM_ATTACK]);
        }
        character.animations[ANIM_ATTACK] = malloc(sizeof(T3DAnim));
        *character.animations[ANIM_ATTACK] = t3d_anim_create(characterModel, kAnimNames[ANIM_ATTACK]);
        t3d_anim_set_looping(character.animations[ANIM_ATTACK], false);
        t3d_anim_set_playing(character.animations[ANIM_ATTACK], true);
        t3d_anim_attach(character.animations[ANIM_ATTACK], character.skeleton);
    }
    if (state == CHAR_STATE_ROLLING) {
        if (character.animations[ANIM_ROLL]) {
            t3d_anim_set_playing(character.animations[ANIM_ROLL], true);
            t3d_anim_update(character.animations[ANIM_ROLL], dt);
        }
        return;
    }
    if (state == CHAR_STATE_JUMPING) {
        // Reuse the roll animation during jump with a quick taper for a smoother roll->jump feel
        if (character.animations[ANIM_ROLL]) {
            float jumpPhase = fminf(1.0f, actionTimer / JUMP_DURATION);
            float rollSpeed = fmaxf(0.4f, 1.0f - jumpPhase * 0.6f);
            t3d_anim_set_playing(character.animations[ANIM_ROLL], true);
            t3d_anim_set_speed(character.animations[ANIM_ROLL], rollSpeed);
            t3d_anim_update(character.animations[ANIM_ROLL], dt);
        }
        return;
    }
    if (state == CHAR_STATE_ATTACKING || state == CHAR_STATE_ATTACKING_STRONG) {
        if (character.animations[ANIM_ATTACK]) {
            t3d_anim_set_playing(character.animations[ANIM_ATTACK], true);
            if (state == CHAR_STATE_ATTACKING_STRONG) {
                t3d_anim_set_speed(character.animations[ANIM_ATTACK], 0.8f);
            } else {
                t3d_anim_set_speed(character.animations[ANIM_ATTACK], 1.0f);
            }
            t3d_anim_update(character.animations[ANIM_ATTACK], dt);
        }
        return;
    }
    if (speedRatio < IDLE_THRESHOLD) {
        if (character.animations[ANIM_IDLE]) {
            t3d_anim_set_playing(character.animations[ANIM_IDLE], true);
            t3d_anim_update(character.animations[ANIM_IDLE], dt);
        }
        return;
    }
    if (speedRatio < WALK_THRESHOLD) {
        if (character.animations[ANIM_WALK]) {
            t3d_anim_set_playing(character.animations[ANIM_WALK], true);
            float transitionSpeed = fmaxf(speedRatio * 2.5f + 0.2f, 0.3f);
            t3d_anim_set_speed(character.animations[ANIM_WALK], transitionSpeed);
            t3d_anim_update(character.animations[ANIM_WALK], dt);
        }
        return;
    }
    if (speedRatio < RUN_THRESHOLD) {
        if (character.animations[ANIM_WALK]) {
            t3d_anim_set_playing(character.animations[ANIM_WALK], true);
            float walkSpeed = fmaxf(speedRatio * 2.0f + 0.3f, 0.5f);
            t3d_anim_set_speed(character.animations[ANIM_WALK], walkSpeed);
            t3d_anim_update(character.animations[ANIM_WALK], dt);
        }
        return;
    }
    if (speedRatio < RUN_THRESHOLD + BLEND_MARGIN) {
        float blendStart = RUN_THRESHOLD;
        float blendEnd = RUN_THRESHOLD + BLEND_MARGIN;
        float blendRatio = (speedRatio - blendStart) / (blendEnd - blendStart);
        blendRatio = fminf(1.0f, fmaxf(0.0f, blendRatio));
        if (character.animations[ANIM_WALK] && character.animations[ANIM_RUN]) {
            t3d_anim_set_playing(character.animations[ANIM_WALK], true);
            t3d_anim_set_playing(character.animations[ANIM_RUN], true);
            float walkSpeed = fmaxf(speedRatio * 2.5f + 0.4f, 0.8f);
            t3d_anim_set_speed(character.animations[ANIM_WALK], walkSpeed);
            float runSpeed = fmaxf(blendRatio * 0.6f + 0.4f, 0.6f);
            t3d_anim_set_speed(character.animations[ANIM_RUN], runSpeed);
            t3d_anim_update(character.animations[ANIM_WALK], dt);
            t3d_anim_update(character.animations[ANIM_RUN], dt);
        }
        return;
    }
    if (character.animations[ANIM_RUN]) {
        t3d_anim_set_playing(character.animations[ANIM_RUN], true);
        float runSpeed = fmaxf(speedRatio * 0.8f + 0.2f, 0.6f);
        t3d_anim_set_speed(character.animations[ANIM_RUN], runSpeed);
        t3d_anim_update(character.animations[ANIM_RUN], dt);
    }
}

// ==== Update Functions ====

/* Initialize character model, skeletons, animations, and camera. */
void character_init(void)
{
    characterModel = t3d_model_load("rom:/catherine.t3dm");

    T3DSkeleton* skeleton = malloc(sizeof(T3DSkeleton));
    *skeleton = t3d_skeleton_create(characterModel);

    T3DSkeleton* skeletonBlend = malloc(sizeof(T3DSkeleton));
    *skeletonBlend = t3d_skeleton_clone(skeleton, false);

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

    if (animationCount > 0)
    {
        animations = malloc(animationCount * sizeof(T3DAnim*));
        for (int i = 0; i < animationCount; i++)
        {
            animations[i] = malloc(sizeof(T3DAnim));
            *animations[i] = t3d_anim_create(characterModel, animationNames[i]);
            t3d_anim_set_looping(animations[i], animationsLooping[i]);
            t3d_anim_set_playing(animations[i], true);
            t3d_anim_attach(animations[i], skeleton);
        }
    }

    rspq_block_begin();
    t3d_model_draw_skinned(characterModel, skeleton);
    rspq_block_t* dpl = rspq_block_end();

    CapsuleCollider collider = {
        .localCapA = {{0.0f, 0.0f, 0.0f}},    // Bottom at feet
        .localCapB = {{0.0f, 20.0f, 0.0f}},  // Top at head height (reduced)
        .radius = 15.0f                       // Smaller radius for tighter collision
    };

    Character newCharacter = {
        .pos = {0.0f, 0.0f, 0.0f},
        .rot = {0.0f, 0.0f, 0.0f},
        .scale = {0.004f, 0.004f, 0.004f},
        .scrollParams = NULL,
        .skeleton = skeleton,
        .skeletonBlend = skeletonBlend,
        .animations = animations,
        .currentAnimation = 0,
        .animationCount = animationCount,
        .capsuleCollider = collider,
        .modelMat = malloc_uncached(sizeof(T3DMat4FP)),
        .dpl = dpl,
        .visible = true,
        .maxHealth = 100.0f,
        .health = 100.0f,
        .damageFlashTimer = 0.0f,
        .currentAttackHasHit = false
    };

    t3d_mat4fp_identity(newCharacter.modelMat);

    character = newCharacter;

    camera_reset_third_person();
    character_update_camera();
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
        if (character.skeleton) {
            t3d_skeleton_update(character.skeleton);
        }
        t3d_mat4fp_from_srt_euler(character.modelMat, character.scale, character.rot, character.pos);
        return;
    }

    // Disable player input during cutscenes
    if (scene_is_cutscene_active()) {
        // Still update animations and apply friction, but no player input
        apply_friction(deltaTime, 1.0f);
        update_current_speed(0.0f, deltaTime); // No input magnitude during cutscenes
        float animationSpeedRatio = currentSpeed;
        update_animations(animationSpeedRatio, characterState, deltaTime);
        
        // Update position with current velocity (with collision check)
        float newPosX = character.pos[0] + movementVelocityX * deltaTime;
        float newPosZ = character.pos[2] + movementVelocityZ * deltaTime;
        
        if (!scene_check_room_bounds(newPosX, character.pos[1], newPosZ)) {
            character.pos[0] = newPosX;
            character.pos[2] = newPosZ;
        } else {
            // Stop movement if collision detected
            movementVelocityX = 0.0f;
            movementVelocityZ = 0.0f;
        }
        
        character_update_camera();
        
        if (character.skeleton) {
            t3d_skeleton_update(character.skeleton);
        }
        
        t3d_mat4fp_from_srt_euler(character.modelMat, character.scale, character.rot, character.pos);
        return;
    }

    joypad_inputs_t joypad = joypad_get_inputs(JOYPAD_PORT_1);
    joypad_buttons_t buttons = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    joypad_buttons_t buttonsReleased = joypad_get_buttons_released(JOYPAD_PORT_1);

    // B is jump now (edge-detected), left trigger is attack/strong attack
    bool jumpJustPressed = buttons.b && !lastBPressed;
    lastBPressed = buttons.b;

    if (buttons.l) {
        leftTriggerHeld = true;
        leftTriggerHoldTime = 0.0f;
    }
    if (leftTriggerHeld) {
        leftTriggerHoldTime += deltaTime;
    }
    if (buttonsReleased.l) {
        leftTriggerHeld = false;
        leftTriggerHoldTime = 0.0f;
    }
    bool leftJustPressed = buttons.l && !lastLPressed;
    lastLPressed = buttons.l;

    StickInput stick = normalize_stick((float)joypad.stick_x, (float)joypad.stick_y);
    update_actions(&buttons, leftTriggerHeld, leftJustPressed, jumpJustPressed, stick.magnitude, deltaTime);

    if (characterState != CHAR_STATE_ATTACKING && characterState != CHAR_STATE_ATTACKING_STRONG && characterState != CHAR_STATE_JUMPING && stick.magnitude > 0.0f) {
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
        accelerate_towards(desiredVelX, desiredVelZ, currentMaxSpeed, deltaTime);
        if (cameraLockOnActive) {
            // During lock-on: if strafing, face movement; if moving forward/back, face target
            bool strafing = fabsf(stick.x) > fabsf(stick.y) * 0.6f;
            if (strafing) {
                update_yaw_from_velocity(deltaTime);
            } else {
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
            }
        } else {
            update_yaw_from_velocity(deltaTime);
        }
    } else if (characterState == CHAR_STATE_ATTACKING || characterState == CHAR_STATE_ATTACKING_STRONG) {
        // Keep momentum during attacks by reducing friction
        float frictionScale = (characterState == CHAR_STATE_ATTACKING_STRONG) ? STRONG_ATTACK_FRICTION_SCALE : ATTACK_FRICTION_SCALE;
        apply_friction(deltaTime, frictionScale);

        // Hit window: apply damage to boss on overlap
        float hitStart = (characterState == CHAR_STATE_ATTACKING_STRONG) ? STRONG_ATTACK_HIT_START : 0.25f;
        float hitEnd = (characterState == CHAR_STATE_ATTACKING_STRONG) ? STRONG_ATTACK_HIT_END : 0.55f;
        float damage = (characterState == CHAR_STATE_ATTACKING_STRONG) ? STRONG_ATTACK_DAMAGE : 10.0f;
        if (actionTimer > hitStart && actionTimer < hitEnd) {
            if (!character.currentAttackHasHit && attack_hit_test()) {
                boss_apply_damage(damage);
                character.currentAttackHasHit = true; // Mark attack as having hit
            }
        }
    } else if (characterState == CHAR_STATE_JUMPING) {
        // Keep horizontal velocity from the takeoff; no air drag so run speed carries through jump
        apply_friction(deltaTime, JUMP_FRICTION_SCALE);
        float jumpPhase = fminf(1.0f, actionTimer / JUMP_DURATION);
        character.pos[1] = fm_sinf(jumpPhase * T3D_PI) * JUMP_HEIGHT;
    } else if (characterState != CHAR_STATE_ROLLING) {
        apply_friction(deltaTime, 1.0f);
    }

    if (characterState != CHAR_STATE_JUMPING) {
        character.pos[1] = 0.0f;
    }

    update_current_speed(stick.magnitude, deltaTime);
    float animationSpeedRatio = currentSpeed;
    update_animations(animationSpeedRatio, characterState, deltaTime);
    prevState = characterState;

    // Calculate proposed new position
    float newPosX = character.pos[0] + movementVelocityX * deltaTime;
    float newPosZ = character.pos[2] + movementVelocityZ * deltaTime;
    
    // Check room bounds collision
    if (scene_check_room_bounds(newPosX, character.pos[1], newPosZ)) {
        // Collision detected - try to clamp position
        // First try X movement only
        if (!scene_check_room_bounds(newPosX, character.pos[1], character.pos[2])) {
            character.pos[0] = newPosX;
            movementVelocityZ = 0.0f;  // Stop Z movement
        }
        // Then try Z movement only
        else if (!scene_check_room_bounds(character.pos[0], character.pos[1], newPosZ)) {
            character.pos[2] = newPosZ;
            movementVelocityX = 0.0f;  // Stop X movement
        }
        // If both cause collision, stop movement
        else {
            movementVelocityX = 0.0f;
            movementVelocityZ = 0.0f;
        }
    } else {
        // No collision, apply movement
        character.pos[0] = newPosX;
        character.pos[2] = newPosZ;
    }

    character_update_camera();

    if (character.skeleton) {
        t3d_skeleton_update(character.skeleton);
    }

	 t3d_mat4fp_from_srt_euler(character.modelMat, character.scale, character.rot, character.pos);
}

void character_update_position(void) 
{
	// Update the full transformation matrix with scale, rotation, and position
	t3d_mat4fp_from_srt_euler(character.modelMat,
		(float[3]){character.scale[0], character.scale[1], character.scale[2]},
		(float[3]){character.rot[0], character.rot[1], character.rot[2]},
		(float[3]){character.pos[0], character.pos[1], character.pos[2]}
	);
}

/* Third-person camera follow with smoothing. Distances scaled to character size. */
void character_update_camera(void)
{
    static bool lastLockOnActive = false;

    float scaledDistance = cameraDistance * 0.1f;
    float scaledHeight = cameraHeight * 0.1f;

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
            cameraLockOnTarget.v[1] - (character.pos[1] + 0.5f),
            cameraLockOnTarget.v[2] - character.pos[2]
        }};
        t3d_vec3_norm(&toTarget);

        // Camera sits behind character along opposite of toTarget, plus height
        desiredCamPos.v[0] = character.pos[0] - toTarget.v[0] * scaledDistance;
        desiredCamPos.v[1] = character.pos[1] + scaledHeight;
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
        character.pos[1] + 1.5f,
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

void character_draw(void) 
{
	t3d_matrix_set(character.modelMat, true);
	rspq_block_run(character.dpl);
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
		// printf("[Character] HP: %.0f/%.0f\n", character.health, character.maxHealth);
	}
	character.damageFlashTimer = 0.3f;
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