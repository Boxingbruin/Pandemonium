#include <libdragon.h>
#include <string.h>
#include "save_controller.h"
#include "audio_controller.h"

#define SAVE_MAGIC 0x50414E44  // "PAND" - Pandemonium save identifier
#define CONTROLLER_PORT 0     // Use controller port 0
#define SAVE_ENTRY_NAME "PANDEMONIUM_SETTINGS"

static bool controllerPakAvailable = false;
static entry_structure_t saveEntry;

// Calculate simple checksum for save data validation (excluding checksum field)
static uint32_t calculate_checksum(const SaveData* data) {
    uint32_t checksum = SAVE_MAGIC;
    checksum ^= (uint32_t)data->masterVolume;
    checksum ^= (uint32_t)data->musicVolume << 8;
    checksum ^= (uint32_t)data->sfxVolume << 16;
    checksum ^= (uint32_t)data->globalMute << 24;
    return checksum;
}

// Calculate checksum without including the checksum field (for validation)
static uint32_t calculate_checksum_for_validation(const SaveData* data) {
    // Create a temporary copy without checksum for calculation
    SaveData tempData = *data;
    tempData.checksum = 0;
    return calculate_checksum(&tempData);
}

void save_controller_init(void) {
    // Check if Controller Pak is valid
    int result = validate_mempak(CONTROLLER_PORT);
    if (result == 0) {
        controllerPakAvailable = true;
        debugf("Controller Pak detected on port %d\n", CONTROLLER_PORT);
        
        // Try to find existing save entry
        for (int i = 0; i < 16; i++) {
            if (get_mempak_entry(CONTROLLER_PORT, i, &saveEntry) == 0) {
                if (strncmp(saveEntry.name, SAVE_ENTRY_NAME, 16) == 0) {
                    debugf("Found existing save entry at slot %d\n", i);
                    return;
                }
            }
        }
        debugf("No existing save entry found (will create on first save)\n");
    } else {
        controllerPakAvailable = false;
        debugf("Controller Pak not available on port %d (error: %d)\n", CONTROLLER_PORT, result);
        debugf("Settings will use defaults and not persist\n");
    }
}

bool save_controller_load_settings(void) {
    if (!controllerPakAvailable) {
        return false;
    }
    
    // Search for our save entry
    bool entryFound = false;
    for (int i = 0; i < 16; i++) {
        if (get_mempak_entry(CONTROLLER_PORT, i, &saveEntry) == 0) {
            if (strncmp(saveEntry.name, SAVE_ENTRY_NAME, 16) == 0) {
                entryFound = true;
                break;
            }
        }
    }
    
    if (!entryFound) {
        return false;
    }
    
    SaveData loadedData;
    
    // Read save data from Controller Pak entry
    int result = read_mempak_entry_data(CONTROLLER_PORT, &saveEntry, (uint8_t*)&loadedData);
    if (result != 0) {
        return false;
    }
    
    // Validate checksum (using function that excludes checksum field)
    uint32_t expectedChecksum = calculate_checksum_for_validation(&loadedData);
    if (loadedData.checksum != expectedChecksum) {
        return false;
    }
    
    // Validate data ranges
    if (loadedData.masterVolume < 0 || loadedData.masterVolume > 10 ||
        loadedData.musicVolume < 0 || loadedData.musicVolume > 10 ||
        loadedData.sfxVolume < 0 || loadedData.sfxVolume > 10) {
        return false;
    }
    
    // Set loading mode to prevent auto-save during load
    audio_set_loading_mode(true);
    
    // Apply loaded settings to audio system
    audio_set_master_volume(loadedData.masterVolume);
    audio_set_music_volume(loadedData.musicVolume);
    audio_set_sfx_volume(loadedData.sfxVolume);
    audio_set_mute(loadedData.globalMute);
    
    // Disable loading mode
    audio_set_loading_mode(false);
    
    return true;
}

bool save_controller_save_settings(void) {
    if (!controllerPakAvailable) {
        debugf("Save failed: Controller Pak not available\n");
        return false;
    }
    
    // Prepare save data
    SaveData saveData;
    saveData.masterVolume = audio_get_master_volume();
    saveData.musicVolume = audio_get_music_volume();
    saveData.sfxVolume = audio_get_sfx_volume();
    saveData.globalMute = audio_is_muted();
    saveData.checksum = calculate_checksum(&saveData);
    
    // Check if we already have a save entry
    bool entryExists = false;
    for (int i = 0; i < 16; i++) {
        if (get_mempak_entry(CONTROLLER_PORT, i, &saveEntry) == 0) {
            if (strncmp(saveEntry.name, SAVE_ENTRY_NAME, 16) == 0) {
                entryExists = true;
                break;
            }
        }
    }
    
    if (entryExists) {
        // Update existing entry
        int result = write_mempak_entry_data(CONTROLLER_PORT, &saveEntry, (uint8_t*)&saveData);
        if (result != 0) {
            debugf("Save failed: Failed to write to existing entry (error: %d)\n", result);
            return false;
        }
        debugf("Settings saved successfully (updated existing entry)\n");
    } else {
        // Create new entry - find an empty slot
        bool slotFound = false;
        entry_structure_t newEntry;
        
        for (int i = 0; i < 16; i++) {
            int entryResult = get_mempak_entry(CONTROLLER_PORT, i, &newEntry);
            if (entryResult == 0) {
                // Entry exists - check if it's empty
                bool isEmpty = (newEntry.valid == 0);
                if (!isEmpty) {
                    // Also check if name is empty
                    isEmpty = true;
                    for (int j = 0; j < 16; j++) {
                        if (newEntry.name[j] != 0 && newEntry.name[j] != 0xFF) {
                            isEmpty = false;
                            break;
                        }
                    }
                }
                
                if (isEmpty) {
                    // Initialize entry structure for new save
                    // Preserve the entry structure (it has slot information) but update fields
                    strncpy(newEntry.name, SAVE_ENTRY_NAME, 16);
                    // Ensure name doesn't overflow and is properly terminated
                    newEntry.name[15] = '\0';
                    newEntry.vendor = 0x4E; // 'N' for Nintendo
                    newEntry.game_id = 0x5041; // "PA" from "PAND"
                    
                    // Calculate size needed (round up to block size)
                    // Controller Pak blocks are 64 bytes
                    int dataSize = sizeof(SaveData);
                    int blocksNeeded = (dataSize + 63) / 64;
                    if (blocksNeeded < 1) blocksNeeded = 1; // Minimum 1 block
                    newEntry.blocks = blocksNeeded;
                    newEntry.valid = 1;
                    
                    // Write the entry data (entry structure from get_mempak_entry already has slot info)
                    int result = write_mempak_entry_data(CONTROLLER_PORT, &newEntry, (uint8_t*)&saveData);
                    if (result == 0) {
                        saveEntry = newEntry;
                        slotFound = true;
                        debugf("Settings saved successfully (created new entry at slot %d)\n", i);
                        break;
                    } else {
                        debugf("Save failed: Failed to write new entry at slot %d (error: %d)\n", i, result);
                    }
                }
            }
            // Note: If get_mempak_entry fails, the slot might be truly empty,
            // but we can't create entries in slots that don't exist in the directory
        }
        
        if (!slotFound) {
            debugf("Save failed: No available slot found (Controller Pak may be full)\n");
            return false;
        }
    }
    
    return true;
}

void save_controller_free(void) {
    // LibDragon's mempak doesn't require explicit cleanup
    controllerPakAvailable = false;
}