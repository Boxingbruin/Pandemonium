#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>

#include "camera_controller.h"
#include "game_time.h"
#include "joypad_utility.h"
#include "character.h"
#include "game_math.h"
#include "utilities/animation_utility.h"

#include "globals.h"
#include "video_controller.h"

CameraState cameraState = CAMERA_NONE;
CameraState lastCameraState = CAMERA_NONE;

T3DVec3 characterCamPos;
T3DVec3 characterCamTarget;

// Lock-on state and target point for character camera mode
bool cameraLockOnActive = false;
T3DVec3 cameraLockOnTarget = {{0.0f, 0.0f, 0.0f}};
float cameraLockBlend = 0.0f; // 0: follow character, 1: lock onto target

// Third-person camera variables
float cameraDistance = 1200.0f;      // Distance behind character (zoomed back)
float cameraHeight = 1200.0f;        // Height offset above character  
float cameraAngleX = 0.0f;           // Horizontal rotation around character
float cameraAngleY = -0.5f;          // Vertical rotation (pitch) - slightly downward
float cameraMinY = -1.4f;            // Minimum pitch angle (about -80 degrees)
float cameraMaxY = 1.0f;             // Maximum pitch angle (about 57 degrees)
float cameraLerpSpeed = 8.0f;        // Camera smoothing speed
float cameraSensitivity = 2.0f;      // Camera rotation sensitivity

T3DVec3 customCamPos;
T3DVec3 customCamTarget;
float customCamRoll;
T3DVec3 customCamDir;

T3DVec3 camPos;
T3DVec3 camTarget;
T3DVec3 camDir;
T3DVec3 up;

float camScale = 0.5f;
float camRotX = 0.0f;
float camRotY = 0.0f;
float camAngle = 0.0f;
float camRoll = 0.0f;
float FOV = 60.0f;
float distanceInFrontOfCamera = 100.0f;

// Clipping planes (raised far clip to keep distant geometry visible)
static const float CAMERA_NEAR_CLIP = 4.0f;
static const float CAMERA_FAR_CLIP = 2000.0f;

// Camera transition (for smooth blends between modes)
static bool cameraTransitionActive = false;
static CameraState cameraTransitionTarget = CAMERA_NONE;
static float cameraTransitionTime = 0.0f;
static float cameraTransitionDuration = 0.0f;
static T3DVec3 cameraTransitionStartPos;
static T3DVec3 cameraTransitionStartTarget;

static bool  breathEnabled = false;

static float breathT = 0.0f;
static float breathHz = 0.1f;
static float breathAmpY = 1.0f;
static float breathAmpX = 1.0f;
static float breathSmooth = 1.0f;

static float breathY = 0.0f;
static float breathX = 0.0f;

static bool  breathBaseValid = false;
static float breathBasePos[3];
static float breathBaseTarget[3];

static void vec3_lerp_local(T3DVec3 *out, const T3DVec3 *a, const T3DVec3 *b, float t)
{
	out->v[0] = a->v[0] + (b->v[0] - a->v[0]) * t;
	out->v[1] = a->v[1] + (b->v[1] - a->v[1]) * t;
	out->v[2] = a->v[2] + (b->v[2] - a->v[2]) * t;
}

static inline float tri_wave(float x) {
    // triangle in [-1,1], period 2π, based on sine -> asin
    return (2.0f / 3.14159265f) * asinf(sinf(x));
}

static inline void vec3_cross_local(T3DVec3 *out, const T3DVec3 *a, const T3DVec3 *b)
{
	out->v[0] = a->v[1] * b->v[2] - a->v[2] * b->v[1];
	out->v[1] = a->v[2] * b->v[0] - a->v[0] * b->v[2];
	out->v[2] = a->v[0] * b->v[1] - a->v[1] * b->v[0];
}

