#include <libdragon.h>
#include <stdio.h>
#include <string.h>
#include "menu_controller.h"
#include "joypad_utility.h"
#include "audio_controller.h"
#include "scene.h"

#include "globals.h"

// Menu state
static MenuState currentMenu = MENU_MAIN;
static MenuState parentMenu = MENU_MAIN;
static int selectedOption = 0;
static int parentSelectedOption = 0;
static bool menuActive = false;
static bool musicWasPaused = false;
static bool menuIsTitleMenu = false;
static GameState menuReturnState = GAME_STATE_PLAYING;

// Pause menu background
static sprite_t *pauseMenuBg = NULL;
static surface_t pauseMenuBgSurf = {0};

// Controls menu icons (prefer black-outline set for readability on bright backgrounds)
static sprite_t *iconA = NULL;
static sprite_t *iconB = NULL;
static sprite_t *iconZ = NULL;
static sprite_t *iconStart = NULL;
static sprite_t *iconStick = NULL;
static sprite_t *iconCLeft = NULL;

// Input handling for edge detection
static bool lastStartPressed = false;
static bool lastUpPressed = false;
static bool lastDownPressed = false;
static bool lastLeftPressed = false;
static bool lastRightPressed = false;
static bool lastAPressed = false;
static bool lastBPressed = false;
static bool lastStickUp = false;
static bool lastStickDown = false;

// Menu text
static const char* mainMenuOptions[MENU_MAIN_COUNT] = {
    "Resume",
    "Restart",
    "Settings",
    "Controls",
};

static const char* titleMenuOptions[MENU_TITLE_COUNT] = {
    "Play",
    "Settings",
    "Controls",
    "Credits",
};

static const char* audioMenuOptions[MENU_AUDIO_COUNT] = {
    "Master Volume: %d",
    "Music Volume: %d", 
    "SFX Volume: %d",
    "Mute All: %s",
    "Audio Mode: %s",
    "Aspect: %s",
    "Back",
};

void menu_controller_init(void) {
    currentMenu = MENU_MAIN;
    parentMenu = MENU_MAIN;
    selectedOption = 0;
    parentSelectedOption = 0;
    menuActive = false;
    menuIsTitleMenu = false;
    
    // Use the vertical dialog sprite as the pause menu background
    pauseMenuBg = sprite_load("rom:/dialog_vertical.ia8.sprite");
    if (pauseMenuBg) {
        pauseMenuBgSurf = sprite_get_pixels(pauseMenuBg);
    }

    // Load button icons for the Controls submenu
    // NOTE: Prefer black-outline sprites so they're visible on light/busy backgrounds.
    // A is currently only available as a colored RGBA sprite in the white-outline set.
    iconA = sprite_load("rom:/buttons/WhiteOutlineButtons/a.rgba16.sprite");

    iconB = sprite_load("rom:/buttons/WhiteOutlineButtons/B.sprite");
    iconZ = sprite_load("rom:/buttons/WhiteOutlineButtons/Z.sprite");
    iconStart = sprite_load("rom:/buttons/WhiteOutlineButtons/Start.sprite");
    iconStick = sprite_load("rom:/buttons/WhiteOutlineButtons/StickTexture.sprite");
    iconCLeft = sprite_load("rom:/buttons/WhiteOutlineButtons/CLeft.sprite");
}

