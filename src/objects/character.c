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

/*
 Character Controller
 - Responsibilities: input handling, action state (roll/attack), movement + rotation,
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
    CHAR_STATE_ATTACKING
} CharacterState;

static CharacterState characterState = CHAR_STATE_NORMAL;
static float actionTimer = 0.0f;
static const float ROLL_DURATION = 0.9f;
static const float ATTACK_DURATION = 0.9f;
static const float ROLL_SPEED = 250.0f; // Speed boost during roll

// Movement momentum system
static float movementVelocityX = 0.0f;
static float movementVelocityZ = 0.0f;
static float currentSpeed = 0.0f; // Track current movement speed for animation
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

static inline void update_actions(const joypad_buttons_t* buttons, float inputMagnitude, float dt) {
    if (buttons->a && characterState == CHAR_STATE_NORMAL && inputMagnitude > 0.1f) {
        characterState = CHAR_STATE_ROLLING;
        actionTimer = 0.0f;
    }
    if (buttons->b && characterState == CHAR_STATE_NORMAL) {
        characterState = CHAR_STATE_ATTACKING;
        actionTimer = 0.0f;
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
        }
    }
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
    if (state == CHAR_STATE_ATTACKING && prevState != CHAR_STATE_ATTACKING) {
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
    if (state == CHAR_STATE_ATTACKING) {
        if (character.animations[ANIM_ATTACK]) {
            t3d_anim_set_playing(character.animations[ANIM_ATTACK], true);
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

    CapsuleCollider collider = {0};

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
        .visible = true
    };

    t3d_mat4fp_identity(newCharacter.modelMat);

    character = newCharacter;

    camera_reset_third_person();
    character_update_camera();
}

/* Main per-frame update: input, actions, movement, rotation, animation, camera. */
void character_update(void) 
{
    // Disable player input during cutscenes
    if (scene_is_cutscene_active()) {
        // Still update animations and apply friction, but no player input
        apply_friction(deltaTime, 1.0f);
        update_current_speed(0.0f, deltaTime); // No input magnitude during cutscenes
        float animationSpeedRatio = currentSpeed;
        update_animations(animationSpeedRatio, characterState, deltaTime);
        
        // Update position with current velocity
        character.pos[0] += movementVelocityX * deltaTime;
        character.pos[2] += movementVelocityZ * deltaTime;
        
        character_update_camera();
        
        if (character.skeleton) {
            t3d_skeleton_update(character.skeleton);
        }
        
        t3d_mat4fp_from_srt_euler(character.modelMat, character.scale, character.rot, character.pos);
        return;
    }

    joypad_inputs_t joypad = joypad_get_inputs(JOYPAD_PORT_1);
    joypad_buttons_t buttons = joypad_get_buttons_pressed(JOYPAD_PORT_1);

    StickInput stick = normalize_stick((float)joypad.stick_x, (float)joypad.stick_y);
    update_actions(&buttons, stick.magnitude, deltaTime);

    if (characterState != CHAR_STATE_ATTACKING && stick.magnitude > 0.0f) {
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
    } else if (characterState == CHAR_STATE_ATTACKING) {
        // Keep momentum during attacks by reducing friction
        apply_friction(deltaTime, ATTACK_FRICTION_SCALE);

        // Hit window: apply damage to boss on overlap
        if (actionTimer > 0.25f && actionTimer < 0.55f) {
            // Build fixed capsules
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

            if (scu_fixed_capsule_vs_capsule(&cc, &bc)) {
                boss_apply_damage(10.0f);
            }
        }
    } else if (characterState != CHAR_STATE_ROLLING) {
        apply_friction(deltaTime, 1.0f);
    }

    update_current_speed(stick.magnitude, deltaTime);
    float animationSpeedRatio = currentSpeed;
    update_animations(animationSpeedRatio, characterState, deltaTime);
    prevState = characterState;

    character.pos[0] += movementVelocityX * deltaTime;
    character.pos[2] += movementVelocityZ * deltaTime;

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
    float scaledDistance = cameraDistance * 0.1f;
    float scaledHeight = cameraHeight * 0.1f;
    
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