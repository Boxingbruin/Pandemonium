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

bool video_player_pump_and_play(T3DViewport *viewport)
{
    if (!video_player_is_pending())
        return false;

    // Latch and clear first (so if something requests again during playback, it queues cleanly)
    const char *path = video_path;
    video_pending = false;
    video_path = NULL;

    // ---- Quiesce the pipeline before ripping display out ----
    rspq_wait();

    // If you are currently attached (you should NOT be if you call this before rdpq_attach),
    // detaching is still safe in most libdragon builds. If it asserts in version,
    // you can remove this line.
    // rdpq_detach();
    // rspq_wait();

    // Stop game audio so FMV audio is clean.
    audio_stop_all_sfx();
    audio_stop_music();
    mixer_ch_set_vol(CHANNEL_MUSIC, 1.0f, 1.0f);

    // Close game display so fmv_play can call display_init internally
    display_close();

    // ---- Play the video (blocking) ----
    // fmv_play will:
    // - display_init(video resolution)
    // - yuv_init
    // - try to load audio (video.wav64) unless you disable audio or specify audio_fn
    // - display_close() at end
    video_player_init_once();
    fmv_play(path, &(fmv_parms_t){
        .osd_callback = NULL,
        // If you ever want to disable audio:
        // .disable_audio = true,
    });

    // ---- Restore game display and render systems ----
    init_game_display();

    // RDPQ can be left in a weird state after display teardown/reinit; re-init is safest.
    rdpq_init();

    // T3D can depend on current display/surfaces; re-init is the "always works" reset.
    // (If you prove you don't need it, you can remove it later.)
    //t3d_init((T3DInitParams){});

    // Recreate viewport if caller passed one in (recommended).
    if (viewport) {
        *viewport = t3d_viewport_create();
    }

    // Optional post-video action:
    on_video_finished();

    return true;
}