static inline void camera_apply_screen_shake(T3DVec3 *pos, T3DVec3 *target, const T3DVec3 *upVec)
{
	const float sx = animation_utility_get_shake_offset_x();
	const float sy = animation_utility_get_shake_offset_y();
	if (sx == 0.0f && sy == 0.0f) return;

	// Build camera-space right/up from forward + provided up vector.
	T3DVec3 forward = {{
		target->v[0] - pos->v[0],
		target->v[1] - pos->v[1],
		target->v[2] - pos->v[2],
	}};
	t3d_vec3_norm(&forward);

	T3DVec3 right;
	vec3_cross_local(&right, &forward, upVec);
	t3d_vec3_norm(&right);

	T3DVec3 up2;
	vec3_cross_local(&up2, &right, &forward);
	t3d_vec3_norm(&up2);

	const float ox = right.v[0] * sx + up2.v[0] * sy;
	const float oy = right.v[1] * sx + up2.v[1] * sy;
	const float oz = right.v[2] * sx + up2.v[2] * sy;

	pos->v[0] += ox; pos->v[1] += oy; pos->v[2] += oz;
	target->v[0] += ox; target->v[1] += oy; target->v[2] += oz;
}

static void camera_get_view_for_state(CameraState state, T3DVec3 *outPos, T3DVec3 *outTarget)
{
	switch (state)
	{
		case CAMERA_CHARACTER:
			*outPos = characterCamPos;
			*outTarget = characterCamTarget;
			break;
		case CAMERA_CUSTOM:
			*outPos = customCamPos;
			*outTarget = customCamTarget;
			break;
		case CAMERA_FREECAM:
		case CAMERA_FIXED:
		default:
			*outPos = camPos;
			*outTarget = camTarget;
			break;
	}
}

void camera_initialize(T3DVec3 *pos, T3DVec3 *dir, float rotX, float rotY) 
{
    // Camera
    camPos = *pos;
    camDir = *dir;

    T3DVec3 targetPos = {{
        camPos.v[0] + camDir.v[0] * distanceInFrontOfCamera,
        camPos.v[1] + camDir.v[1] * distanceInFrontOfCamera,
        camPos.v[2] + camDir.v[2] * distanceInFrontOfCamera
    }};

    camTarget = targetPos;

    camRotX = rotX;
    camRotY = rotY;

    up = (T3DVec3){{0.0f, 1.0f, 0.0f}};

    characterCamPos = camPos;
    characterCamTarget = camTarget;

    customCamPos = camPos;
    customCamTarget = camTarget;
}

void camera_breath_active(bool enabled)
{
    breathEnabled = enabled;

    if (enabled) {
        breathBasePos[0] = customCamPos.v[0];
        breathBasePos[1] = customCamPos.v[1];
        breathBasePos[2] = customCamPos.v[2];

        breathBaseTarget[0] = customCamTarget.v[0];
        breathBaseTarget[1] = customCamTarget.v[1];
        breathBaseTarget[2] = customCamTarget.v[2];

        breathBaseValid = true;

        breathT = 0.0f;
        breathY = 0.0f;
        breathX = 0.0f;
    } else {
        breathBaseValid = false;
    }
}

void camera_breath_update(float dt)
{
    if (!breathEnabled) return;

    if (!breathBaseValid) {
        breathBasePos[0] = customCamPos.v[0];
        breathBasePos[1] = customCamPos.v[1];
        breathBasePos[2] = customCamPos.v[2];

        breathBaseTarget[0] = customCamTarget.v[0];
        breathBaseTarget[1] = customCamTarget.v[1];
        breathBaseTarget[2] = customCamTarget.v[2];

        breathBaseValid = true;
    }

    breathT += dt;

    const float TAU = 6.2832f;
    float x = breathT * TAU * breathHz;

    float sinw = sinf(x);
    float tri  = tri_wave(x);

    // 0.0 = pure sine (most hang), 1.0 = pure triangle (least hang)
    const float triMix = 0.65f;
    float w = (1.0f - triMix) * sinw + triMix * tri;

    float rawY = breathAmpY * w;

    // You can honestly set this to 0 (or remove entirely) since triangle is already smooth-ish
    float k = 1.0f - expf(-breathSmooth * dt);
    k = clampf(k, 0.0f, 1.0f);
    breathY += (rawY - breathY) * k;

    customCamPos.v[0]    = breathBasePos[0];
    customCamPos.v[1]    = breathBasePos[1] + breathY;
    customCamPos.v[2]    = breathBasePos[2];

    customCamTarget.v[0] = breathBaseTarget[0];
    customCamTarget.v[1] = breathBaseTarget[1] + breathY * 0.95f;
    customCamTarget.v[2] = breathBaseTarget[2];
}

