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

#endif