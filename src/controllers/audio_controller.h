#ifndef AUDIO_CONTROLLER_H
#define AUDIO_CONTROLLER_H

// Audio Channel Identifiers

#define CHANNEL_MUSIC 2
#define CHANNEL_PLAYER1 1
#define CHANNEL_SFX1 0

void audio_initialize(void);
void audio_play_music(const char *path, bool loop);
void audio_stop_music(void);
void audio_play_sfx(wav64_t* sfx, int sfxLayer, float volume);
void audio_stop_sfx(int sfxLayer);
void audio_play_player(wav64_t* playerSfx, int playerLayer, float volume);
void audio_stop_player(int playerLayer);
void audio_controller_free(void);

// Volume control functions (0-10 scale)
void audio_set_master_volume(int volume);
void audio_set_music_volume(int volume);
void audio_set_sfx_volume(int volume);
int audio_get_master_volume(void);
int audio_get_music_volume(void);
int audio_get_sfx_volume(void);
void audio_adjust_master_volume(int direction);
void audio_adjust_music_volume(int direction);
void audio_adjust_sfx_volume(int direction);

// Mute functionality
void audio_set_mute(bool muted);
void audio_toggle_mute(void);
bool audio_is_muted(void);

// Internal function for preventing auto-save during loading
void audio_set_loading_mode(bool loading);

// Music pause/resume functions
void audio_pause_music(void);
void audio_resume_music(void);
bool audio_is_music_playing(void);

#endif