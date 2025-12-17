#include <libdragon.h>
#include <string.h>
#include "save_controller.h"
#include "audio_controller.h"

#define SAVE_MAGIC 0x50414E44  // "PAND" - Pandemonium save identifier
#define CONTROLLER_PORT 0     // Use controller port 0
#define SAVE_ENTRY_NAME "PANDEMONIUM_SETTINGS"

static bool controllerPakAvailable = false;
static entry_structure_t saveEntry;

// Calculate simple checksum for save data validation
static uint32_t calculate_checksum(const SaveData* data) {
    uint32_t checksum = SAVE_MAGIC;
    checksum ^= (uint32_t)data->masterVolume;
    checksum ^= (uint32_t)data->musicVolume << 8;
    checksum ^= (uint32_t)data->sfxVolume << 16;
    checksum ^= (uint32_t)data->globalMute << 24;
    return checksum;
}

void save_controller_init(void) {
    // Check if Controller Pak is valid
    int result = validate_mempak(CONTROLLER_PORT);
    if (result == 0) {
        controllerPakAvailable = true;
        debugf("Controller Pak validated successfully\n");
        
        // Try to find existing save entry
        for (int i = 0; i < 16; i++) {
            if (get_mempak_entry(CONTROLLER_PORT, i, &saveEntry) == 0) {
                if (strncmp(saveEntry.name, SAVE_ENTRY_NAME, 16) == 0) {
                    debugf("Found existing save entry at slot %d\n", i);
                    return;
                }
            }
        }
        debugf("No existing save entry found\n");
    } else {
        controllerPakAvailable = false;
        debugf("Controller Pak not available or invalid (error: %d)\n", result);
    }
}

bool save_controller_load_settings(void) {
    if (!controllerPakAvailable) {
        debugf("Controller Pak not available for loading\n");
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
        debugf("No save entry found on Controller Pak\n");
        return false;
    }
    
    SaveData loadedData;
    
    // Read save data from Controller Pak entry
    int result = read_mempak_entry_data(CONTROLLER_PORT, &saveEntry, (uint8_t*)&loadedData);
    if (result != 0) {
        debugf("Failed to read save data from Controller Pak (error: %d)\n", result);
        return false;
    }
    
    // Validate checksum
    uint32_t expectedChecksum = calculate_checksum(&loadedData);
    if (loadedData.checksum != expectedChecksum) {
        debugf("Save data checksum mismatch - using defaults\n");
        return false;
    }
    
    // Validate data ranges
    if (loadedData.masterVolume < 0 || loadedData.masterVolume > 10 ||
        loadedData.musicVolume < 0 || loadedData.musicVolume > 10 ||
        loadedData.sfxVolume < 0 || loadedData.sfxVolume > 10) {
        debugf("Save data contains invalid values - using defaults\n");
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
    
    debugf("Audio settings loaded from Controller Pak\n");
    debugf("  Master: %d, Music: %d, SFX: %d, Mute: %s\n", 
           loadedData.masterVolume, loadedData.musicVolume, 
           loadedData.sfxVolume, loadedData.globalMute ? "ON" : "OFF");
    
    return true;
}

bool save_controller_save_settings(void) {
    if (!controllerPakAvailable) {
        debugf("Controller Pak not available for saving\n");
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
            debugf("Failed to update save data on Controller Pak (error: %d)\n", result);
            return false;
        }
    } else {
        // Create new entry - this would require more complex implementation
        // For now, just report that we can't create new entries
        debugf("Cannot create new save entry - functionality not implemented\n");
        return false;
    }
    
    debugf("Audio settings saved to Controller Pak\n");
    return true;
}

void save_controller_free(void) {
    // LibDragon's mempak doesn't require explicit cleanup
    controllerPakAvailable = false;
}