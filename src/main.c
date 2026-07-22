#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3ddebug.h>

#include "globals.h"
#include "game_time.h"
#include "joypad_utility.h"
#include "camera_controller.h"
#include "audio_controller.h"
#include "display_utility.h"
#include "controllers/menu_controller.h"
#include "save_controller.h"
#include "collision_system.h"
#include "scenes/guardian/guardian_scene.h"
#include "scene_controller.h"
#include "dev.h"
#include "dev/crt_safe_area_overlay.h"
#include "controllers/fmv_controller.h"
#include "character/character.h"

int main(void)
{
    if (DEV_MODE)
    {
        dev_tools_init();
    }

    if (DEBUG_DRAW)
    {
        debugDraw = true;
    }
    else
    {
        debugDraw = false;
    }

    // INIT
    asset_init_compression(2);
    dfs_init(DFS_DEFAULT_LOCATION);

    // Safe: in case something left it open (some emus / hot reload flows)
    display_close();

    if (DITHER_ENABLED) {
        display_init(RESOLUTION_320x240, DEPTH_16_BPP, FRAME_BUFFER_COUNT, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS);
    } else {
        if (ARES_AA_ENABLED) {
            display_init(RESOLUTION_320x240, DEPTH_32_BPP, FRAME_BUFFER_COUNT, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS);
        } else {
            display_init(
                RESOLUTION_320x240,
                DEPTH_32_BPP,
                FRAME_BUFFER_COUNT,
                GAMMA_CORRECT,
                FILTERS_RESAMPLE
            );
        }
    }

    rdpq_init();

    // /*
    //  * Opt-in: frozen blocks skip the RSP-side resolver on block replay as long
    //  * as the recorded RDP state baseline still matches the live state. Only
    //  * meaningful between rspq_block_begin_frozen/rspq_block_end_frozen pairs;
    //  * normal (non-frozen) blocks are unaffected by this flag.
    //  */
    // rdpq_config_enable(RDPQ_CFG_FROZEN_BLOCKS);


    audio_initialize();

    /*
     * Fonts registered once after final rdpq_init.
     * Opening credits now run as a normal scene, so they do not close/reinit display.
     */
    rdpq_text_register_font(
        FONT_BUILTIN_DEBUG_MONO,
        rdpq_font_load_builtin(FONT_BUILTIN_DEBUG_MONO)
    );

    rdpq_font_t *font1 = rdpq_font_load("rom:/fonts/unbalanced.font64");
    if (!font1) {
        debugf("[FATAL] failed to load rom:/fonts/unbalanced.font64\n");
        for (;;) {}
    }

    rdpq_text_register_font(FONT_UNBALANCED, font1);

    game_time_init();
    joypad_utility_init();

    save_controller_init();
    (void)save_controller_load_settings();

    t3d_init((T3DInitParams){});
    T3DViewport viewport = t3d_viewport_create();

    if (DEV_MODE) {
        t3d_debug_print_init();
        dev_models_init();
    }

    /*
     * Transitional character ownership:
     * Character is initialized once at boot and is shared by title + Guardian.
     * Title and Guardian may position/reset/state-switch it, but neither scene
     * should call character_init() or character_free() during scene transitions.
     */
    character_init();

    /*
     * Menu controller is shared by title and Guardian.
     * Initialize it before scene_controller_init(), because title_scene_enter()
     * can immediately use title menu state.
     */
    menu_controller_init();

    /*
     * scene_controller now starts opening credits first unless skipped.
     * Flow:
     *   Opening Credits -> Title -> Guardian
     */
    scene_controller_init();

    if (DEV_MODE && debugDraw) {
        /*
         * Keep this RGBA32 if cheap bloom can run while debugDraw is active.
         * The cheap bloom path assumes the scene framebuffer is 32-bit.
         */
        offscreenBuffer = surface_alloc(FMT_RGBA32, SCREEN_WIDTH, SCREEN_HEIGHT);

        if (!offscreenBuffer.buffer) {
            debugf("[FATAL] failed to allocate offscreenBuffer\n");
            for (;;) {}
        }
    }

    for (uint64_t frame = 0;; ++frame)
    {
        // Update time + input first.
        game_time_update();
        joypad_update();

        // Debounced EEPROM save flush.
        save_controller_update();

        // ------------------------------------------------------------
        // VIDEO PUMP
        // Must be before any rdpq_attach() in the frame.
        // ------------------------------------------------------------
        if (video_player_pump_and_play(&viewport)) {
            // Video played. The utility restores display/rdpq/t3d and can restart through scene_controller.
            continue;
        }

        SceneControllerSceneId activeScene = scene_controller_get_active_scene();
        bool isOpeningCredits = activeScene == SCENE_CONTROLLER_SCENE_OPENING_CREDITS;
        bool isTitle = activeScene == SCENE_CONTROLLER_SCENE_TITLE;
        bool isGuardian = activeScene == SCENE_CONTROLLER_SCENE_GUARDIAN;

        surface_t *framebuffer = NULL;

        // Attach render target for the frame.
        if (DEV_MODE && debugDraw) {
            framebuffer = &offscreenBuffer;
            rdpq_attach(framebuffer, display_get_zbuf());
        } else {
            framebuffer = display_get();
            rdpq_attach(framebuffer, display_get_zbuf());
        }

        // ===== UPDATE LOOP =====
        mixer_try_play();

        if (DEV_MODE) {
            dev_controller_update();
        }

        bool devMenuOpen = DEV_MODE && dev_menu_is_open();
        bool cameraNeedsUpdate = (cameraState == CAMERA_FREECAM);

        if (!devMenuOpen)
        {
            /*
             * Opening credits are a real scene now, but they do not need camera,
             * menu, character, or fixed-update ownership.
             */
            if (!isOpeningCredits) {
                camera_update(&viewport);
            }

            /*
             * Title menu input is updated inside title_scene_update()
             * via menu_controller_update_title().
             *
             * Normal menu_controller_update() still asks scene.c for GameState,
             * so only call it while Guardian is active.
             */
            if (isGuardian) {
                menu_controller_update();
            }

            scene_controller_update();

            /*
             * Fixed update currently belongs to Guardian only.
             * Title and Opening Credits do not need it.
             */
            if (isGuardian) {
                scene_fixed_update();
            }
        }
        else
        {
            if (cameraNeedsUpdate && !isOpeningCredits) {
                camera_update(&viewport);
            }

            if (isGuardian) {
                menu_controller_update();
            }
        }

        // Refresh active scene after update in case the scene controller switched scenes.
        activeScene = scene_controller_get_active_scene();
        isOpeningCredits = activeScene == SCENE_CONTROLLER_SCENE_OPENING_CREDITS;
        isTitle = activeScene == SCENE_CONTROLLER_SCENE_TITLE;
        isGuardian = activeScene == SCENE_CONTROLLER_SCENE_GUARDIAN;

        // ===== DRAW LOOP =====
        if (!devMenuOpen || cameraNeedsUpdate || isOpeningCredits) {
            scene_controller_draw(&viewport);
        }

        /*
         * Title, Guardian, and Testing use Tiny3D. Opening Credits are plain RDPQ sprites,
         * so avoid forcing Tiny3D sync for that scene.
         */
        if (!isOpeningCredits) {
            t3d_tri_sync();
        }

        rdpq_sync_pipe();

        /*
         * Framebuffer postprocess.
         *
         * Keep this after scene drawing and Tiny3D sync, but before menu/dev/FPS overlays,
         * so bloom affects the 3D scene but not the UI.
         *
         * display_utility_apply_postprocess() internally checks:
         *   - CHEAP_BLOOM_ENABLED
         *   - displayBloomEnabled
         *   - framebuffer != NULL
         */
        display_utility_apply_postprocess(framebuffer);

        rdpq_sync_pipe();

        /*
         * Shared menu draw.
         * On title, title_scene_update() updates title menu state.
         * On Guardian, main updates pause menu state above.
         * Opening Credits should not draw the menu.
         */
        if (!isOpeningCredits) {
            menu_controller_draw();
        }

        if (DEV_MODE)
        {
            /*
             * Dev arrow/collision drawing are Tiny3D/game-world debug tools.
             * Do not draw them over opening credits.
             */
            if (!isOpeningCredits) {
                dev_draw_update(&viewport);
            }

            dev_update();

            if (debugDraw && isGuardian) {
                collision_draw(&viewport);
            }
        }

        if (SHOW_FPS)
        {
            rdpq_sync_pipe();
            rdpq_text_printf(
                NULL,
                FONT_BUILTIN_DEBUG_MONO,
                250,
                225,
                " %.2f",
                display_get_fps()
            );
        }

        if (DEV_MODE && debugDraw)
        {
            rdpq_detach();

            rdpq_attach(display_get(), display_get_zbuf());
            rdpq_set_mode_standard();
            rdpq_tex_blit(&offscreenBuffer, 0, 0, NULL);

            if (debugDraw && DRAW_CRT_SAFE_AREA) {
                DrawCrtSafeAreaOverlay(display_get_width(), display_get_height());
            }

            rdpq_detach_show();
        }
        else
        {
            if (debugDraw && DRAW_CRT_SAFE_AREA) {
                DrawCrtSafeAreaOverlay(display_get_width(), display_get_height());
            }

            rdpq_detach_show();
        }

        if (DEV_MODE)
        {
            dev_frame_update();
        }

        if (frame >= 30)
        {
            if (DEV_MODE) {
                dev_frames_end_update();
            }

            frame = 0;
        }

        (void)isTitle;
    }

    // Unreachable in normal runtime.
    scene_cleanup();
    character_free();
    menu_controller_free();
    save_controller_free();

    return 0;
}