void camera_set_projection(T3DViewport *viewport)
{
    if(!hdAspect)
    {
        t3d_viewport_set_projection(viewport, T3D_DEG_TO_RAD(FOV), CAMERA_NEAR_CLIP, CAMERA_FAR_CLIP);
    }
    else
    {
        t3d_viewport_set_perspective(
            viewport,
            T3D_DEG_TO_RAD(FOV),
            1.7777778f,
            CAMERA_NEAR_CLIP,
            CAMERA_FAR_CLIP
        );
    }
}

void camera_update(T3DViewport *viewport)
{
	animation_utility_screen_shake_update();

	if (cameraTransitionActive)
	{
		cameraTransitionTime += deltaTime;
		float t = (cameraTransitionDuration > 0.0f) ? (cameraTransitionTime / cameraTransitionDuration) : 1.0f;
		if (t > 1.0f) t = 1.0f;

		T3DVec3 endPos;
		T3DVec3 endTarget;
		camera_get_view_for_state(cameraTransitionTarget, &endPos, &endTarget);

		// Smoothstep easing for a softer blend
		float s = t * t * (3.0f - 2.0f * t);
		vec3_lerp_local(&camPos, &cameraTransitionStartPos, &endPos, s);
		vec3_lerp_local(&camTarget, &cameraTransitionStartTarget, &endTarget, s);

		camDir.v[0] = camTarget.v[0] - camPos.v[0];
		camDir.v[1] = camTarget.v[1] - camPos.v[1];
		camDir.v[2] = camTarget.v[2] - camPos.v[2];
		t3d_vec3_norm(&camDir);

        camera_set_projection(viewport);

		camera_apply_screen_shake(&camPos, &camTarget, &up);
		t3d_viewport_look_at(viewport, &camPos, &camTarget, &up);

		if (t >= 1.0f)
		{
			cameraTransitionActive = false;
			lastCameraState = cameraState;
			cameraState = cameraTransitionTarget;
		}
		return;
	}

    if(cameraState == CAMERA_CHARACTER)
    {
        // Handle camera rotation input with C-buttons
        float rotateX = 0.0f;
        float rotateY = 0.0f;
        
        // When Z is held (lock-on / lock target cycling), don't rotate the free camera orbit with C-left/C-right.
        if(joypad.btn.c_left && !joypad.btn.z)
        {
            rotateX = 1.0f;
        }
        else if(joypad.btn.c_right && !joypad.btn.z)
        {
            rotateX = -1.0f;
        }
        
        if(joypad.btn.c_down)
        {
            rotateY = 1.0f;
        }
        else if(joypad.btn.c_up)
        {
            rotateY = -1.0f;
        }
        
        // Apply rotation with sensitivity and delta time
        cameraAngleX += rotateX * cameraSensitivity * deltaTime;
        cameraAngleY += rotateY * cameraSensitivity * deltaTime;
        
        // Clamp vertical rotation
        if(cameraAngleY < cameraMinY) cameraAngleY = cameraMinY;
        if(cameraAngleY > cameraMaxY) cameraAngleY = cameraMaxY;
        
        // Camera reset with L button
        if(joypad.btn.l)
        {
            camera_reset_third_person();
        }
        
        // Set up camera viewport
        camPos = characterCamPos;
        camTarget = characterCamTarget;

        camDir.v[0] = camTarget.v[0] - camPos.v[0];  // X component
        camDir.v[1] = camTarget.v[1] - camPos.v[1];  // Y component
        camDir.v[2] = camTarget.v[2] - camPos.v[2];  // Z component
        t3d_vec3_norm(&camDir);

        camera_set_projection(viewport);

        camera_apply_screen_shake(&camPos, &camTarget, &up);
        t3d_viewport_look_at(viewport, &camPos, &camTarget, &up);
    }
    else if(cameraState == CAMERA_FREECAM)
    {
        float camSpeed = deltaTime;
        float camRotSpeed = deltaTime;

        float moveHorrDir = 0;
        float moveVirtDir = 0;

        camDir.v[0] = fm_cosf(camRotX) * fm_cosf(camRotY);
        camDir.v[1] = fm_sinf(camRotY);
        camDir.v[2] = fm_sinf(camRotX) * fm_cosf(camRotY);
        t3d_vec3_norm(&camDir);

        if((float)joypad.btn.c_left)
        {
            moveHorrDir = -1;
        }
        else if((float)joypad.btn.c_right)
        {
            moveHorrDir = 1;
        }

        if((float)joypad.btn.c_down)
        {
            moveVirtDir = 1;
        }
        else if((float)joypad.btn.c_up)
        {
            moveVirtDir = -1;
        }

        camRotX += moveHorrDir * camRotSpeed;
        camRotY += moveVirtDir * camRotSpeed;

        camPos.v[0] += camDir.v[0] * (float)joypad.stick_y * camSpeed;
        camPos.v[1] += camDir.v[1] * (float)joypad.stick_y * camSpeed;
        camPos.v[2] += camDir.v[2] * (float)joypad.stick_y * camSpeed;

        camPos.v[0] += camDir.v[2] * (float)joypad.stick_x * -camSpeed;
        camPos.v[2] -= camDir.v[0] * (float)joypad.stick_x * -camSpeed;


        if(joypad.btn.b)
        {
            camPos.v[1] += camSpeed * 60.0f;
        }
        if(joypad.btn.a)
        {
            camPos.v[1] -= camSpeed * 60.0f;
        }

        camTarget.v[0] = camPos.v[0] + camDir.v[0] * distanceInFrontOfCamera;
        camTarget.v[1] = camPos.v[1] + camDir.v[1] * distanceInFrontOfCamera;
        camTarget.v[2] = camPos.v[2] + camDir.v[2] * distanceInFrontOfCamera;

        camera_set_projection(viewport);

        camera_apply_screen_shake(&camPos, &camTarget, &up);
        t3d_viewport_look_at(viewport, &camPos, &camTarget, &up);

    }
    else if(cameraState == CAMERA_CUSTOM)
    {
        customCamDir.v[0] = customCamTarget.v[0] - customCamPos.v[0];  // X component
        customCamDir.v[1] = customCamTarget.v[1] - customCamPos.v[1];  // Y component
        customCamDir.v[2] = customCamTarget.v[2] - customCamPos.v[2];  // Z component
        t3d_vec3_norm(&customCamDir);

        T3DVec3 rolledUp;  // Keep this as a T3DVec3 for the final output

        if (camRoll != 0.0f)
        {
            T3DMat4 rollMat;
            t3d_mat4_rotate(&rollMat, &customCamDir, camRoll);  // Rotate around camDir (camera forward)
        
            // World Up remains a T3DVec3 as expected
            T3DVec3 worldUp = {{0.0f, 1.0f, 0.0f}};  // World Up as a 3D vector
        
            // Perform the multiplication with the 3x3 portion of the matrix
            t3d_mat3_mul_vec3(&rolledUp, &rollMat, &worldUp);  // Now using 3x3 matrix multiplication
        
        }
        else
        {
            // No roll, use default world-up vector
            rolledUp = (T3DVec3){{0.0f, 1.0f, 0.0f}};  // No rotation, use default world up
        }
        
        // Pass the rolled-up vector to the camera look-at function
        camera_set_projection(viewport);
        
        camera_apply_screen_shake(&customCamPos, &customCamTarget, &rolledUp);
        t3d_viewport_look_at(viewport, &customCamPos, &customCamTarget, &rolledUp);
    }
    else if(cameraState == CAMERA_FIXED)
    {
        camDir.v[0] = camTarget.v[0] - camPos.v[0];  // X component
        camDir.v[1] = camTarget.v[1] - camPos.v[1];  // Y component
        camDir.v[2] = camTarget.v[2] - camPos.v[2];  // Z component
        t3d_vec3_norm(&camDir);

        T3DVec3 rolledUp;  // Keep this as a T3DVec3 for the final output

        if (camRoll != 0.0f)
        {
            T3DMat4 rollMat;
            t3d_mat4_rotate(&rollMat, &camDir, camRoll);  // Rotate around camDir (camera forward)
        
            // World Up remains a T3DVec3 as expected
            T3DVec3 worldUp = {{0.0f, 1.0f, 0.0f}};  // World Up as a 3D vector
        
            // Perform the multiplication with the 3x3 portion of the matrix
            t3d_mat3_mul_vec3(&rolledUp, &rollMat, &worldUp);  // Now using 3x3 matrix multiplication
        
        }
        else
        {
            // No roll, use default world-up vector
            rolledUp = (T3DVec3){{0.0f, 1.0f, 0.0f}};  // No rotation, use default world up
        }
        
        // Pass the rolled-up vector to the camera look-at function
        camera_set_projection(viewport);
        camera_apply_screen_shake(&camPos, &camTarget, &rolledUp);
        t3d_viewport_look_at(viewport, &camPos, &camTarget, &rolledUp);
    }
    else if(cameraState == CAMERA_TITLE)
    {
        customCamDir.v[0] = customCamTarget.v[0] - customCamPos.v[0];  // X component
        customCamDir.v[1] = customCamTarget.v[1] - customCamPos.v[1];  // Y component
        customCamDir.v[2] = customCamTarget.v[2] - customCamPos.v[2];  // Z component
        t3d_vec3_norm(&customCamDir);

        T3DVec3 rolledUp;  // Keep this as a T3DVec3 for the final output

        if (camRoll != 0.0f)
        {
            T3DMat4 rollMat;
            t3d_mat4_rotate(&rollMat, &customCamDir, camRoll);  // Rotate around camDir (camera forward)
        
            // World Up remains a T3DVec3 as expected
            T3DVec3 worldUp = {{0.0f, 1.0f, 0.0f}};  // World Up as a 3D vector
        
            // Perform the multiplication with the 3x3 portion of the matrix
            t3d_mat3_mul_vec3(&rolledUp, &rollMat, &worldUp);  // Now using 3x3 matrix multiplication
        
        }
        else
        {
            // No roll, use default world-up vector
            rolledUp = (T3DVec3){{0.0f, 1.0f, 0.0f}};  // No rotation, use default world up
        }
        
        // Pass the rolled-up vector to the camera look-at function
        camera_set_projection(viewport);
        camera_apply_screen_shake(&customCamPos, &customCamTarget, &rolledUp);
        t3d_viewport_look_at(viewport, &customCamPos, &customCamTarget, &rolledUp);
    }
}

