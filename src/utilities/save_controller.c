#include <libdragon.h>
#include <eepromfs.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "save_controller.h"
#include "audio_controller.h"
#include "joypad_utility.h"
#include "globals.h"
#include "video_controller.h"

/*
 * EEPROMFS notes:
 * - Files always exist at the size specified during eepfs_init
 * - eepfs_verify_signature validates only filesystem layout, not file contents
 * - It's still on us to validate magic/version/checksum
 */

// Keep stable across builds.
#define SAVE_FILE_NAME "/pandemonium_save.dat"

#define SAVE_MAGIC   0x50414E44u  // "PAND", short for our game name "Pandemonium"
#define SAVE_VERSION 1u

// Default to 3 slots (Save 1..3). Increase later once a UI exists.
#define SAVE_SLOT_COUNT 3

// Debounce auto-saving (audio sliders) to avoid hitches + EEPROM wear.
#define SAVE_DEBOUNCE_S 0.50

typedef struct SaveBlob {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved0;
    SaveData slots[SAVE_SLOT_COUNT];
    uint32_t crc32; // CRC32 over slots[] only
} SaveBlob;

_Static_assert((sizeof(SaveBlob) % 8) == 0, "SaveBlob size should be a multiple of 8 bytes for EEPROM blocks");

static bool s_initialized = false;
static bool s_eeprom_available = false;
static int  s_active_slot = 0;

static SaveBlob s_blob;
static bool s_dirty = false;
static double s_last_dirty_time_s = 0.0;
static uint32_t s_play_session_counter = 0;

static uint32_t calculate_crc32(const void *data, size_t size) {
    static const uint32_t CRC32_POLY = 0xEDB88320u;
    uint32_t crc = 0xFFFFFFFFu;
    const uint8_t *bytes = (const uint8_t*)data;

    for (size_t i = 0; i < size; i++) {
        crc ^= bytes[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (CRC32_POLY & -(int)(crc & 1u));
        }
    }

    return ~crc;
}

static uint32_t fnv1a32(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t*)data;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint32_t)p[i];
        h *= 16777619u;
    }
    return h;
}

static uint32_t compute_slot_checksum(const SaveData *d) {
    const size_t len = offsetof(SaveData, checksum);
    return fnv1a32(d, len);
}

static int clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int8_t saved_overscan_x(const SaveData *d)
{
    if (!d) return 0;
    // Stored in padding to keep save size stable across versions.
    int8_t v = (int8_t)d->_pad[0];
    // Clamp to a conservative range (pixels). Keep within half-screen too.
    int maxX = (SCREEN_WIDTH / 2) - 2;
    return (int8_t)clampi((int)v, 0, maxX);
}

static int8_t saved_overscan_y(const SaveData *d)
{
    if (!d) return 0;
    int8_t v = (int8_t)d->_pad[1];
    int maxY = (SCREEN_HEIGHT / 2) - 2;
    return (int8_t)clampi((int)v, 0, maxY);
}

static void save_set_overscan(SaveData *d, int8_t x, int8_t y)
{
    if (!d) return;
    // Clamp before storing.
    int maxX = (SCREEN_WIDTH / 2) - 2;
    int maxY = (SCREEN_HEIGHT / 2) - 2;
    x = (int8_t)clampi((int)x, 0, maxX);
    y = (int8_t)clampi((int)y, 0, maxY);

    d->_pad[0] = (uint8_t)x;
    d->_pad[1] = (uint8_t)y;
}

static bool saved_rumble_enabled(const SaveData *d)
{
    if (!d) return true;
    // Stored in padding to keep save size stable across versions.
    return d->_pad[2] != 0;
}

static void save_set_rumble_enabled(SaveData *d, bool enabled)
{
    if (!d) return;
    d->_pad[2] = enabled ? 1 : 0;
}

