#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include <t3d/t3ddebug.h>

#include "audio_controller.h"

#include "camera_controller.h"

#include "joypad_utility.h"
#include "general_utility.h"
#include "game_lighting.h"

#include "globals.h"

#include "scene.h"

// TODO: This should not be declared in the header file, as it is only used externally (temp)
#include "dev.h"


void scene_init(void) 
{
    cameraState = CAMERA_CHARACTER;
    lastCameraState = CAMERA_CHARACTER;

    camera_initialize(
        &(T3DVec3){{16.0656f, 11.3755f, -1.6229f}}, 
        &(T3DVec3){{0,0,1}}, 
        1.544792654048f, 
        4.05f
    );
    
    // ==== Lighting ====
    game_lighting_initialize();
    colorAmbient[2] = 100;
    colorAmbient[1] = 100;
    colorAmbient[0] = 100;
    colorAmbient[3] = 0xFF;

    colorDir[2] = 0xFF;
    colorDir[1] = 0xFF;
    colorDir[0] = 0xFF;
    colorDir[3] = 0xFF;

    lightDirVec = (T3DVec3){{-0.9833f, 0.1790f, -0.0318f}};
    t3d_vec3_norm(&lightDirVec);
}

void scene_update(void) 
{

}

void scene_fixed_update(void) 
{
}

void scene_draw(T3DViewport *viewport) 
{
    t3d_frame_start();
    t3d_viewport_attach(viewport);

    // Fog
    color_t fogColor = (color_t){242, 218, 166, 0xFF};
    rdpq_set_prim_color((color_t){0xFF, 0xFF, 0xFF, 0xFF});
    rdpq_mode_fog(RDPQ_FOG_STANDARD);
    rdpq_set_fog_color(fogColor);

    t3d_screen_clear_color(RGBA32(0, 0, 0, 0xFF));
    t3d_screen_clear_depth();

    t3d_fog_set_range(150.0f, 450.0f);
    t3d_fog_set_enabled(true);

    // Lighting
    t3d_light_set_ambient(colorAmbient);
    t3d_light_set_directional(0, colorDir, &lightDirVec);
    t3d_light_set_count(1);

    t3d_matrix_push_pos(1);
        //bvh_utility_draw_collision_mesh();
    t3d_matrix_pop(1);
}

void demo_scene_cleanup(void) 
{
    camera_reset();
    //collision_system_free();
}