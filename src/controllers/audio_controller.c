
#include <libdragon.h>
#include <string.h>
#include <stdbool.h>

#include "audio_controller.h"
#include "save_controller.h"
#include "globals.h"

/* ============================================================================
 * Internal helpers / state
 * ============================================================================
 */

static wav64_t currentMusic;
static bool musicPlaying = false;
static char currentMusicPath[256] = "";
static bool currentMusicLoop = false;

// Volume control (0-10 scale)
static int masterVolume = 8;
static int musicVolume  = 8;
static int sfxVolume    = 8;

static bool globalMute        = false;
static bool isLoadingSettings = false;
static bool pauseMuted        = false;

// Music fade-out state
static bool  musicFadingOut    = false;
static float musicFadeT        = 0.0f;
static float musicFadeDuration = 0.0f;
static float musicFadeStartVol = 0.0f;

static inline float clamp01(float x) {
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

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

/* ============================================================================
 * Scene SFX Cache (Option A)
 * ============================================================================
 *
 * The scene owns the IDs (enum values) and the path table.
 * audio_scene_load_paths(paths, count) loads wav64s into indices [0..count-1].
 */

static wav64_t g_scene_wavs[AUDIO_SCENE_MAX_SFX];
static bool    g_scene_loaded[AUDIO_SCENE_MAX_SFX];
static int     g_scene_count = 0;

/* ============================================================================
 * Dynamic SFX channel slots
 * ============================================================================
 */

typedef struct {
    int   ch;
    bool  in_use;

    int   scene_index;     // 0..(g_scene_count-1)
    float base_vol_mul;    // 0..1 from caller
    float distance;        // last provided distance (static unless caller changes design)
} SfxSlot;

static SfxSlot g_sfx_slots[MIXER_NUM_CHANNELS];

// Distance attenuation parameters (tune these to your world scale)
static float sfx_min_dist = 1.0f;   // full volume at/inside this
static float sfx_max_dist = 30.0f;  // silent at/after this
static float sfx_min_gain = 0.0f;   // floor gain (usually 0)

static inline bool ch_is_sfx_eligible(int ch)
{
    return (ch >= SFX_CH_FIRST && ch <= SFX_CH_LAST);
}

// Linear falloff (cheap)
static float sfx_distance_gain(float d)
{
    if (d <= sfx_min_dist) return 1.0f;
    if (d >= sfx_max_dist) return sfx_min_gain;

    float t = (d - sfx_min_dist) / (sfx_max_dist - sfx_min_dist); // 0..1
    t = clamp01(t);

    float g = 1.0f - t;
    if (g < sfx_min_gain) g = sfx_min_gain;
    return g;
}

static void sfx_slots_init(void)
{
    for (int i = 0; i < MIXER_NUM_CHANNELS; i++) {
        g_sfx_slots[i].ch = i;
        g_sfx_slots[i].in_use = false;
        g_sfx_slots[i].scene_index = -1;
        g_sfx_slots[i].base_vol_mul = 1.0f;
        g_sfx_slots[i].distance = 0.0f;
    }
}

static void sfx_slot_release(SfxSlot *s)
{
    if (!s) return;
    mixer_ch_stop(s->ch);
    s->in_use = false;
    s->scene_index = -1;
}

static void sfx_reap_finished(void)
{
    for (int ch = 0; ch < MIXER_NUM_CHANNELS; ch++) {
        if (!ch_is_sfx_eligible(ch)) continue;

        SfxSlot *s = &g_sfx_slots[ch];
        if (!s->in_use) continue;

        if (!mixer_ch_playing(ch)) {
            sfx_slot_release(s);
        }
    }
}

static SfxSlot* sfx_find_free_slot(void)
{
    // reclaim finished first
    sfx_reap_finished();

    for (int ch = 0; ch < MIXER_NUM_CHANNELS; ch++) {
        if (!ch_is_sfx_eligible(ch)) continue;

        SfxSlot *s = &g_sfx_slots[ch];
        if (!s->in_use) return s;
    }
    return NULL;
}

static void sfx_update_volumes(void)
{
    if (globalMute || pauseMuted) return;

    float sfxBase = apply_volume_settings(sfxVolume);

    for (int ch = 0; ch < MIXER_NUM_CHANNELS; ch++) {
        if (!ch_is_sfx_eligible(ch)) continue;

        SfxSlot *s = &g_sfx_slots[ch];
        if (!s->in_use) continue;
        if (!mixer_ch_playing(ch)) continue;

        float gain = sfx_distance_gain(s->distance);
        float v = sfxBase * s->base_vol_mul * gain;
        mixer_ch_set_vol(ch, v, v);
    }
}

static void audio_refresh_all_channel_volumes(void)
{
    // Music
    if (!pauseMuted && !musicFadingOut) {
        float mv = musicPlaying ? apply_volume_settings(musicVolume) : 0.0f;
        mixer_ch_set_vol(CHANNEL_MUSIC, mv, mv);
    }

    // SFX
    if (!pauseMuted) {
        sfx_update_volumes();
    } else {
        mixer_ch_set_vol(CHANNEL_MUSIC, 0.0f, 0.0f);

        for (int ch = 0; ch < MIXER_NUM_CHANNELS; ch++) {
            if (!ch_is_sfx_eligible(ch)) continue;
            if (g_sfx_slots[ch].in_use) mixer_ch_set_vol(ch, 0.0f, 0.0f);
        }
    }
}

/* ============================================================================
 * Scene SFX API (Option A)
 * ============================================================================
 */

void audio_scene_unload_sfx(void)
{
    // stop any active SFX channels first
    audio_stop_all_sfx();

    // close all loaded wavs
    for (int i = 0; i < g_scene_count; i++) {
        if (g_scene_loaded[i]) {
            wav64_close(&g_scene_wavs[i]);
            g_scene_loaded[i] = false;
        }
    }

    g_scene_count = 0;
}

void audio_scene_load_paths(const char *const *paths, int count)
{
    audio_scene_unload_sfx();

    if (!paths || count <= 0) return;

    if (count > AUDIO_SCENE_MAX_SFX)
        count = AUDIO_SCENE_MAX_SFX;

    g_scene_count = count;

    for (int i = 0; i < g_scene_count; i++) {
        g_scene_loaded[i] = false;

        const char *p = paths[i];
        if (!p) continue;

        wav64_open(&g_scene_wavs[i], p);
        wav64_set_loop(&g_scene_wavs[i], false);
        g_scene_loaded[i] = true;
    }
}

/* ============================================================================
 * Public API
 * ============================================================================
 */

void audio_set_loading_mode(bool loading) {
    isLoadingSettings = loading;
}

void audio_initialize(void)
{
    audio_init(22050, 4);
    mixer_init(MIXER_NUM_CHANNELS);
    wav64_init_compression(1);

    // Base channel only; stereo uses +1 internally
    mixer_ch_set_limits(CHANNEL_MUSIC, 0, 22050, 0);

    // clear scene cache
    g_scene_count = 0;
    for (int i = 0; i < AUDIO_SCENE_MAX_SFX; i++) {
        g_scene_loaded[i] = false;
    }

    sfx_slots_init();
}

static void audio_reset_fade(void)
{
    musicFadingOut = false;
    musicFadeT = 0.0f;
    musicFadeDuration = 0.0f;
    musicFadeStartVol = 0.0f;
}

void audio_stop_music(void)
{
    if (musicFadingOut)
        audio_reset_fade();

    if (musicPlaying) {
        mixer_ch_stop(CHANNEL_MUSIC);
        wav64_close(&currentMusic);
        musicPlaying = false;
    }
}

void audio_play_music(const char *path, bool loop)
{
    if (musicFadingOut)
        audio_reset_fade();

    if (musicPlaying)
        audio_stop_music();

    strncpy(currentMusicPath, path, sizeof(currentMusicPath) - 1);
    currentMusicPath[sizeof(currentMusicPath) - 1] = '\0';
    currentMusicLoop = loop;

    wav64_open(&currentMusic, path);
    wav64_set_loop(&currentMusic, loop);

    mixer_ch_set_freq(CHANNEL_MUSIC, currentMusic.wave.frequency);

    float v = apply_volume_settings(musicVolume);
    if (pauseMuted) v = 0.0f;
    mixer_ch_set_vol(CHANNEL_MUSIC, v, v);

    wav64_play(&currentMusic, CHANNEL_MUSIC);
    musicPlaying = true;
}

bool audio_is_music_playing(void) {
    return musicPlaying && !pauseMuted;
}

void audio_pause_music(void)
{
    if (pauseMuted) return;

    mixer_ch_set_vol(CHANNEL_MUSIC, 0.0f, 0.0f);

    for (int ch = 0; ch < MIXER_NUM_CHANNELS; ch++) {
        if (!ch_is_sfx_eligible(ch)) continue;
        if (g_sfx_slots[ch].in_use) {
            mixer_ch_set_vol(ch, 0.0f, 0.0f);
        }
    }

    pauseMuted = true;
}

void audio_resume_music(void)
{
    if (!pauseMuted) return;

    pauseMuted = false;
    audio_refresh_all_channel_volumes();
}

/* ============================================================================
 * Music fade-out
 * ============================================================================
 */

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

void audio_stop_music_fade(float durationSec)
{
    if (!musicPlaying) return;

    if (durationSec <= 0.0f) {
        audio_stop_music();
        return;
    }

    pauseMuted = false;

    musicFadingOut = true;
    musicFadeT = 0.0f;
    musicFadeDuration = durationSec;

    musicFadeStartVol = apply_volume_settings(musicVolume);

    mixer_ch_set_vol(CHANNEL_MUSIC, musicFadeStartVol, musicFadeStartVol);
}

/* ============================================================================
 * SFX: dynamic + distance-aware (Option A: scene index)
 * ============================================================================
 */

void audio_play_scene_sfx_dist(int sceneSfxIndex, float baseVolume, float distance)
{
    if (globalMute || pauseMuted) return;

    if (sceneSfxIndex < 0 || sceneSfxIndex >= g_scene_count) return;
    if (!g_scene_loaded[sceneSfxIndex]) return;

    baseVolume = clamp01(baseVolume);
    if (distance < 0.0f) distance = 0.0f;

    SfxSlot *slot = sfx_find_free_slot();
    if (!slot) return; // all channels busy => drop

    slot->in_use = true;
    slot->scene_index = sceneSfxIndex;
    slot->base_vol_mul = baseVolume;
    slot->distance = distance;

    float gain = sfx_distance_gain(distance);
    float finalVol = apply_volume_settings(sfxVolume) * baseVolume * gain;
    mixer_ch_set_vol(slot->ch, finalVol, finalVol);

    wav64_play(&g_scene_wavs[sceneSfxIndex], slot->ch);
}

void audio_stop_all_sfx(void)
{
    for (int ch = 0; ch < MIXER_NUM_CHANNELS; ch++) {
        if (!ch_is_sfx_eligible(ch)) continue;
        sfx_slot_release(&g_sfx_slots[ch]);
    }
}

/* ============================================================================
 * Per-frame update
 * ============================================================================
 */

void audio_update(float dt)
{
    audio_update_fade(dt);
    sfx_reap_finished();
    sfx_update_volumes();
}

/* ============================================================================
 * Shutdown
 * ============================================================================
 */

void audio_controller_free(void)
{
    audio_stop_music();
    audio_stop_all_sfx();
    audio_scene_unload_sfx();
}

/* ============================================================================
 * Volume controls (0–10)
 * ============================================================================
 */

void audio_set_master_volume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 10) volume = 10;
    masterVolume = volume;

    if (!pauseMuted) {
        audio_refresh_all_channel_volumes();
    }

    if (!isLoadingSettings) {
        save_controller_save_settings();
    }
}