static void save_defaults_for_slot(SaveData *d, int slot) {
    memset(d, 0, sizeof(*d));

    d->magic = SAVE_MAGIC;
    d->version = (uint16_t)SAVE_VERSION;
    d->slot_index = (uint8_t)slot;

    // Defaults: take current audio state (whatever audio_controller initialized to)
    d->masterVolume = (int8_t)audio_get_master_volume();
    d->musicVolume  = (int8_t)audio_get_music_volume();
    d->sfxVolume    = (int8_t)audio_get_sfx_volume();
    d->globalMute   = (uint8_t)(audio_is_muted() ? 1 : 0);
    d->stereoMode   = (uint8_t)(audio_get_stereo_mode() ? 1 : 0);

    // UI overscan defaults (extra padding beyond TITLE_SAFE)
    save_set_overscan(d, 0, 0);
    save_set_rumble_enabled(d, true);

    d->checksum = compute_slot_checksum(d);
}

static bool validate_slot(const SaveData *d, int expected_slot) {
    if (!d) return false;
    if (d->magic != SAVE_MAGIC) return false;
    if (d->version != (uint16_t)SAVE_VERSION) return false;
    if ((int)d->slot_index != expected_slot) return false;

    if (d->masterVolume < 0 || d->masterVolume > 10) return false;
    if (d->musicVolume  < 0 || d->musicVolume  > 10) return false;
    if (d->sfxVolume    < 0 || d->sfxVolume    > 10) return false;

    return d->checksum == compute_slot_checksum(d);
}

static void blob_seed_defaults(void) {
    memset(&s_blob, 0, sizeof(s_blob));
    s_blob.magic = SAVE_MAGIC;
    s_blob.version = (uint16_t)SAVE_VERSION;

    for (int i = 0; i < SAVE_SLOT_COUNT; i++) {
        save_defaults_for_slot(&s_blob.slots[i], i);
    }

    s_blob.crc32 = calculate_crc32(s_blob.slots, sizeof(s_blob.slots));
}

static bool blob_is_valid(const SaveBlob *b) {
    if (!b) return false;
    if (b->magic != SAVE_MAGIC) return false;
    if (b->version != (uint16_t)SAVE_VERSION) return false;
    if (b->crc32 != calculate_crc32(b->slots, sizeof(b->slots))) return false;

    for (int i = 0; i < SAVE_SLOT_COUNT; i++) {
        if (!validate_slot(&b->slots[i], i)) return false;
    }
    return true;
}

static bool blob_write_now(void) {
    if (!s_initialized) return false;

    // Refresh per-slot checksums (cheap)
    for (int i = 0; i < SAVE_SLOT_COUNT; i++) {
        s_blob.slots[i].magic = SAVE_MAGIC;
        s_blob.slots[i].version = (uint16_t)SAVE_VERSION;
        s_blob.slots[i].slot_index = (uint8_t)i;
        s_blob.slots[i].checksum = compute_slot_checksum(&s_blob.slots[i]);
    }

    s_blob.magic = SAVE_MAGIC;
    s_blob.version = (uint16_t)SAVE_VERSION;
    s_blob.crc32 = calculate_crc32(s_blob.slots, sizeof(s_blob.slots));

    int r = eepfs_write(SAVE_FILE_NAME, &s_blob, sizeof(s_blob));
    if (r == EEPFS_ESUCCESS) {
        s_dirty = false;
        return true;
    }
    debugf("EEPROMFS write failed (%d)\n", r);
    return false;
}

static bool blob_read_or_seed_defaults(void) {
    if (!s_initialized) return false;

    // Layout mismatch / brand new EEPROM / another game: wipe + seed defaults
    if (!eepfs_verify_signature()) {
        debugf("EEPROMFS signature mismatch: wiping + seeding defaults\n");
        eepfs_wipe();
        blob_seed_defaults();
        return blob_write_now();
    }

    SaveBlob b;
    memset(&b, 0, sizeof(b));
    int r = eepfs_read(SAVE_FILE_NAME, &b, sizeof(b));
    if (r != EEPFS_ESUCCESS) {
        debugf("EEPROMFS read failed (%d): seeding defaults\n", r);
        blob_seed_defaults();
        return blob_write_now();
    }

    if (!blob_is_valid(&b)) {
        debugf("Save blob invalid: wiping + seeding defaults\n");
        eepfs_wipe();
        blob_seed_defaults();
        return blob_write_now();
    }

    s_blob = b;
    return true;
}

