#include <libdragon.h>

#include "audio_controller.h"
#include "globals.h"

static wav64_t currentMusic;
//static xm64player_t currentMusic;
static bool musicPlaying = false;

static bool sfx1Playing = false;

static bool player1Playing = false;

void audio_initialize(void) 
{
    int ret = dfs_init(DFS_DEFAULT_LOCATION);
    assert(ret == DFS_ESUCCESS);

    audio_init(22050, 4);
    mixer_init(16);
    wav64_init_compression(1);

    // Maximum frequency of music channel is 128k
    // Set the limits of the music channel to 0 and 48000
    mixer_ch_set_limits(CHANNEL_MUSIC, 0, 48000, 0);
}

void audio_play_music(const char *path, bool loop) 
{
    // Stop any currently playing music
    if (musicPlaying)
        audio_stop_music();

    // Load the new music file
    wav64_open(&currentMusic, path);
    wav64_set_loop(&currentMusic, loop);

    // Play the music on the music channel
    wav64_play(&currentMusic, CHANNEL_MUSIC);
    musicPlaying = true;
}

void audio_stop_music(void) 
{
    if (musicPlaying) 
    {
        mixer_ch_stop(CHANNEL_MUSIC);
        wav64_close(&currentMusic);
        musicPlaying = false;
    }
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

            mixer_ch_set_vol(CHANNEL_SFX1, volume, volume);
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