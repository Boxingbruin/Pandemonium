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
#include "game_time.h"

#include "globals.h"

#include "scene.h"
#include "character.h"
#include "boss.h"
#include "dialog_controller.h"

// TODO: This should not be declared in the header file, as it is only used externally (temp)
#include "dev.h"

T3DModel* mapModel;
rspq_block_t* mapDpl;
T3DMat4FP* mapMatrix;

// Cutscene state management
typedef enum {
    CUTSCENE_NONE,
    CUTSCENE_BOSS_INTRO,
    CUTSCENE_BOSS_INTRO_WAIT
} CutsceneState;

static CutsceneState cutsceneState = CUTSCENE_BOSS_INTRO;
static float cutsceneTimer = 0.0f;
static bool bossActivated = false;

bool scene_is_cutscene_active(void) {
    return cutsceneState != CUTSCENE_NONE;
}

bool scene_is_boss_active(void) {
    return bossActivated;
}

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
    
    // Load and setup map
    mapModel = t3d_model_load("rom:/testing_map.t3dm");
    rspq_block_begin();
    t3d_model_draw(mapModel);
    mapDpl = rspq_block_end();
    
    // Create map matrix once
    mapMatrix = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_from_srt_euler(mapMatrix, 
        (float[3]){0.1f, 0.1f, 0.1f},    // scale to match character
        (float[3]){0.0f, 0.0f, 0.0f},    // rotation
        (float[3]){0.0f, -5.0f, 0.0f}    // ground level position
    );
    
    // Initialize character
    character_init();
    
    // Set character initial position to be on the ground
    character.pos[0] = 0.0f;
    character.pos[1] = -4.8f;  // Position character feet on map surface
    character.pos[2] = 100.0f;
    character_update_position();  // Ensure matrix is updated

    // Initialize boss model (imported from boss.glb -> boss.t3dm)
    boss_init();

    // Place boss at a dramatic distance from the character for the intro
    boss.pos[0] = 0.0f;  // Dramatic but visible distance from character
    boss.pos[1] = -4.8f; // align to ground level similar to character
    boss.pos[2] = 0.0f;  // Back for dramatic reveal
    boss_update_position();

    // Initialize dialog controller
    dialog_controller_init();

    // Start boss music
    // TODO: Its turned off for now as it gets annoying to listen to and it crackles
    // audio_play_music("rom:/boss_final_phase.wav64", true);

    // Set up camera to focus on boss for intro cutscene
    customCamPos = (T3DVec3){{boss.pos[0]+50.0f, boss.pos[1] + 25.0f, boss.pos[2] + 100.0f}};  // Position camera to view boss
    customCamTarget = (T3DVec3){{boss.pos[0], boss.pos[1] + 15.0f, boss.pos[2]}};  // Look at boss center/chest area
    camera_mode(CAMERA_CUSTOM);

    // Start boss intro cutscene after character and boss are loaded and positioned
    dialog_controller_speak("^A powerful enemy approaches...~\n<Prepare for battle!", 0, 3.0f, false, true);
}

void scene_update(void) 
{
    // Update cutscene state
    cutsceneTimer += deltaTime;
    
    // Check for A button press to skip intro cutscene
    static bool lastAPressed = false;
    bool aPressed = joypad.btn.a;
    bool aJustPressed = aPressed && !lastAPressed;
    lastAPressed = aPressed;
    
    switch (cutsceneState) {
        case CUTSCENE_BOSS_INTRO:
            // During intro cutscene, update character and boss for rendering but disable AI
            character_update();
            boss_update_position();  // Keep boss position updated but no AI
            dialog_controller_update();
            
            // Allow skipping intro with A button
            if (aJustPressed) {
                // Skip dialog and cutscene
                dialog_controller_stop_speaking();
                cutsceneState = CUTSCENE_NONE;
                bossActivated = true;
                // Return camera control to the player
                camera_mode(CAMERA_CHARACTER);
                break;
            }
            
            // Wait for dialog to finish
            if (!dialog_controller_speaking()) {
                cutsceneState = CUTSCENE_BOSS_INTRO_WAIT;
                cutsceneTimer = 0.0f;
            }
            break;
            
        case CUTSCENE_BOSS_INTRO_WAIT:
            // Allow skipping wait period with A button
            if (aJustPressed || cutsceneTimer >= 1.0f) {
                cutsceneState = CUTSCENE_NONE;
                bossActivated = true;
                // Return camera control to the player
                camera_mode(CAMERA_CHARACTER);
            }
            break;
            
        case CUTSCENE_NONE:
            // Normal gameplay
            character_update();
            if (bossActivated) {
                boss_update();
            }
            dialog_controller_update();
            break;
    }

    // Z-target toggle: press Z to toggle lock-on, target updates with boss movement when active
    static bool lastZPressed = false;
    bool zPressed = btn.z;
    
    if (zPressed && !lastZPressed)  // Z button just pressed (rising edge)
    {
        if (!cameraLockOnActive)
        {
            cameraLockOnActive = true;
        }
        else
        {
            cameraLockOnActive = false;
        }
    }
    lastZPressed = zPressed;
    
    // Update target position when lock-on is active
    if (cameraLockOnActive)
    {
        cameraLockOnTarget = (T3DVec3){{ boss.pos[0], boss.pos[1] + 1.5f, boss.pos[2] }};
    }
}

void scene_fixed_update(void) 
{
}

#include "dev/debug_draw.h"

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
        // Draw map at origin - position as ground level
        t3d_matrix_set(mapMatrix, true);
        rspq_block_run(mapDpl);
        
        //bvh_utility_draw_collision_mesh();
        character_draw();
        boss_draw();
    t3d_matrix_pop(1);
    
    // Draw dialog on top of everything
    dialog_controller_draw();
}

void scene_cleanup(void) 
{
    character_delete();
    camera_reset();
    
    // Clean up map
    if (mapModel) {
        t3d_model_free(mapModel);
        mapModel = NULL;
    }
    if (mapDpl) {
        rspq_block_free(mapDpl);
        mapDpl = NULL;
    }
    if (mapMatrix) {
        free_uncached(mapMatrix);
        mapMatrix = NULL;
    }
    boss_delete();
    dialog_controller_free();
    //collision_system_free();
}