void save_controller_init(void) {
    eeprom_type_t t = eeprom_present();
    s_eeprom_available = (t != EEPROM_NONE);
    s_initialized = false;

    if (!s_eeprom_available) {
        debugf("EEPROM not present (saves disabled)\n");
        return;
    }

    static const eepfs_entry_t entries[] = {
        { SAVE_FILE_NAME, sizeof(SaveBlob) },
    };

    int r = eepfs_init(entries, 1);
    if (r != EEPFS_ESUCCESS) {
        debugf("EEPROMFS init failed (%d) (saves disabled)\n", r);
        return;
    }

    s_initialized = true;
    debugf("EEPROMFS initialized. Using %d save slots.\n", SAVE_SLOT_COUNT);

    // Load blob or seed defaults, then select default slot 0 ("Save 1")
    (void)blob_read_or_seed_defaults();
    (void)save_controller_set_active_slot(0);
}

void save_controller_update(void) {
    if (!s_initialized) return;
    if (!s_dirty) return;

    // Provided by your game_time module
    extern double nowS;
    if ((nowS - s_last_dirty_time_s) < SAVE_DEBOUNCE_S) return;

    (void)blob_write_now();
}

bool save_controller_is_enabled(void) {
    return s_initialized;
}

int save_controller_get_active_slot(void) {
    return s_active_slot;
}

bool save_controller_set_active_slot(int slot) {
    if (slot < 0) slot = 0;
    if (slot >= SAVE_SLOT_COUNT) slot = SAVE_SLOT_COUNT - 1;
    s_active_slot = slot;

    if (!s_initialized) {
        // Keep defaults in memory even if EEPROM isn't available
        blob_seed_defaults();
        return false;
    }

    // Ensure we have a valid blob loaded
    (void)blob_read_or_seed_defaults();

    // If just this slot is invalid, reset it and persist.
    if (!validate_slot(&s_blob.slots[s_active_slot], s_active_slot)) {
        debugf("Save slot %d invalid: resetting to defaults\n", s_active_slot);
        save_defaults_for_slot(&s_blob.slots[s_active_slot], s_active_slot);
        (void)blob_write_now();
    }

    (void)save_controller_load_settings();
    return true;
}

bool save_controller_load_settings(void) {
    if (!s_initialized) return false;

    const SaveData *d = &s_blob.slots[s_active_slot];

    audio_set_loading_mode(true);
    audio_set_master_volume((int)d->masterVolume);
    audio_set_music_volume((int)d->musicVolume);
    audio_set_sfx_volume((int)d->sfxVolume);
    audio_set_mute(d->globalMute != 0);
    audio_set_stereo_mode(d->stereoMode != 0);
    audio_set_loading_mode(false);

    // UI overscan (applied to edge-anchored UI via globals.h helpers)
    uiOverscanX = saved_overscan_x(d);
    uiOverscanY = saved_overscan_y(d);

    joypad_set_rumble_enabled(saved_rumble_enabled(d));
    return true;
}

bool save_controller_save_settings(void) {
    if (!s_initialized) return false;

    SaveData *d = &s_blob.slots[s_active_slot];
    d->masterVolume = (int8_t)audio_get_master_volume();
    d->musicVolume  = (int8_t)audio_get_music_volume();
    d->sfxVolume    = (int8_t)audio_get_sfx_volume();
    d->globalMute   = (uint8_t)(audio_is_muted() ? 1 : 0);
    d->stereoMode   = (uint8_t)(audio_get_stereo_mode() ? 1 : 0);

    // Persist current UI overscan values
    save_set_overscan(d, uiOverscanX, uiOverscanY);
    save_set_rumble_enabled(d, joypad_is_rumble_enabled());

    d->checksum     = compute_slot_checksum(d);

    // Debounced flush
    extern double nowS;
    s_last_dirty_time_s = nowS;
    s_dirty = true;
    return true;
}