void audio_set_music_volume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 10) volume = 10;
    musicVolume = volume;

    if (musicPlaying && !pauseMuted && !musicFadingOut) {
        float mv = apply_volume_settings(musicVolume);
        mixer_ch_set_vol(CHANNEL_MUSIC, mv, mv);
    }

    if (!isLoadingSettings) {
        save_controller_save_settings();
    }
}

void audio_set_sfx_volume(int volume)
{
    if (volume < 0) volume = 0;
    if (volume > 10) volume = 10;
    sfxVolume = volume;

    if (!pauseMuted) {
        sfx_update_volumes();
    }

    if (!isLoadingSettings) {
        save_controller_save_settings();
    }
}

int audio_get_master_volume(void) { return masterVolume; }
int audio_get_music_volume(void)  { return musicVolume; }
int audio_get_sfx_volume(void)    { return sfxVolume; }

void audio_adjust_master_volume(int direction) { audio_set_master_volume(masterVolume + direction); }
void audio_adjust_music_volume(int direction)  { audio_set_music_volume(musicVolume + direction); }
void audio_adjust_sfx_volume(int direction)    { audio_set_sfx_volume(sfxVolume + direction); }

/* ============================================================================
 * Mute
 * ============================================================================
 */

void audio_set_mute(bool muted)
{
    globalMute = muted;

    if (!pauseMuted) {
        audio_refresh_all_channel_volumes();
    }

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