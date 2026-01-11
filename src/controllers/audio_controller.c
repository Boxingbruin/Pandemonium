#include <libdragon.h>
#include <string.h>

#include "audio_controller.h"
#include "save_controller.h"
#include "globals.h"

static wav64_t currentMusic;
//static xm64player_t currentMusic;
static bool musicPlaying = false;
static char currentMusicPath[256] = "";
static bool currentMusicLoop = false;

static bool sfx1Playing = false;

static bool player1Playing = false;

// Volume control (0-10 scale)
static int masterVolume = 8;
static int musicVolume = 8;
static int sfxVolume = 8;
static bool globalMute = false;
static bool isLoadingSettings = false;
static bool pauseMuted = false;  // Track if we muted for pause

static bool musicFadingOut = false;
static float musicFadeT = 0.0f;
static float musicFadeDuration = 0.0f;
static float musicFadeStartVol = 0.0f;

// Convert 0-10 scale to 0.0-1.0 float
static float volume_to_float(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 10) volume = 10;
    return (float)volume / 10.0f;
}

// Apply master volume and mute to a specific volume
static float apply_volume_settings(int specificVolume) {
    if (globalMute) return 0.0f;
    return volume_to_float(masterVolume) * volume_to_float(specificVolume);
}

// Control auto-saving during settings load
void audio_set_loading_mode(bool loading) {
    isLoadingSettings = loading;
}

void audio_initialize(void) 
{
    // int ret = dfs_init(DFS_DEFAULT_LOCATION);
    // assert(ret == DFS_ESUCCESS);

    audio_init(44100, 4);
    mixer_init(16);
    wav64_init_compression(1);

    // Maximum frequency of music channel is 128k
    // Set the limits of the music channel to 0 and 44100
    mixer_ch_set_limits(CHANNEL_MUSIC, 0, 44100, 0);
}

void audio_reset_fade(void)
{
    musicFadingOut = false;
    musicFadeT = 0.0f;
    musicFadeDuration = 0.0f;
    musicFadeStartVol = 0.0f;
}

void audio_stop_music(void) 
{
    if(musicFadingOut)
        audio_reset_fade();

    if (musicPlaying) 
    {
        mixer_ch_stop(CHANNEL_MUSIC);
        wav64_close(&currentMusic);
        musicPlaying = false;
    }
}

void audio_play_music(const char *path, bool loop) 
{
    // Cancel any fade in progress
    if(musicFadingOut)
        audio_reset_fade();

    // Stop any currently playing music
    if (musicPlaying)
        audio_stop_music();

    // Store current music info for resume functionality
    strncpy(currentMusicPath, path, sizeof(currentMusicPath) - 1);
    currentMusicPath[sizeof(currentMusicPath) - 1] = '\0';
    currentMusicLoop = loop;

    // Load the new music file
    wav64_open(&currentMusic, path);
    mixer_ch_set_freq(CHANNEL_MUSIC, currentMusic.wave.frequency);
    wav64_set_loop(&currentMusic, loop);

    // Apply volume settings
    float volume = apply_volume_settings(musicVolume);
    mixer_ch_set_vol(CHANNEL_MUSIC, volume, volume);

    // Play the music on the music channel
    wav64_play(&currentMusic, CHANNEL_MUSIC);
    musicPlaying = true;
}

void audio_play_sfx(wav64_t* sfx, int sfxLayer, float volume) 
{
    switch (sfxLayer) 
    {
        case 0:
            if (sfx1Playing) 
            {
                audio_stop_sfx(CHANNEL_SFX1);
            }

            // Apply volume settings (use provided volume as additional multiplier)
            float finalVolume = apply_volume_settings(sfxVolume) * volume;
            mixer_ch_set_vol(CHANNEL_SFX1, finalVolume, finalVolume);
            wav64_play(sfx, CHANNEL_SFX1);
            sfx1Playing = true;
            break;
        default:
            break;
    }
}

void audio_stop_sfx(int sfxLayer) 
{
    switch (sfxLayer) 
    {
        case 0:
            if (sfx1Playing) 
            {
                mixer_ch_stop(CHANNEL_SFX1);
                sfx1Playing = false;
            }
            break;
        default:
            break;
    }
}

void audio_play_player(wav64_t* playerSfx, int playerLayer, float volume) 
{
    switch (playerLayer) 
    {
        case 0:
            if (player1Playing) 
            {
                audio_stop_player(CHANNEL_PLAYER1);
            }
            mixer_ch_set_vol(CHANNEL_PLAYER1, volume, volume);
            wav64_play(playerSfx, CHANNEL_PLAYER1);
            player1Playing = true;
            break;
        default:
            break;
    }
}