bool save_controller_increment_run_count(void) {
    if (!s_initialized) {
        debugf("Run count increment skipped (saves disabled)\n");
        return false;
    }

    SaveData *d = &s_blob.slots[s_active_slot];
    d->run_count += 1;
    d->checksum = compute_slot_checksum(d);
    return blob_write_now();
}

bool save_controller_record_boss_clear_time_ms(uint32_t clear_time_ms) {
    if (!s_initialized) return false;
    if (clear_time_ms == 0) return false;

    SaveData *d = &s_blob.slots[s_active_slot];
    if (d->best_boss_time_ms == 0 || clear_time_ms < d->best_boss_time_ms) {
        d->best_boss_time_ms = clear_time_ms;
        d->checksum = compute_slot_checksum(d);
        return blob_write_now();
    }
    return true;
}

uint32_t save_controller_get_run_count(void) {
    if (!s_initialized) return 0;
    return s_blob.slots[s_active_slot].run_count;
}

uint32_t save_controller_get_best_boss_time_ms(void) {
    if (!s_initialized) return 0;
    return s_blob.slots[s_active_slot].best_boss_time_ms;
}

const SaveData* save_controller_get_slot_data(int slot) {
    if (!s_initialized) return NULL;
    if (slot < 0 || slot >= SAVE_SLOT_COUNT) return NULL;
    return &s_blob.slots[slot];
}

uint32_t save_controller_get_slot_run_count(int slot) {
    if (!s_initialized) return 0;
    if (slot < 0 || slot >= SAVE_SLOT_COUNT) return 0;
    return s_blob.slots[slot].run_count;
}

uint32_t save_controller_get_slot_best_boss_time_ms(int slot) {
    if (!s_initialized) return 0;
    if (slot < 0 || slot >= SAVE_SLOT_COUNT) return 0;
    return s_blob.slots[slot].best_boss_time_ms;
}

uint32_t save_controller_get_slot_last_played_timestamp(int slot) {
    if (!s_initialized) return 0;
    if (slot < 0 || slot >= SAVE_SLOT_COUNT) return 0;
    return s_blob.slots[slot].last_played_timestamp;
}

int save_controller_get_last_played_slot(void) {
    if (!s_initialized) return -1;
    
    int lastPlayedSlot = -1;
    uint32_t lastPlayedTime = 0;
    
    for (int i = 0; i < SAVE_SLOT_COUNT; i++) {
        if (s_blob.slots[i].last_played_timestamp > lastPlayedTime) {
            lastPlayedTime = s_blob.slots[i].last_played_timestamp;
            lastPlayedSlot = i;
        }
    }
    
    return lastPlayedSlot;
}

bool save_controller_update_last_played_timestamp(void) {
    if (!s_initialized) return false;
    
    // Use a simple incrementing counter to track last played
    // In a real game, you might use actual timestamps
    SaveData *d = &s_blob.slots[s_active_slot];
    d->last_played_timestamp = ++s_play_session_counter;
    d->checksum = compute_slot_checksum(d);
    return blob_write_now();
}

bool save_controller_clear_slot(int slot) {
    if (!s_initialized) return false;
    if (slot < 0 || slot >= SAVE_SLOT_COUNT) return false;
    
    // Reset slot to defaults (empty)
    save_defaults_for_slot(&s_blob.slots[slot], slot);
    return blob_write_now();
}

void save_controller_free(void) {
    if (s_initialized && s_dirty) {
        (void)blob_write_now();
    }
    s_dirty = false;
    s_initialized = false;
    s_eeprom_available = false;

    // Optional: clean up the FS table. Not strictly required.
    (void)eepfs_close();
}