#include <libdragon.h>
#include "menu_controller.h"
#include "joypad_utility.h"
#include "audio_controller.h"
#include "scene.h"
#include "globals.h"

// Menu state
static MenuState currentMenu = MENU_MAIN;
static int selectedOption = 0;
static bool menuActive = false;
static bool musicWasPaused = false;

// Pause menu background
static sprite_t *pauseMenuBg = NULL;
static surface_t pauseMenuBgSurf = {0};

// Input handling for edge detection
static bool lastStartPressed = false;
static bool lastUpPressed = false;
static bool lastDownPressed = false;
static bool lastLeftPressed = false;
static bool lastRightPressed = false;
static bool lastAPressed = false;
static bool lastBPressed = false;

// Menu text
static const char* mainMenuOptions[MENU_MAIN_COUNT] = {
    "Resume",
    "Restart",
    "Audio Settings",
    "Exit"
};

static const char* audioMenuOptions[MENU_AUDIO_COUNT] = {
    "Master Volume: %d",
    "Music Volume: %d", 
    "SFX Volume: %d",
    "Mute All: %s",
    "Audio Mode: %s",
    "Back"
};

void menu_controller_init(void) {
    currentMenu = MENU_MAIN;
    selectedOption = 0;
    menuActive = false;
    
    pauseMenuBg = sprite_load("rom:/pauseMenuBg.sprite");
    if (pauseMenuBg) {
        pauseMenuBgSurf = sprite_get_pixels(pauseMenuBg);
    }
}

void menu_controller_update(void) {
    GameState state = scene_get_game_state();
    if (state == GAME_STATE_DEAD || state == GAME_STATE_VICTORY) {
        return;
    }

    // Handle start button to toggle menu
    bool startPressed = btn.start;
    bool startJustPressed = startPressed && !lastStartPressed;
    lastStartPressed = startPressed;
    
    if (startJustPressed) {
        // Don't allow pausing during cutscenes
        if (!scene_is_cutscene_active()) {
            menu_controller_toggle();
        }
        return;
    }
    
    if (!menuActive) {
        return;
    }
    
    // Handle navigation
    bool upPressed = btn.d_up;
    bool downPressed = btn.d_down;
    bool leftPressed = btn.d_left;
    bool rightPressed = btn.d_right;
    bool aPressed = btn.a;
    bool bPressed = btn.b;
    
    bool upJustPressed = upPressed && !lastUpPressed;
    bool downJustPressed = downPressed && !lastDownPressed;
    bool leftJustPressed = leftPressed && !lastLeftPressed;
    bool rightJustPressed = rightPressed && !lastRightPressed;
    bool aJustPressed = aPressed && !lastAPressed;
    bool bJustPressed = bPressed && !lastBPressed;
    
    lastUpPressed = upPressed;
    lastDownPressed = downPressed;
    lastLeftPressed = leftPressed;
    lastRightPressed = rightPressed;
    lastAPressed = aPressed;
    lastBPressed = bPressed;
    
    // Navigation between options
    if (upJustPressed) {
        selectedOption--;
        int maxOptions = (currentMenu == MENU_MAIN) ? MENU_MAIN_COUNT : MENU_AUDIO_COUNT;
        if (selectedOption < 0) {
            selectedOption = maxOptions - 1; // Wrap to bottom
        }
    }
    
    if (downJustPressed) {
        selectedOption++;
        int maxOptions = (currentMenu == MENU_MAIN) ? MENU_MAIN_COUNT : MENU_AUDIO_COUNT;
        if (selectedOption >= maxOptions) {
            selectedOption = 0; // Wrap to top
        }
    }
    
    // Handle menu-specific input
    if (currentMenu == MENU_MAIN) {
        // B button to close menu (check this first before processing other input)
        if (bJustPressed) {
            menu_controller_close();
            return; // Exit early to prevent processing other input
        }
        
        if (aJustPressed) {
            switch (selectedOption) {
                case MENU_MAIN_RESUME:
                    menu_controller_close();
                    break;
                case MENU_MAIN_RESTART:
                    scene_restart();
                    menu_controller_close();
                    break;
                case MENU_MAIN_SETTINGS:
                    currentMenu = MENU_AUDIO;
                    selectedOption = 0;
                    break;
                case MENU_MAIN_EXIT:
                    menu_controller_close();
                    break;
            }
        }
    } else if (currentMenu == MENU_AUDIO) {
        // Handle audio menu input
        if (leftJustPressed || rightJustPressed) {
            int direction = rightJustPressed ? 1 : -1;
            
            switch (selectedOption) {
                case MENU_AUDIO_MASTER_VOLUME:
                    audio_adjust_master_volume(direction);
                    break;
                case MENU_AUDIO_MUSIC_VOLUME:
                    audio_adjust_music_volume(direction);
                    break;
                case MENU_AUDIO_SFX_VOLUME:
                    audio_adjust_sfx_volume(direction);
                    break;
                case MENU_AUDIO_MUTE_TOGGLE:
                    if (leftJustPressed || rightJustPressed) {
                        audio_toggle_mute();
                    }
                    break;
                case MENU_AUDIO_STEREO_MODE:
                    if (leftJustPressed || rightJustPressed) {
                        audio_toggle_stereo_mode();
                    }
                    break;
            }
        }
        
        if (aJustPressed) {
            switch (selectedOption) {
                case MENU_AUDIO_MUTE_TOGGLE:
                    audio_toggle_mute();
                    break;
                case MENU_AUDIO_STEREO_MODE:
                    audio_toggle_stereo_mode();
                    break;
                case MENU_AUDIO_BACK:
                    currentMenu = MENU_MAIN;
                    selectedOption = MENU_MAIN_SETTINGS;
                    break;
            }
        }
        
        // B button to go back to main menu (not close the entire menu)
        if (bJustPressed) {
            currentMenu = MENU_MAIN;
            selectedOption = MENU_MAIN_SETTINGS;
        }
    }
}