void menu_controller_update(void) {
    GameState state = scene_get_game_state();
    // Allow pause menu during victory (eg. after defeating the boss).
    // Still disable during death screen.
    if (state == GAME_STATE_DEAD) {
        return;
    }

    // Hide title menu during the transition/cinematic move.
    if (state == GAME_STATE_TITLE_TRANSITION) {
        if (menuActive && menuIsTitleMenu) {
            menuActive = false;
        }
        return;
    }

    // Only allow the pause menu during gameplay/victory.
    // While in GAME_STATE_MENU we still need to process menu input to allow closing/navigating.
    // While in GAME_STATE_TITLE we use the same controller for the title menu.
    if (state != GAME_STATE_PLAYING && state != GAME_STATE_MENU && state != GAME_STATE_TITLE && state != GAME_STATE_VICTORY) {
        return;
    }

    // Title menu should always be visible (no "press Start to open").
    if (state == GAME_STATE_TITLE) {
        menuIsTitleMenu = true;
        // Only initialize when it is closed; do NOT stomp submenus.
        if (!menuActive) {
            menuActive = true;
            currentMenu = MENU_TITLE;
            parentMenu = MENU_TITLE;
            parentSelectedOption = 0;
            selectedOption = 0;
        }
    }

    // Handle start button to toggle menu
    bool startPressed = btn.start;
    bool startJustPressed = startPressed && !lastStartPressed;
    lastStartPressed = startPressed;
    
    if (startJustPressed && state != GAME_STATE_TITLE) {
        // Don't allow pausing during cutscenes
        if (!scene_is_cutscene_active() && state != GAME_STATE_TITLE_TRANSITION) {
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

    // Add analog-stick navigation on title/pause menus (edge-triggered)
    const int stickThreshold = 40;
    bool stickUp = (joypad.stick_y > stickThreshold);
    bool stickDown = (joypad.stick_y < -stickThreshold);
    bool stickUpJustPressed = stickUp && !lastStickUp;
    bool stickDownJustPressed = stickDown && !lastStickDown;
    lastStickUp = stickUp;
    lastStickDown = stickDown;

    if (state == GAME_STATE_TITLE && startJustPressed) {
        // On title, Start should act like A (activate current selection)
        aJustPressed = true;
    }
    
    // Navigation between options
    if (upJustPressed || stickUpJustPressed) {
        selectedOption--;
        int maxOptions = 0;
        if (currentMenu == MENU_MAIN) maxOptions = MENU_MAIN_COUNT;
        else if (currentMenu == MENU_TITLE) maxOptions = MENU_TITLE_COUNT;
        else if (currentMenu == MENU_AUDIO) maxOptions = MENU_AUDIO_COUNT;
        else if (currentMenu == MENU_CONTROLS) maxOptions = 1;
        else if (currentMenu == MENU_CREDITS) maxOptions = 1;
        if (selectedOption < 0) {
            selectedOption = maxOptions - 1; // Wrap to bottom
        }
    }
    
    if (downJustPressed || stickDownJustPressed) {
        selectedOption++;
        int maxOptions = 0;
        if (currentMenu == MENU_MAIN) maxOptions = MENU_MAIN_COUNT;
        else if (currentMenu == MENU_TITLE) maxOptions = MENU_TITLE_COUNT;
        else if (currentMenu == MENU_AUDIO) maxOptions = MENU_AUDIO_COUNT;
        else if (currentMenu == MENU_CONTROLS) maxOptions = 1;
        else if (currentMenu == MENU_CREDITS) maxOptions = 1;
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
                    // Close the menu first so it doesn't override the new gameState set by restart.
                    menu_controller_close();
                    scene_restart();
                    break;
                case MENU_MAIN_SETTINGS:
                    parentMenu = MENU_MAIN;
                    parentSelectedOption = MENU_MAIN_SETTINGS;
                    currentMenu = MENU_AUDIO;
                    selectedOption = 0;
                    break;
                case MENU_MAIN_CONTROLS:
                    parentMenu = MENU_MAIN;
                    parentSelectedOption = MENU_MAIN_CONTROLS;
                    currentMenu = MENU_CONTROLS;
                    selectedOption = 0;
                    break;
            }
        }
    } else if (currentMenu == MENU_TITLE) {
        // Title menu is always visible; B does nothing here.

        if (aJustPressed) {
            switch (selectedOption) {
                case MENU_TITLE_PLAY:
                    // Close first so title doesn't interpret stale edges on the next frame.
                    menu_controller_close();
                    scene_begin_title_transition();
                    break;
                case MENU_TITLE_AUDIO_SETTINGS:
                    parentMenu = MENU_TITLE;
                    parentSelectedOption = MENU_TITLE_AUDIO_SETTINGS;
                    currentMenu = MENU_AUDIO;
                    selectedOption = 0;
                    break;
                case MENU_TITLE_CONTROLS:
                    parentMenu = MENU_TITLE;
                    parentSelectedOption = MENU_TITLE_CONTROLS;
                    currentMenu = MENU_CONTROLS;
                    selectedOption = 0;
                    break;
                case MENU_TITLE_CREDITS:
                    parentMenu = MENU_TITLE;
                    parentSelectedOption = MENU_TITLE_CREDITS;
                    currentMenu = MENU_CREDITS;
                    selectedOption = 0;
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
                case MENU_AUDIO_ASPECT:
                    {
                        if (leftJustPressed || rightJustPressed) {
                            hdAspect = !hdAspect;
                        }
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
                case MENU_AUDIO_ASPECT:
                    {
                        hdAspect = !hdAspect;
                    }
                    break;
                case MENU_AUDIO_BACK:
                    currentMenu = parentMenu;
                    selectedOption = parentSelectedOption;
                    break;
            }
        }
        
        // B button to go back to main menu (not close the entire menu)
        if (bJustPressed) {
            currentMenu = parentMenu;
            selectedOption = parentSelectedOption;
        }
    } else if (currentMenu == MENU_CONTROLS) {
        // Any A/B returns
        if (aJustPressed || bJustPressed) {
            currentMenu = parentMenu;
            selectedOption = parentSelectedOption;
        }
    } else if (currentMenu == MENU_CREDITS) {
        // Any A/B returns
        if (aJustPressed || bJustPressed) {
            currentMenu = parentMenu;
            selectedOption = parentSelectedOption;
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

    // "Fit + center" (keep aspect ratio) so the vertical dialog sprite doesn't get blown up too much.
    // Optionally stretch a bit on X to better match the menu frame.
    const float maxScaleX = (float)dialogWidth / (float)imgW;
    const float maxScaleY = (float)dialogHeight / (float)imgH;
    const float fitScale = (maxScaleX < maxScaleY) ? maxScaleX : maxScaleY;

    // Let the background fill more of the menu frame.
    const float heightShrink = 0.98f;
    const float baseScale = fitScale * heightShrink;

    const float extraXStretch = 1.15f;
    float scaleX = baseScale * extraXStretch;
    if (scaleX > maxScaleX) scaleX = maxScaleX;
    const float scaleY = baseScale;

    const int drawW = (int)(imgW * scaleX);
    const int drawH = (int)(imgH * scaleY);
    const int drawX = x + (dialogWidth - drawW) / 2;
    const int drawY = y + (dialogHeight - drawH) / 2;

    // Respect sprite alpha so transparent areas don't draw as black.
    rdpq_mode_alphacompare(1);
    rdpq_tex_blit(&pauseMenuBgSurf, drawX, drawY, &(rdpq_blitparms_t){
            .scale_x = scaleX,
            .scale_y = scaleY,
        });
}

static void draw_menu_selection_highlight(int x, int y, int w, int h)
{
    // Black translucent selection box
    const int padX = 6;
    const int padY = 2;
    const int x0 = x - padX;
    // rdpq_text_printf uses a top-left origin for y0, so match that.
    const int y0 = y - padY;
    const int x1 = x + w + padX;
    const int y1 = y + h + padY;

    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    rdpq_set_prim_color(RGBA32(0, 0, 0, 140));
    rdpq_fill_rectangle(x0, y0, x1, y1);

    // Thin white outline (requested)
    rdpq_set_prim_color(RGBA32(255, 255, 255, 70));
    rdpq_fill_rectangle(x0, y0, x1, y0 + 1);
    rdpq_fill_rectangle(x0, y1 - 1, x1, y1);
    rdpq_fill_rectangle(x0, y0, x0 + 1, y1);
    rdpq_fill_rectangle(x1 - 1, y0, x1, y1);
}

// Title menu uses a slightly different text Y convention; keep a baseline-style variant.
static void draw_menu_selection_highlight_baseline(int x, int baselineY, int w, int h)
{
    const int padX = 6;
    const int padY = 2;
    const int x0 = x - padX;
    const int y0 = (baselineY - h) - padY + 2;
    const int x1 = x + w + padX;
    const int y1 = baselineY + padY + 4;

    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    rdpq_set_prim_color(RGBA32(0, 0, 0, 140));
    rdpq_fill_rectangle(x0, y0, x1, y1);
}

static void draw_menu_selection_highlight_centered(int contentX, int y, int contentW, int lineH, int maxW)
{
    int w = maxW;
    if (w > contentW) w = contentW;
    int x = contentX + (contentW - w) / 2;
    draw_menu_selection_highlight(x, y, w, lineH);
}

static void draw_icon_line(sprite_t *icon, int x, int rowBaselineY, int lineH, int w, const char *text)
{
    const int gap = 8;
    const int srcW = (icon && icon->width > 0) ? icon->width : lineH;
    const int srcH = (icon && icon->height > 0) ? icon->height : lineH;
    // Fixed icon box so every row aligns regardless of sprite dimensions.
    const int box = lineH;
    // rdpq_text_* uses Y as a BASELINE coordinate (see libdragon's rdpq_text.h).
    // Our menu code generally tracks "y" as a baseline too, so convert that
    // baseline into a row-top for sprite placement.
    const int rowTop = rowBaselineY - lineH;

    // Scale to fit within the box (keep aspect ratio)
    float s = 1.0f;
    if (srcW > 0 && srcH > 0) {
        float sx = (float)box / (float)srcW;
        float sy = (float)box / (float)srcH;
        s = (sx < sy) ? sx : sy;
    }

    const int iconW = (int)(srcW * s);
    const int iconH = (int)(srcH * s);
    const int iconX = x + (box - iconW) / 2;
    const int iconY = rowTop + (box - iconH) / 2;

    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    // Avoid alpha-compare clipping on anti-aliased/soft edges (eg. Z sprite)
    rdpq_mode_alphacompare(0);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    if (icon) {
        // Use sprite blit so paletted sprites (CI4/CI8) render correctly (TLUT handled automatically)
        rdpq_sprite_blit(icon, iconX, iconY, &(rdpq_blitparms_t){
            .scale_x = s,
            .scale_y = s,
        });
    }

    rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    rdpq_text_printf(&(rdpq_textparms_t){
        .align = ALIGN_LEFT,
        .width = w,
        .wrap = WRAP_WORD,
    }, FONT_UNBALANCED, x + box + gap, rowBaselineY, "%s", text ? text : "");
}

void menu_controller_draw(void) {
    if (!menuActive) {
        return;
    }

    // Title main menu: left-aligned list, no dialog background/gradient.
    if (currentMenu == MENU_TITLE) {
        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);

        // Nudge right so the title menu sits inside title-safe on CRTs.
        const int menuX = TITLE_SAFE_MARGIN_X + 4; // ~+10px vs previous
        int y = 40;
        const int menuW = 140;
        const int lineHeight = 16;

        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_LEFT,
            .width = menuW,
            .wrap = WRAP_WORD,
        }, FONT_UNBALANCED, menuX, y, "Pandemonium");

        y += (lineHeight * 2) + 6;

        for (int i = 0; i < MENU_TITLE_COUNT; i++) {
            if (i == selectedOption) {
                // Slightly wider selection background on the top-level title menu.
                draw_menu_selection_highlight_baseline(menuX, y, (menuW * 3) / 4, lineHeight);
                rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
            } else {
                rdpq_set_prim_color(RGBA32(220, 220, 220, 255));
            }

            rdpq_text_printf(&(rdpq_textparms_t){
                .align = ALIGN_LEFT,
                .width = menuW,
                .wrap = WRAP_WORD,
            }, FONT_UNBALANCED, menuX, y, "%s", titleMenuOptions[i]);

            y += lineHeight + 6;
        }

        return;
    }
    
    // Use different dialog sizes based on current menu
    // Make the pause menu panel as large as possible within the 320x240 screen.
    const int dialogMarginX = 0; // pixels of margin on each side
    int dialogWidth = SCREEN_WIDTH - (dialogMarginX * 2);
    int dialogHeight = 200;
    if (currentMenu == MENU_AUDIO) {
        dialogHeight = 230;
    } else if (currentMenu == MENU_CONTROLS || currentMenu == MENU_CREDITS) {
        dialogHeight = 230;
    }

    // Submenus: use a tighter dialog width so the background fits the menu content better.
    if (currentMenu == MENU_CONTROLS) {
        // Controls screen needs more horizontal room (icons + text)
        dialogWidth = 320; // ~2x the narrow submenu panel
    } else if (currentMenu == MENU_AUDIO || currentMenu == MENU_CREDITS) {
        dialogWidth = 260;
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
    
    // Text padding inside the frame (keep modest; dialog size controls overall space)
    const int paddingX = 30;
    const int paddingY = 20;
    const int contentX = x + paddingX;
    const int contentY = y + paddingY;
    const int contentW = dialogWidth - (paddingX * 2);
    const int contentH = dialogHeight - (paddingY * 2);

    // Cursor for rendering text lines
    x = contentX;
    y = contentY;
    int lineHeight = 16;
    const int titleYOffset = 8;
    
    if (currentMenu == MENU_MAIN) {
        // Draw main menu with centered text
        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_CENTER,
            .width = contentW,
            .height = contentH,
            .wrap = WRAP_WORD,
        }, FONT_UNBALANCED, x, y + titleYOffset, "PAUSE MENU");
        
        y += (lineHeight * 3) + titleYOffset;
        
        for (int i = 0; i < MENU_MAIN_COUNT; i++) {
            // Highlight selected option
            if (i == selectedOption) {
                // Pause menu panel is very wide; clamp highlight so it matches other menus visually.
                draw_menu_selection_highlight_centered(x, y, contentW, lineHeight, 90);
                rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
                rdpq_text_printf(&(rdpq_textparms_t){
                    .align = ALIGN_CENTER,
                    .width = contentW,
                    .height = contentH,
                    .wrap = WRAP_WORD,
                }, FONT_UNBALANCED, x, y, "%s", mainMenuOptions[i]);
            } else {
                rdpq_set_prim_color(RGBA32(220, 220, 220, 255));
                rdpq_text_printf(&(rdpq_textparms_t){
                    .align = ALIGN_CENTER,
                    .width = contentW,
                    .height = contentH,
                    .wrap = WRAP_WORD,
                }, FONT_UNBALANCED, x, y, "%s", mainMenuOptions[i]);
            }
            y += lineHeight + 4;
        }
        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    } else if (currentMenu == MENU_TITLE) {
        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_CENTER,
            .width = contentW,
            .height = contentH,
            .wrap = WRAP_WORD,
        }, FONT_UNBALANCED, x, y + titleYOffset, "MAIN MENU");

        y += (lineHeight * 3) + titleYOffset;

        for (int i = 0; i < MENU_TITLE_COUNT; i++) {
            if (i == selectedOption) {
                draw_menu_selection_highlight_centered(x, y, contentW, lineHeight, contentW / 2);
                rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
                rdpq_text_printf(&(rdpq_textparms_t){
                    .align = ALIGN_CENTER,
                    .width = contentW,
                    .height = contentH,
                    .wrap = WRAP_WORD,
                }, FONT_UNBALANCED, x, y, "%s", titleMenuOptions[i]);
            } else {
                rdpq_set_prim_color(RGBA32(220, 220, 220, 255));
                rdpq_text_printf(&(rdpq_textparms_t){
                    .align = ALIGN_CENTER,
                    .width = contentW,
                    .height = contentH,
                    .wrap = WRAP_WORD,
                }, FONT_UNBALANCED, x, y, "%s", titleMenuOptions[i]);
            }
            y += lineHeight + 4;
        }
        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    } else if (currentMenu == MENU_AUDIO) {
        // Draw audio menu with centered text
        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_CENTER,
            .width = contentW,
            .height = contentH,
            .wrap = WRAP_WORD,
        }, FONT_UNBALANCED, x, y + titleYOffset, "SETTINGS");
        
        y += (lineHeight * 3) + titleYOffset;
        
        for (int i = 0; i < MENU_AUDIO_COUNT; i++) {
            char optionText[64];
            const char *fmt = audioMenuOptions[i] ? audioMenuOptions[i] : "";
            
            // Format option text with current values
            if (i == MENU_AUDIO_MASTER_VOLUME) {
                snprintf(optionText, sizeof(optionText), fmt, audio_get_master_volume());
            } else if (i == MENU_AUDIO_MUSIC_VOLUME) {
                snprintf(optionText, sizeof(optionText), fmt, audio_get_music_volume());
            } else if (i == MENU_AUDIO_SFX_VOLUME) {
                snprintf(optionText, sizeof(optionText), fmt, audio_get_sfx_volume());
            } else if (i == MENU_AUDIO_MUTE_TOGGLE) {
                snprintf(optionText, sizeof(optionText), fmt, audio_is_muted() ? "ON" : "OFF");
            } else if (i == MENU_AUDIO_STEREO_MODE) {
                snprintf(optionText, sizeof(optionText), fmt, audio_get_stereo_mode() ? "Stereo" : "Mono");
            }else if (i == MENU_AUDIO_ASPECT) {
                snprintf(optionText, sizeof(optionText), fmt, hdAspect ? "16:9" : "4:3");
            } else {
                snprintf(optionText, sizeof(optionText), "%s", fmt);
            }
            
            // Highlight selected option
            if (i == selectedOption) {
                draw_menu_selection_highlight_centered(x, y, contentW, lineHeight, contentW / 2);
                rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
                rdpq_text_printf(&(rdpq_textparms_t){
                    .align = ALIGN_CENTER,
                    .width = contentW,
                    .height = contentH,
                    .wrap = WRAP_WORD,
                }, FONT_UNBALANCED, x, y, "%s", optionText);
            } else {
                rdpq_set_prim_color(RGBA32(220, 220, 220, 255));
                rdpq_text_printf(&(rdpq_textparms_t){
                    .align = ALIGN_CENTER,
                    .width = contentW,
                    .height = contentH,
                    .wrap = WRAP_WORD,
                }, FONT_UNBALANCED, x, y, "%s", optionText);
            }
            y += lineHeight + 2;
        }
        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    } else if (currentMenu == MENU_CONTROLS) {
        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_CENTER,
            .width = contentW,
            .height = contentH,
            .wrap = WRAP_WORD,
        }, FONT_UNBALANCED, x, y + titleYOffset, "CONTROLS");

        y += (lineHeight * 3) + titleYOffset;

        // Controls legend: left-aligned, but the whole block is centered in the panel.
        // Slight right nudge for nicer visual balance with the wider panel.
        const int listW = 150;
        const int listX = x + (contentW - listW) / 2 + 12;

        draw_icon_line(iconStick, listX, y, lineHeight, listW, "Move");
        y += lineHeight + 6;
        draw_icon_line(iconA, listX, y, lineHeight, listW, "Dodge / Interact");
        y += lineHeight + 6;
        draw_icon_line(iconB, listX, y, lineHeight, listW, "Attack");
        y += lineHeight + 6;
        draw_icon_line(iconZ, listX, y, lineHeight, listW, "Target");
        y += lineHeight + 6;
        draw_icon_line(iconStart, listX, y, lineHeight, listW, "Pause Menu");
        y += lineHeight + 6;
        // Use one C-button sprite to represent the cluster
        draw_icon_line(iconCLeft, listX, y, lineHeight, listW, "Move Camera");
    } else if (currentMenu == MENU_CREDITS) {
        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_CENTER,
            .width = contentW,
            .height = contentH,
            .wrap = WRAP_WORD,
        }, FONT_UNBALANCED, x, y + titleYOffset, "CREDITS");

        y += (lineHeight * 3) + titleYOffset;

        // Credits (requested)
        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
        rdpq_text_printf(&(rdpq_textparms_t){ .align = ALIGN_CENTER, .width = contentW, .height = contentH, .wrap = WRAP_WORD },
                         FONT_UNBALANCED, x, y, "Zero Cool");
        y += lineHeight + 8;
        rdpq_text_printf(&(rdpq_textparms_t){ .align = ALIGN_CENTER, .width = contentW, .height = contentH, .wrap = WRAP_WORD },
                         FONT_UNBALANCED, x, y, "BoxingBruin");
        y += lineHeight + 6;
        rdpq_text_printf(&(rdpq_textparms_t){ .align = ALIGN_CENTER, .width = contentW, .height = contentH, .wrap = WRAP_WORD },
                         FONT_UNBALANCED, x, y, "HelloNewman");
    }
}