void camera_mode(CameraState state)
{
    lastCameraState = cameraState;
    cameraState = state;
}

void camera_mode_smooth(CameraState state, float duration)
{
	if (duration <= 0.0f)
	{
		camera_mode(state);
		return;
	}

	lastCameraState = cameraState;
	cameraTransitionTarget = state;
	cameraTransitionDuration = duration;
	cameraTransitionTime = 0.0f;
	camera_get_view_for_state(cameraState, &cameraTransitionStartPos, &cameraTransitionStartTarget);
	cameraTransitionActive = true;
}

void camera_roll_camera(void)
{
    camRoll = customCamRoll;
}

T3DVec3* camera_get_camera_pos(void)
{
    return &camPos;
}

void camera_reset(void)
{
    camPos = (T3DVec3){{0.0f, 0.0f, 0.0f}};
    camTarget = (T3DVec3){{0.0f, 0.0f, 0.0f}};
    camDir = (T3DVec3){{0.0f, 0.0f, 0.0f}};
    camRotX = 0.0f;
    camRotY = 0.0f;
    camAngle = 0.0f;

    customCamPos = camPos;
    customCamTarget = camTarget;

    characterCamPos = camPos;
    characterCamTarget = camTarget;

    cameraLockOnActive = false;
    cameraLockOnTarget = (T3DVec3){{0.0f, 0.0f, 0.0f}};
    cameraLockBlend = 0.0f;
}

void camera_reset_third_person(void)
{
    cameraAngleX = 0.0f;
    cameraAngleY = -0.5f;
}