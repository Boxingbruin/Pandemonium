#include "video_player_utility.h"

#include <libdragon.h>
#include <t3d/t3d.h>
#include <stdbool.h>

#include "globals.h"
#include "audio_controller.h"
#include "scene.h"
#include "dev.h"

// ----------------------------
// Internal state
// ----------------------------
static bool video_pending = false;
static const char *video_path = NULL;

static bool s_video_inited = false;

static void video_player_init_once(void)
{
    if (s_video_inited) return;
    s_video_inited = true;

    // Only needed once
    yuv_init();

    // Register codecs (order doesn't matter)
    video_register_codec(&h264_codec);
    //video_register_codec(&mpeg1_codec);
}

// ----------------------------
// Local helpers
// ----------------------------
static void init_game_display(void)
{
    if (DITHER_ENABLED) {
        display_init(RESOLUTION_320x240, DEPTH_16_BPP, FRAME_BUFFER_COUNT, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS);
    } else {
        if (ARES_AA_ENABLED) {
            display_init(RESOLUTION_320x240, DEPTH_32_BPP, FRAME_BUFFER_COUNT, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS);
        } else {
            display_init(RESOLUTION_320x240, DEPTH_32_BPP, FRAME_BUFFER_COUNT, GAMMA_NONE, FILTERS_DISABLED);
        }
    }
}

// If you want different behavior after the video ends, change this:
static void on_video_finished(void)
{
    // Restart after movie.
    scene_restart();
}

// ----------------------------
// Public API
// ----------------------------
void video_player_request(const char *rom_video_path)
{
    if (!rom_video_path) return;

    // Ignore duplicate request if one is already queued
    if (video_pending) return;

    video_pending = true;
    video_path = rom_video_path;
}

void video_player_cancel(void)
{
    video_pending = false;
    video_path = NULL;
}

bool video_player_is_pending(void)
{
    return video_pending && video_path;
}

static void osd_freeze_near_end(void *ctx, int frame_idx, float time_sec, fmv_control_t *ctrl)
{
    (void)ctx; (void)frame_idx; (void)ctrl;

    // Freeze at ~last half second (tune this)
    if (time_sec >= 70.5f) {
        // Hard hang on the last frame forever.
        // (You can still poll input here if you want an exit button.)
        while (1) {
            // Optional: allow Start to exit
            // joypad_poll();
            // joypad_buttons_t b = joypad_get_buttons_pressed(JOYPAD_PORT_1);
            // if (b.start) return; // but return would keep playing unless you also stop ctrl
        }
    }
}


bool video_player_pump_and_play(T3DViewport *viewport)
{
    if (!video_player_is_pending())
        return false;

    const char *path = video_path;
    video_pending = false;
    video_path = NULL;

    // Drain queued RSPQ/RDPQ work from the game frame (safe even if not attached)
    rspq_wait();

    // Stop game audio so FMV audio is clean.
    audio_stop_all_sfx();
    audio_stop_music();
    mixer_ch_set_vol(CHANNEL_MUSIC, 1.0f, 1.0f);

    // IMPORTANT: do not call rdpq_sync_pipe() here (we are not attached)
    // Close game display so fmv_play can init its own display
    display_close();

    video_player_init_once();

    // Play the video (blocking). fmv_play handles its own display init/close.
    fmv_play(path, &(fmv_parms_t){
        .osd_callback = osd_freeze_near_end,
    });

    // After FMV returns, display is closed again. Only do safe queue drain.
    rspq_wait();

    // Restore game display and RDPQ
    init_game_display();
    rdpq_init();

    // If your game depends on T3D state after display reinit, you may need:
    // t3d_init((T3DInitParams){});

    on_video_finished();
    return true;
}