void menu_controller_free(void) {
    rspq_wait(); // Ensure rendering is complete before freeing
    if (pauseMenuBg) {
        sprite_free(pauseMenuBg);
        pauseMenuBg = NULL;
        surface_free(&pauseMenuBgSurf);
    }

    if (iconA) { sprite_free(iconA); iconA = NULL; }
    if (iconB) { sprite_free(iconB); iconB = NULL; }
    if (iconZ) { sprite_free(iconZ); iconZ = NULL; }
    if (iconStart) { sprite_free(iconStart); iconStart = NULL; }
    if (iconStick) { sprite_free(iconStick); iconStick = NULL; }
    if (iconCLeft) { sprite_free(iconCLeft); iconCLeft = NULL; }
}

bool menu_controller_is_active(void) {
    return menuActive;
}

bool menu_controller_is_title_submenu_active(void) {
    return menuActive && menuIsTitleMenu && currentMenu != MENU_TITLE;
}

bool menu_controller_is_pause_menu_active(void) {
    return menuActive && !menuIsTitleMenu;
}

void menu_controller_toggle(void) {
    menuActive = !menuActive;
    if (menuActive) {
        GameState state = scene_get_game_state();

        // Pick which "root" menu to show based on where we are.
        menuIsTitleMenu = (state == GAME_STATE_TITLE);
        parentMenu = menuIsTitleMenu ? MENU_TITLE : MENU_MAIN;
        parentSelectedOption = 0;

        if (menuIsTitleMenu) {
            currentMenu = MENU_TITLE;
        } else {
            // Remember which game state to return to when closing the pause menu.
            menuReturnState = state;
            // IMPORTANT: if we're on the victory screen, do NOT transition out of VICTORY.
            // Otherwise, scene_set_game_state() will reset the victory title-card timer when
            // we re-enter VICTORY, causing "Enemy restored" to replay after closing the menu.
            if (state != GAME_STATE_VICTORY) {
                scene_set_game_state(GAME_STATE_MENU);
            }
            currentMenu = MENU_MAIN;
        }

        selectedOption = 0;
        
        // Pause/Resume music only for in-game pause menu.
        if (!menuIsTitleMenu) {
            musicWasPaused = !audio_is_music_playing();
            if (!musicWasPaused) {
                audio_pause_music();
            }
        }
    } else {
        if (!menuIsTitleMenu) {
            scene_set_game_state(menuReturnState);

            // Unmute audio when menu closes (if it was playing)
            if (!musicWasPaused) {
                audio_resume_music();
            }
        }
    }
}

void menu_controller_close(void) {
    menuActive = false;

    if (!menuIsTitleMenu) {
        scene_set_game_state(menuReturnState);

        // Unmute audio when menu closes (if it was playing)
        if (!musicWasPaused) {
            audio_resume_music();
        }
    }
}