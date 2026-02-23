#ifndef SAVE_CONTROLLER_H
#define SAVE_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

/**
 * Save data stored per slot in EEPROM.
 * Notes:
 * - Slot selection UI doesn't exist yet; default slot is 0 ("Save 1").
 * - This struct is serialized directly to EEPROM; keep it stable and versioned.
 */
typedef struct {
    // Header / validation
    uint32_t magic;        // 'PAND'
    uint16_t version;      // Save format version
    uint8_t slot_index;    // 0..SAVE_SLOT_COUNT-1
    uint8_t reserved0;

    // Tracked stats/settings (per slot)
    uint32_t run_count;
    uint32_t best_boss_time_ms; // 0 => no recorded clear yet
    uint32_t last_played_timestamp; // Timestamp of last play (0 = never played)

    // Audio settings (0-10)
    int8_t masterVolume;
    int8_t musicVolume;
    int8_t sfxVolume;
    uint8_t globalMute;
    uint8_t stereoMode; // 1 = stereo, 0 = mono

    // Validation
    uint32_t checksum; // FNV-1a over all bytes before this field

    // Pad to a fixed size on EEPROM (see SAVE_SLOT_SIZE_BYTES in .c)
    uint8_t _pad[28];
} SaveData;

void save_controller_init(void);
void save_controller_update(void);

// Save availability / status
bool save_controller_is_enabled(void); // true if EEPROM + EEPROMFS initialized

// Save slot selection (default is 0 / "Save 1")
bool save_controller_set_active_slot(int slot);
int  save_controller_get_active_slot(void);

// Audio settings
bool save_controller_load_settings(void);
bool save_controller_save_settings(void);

// Run + boss stats
bool save_controller_increment_run_count(void);
bool save_controller_record_boss_clear_time_ms(uint32_t clear_time_ms);
uint32_t save_controller_get_run_count(void);
uint32_t save_controller_get_best_boss_time_ms(void);

// Get save data from any slot (for load game menu, etc)
const SaveData* save_controller_get_slot_data(int slot);
uint32_t save_controller_get_slot_run_count(int slot);
uint32_t save_controller_get_slot_best_boss_time_ms(int slot);
uint32_t save_controller_get_slot_last_played_timestamp(int slot);

// Find last played save slot (-1 if no saves have been played)
int save_controller_get_last_played_slot(void);

// Update timestamp when a save is played
bool save_controller_update_last_played_timestamp(void);

// Clear a save slot (reset to empty/defaults)
bool save_controller_clear_slot(int slot);
void save_controller_free(void);

#endif