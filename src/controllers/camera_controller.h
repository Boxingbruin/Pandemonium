#include <t3d/t3d.h>

#ifndef CAMERA_CONTROLLER_H
#define CAMERA_CONTROLLER_H

typedef enum {
    CAMERA_NONE,
    CAMERA_CHARACTER,
    CAMERA_FREECAM,
    CAMERA_NOCLIP,
    CAMERA_CUSTOM,
    CAMERA_FIXED,
    CAMERA_TITLE,
    // Add other audio channels as needed
} CameraState;

// TODO: Convert this to a struct, lazy...
extern float camScale;
extern T3DVec3 camPos;
extern T3DVec3 camTarget;
extern T3DVec3 camDir;
extern float camRotX;
extern float camRotY;
extern float camAngle;
extern float camRoll;
extern T3DVec3 up;
extern float FOV;
extern float distanceInFrontOfCamera;

extern CameraState cameraState;
extern CameraState lastCameraState;

extern T3DVec3 customCamPos;
extern T3DVec3 customCamTarget;
extern T3DVec3 customCamDir;

extern T3DVec3 characterCamPos;
extern T3DVec3 characterCamTarget;

// Simple lock-on targeting support
#include <stdbool.h>
extern bool cameraLockOnActive;
extern T3DVec3 cameraLockOnTarget;
extern float cameraLockBlend;    // 0: follow, 1: lock-on

// Third-person camera variables
extern float cameraDistance;      // Distance behind character
extern float cameraHeight;        // Height offset above character  
extern float cameraAngleX;        // Horizontal rotation around character
extern float cameraAngleY;        // Vertical rotation (pitch)
extern float cameraMinY;          // Minimum pitch angle
extern float cameraMaxY;          // Maximum pitch angle
extern float cameraLerpSpeed;     // Camera smoothing speed
extern float cameraSensitivity;   // Camera rotation sensitivity

extern float customCamRoll;

void camera_initialize(T3DVec3 *pos, T3DVec3 *dir, float rotX, float rotY);
void camera_update(T3DViewport *viewport);
void camera_switch_state(CameraState state);
T3DVec3* camera_get_camera_pos(void);
void camera_reset(void);
void camera_reset_third_person(void);
void camera_roll_camera(void);
void camera_mode(CameraState state);
void camera_mode_smooth(CameraState state, float duration);

void camera_breath_active(bool enabled);
void camera_breath_update(float dt);
#endif