static void draw_pause_menu_background(int x, int y, int dialogWidth, int dialogHeight) {
    // If we don't have a texture, fall back to the plain fill
    if (!pauseMenuBg || pauseMenuBgSurf.width <= 0 || pauseMenuBgSurf.height <= 0) {
        rdpq_set_prim_color(RGBA32(40, 35, 30, 180));
        rdpq_fill_rectangle(x, y, x + dialogWidth, y + dialogHeight);
        return;
    }

    int imgW = pauseMenuBgSurf.width;
    int imgH = pauseMenuBgSurf.height;

    // Scale uniformly so the single image covers the area, then center it
    float scaleX = (float)dialogWidth / (float)imgW;
    float scaleY = (float)dialogHeight / (float)imgH;
    float scale = (scaleX > scaleY) ? scaleX : scaleY;
    int drawW = (int)(imgW * scale);
    int drawH = (int)(imgH * scale);
    int drawX = x + (dialogWidth - drawW) / 2;
    int drawY = y + (dialogHeight - drawH) / 2;

    rdpq_mode_alphacompare(1);
    rdpq_tex_blit(&pauseMenuBgSurf, drawX, drawY, &(rdpq_blitparms_t){
        .scale_x = scale,
        .scale_y = scale,
    });
}

void menu_controller_draw(void) {
    if (!menuActive) {
        return;
    }
    
    // Use different dialog sizes based on current menu
    int dialogWidth, dialogHeight;
    if (currentMenu == MENU_MAIN) {
        dialogWidth = 280;
        dialogHeight = 280;
    }
    
    // Draw custom menu background
    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    // Use standard multiply blender; texture alpha still applies
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    
    // Calculate positioning
    int x = (SCREEN_WIDTH - dialogWidth) / 2;
    int y = (SCREEN_HEIGHT - dialogHeight) / 2;
    
    // Draw textured background (single image, centered and scaled to cover)
    draw_pause_menu_background(x, y, dialogWidth, dialogHeight);
    
    // Light overlay to keep text readable while leaving the image visible
    rdpq_set_prim_color(RGBA32(0, 0, 0, 90));
    rdpq_fill_rectangle(x, y, x + dialogWidth, y + dialogHeight);
    
    // Draw border frame
    rdpq_set_prim_color(RGBA32(180, 160, 120, 255));
    // Outer border
    rdpq_fill_rectangle(x, y, x + dialogWidth, y + 3);                    // Top
    rdpq_fill_rectangle(x, y + dialogHeight - 3, x + dialogWidth, y + dialogHeight); // Bottom
    rdpq_fill_rectangle(x, y, x + 3, y + dialogHeight);                  // Left
    rdpq_fill_rectangle(x + dialogWidth - 3, y, x + dialogWidth, y + dialogHeight);  // Right
    
    // Inner border highlight
    rdpq_set_prim_color(RGBA32(220, 200, 160, 255));
    rdpq_fill_rectangle(x + 3, y + 3, x + dialogWidth - 3, y + 4);       // Top highlight
    rdpq_fill_rectangle(x + 3, y + 3, x + 4, y + dialogHeight - 3);      // Left highlight
    
    // Calculate centered text positioning
    x += 20;
    y += 20;
    int lineHeight = 16;
    
    if (currentMenu == MENU_MAIN) {
        // Draw main menu with centered text
        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_CENTER,
            .width = dialogWidth - 40,
            .height = dialogHeight - 40,
            .wrap = WRAP_WORD,
        }, FONT_UNBALANCED, x, y, "PAUSE MENU");
        
        y += lineHeight * 3;
        
        for (int i = 0; i < MENU_MAIN_COUNT; i++) {
            // Highlight selected option
            if (i == selectedOption) {
                rdpq_set_prim_color(RGBA32(255, 255, 0, 255));
                rdpq_text_printf(&(rdpq_textparms_t){
                    .align = ALIGN_CENTER,
                    .width = dialogWidth - 40,
                    .height = dialogHeight - 40,
                    .wrap = WRAP_WORD,
                }, FONT_UNBALANCED, x, y, "> %s", mainMenuOptions[i]);
                rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
            } else {
                rdpq_text_printf(&(rdpq_textparms_t){
                    .align = ALIGN_CENTER,
                    .width = dialogWidth - 40,
                    .height = dialogHeight - 40,
                    .wrap = WRAP_WORD,
                }, FONT_UNBALANCED, x, y, "%s", mainMenuOptions[i]);
            }
            y += lineHeight + 4;
        }
    } else if (currentMenu == MENU_AUDIO) {
        // Draw audio menu with centered text
        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_CENTER,
            .width = dialogWidth - 40,
            .height = dialogHeight - 40,
            .wrap = WRAP_WORD,
        }, FONT_UNBALANCED, x, y, "AUDIO SETTINGS");
        
        y += lineHeight * 3;
        
        for (int i = 0; i < MENU_AUDIO_COUNT; i++) {
            char optionText[64];
            
            // Format option text with current values
            if (i == MENU_AUDIO_MASTER_VOLUME) {
                sprintf(optionText, audioMenuOptions[i], audio_get_master_volume());
            } else if (i == MENU_AUDIO_MUSIC_VOLUME) {
                sprintf(optionText, audioMenuOptions[i], audio_get_music_volume());
            } else if (i == MENU_AUDIO_SFX_VOLUME) {
                sprintf(optionText, audioMenuOptions[i], audio_get_sfx_volume());
            } else if (i == MENU_AUDIO_MUTE_TOGGLE) {
                sprintf(optionText, audioMenuOptions[i], audio_is_muted() ? "ON" : "OFF");
            } else if (i == MENU_AUDIO_STEREO_MODE) {
                sprintf(optionText, audioMenuOptions[i], audio_get_stereo_mode() ? "Stereo" : "Mono");
            } else {
                strcpy(optionText, audioMenuOptions[i]);
            }
            
            // Highlight selected option
            if (i == selectedOption) {
                rdpq_set_prim_color(RGBA32(255, 255, 0, 255));
                rdpq_text_printf(&(rdpq_textparms_t){
                    .align = ALIGN_CENTER,
                    .width = dialogWidth - 40,
                    .height = dialogHeight - 40,
                    .wrap = WRAP_WORD,
                }, FONT_UNBALANCED, x, y, "> %s", optionText);
                rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
            } else {
                rdpq_text_printf(&(rdpq_textparms_t){
                    .align = ALIGN_CENTER,
                    .width = dialogWidth - 40,
                    .height = dialogHeight - 40,
                    .wrap = WRAP_WORD,
                }, FONT_UNBALANCED, x, y, "%s", optionText);
            }
            y += lineHeight + 2;
        }
        
        // Add instructions (centered)
        y += lineHeight;
        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_CENTER,
            .width = dialogWidth - 40,
            .height = dialogHeight - 40,
            .wrap = WRAP_WORD,
        }, FONT_UNBALANCED, x, y, "< > to adjust, B to go back");
    }
}

void menu_controller_free(void) {
    rspq_wait(); // Ensure rendering is complete before freeing
    if (pauseMenuBg) {
        sprite_free(pauseMenuBg);
        pauseMenuBg = NULL;
        surface_free(&pauseMenuBgSurf);
    }
}

bool menu_controller_is_active(void) {
    return menuActive;
}

void menu_controller_toggle(void) {
    menuActive = !menuActive;
    if (menuActive) {
        scene_set_game_state(GAME_STATE_MENU);
        currentMenu = MENU_MAIN;
        selectedOption = 0;
        
        // Mute audio when menu opens (only if music is playing)
        musicWasPaused = !audio_is_music_playing();
        if (!musicWasPaused) {
            audio_pause_music();
        }
    } else {
        scene_set_game_state(GAME_STATE_PLAYING);
        
        // Unmute audio when menu closes (if it was playing)
        if (!musicWasPaused) {
            audio_resume_music();
        }
    }
}

void menu_controller_close(void) {
    menuActive = false;
    scene_set_game_state(GAME_STATE_PLAYING);
    
    // Unmute audio when menu closes (if it was playing)
    if (!musicWasPaused) {
        audio_resume_music();
    }
}