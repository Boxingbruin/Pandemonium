#ifndef AUDIO_CONTROLLER_H
#define AUDIO_CONTROLLER_H

#include <libdragon.h>
#include <stdbool.h>

// Mixer / Channel Layout
#define MIXER_NUM_CHANNELS   16
#define CHANNEL_MUSIC        0
// #define CHANNEL_MUSIC_R      1   // reserved by stereo music
#define SFX_CH_FIRST         2
#define SFX_CH_LAST          (MIXER_NUM_CHANNELS - 1)

// Init / Shutdown
void audio_initialize(void);
void audio_controller_free(void);

// Music
void audio_play_music(const char *path, bool loop);
void audio_stop_music(void);
bool audio_is_music_playing(void);

void audio_pause_music(void);
void audio_resume_music(void);

void audio_stop_music_fade(float durationSec);
void audio_update_fade(float dt);

// Scene SFX Loading (Option A)
#define AUDIO_SCENE_MAX_SFX  64   // pick a limit that works for you

void audio_scene_load_paths(const char *const *paths, int count);
void audio_scene_unload_sfx(void);

// Play a scene-local SFX index
void audio_play_scene_sfx_dist(
    int sceneSfxIndex,   // 0..(scene_count-1)
    float baseVolume,    // 0..1
    float distance       // >=0
);

void audio_stop_all_sfx(void);

// Volumes
void audio_set_master_volume(int volume);
void audio_set_music_volume(int volume);
void audio_set_sfx_volume(int volume);

int  audio_get_master_volume(void);
int  audio_get_music_volume(void);
int  audio_get_sfx_volume(void);

void audio_adjust_master_volume(int direction);
void audio_adjust_music_volume(int direction);
void audio_adjust_sfx_volume(int direction);

// Mute
void audio_set_mute(bool muted);
void audio_toggle_mute(void);
bool audio_is_muted(void);

// Settings load guard
void audio_set_loading_mode(bool loading);

void audio_update(float dt);

#endif