#include "video_player_utility.h"

#include <libdragon.h>
#include <t3d/t3d.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <alloca.h>
#include <sys/stat.h>

#include <fmv.h>
#include <video.h>
#include <video_sync.h>
#include <yuv.h>
#include <display.h>
#include <wav64.h>
#include <mixer.h>
#include <subtitles.h>
#include <graphics.h>
#include <rdpq.h>
#include <rdpq_attach.h>
#include <joypad.h>

#include "globals.h"
#include "audio_controller.h"
#include "scene.h"
#include "../managers/cutscene_manager.h"
#include "dev.h"

/*
 * Decoded-picture buffer sizing (see libdragon h264bsdInitDpb): dpbSize includes this many
 * extra *slots* beyond the stream's max DPB. Stock fmv_play uses 8 — very heavy.
 *
 * OOM at end-of-stream often hits h264bsdResetDpb/h264bsdInitDpb (SPS activation / flush)
 * when the heap no longer has room for one contiguous DPB layout; keeping this at 0 avoids
 * lookahead via video_poll() and minimizes extra slots (sequential decode only).
 * Override with -DPANDEMONIUM_FMV_BUFFERED_PICS=2 if you need audio-sync lookahead on slow paths.
 */
#ifndef PANDEMONIUM_FMV_BUFFERED_PICS
#define PANDEMONIUM_FMV_BUFFERED_PICS 0
#endif

/** FMV OSD: same A + "skip" overlay as cutscenes; A or Start ends playback (fmv_parms_t::osd_callback). */
static void video_player_fmv_osd(void *osd_ctx, int frame_idx, float time_sec, fmv_control_t *ctrl)
{
    (void)osd_ctx;
    (void)frame_idx;
    (void)time_sec;

    cutscene_manager_draw_skip_overlay(true);

    joypad_poll();
    joypad_buttons_t pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    if (pressed.start || pressed.a) {
        ctrl->stop(ctrl);
    }
}

/*
 * Same behavior as libdragon fmv_play(), but with a lower H264 decode buffer count
 * for real hardware.
 *
 * IMPORTANT: this variant intentionally reuses the already-active game display.
 * We do not call display_init/display_close here, which avoids an additional
 * framebuffer allocation spike at video launch on hardware.
 */
