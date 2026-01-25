#ifndef SAVE_CONTROLLER_H
#define SAVE_CONTROLLER_H

#include <stdbool.h>

// Save data structure for audio settings
typedef struct {
    int masterVolume;
    int musicVolume;
    int sfxVolume;
    bool globalMute;
    bool stereoMode; // true = stereo, false = mono
    uint32_t checksum; // Simple validation
} SaveData;

void save_controller_init(void);
bool save_controller_load_settings(void);
bool save_controller_save_settings(void);
void save_controller_free(void);

#endif