void audio_stop_player(int playerLayer) 
{
    switch (playerLayer) 
    {
        case 0:
            if (player1Playing) 
            {
                mixer_ch_stop(CHANNEL_PLAYER1);
                //wav64_close(&currentPlayer1);  // Free the WAV64 resources
                player1Playing = false;
            }
            break;
        default:
            break;
    }
}

void audio_controller_free(void) 
{
    // TODO: not sure if i need to completely free the mixer or not, so lets just stop the music and sfx for now
    audio_stop_music();
    audio_stop_sfx(0);
    audio_stop_player(0);
}

// Volume control functions
void audio_set_master_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 10) volume = 10;
    masterVolume = volume;
    
    // Update currently playing audio
    if (musicPlaying) {
        float musicVol = apply_volume_settings(musicVolume);
        mixer_ch_set_vol(CHANNEL_MUSIC, musicVol, musicVol);
    }
    
    // Auto-save settings only if not loading
    if (!isLoadingSettings) {
        save_controller_save_settings();
    }
}

void audio_set_music_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 10) volume = 10;
    musicVolume = volume;
    
    // Update currently playing music
    if (musicPlaying) {
        float musicVol = apply_volume_settings(musicVolume);
        mixer_ch_set_vol(CHANNEL_MUSIC, musicVol, musicVol);
    }
    
    // Auto-save settings only if not loading
    if (!isLoadingSettings) {
        save_controller_save_settings();
    }
}

void audio_set_sfx_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 10) volume = 10;
    sfxVolume = volume;
    
    // Auto-save settings only if not loading
    if (!isLoadingSettings) {
        save_controller_save_settings();
    }
}

int audio_get_master_volume(void) {
    return masterVolume;
}

int audio_get_music_volume(void) {
    return musicVolume;
}

int audio_get_sfx_volume(void) {
    return sfxVolume;
}

void audio_adjust_master_volume(int direction) {
    audio_set_master_volume(masterVolume + direction);
}

void audio_adjust_music_volume(int direction) {
    audio_set_music_volume(musicVolume + direction);
}

void audio_adjust_sfx_volume(int direction) {
    audio_set_sfx_volume(sfxVolume + direction);
}

// Mute functionality
void audio_set_mute(bool muted) {
    globalMute = muted;
    
    // Update currently playing audio only if not pause-muted
    if (!pauseMuted) {
        if (musicPlaying) {
            float musicVol = apply_volume_settings(musicVolume);
            mixer_ch_set_vol(CHANNEL_MUSIC, musicVol, musicVol);
        }
    }
    
    // Auto-save settings only if not loading
    if (!isLoadingSettings) {
        save_controller_save_settings();
    }
}

void audio_toggle_mute(void) {
    audio_set_mute(!globalMute);
}

bool audio_is_muted(void) {
    return globalMute;
}

// Music pause/resume functionality using mute
void audio_pause_music(void) {
    if (musicPlaying && !pauseMuted) {
        // Mute all audio channels
        mixer_ch_set_vol(CHANNEL_MUSIC, 0.0f, 0.0f);
        mixer_ch_set_vol(CHANNEL_SFX1, 0.0f, 0.0f);
        mixer_ch_set_vol(CHANNEL_PLAYER1, 0.0f, 0.0f);
        pauseMuted = true;
    }
}

void audio_resume_music(void) {
    if (pauseMuted) {
        // Restore audio channel volumes
        if (musicPlaying) {
            float musicVol = apply_volume_settings(musicVolume);
            mixer_ch_set_vol(CHANNEL_MUSIC, musicVol, musicVol);
        }
        
        // Note: SFX volumes will be restored when new sounds play
        // as they apply volume settings on each play call
        
        pauseMuted = false;
    }
}

bool audio_is_music_playing(void) {
    return musicPlaying && !pauseMuted;
}

void audio_update_fade(float dt)
{
    if (!musicFadingOut) return;

    musicFadeT += dt;
    float t = (musicFadeDuration > 0.0f) ? (musicFadeT / musicFadeDuration) : 1.0f;
    if (t > 1.0f) t = 1.0f;

    float v = musicFadeStartVol * (1.0f - t);
    mixer_ch_set_vol(CHANNEL_MUSIC, v, v);

    if (t >= 1.0f) {
        audio_stop_music();
    }
}

// Start a fade-out. durationSec <= 0 => stop immediately.
void audio_stop_music_fade(float durationSec)
{
    if (!musicPlaying) return;

    if (durationSec <= 0.0f) {
        audio_stop_music();
        return;
    }

    // Cancel pause-mute so fade is audible / deterministic
    pauseMuted = false;

    musicFadingOut = true;
    musicFadeT = 0.0f;
    musicFadeDuration = durationSec;

    // capture the current intended music volume (0..1)
    musicFadeStartVol = apply_volume_settings(musicVolume);

    // ensure we start from the expected level
    mixer_ch_set_vol(CHANNEL_MUSIC, musicFadeStartVol, musicFadeStartVol);
}