static void pandemonium_fmv_play(const char *video_fn, const fmv_parms_t *parms)
{
    video_t *video = video_open(video_fn, &(video_parms_t){ .buffered_pics = PANDEMONIUM_FMV_BUFFERED_PICS });
    video_info_t info = video_get_info(video);
    if (!parms) {
        parms = alloca(sizeof(fmv_parms_t));
        memset((void *)parms, 0, sizeof(fmv_parms_t));
    }

    yuv_init();
    yuv_blitter_t yuv = yuv_blitter_new_fmv(
        info.width, info.height,
        display_get_width(), display_get_height(),
        &(yuv_fmv_parms_t){ .cs = &info.colorspace });

    display_set_fps_limit(info.framerate);

    wav64_t *audio = NULL;
    if (!parms->disable_audio) {
        const char *audio_fn = parms->audio_fn;
        if (!audio_fn) {
            const char *ext = strrchr(video_fn, '.');
            assertf(ext, "Audio filename not specified for video playback");

            audio_fn = alloca(strlen(video_fn) + 7);
            size_t base_len = ext - video_fn;
            strncpy((char *)audio_fn, video_fn, base_len);
            strcpy((char *)audio_fn + base_len, ".wav64");
        }

        struct stat st;
        if (stat(audio_fn, &st) == 0) {
            audio = wav64_load(audio_fn, NULL);
        }
    }

    subtitles_t *subs = NULL;
    subrenderer_t *subrenderer = parms->sub_renderer;
    if (!parms->disable_subtitles) {
        const char *subs_fn = parms->subtitles_fn;
        if (!subs_fn) {
            const char *ext = strrchr(video_fn, '.');
            assertf(ext, "Subtitle filename not specified for video playback");

            subs_fn = alloca(strlen(video_fn) + 7);
            size_t base_len = ext - video_fn;
            strncpy((char *)subs_fn, video_fn, base_len);
            strcpy((char *)subs_fn + base_len, ".sub64");
        }

        struct stat st;
        if (stat(subs_fn, &st) == 0) {
            subs = subtitles_load(subs_fn);

            if (!subrenderer)
                subrenderer = subrenderer_create_rdpq(
                    &(subrenderer_rdpq_parms_t){
                        .bkg_color = RGBA32(0, 0, 0, 128),
                    });
        }
    }

    int frame_idx = 0;
    bool paused = false;
    bool abort = false;
    video_sync_t *vsync = NULL;

    void ctrl_pause(fmv_control_t *ctrl, bool pause) { paused = pause; }
    void ctrl_stop(fmv_control_t *ctrl) { abort = true; }
    int ctrl_seek_frame(fmv_control_t *ctrl, int idx, bool exact)
    {
        int new_idx = video_seek(video, idx);
        if (new_idx < 0) return -1;
        frame_idx = new_idx;
        if (exact) {
            while (frame_idx < idx) {
                if (!video_next_frame(video)) break;
                frame_idx++;
            }
        }
        if (audio) {
            double time_sec = (double)frame_idx / (double)info.framerate;
            wav64_seek(audio, parms->audio_mixer_channel, time_sec);
        }
        if (subs) {
            subtitles_seek(subs, frame_idx);
        }
        if (vsync) {
            video_sync_reset(vsync, frame_idx);
        }
        return frame_idx;
    }
    float ctrl_seek_time(fmv_control_t *ctrl, float time_sec, bool exact)
    {
        int f = (int)(time_sec * info.framerate);
        f = ctrl_seek_frame(ctrl, f, exact);
        if (f < 0) return -1;
        return (float)f / info.framerate;
    }

    fmv_control_t ctrl = {
        .video = video,
        .audio = audio,
        .subs = subs,
        .pause = ctrl_pause,
        .stop = ctrl_stop,
        .seek_frame = ctrl_seek_frame,
        .seek_time = ctrl_seek_time,
    };

    if (audio) {
        mixer_ch_play(parms->audio_mixer_channel, &audio->wave);

        if (!parms->disable_frame_skipping) {
            vsync = video_sync_create(video, NULL);
        }
    }

    while (!abort) {
        bool skip_render = false;

        mixer_try_play();

        if (!paused) {
            if (vsync && mixer_ch_playing(parms->audio_mixer_channel)) {
                double master_time_sec =
                    mixer_ch_get_pos(parms->audio_mixer_channel) / (double)audio->wave.frequency;
                video_sync_action_t a = video_sync_step(vsync, master_time_sec, frame_idx);

                if (a.kind == VIDEO_SYNC_SKIP_NEXT) {
                    skip_render = true;
                } else if (a.kind == VIDEO_SYNC_SEEK_AND_RENDER) {
                    int new_idx = video_seek(video, a.seek_frame);
                    if (new_idx >= 0) {
                        frame_idx = new_idx;
                        if (subs) subtitles_seek(subs, frame_idx);
                    }
                }
            }

            if (!video_next_frame(video)) {
                if (parms->loop) {
                    frame_idx = 0;
                    video_rewind(video);
                    if (audio) wav64_seek(audio, parms->audio_mixer_channel, 0.0);
                    if (subs) subtitles_seek(subs, 0);
                    if (vsync) video_sync_reset(vsync, frame_idx);
                    continue;
                } else {
                    break;
                }
            }

            if (subs) subtitles_next_frame(subs);
        }

        mixer_try_play();

        if (!skip_render) {
            surface_t *disp = display_try_get();
            while (disp == NULL) {
                if (!paused && !video_poll(video)) {
                    disp = display_get();
                    break;
                }
                disp = display_try_get();
            }

            rdpq_attach(disp, NULL);

            yuv_frame_t frame = video_get_frame(video);
            yuv_blitter_run(&yuv, &frame);

            if (subs) {
                subtitle_cue_t cues[3];
                int num_cues = subtitles_get_current_cues(subs, cues, 3);
                subrenderer_render(subrenderer, cues, num_cues);
            }

            if (parms->osd_callback) {
                float time_sec = frame_idx / info.framerate;
                parms->osd_callback(parms->osd_ctx, frame_idx, time_sec, &ctrl);
            }

            rdpq_detach_show();
        }

        if (!paused)
            frame_idx++;
    }

    rspq_wait();

    if (audio) {
        mixer_ch_stop(parms->audio_mixer_channel);
        wav64_close(audio);
    }

    yuv_blitter_free(&yuv);
    yuv_close();
    if (vsync) video_sync_destroy(vsync);
    video_close(video);
    display_set_fps_limit(0);
}

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
    (void)viewport;

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

    video_player_init_once();

    // Blocking playback on the current display (no extra display alloc/close).
    pandemonium_fmv_play(path, &(fmv_parms_t){
                                 .osd_callback = video_player_fmv_osd,
                             });

    // Safe queue drain after FMV finishes.
    rspq_wait();

    on_video_finished();
    return true;
}