#include <libdragon.h>
#include <rspq_profile.h>
#include <t3d/t3d.h>
#include <t3d/t3ddebug.h>

#include "globals.h"
#include "game_time.h"
#include "joypad_utility.h"

#include "camera_controller.h"
#include "audio_controller.h"

#include "display_utility.h"
#include "menu_controller.h"
#include "save_controller.h"

#include "collision_system.h"

#include "scene.h"
#include "dev.h"

int main(void) 
{
    if(DEV_MODE)
    {
        dev_tools_init();
    }
    else
    {
        debugDraw = false;
    }

    // INIT
    asset_init_compression(2);
    // read from cartridge space
    dfs_init(DFS_DEFAULT_LOCATION);
    display_close(); // Close the display to reset it

    if(DITHER_ENABLED) // ONLY ENABLE THIS IF WE ARE EXPERIENCING BAD FRAMERATES ON HARDWARE
    {
        display_init(RESOLUTION_320x240, DEPTH_16_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS);
    }
    else
    {
        if(ARES_AA_ENABLED)
        {
            display_init(RESOLUTION_320x240, DEPTH_32_BPP, 3, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS);
        }
        else
        {
            display_init(RESOLUTION_320x240, DEPTH_32_BPP, 3, GAMMA_NONE, FILTERS_DISABLED);
        }
    }

    rdpq_init();

    // if(DEV_MODE)
    //     rspq_profile_start();
    
    audio_initialize();

    rdpq_text_register_font(FONT_BUILTIN_DEBUG_MONO, rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO));
    
    // Load custom unbalanced font
    rdpq_font_t *font1 = rdpq_font_load("rom:/fonts/unbalanced.font64");
    rdpq_text_register_font(FONT_UNBALANCED, font1);

    game_time_init();
    joypad_utility_init();

    // Initialize save system and load settings (after joypad init)
    save_controller_init();
    if (!save_controller_load_settings()) {
        // Silently use defaults - no error message needed for graceful fallback
    }

    t3d_init((T3DInitParams){});
    T3DViewport viewport = t3d_viewport_create();

    if(DEV_MODE)
    {
        t3d_debug_print_init();
        dev_models_init();
    }

    scene_init();

    // // Initialize menu controller
    menu_controller_init();

    rspq_syncpoint_t syncPoint = 0; // TODO: I have no idea what this does but it's needed for flipbook textures.

    if(DEV_MODE && debugDraw)
    {
        offscreenBuffer = surface_alloc(FMT_RGBA16, SCREEN_WIDTH, SCREEN_HEIGHT);
    }

    for(uint64_t frame = 0;; ++frame)
    {
        // Update the time and calculate delta
        game_time_update();

        joypad_update();
        if(DEV_MODE && debugDraw)
        {
            rdpq_attach(&offscreenBuffer, display_get_zbuf());
        }
        else
        {
            rdpq_attach(display_get(), display_get_zbuf());
        }


        if(syncPoint)rspq_syncpoint_wait(syncPoint); // wait for the RSP to process the previous frame
        
        // ===== UPDATE LOOP =====
        mixer_try_play();
        
        // Update dev controller to check menu state
        if(DEV_MODE)
        {
            dev_controller_update();
        }
        
        // Pause game updates when dev menu is open
        bool devMenuOpen = DEV_MODE && dev_menu_is_open();
        
        // Check if camera is in free camera mode (needs updates even when paused)
        bool cameraNeedsUpdate = (cameraState == CAMERA_FREECAM);
        
        if(!devMenuOpen)
        {
            camera_update(&viewport);

            // Update menu controller first to handle input
            menu_controller_update();

            scene_update();
            scene_fixed_update();
        }
        else
        {
            // Still update camera if in free camera mode (for dev tools)
            if(cameraNeedsUpdate)
            {
                camera_update(&viewport);
            }
            
            // Still update menu controller even when dev menu is open (for pause menu)
            menu_controller_update();
        }

        // ===== DRAW LOOP =====
        // Draw scene when dev menu is not open, or when free camera is active (so we can see the world)
        if(!devMenuOpen || cameraNeedsUpdate)
        {
            scene_draw(&viewport); // Draw scene
        }
        
        // Draw menu on top of everything
        menu_controller_draw();

        syncPoint = rspq_syncpoint_new();
        
        if(DEV_MODE)
        {
            // TODO: There is a reason the update comes after the draw but it shouldnt, this needs to be fixed and flipped.
            dev_draw_update(&viewport); // Draw dev tools if in dev mode
            dev_update();

            if(debugDraw)
                collision_draw(&viewport);
        }
        
        // FPS for Dev
        if(SHOW_FPS)
        {
            rdpq_sync_pipe();
            rdpq_text_printf(NULL, FONT_BUILTIN_DEBUG_MONO, 250, 225, " %.2f", display_get_fps());
        }

        if(DEV_MODE && debugDraw)
        {
            rdpq_detach();
            rdpq_attach(display_get(), display_get_zbuf());
            rdpq_set_mode_standard();
            rdpq_tex_blit(&offscreenBuffer, 0, 0, NULL);
            rdpq_detach_show();
        }
        else
        {
            rdpq_detach_show();
        }

        // TODO: this dev area is messy.  Clean it up.
        if(DEV_MODE)
        {
            dev_frame_update();
        }

        if(frame >= 30)
        {
            if(DEV_MODE)
                dev_frames_end_update();

            frame = 0;
        }

    }

    scene_cleanup();  // Call cleanup before exiting
    menu_controller_free();
    save_controller_free();

    return 0;
}