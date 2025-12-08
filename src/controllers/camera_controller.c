#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>

#include "camera_controller.h"
#include "game_time.h"
#include "joypad_utility.h"

CameraState cameraState = CAMERA_NONE;
CameraState lastCameraState = CAMERA_NONE;

T3DVec3 characterCamPos;
T3DVec3 characterCamTarget;

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
float FOV = 50.0f;
float distanceInFrontOfCamera = 40.0f;

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

void camera_update(T3DViewport *viewport)
{
    if(cameraState == CAMERA_FREECAM)
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

        //t3d_viewport_set_projection(viewport, T3D_DEG_TO_RAD(FOV), 4.0f, 500.0f);
        t3d_viewport_set_projection(viewport, T3D_DEG_TO_RAD(FOV), 4.0f, 1000.0f);
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
        t3d_viewport_set_projection(viewport, T3D_DEG_TO_RAD(FOV), 4.0f, 400.0f);
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
        t3d_viewport_set_projection(viewport, T3D_DEG_TO_RAD(FOV), 4.0f, 400.0f);
        t3d_viewport_look_at(viewport, &camPos, &camTarget, &rolledUp);
    }
    else if(cameraState == CAMERA_CHARACTER)
    {
        
        camPos = characterCamPos;
        camTarget = characterCamTarget;

        camDir.v[0] = camTarget.v[0] - camPos.v[0];  // X component
        camDir.v[1] = camTarget.v[1] - camPos.v[1];  // Y component
        camDir.v[2] = camTarget.v[2] - camPos.v[2];  // Z component
        t3d_vec3_norm(&camDir);

        t3d_viewport_set_projection(viewport, T3D_DEG_TO_RAD(60), 4.0f, 500.0f);
        t3d_viewport_look_at(viewport, &camPos, &camTarget, &up);

    }
}

void camera_mode(CameraState state)
{
    lastCameraState = cameraState;
    cameraState = state;
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
}