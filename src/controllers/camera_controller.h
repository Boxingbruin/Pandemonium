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

extern float customCamRoll;

void camera_initialize(T3DVec3 *pos, T3DVec3 *dir, float rotX, float rotY);
void camera_update(T3DViewport *viewport);
void camera_switch_state(CameraState state);
T3DVec3* camera_get_camera_pos(void);
void camera_reset(void);
void camera_roll_camera(void);
void camera_mode(CameraState state);
#endif