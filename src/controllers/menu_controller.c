#include <libdragon.h>
#include <stdio.h>
#include <string.h>

#include "menu_controller.h"
#include "../utilities/joypad_utility.h"
#include "audio_controller.h"
#include "../scenes/guardian/guardian_scene.h"
#include "../utilities/save_controller.h"

#include "../utilities/globals.h"
#include "../utilities/video_layout.h"

// Menu state
static MenuState currentMenu = MENU_MAIN;
static MenuState parentMenu = MENU_MAIN;
static int selectedOption = 0;
static int parentSelectedOption = 0;
static bool menuActive = false;
static bool musicWasPaused = false;
static bool menuIsTitleMenu = false;
static GameState menuReturnState = GAME_STATE_PLAYING;

// Scene/UI action output.
// The menu controller does not switch scenes directly.
// Consumers call menu_controller_consume_action().
static MenuAction pendingAction = MENU_ACTION_NONE;

// Settings hub "return to" target (since parentMenu is reused for submenus).
static MenuState settingsHubReturnMenu = MENU_MAIN;
static int       settingsHubReturnSelectedOption = 0;

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

// Overscan calibration overlay (inside Video settings)
static bool overscanCalibrating = false;
static int8_t overscanPrevX = 0;
static int8_t overscanPrevY = 0;

// Menu text
static const char* mainMenuOptions[MENU_MAIN_COUNT] = {
    "Resume",
    "Restart",
    "Settings",
    "Controls",
};

static const char* titleMenuOptions[MENU_TITLE_COUNT] = {
    "Continue",
    "Load Game",
    "Settings",
    "Credits",
};

static const char* settingsMenuOptions[MENU_SETTINGS_COUNT] = {
    "Audio",
    "Video",
    "Controls",
    "Back",
};

static const char* audioMenuOptions[MENU_AUDIO_COUNT] = {
    "Master Volume: %d",
    "Music Volume: %d",
    "SFX Volume: %d",
    "Mute All: %s",
    "Audio Mode: %s",
    "Rumble: %s",
    "Back",
};

static const char* videoMenuOptions[MENU_VIDEO_COUNT] = {
    "Aspect: %s",
    "Calibrate Overscan",
    "Back",
};

static const char* loadGameMenuOptions[MENU_LOAD_GAME_COUNT] = {
    "Save 1",
    "Save 2",
    "Save 3",
    "Back",
};

static int8_t clamp_overscan_x(int v)
{
    if (v < UI_OVERSCAN_MIN) v = UI_OVERSCAN_MIN;
    int maxX = (SCREEN_WIDTH / 2) - 2;
    if (v > maxX) v = maxX;
    return (int8_t)v;
}

static int8_t clamp_overscan_y(int v)
{
    if (v < UI_OVERSCAN_MIN) v = UI_OVERSCAN_MIN;
    int maxY = (SCREEN_HEIGHT / 2) - 2;
    if (v > maxY) v = maxY;
    return (int8_t)v;
}

static int get_menu_option_count(MenuState menu)
{
    switch (menu) {
        case MENU_MAIN:      return MENU_MAIN_COUNT;
        case MENU_TITLE:     return MENU_TITLE_COUNT;
        case MENU_SETTINGS:  return MENU_SETTINGS_COUNT;
        case MENU_AUDIO:     return MENU_AUDIO_COUNT;
        case MENU_VIDEO:     return MENU_VIDEO_COUNT;
        case MENU_LOAD_GAME: return MENU_LOAD_GAME_COUNT;
        case MENU_CONTROLS:  return 1;
        case MENU_CREDITS:   return 1;
        default:             return 1;
    }
}

static void overscan_apply_and_save(void)
{
    uiOverscanX = clamp_overscan_x((int)uiOverscanX);
    uiOverscanY = clamp_overscan_y((int)uiOverscanY);
    (void)save_controller_save_settings();
}

static void draw_overscan_corner_markers(void)
{
    const int left   = ui_safe_margin_x();
    const int right  = SCREEN_WIDTH - ui_safe_margin_x();
    const int top    = ui_safe_margin_y();
    const int bottom = SCREEN_HEIGHT - ui_safe_margin_y();

    const int len = 14;
    const int t = 2;

    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);

    rdpq_set_prim_color(RGBA32(0, 0, 0, 120));
    rdpq_fill_rectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    rdpq_set_prim_color(RGBA32(80, 255, 80, 255));

    rdpq_fill_rectangle(left, top, left + len, top + t);
    rdpq_fill_rectangle(left, top, left + t, top + len);

    rdpq_fill_rectangle(right - len, top, right, top + t);
    rdpq_fill_rectangle(right - t, top, right, top + len);

    rdpq_fill_rectangle(left, bottom - t, left + len, bottom);
    rdpq_fill_rectangle(left, bottom - len, left + t, bottom);

    rdpq_fill_rectangle(right - len, bottom - t, right, bottom);
    rdpq_fill_rectangle(right - t, bottom - len, right, bottom);

    rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    const int textY = top + 18;

    rdpq_text_printf(&(rdpq_textparms_t){
        .align = ALIGN_CENTER,
        .width = SCREEN_WIDTH,
        .wrap = WRAP_WORD,
    }, FONT_UNBALANCED, 0, textY, "Overscan Calibration");

    rdpq_text_printf(&(rdpq_textparms_t){
        .align = ALIGN_CENTER,
        .width = SCREEN_WIDTH,
        .wrap = WRAP_WORD,
    }, FONT_UNBALANCED, 0, textY + 16, "D-PAD: adjust   A: save   B: cancel");

    rdpq_text_printf(&(rdpq_textparms_t){
        .align = ALIGN_CENTER,
        .width = SCREEN_WIDTH,
        .wrap = WRAP_WORD,
    }, FONT_UNBALANCED, 0, textY + 34, "X: %d   Y: %d", (int)uiOverscanX, (int)uiOverscanY);
}

void menu_controller_init(void)
{
    currentMenu = MENU_MAIN;
    parentMenu = MENU_MAIN;
    selectedOption = 0;
    parentSelectedOption = 0;
    menuActive = false;
    musicWasPaused = false;
    menuIsTitleMenu = false;
    menuReturnState = GAME_STATE_PLAYING;
    pendingAction = MENU_ACTION_NONE;

    settingsHubReturnMenu = MENU_MAIN;
    settingsHubReturnSelectedOption = 0;

    overscanCalibrating = false;
    overscanPrevX = 0;
    overscanPrevY = 0;

    lastStartPressed = false;
    lastUpPressed = false;
    lastDownPressed = false;
    lastLeftPressed = false;
    lastRightPressed = false;
    lastAPressed = false;
    lastBPressed = false;
    lastStickUp = false;
    lastStickDown = false;

    pauseMenuBg = sprite_load("rom:/dialog_vertical.ia8.sprite");
    if (pauseMenuBg) {
        pauseMenuBgSurf = sprite_get_pixels(pauseMenuBg);
    }

    iconA = sprite_load("rom:/buttons/A.sprite");
    iconB = sprite_load("rom:/buttons/B.sprite");
    iconZ = sprite_load("rom:/buttons/Z.sprite");
    iconStart = sprite_load("rom:/buttons/Start.sprite");
    iconStick = sprite_load("rom:/buttons/StickTexture.sprite");
    iconCLeft = sprite_load("rom:/buttons/CLeft.sprite");
}

MenuAction menu_controller_consume_action(void)
{
    MenuAction action = pendingAction;
    pendingAction = MENU_ACTION_NONE;
    return action;
}

static void menu_controller_update_for_state(GameState state, bool cutsceneActive)
{
    if (state == GAME_STATE_DEAD) {
        return;
    }

    if (state == GAME_STATE_TITLE_TRANSITION) {
        if (menuActive && menuIsTitleMenu) {
            menuActive = false;
        }
        return;
    }

    if (
        state != GAME_STATE_PLAYING &&
        state != GAME_STATE_MENU &&
        state != GAME_STATE_TITLE &&
        state != GAME_STATE_VICTORY
    ) {
        return;
    }

    if (state == GAME_STATE_TITLE) {
        menuIsTitleMenu = true;

        if (!menuActive) {
            menuActive = true;
            currentMenu = MENU_TITLE;
            parentMenu = MENU_TITLE;
            parentSelectedOption = 0;
            selectedOption = 0;
        }
    }

    bool startPressed = btn.start;
    bool startJustPressed = startPressed && !lastStartPressed;
    lastStartPressed = startPressed;

    if (startJustPressed && state != GAME_STATE_TITLE) {
        if (!cutsceneActive && state != GAME_STATE_TITLE_TRANSITION) {
            menu_controller_toggle();
        }
        return;
    }

    if (!menuActive) {
        return;
    }

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

    const int stickThreshold = 40;
    bool stickUp = joypad.stick_y > stickThreshold;
    bool stickDown = joypad.stick_y < -stickThreshold;
    bool stickUpJustPressed = stickUp && !lastStickUp;
    bool stickDownJustPressed = stickDown && !lastStickDown;
    lastStickUp = stickUp;
    lastStickDown = stickDown;

    if (state == GAME_STATE_TITLE && startJustPressed) {
        aJustPressed = true;
    }

    if (!(currentMenu == MENU_VIDEO && overscanCalibrating)) {
        int maxOptions = get_menu_option_count(currentMenu);

        if (upJustPressed || stickUpJustPressed) {
            selectedOption--;
            if (selectedOption < 0) {
                selectedOption = maxOptions - 1;
            }
        }

        if (downJustPressed || stickDownJustPressed) {
            selectedOption++;
            if (selectedOption >= maxOptions) {
                selectedOption = 0;
            }
        }
    }

    if (currentMenu == MENU_MAIN) {
        if (bJustPressed) {
            menu_controller_close();
            return;
        }

        if (aJustPressed) {
            switch (selectedOption) {
                case MENU_MAIN_RESUME:
                    menu_controller_close();
                    break;

                case MENU_MAIN_RESTART:
                    menu_controller_close();

                    /*
                     * First pass:
                     * restart is still guardian-scene-owned.
                     * Later, this can become pendingAction = MENU_ACTION_RESTART_GAME.
                     */
                    scene_restart();
                    break;

                case MENU_MAIN_SETTINGS:
                    parentMenu = MENU_MAIN;
                    parentSelectedOption = MENU_MAIN_SETTINGS;
                    settingsHubReturnMenu = parentMenu;
                    settingsHubReturnSelectedOption = parentSelectedOption;
                    currentMenu = MENU_SETTINGS;
                    selectedOption = 0;
                    break;

                case MENU_MAIN_CONTROLS:
                    parentMenu = MENU_MAIN;
                    parentSelectedOption = MENU_MAIN_CONTROLS;
                    currentMenu = MENU_CONTROLS;
                    selectedOption = 0;
                    break;

                default:
                    break;
            }
        }
    } else if (currentMenu == MENU_TITLE) {
        if (aJustPressed) {
            switch (selectedOption) {
                case MENU_TITLE_CONTINUE: {
                    int lastPlayedSlot = save_controller_get_last_played_slot();
                    int slotToLoad = (lastPlayedSlot >= 0) ? lastPlayedSlot : 0;

                    save_controller_set_active_slot(slotToLoad);
                    save_controller_update_last_played_timestamp();

                    menu_controller_close();
                    pendingAction = MENU_ACTION_TITLE_START_GAME;
                    break;
                }

                case MENU_TITLE_LOAD_GAME:
                    parentMenu = MENU_TITLE;
                    parentSelectedOption = MENU_TITLE_LOAD_GAME;
                    currentMenu = MENU_LOAD_GAME;
                    selectedOption = 0;
                    break;

                case MENU_TITLE_SETTINGS:
                    parentMenu = MENU_TITLE;
                    parentSelectedOption = MENU_TITLE_SETTINGS;
                    settingsHubReturnMenu = parentMenu;
                    settingsHubReturnSelectedOption = parentSelectedOption;
                    currentMenu = MENU_SETTINGS;
                    selectedOption = 0;
                    break;

                case MENU_TITLE_CREDITS:
                    parentMenu = MENU_TITLE;
                    parentSelectedOption = MENU_TITLE_CREDITS;
                    currentMenu = MENU_CREDITS;
                    selectedOption = 0;
                    break;

                default:
                    break;
            }
        }
    } else if (currentMenu == MENU_SETTINGS) {
        if (aJustPressed) {
            switch (selectedOption) {
                case MENU_SETTINGS_AUDIO:
                    parentMenu = MENU_SETTINGS;
                    parentSelectedOption = MENU_SETTINGS_AUDIO;
                    currentMenu = MENU_AUDIO;
                    selectedOption = 0;
                    break;

                case MENU_SETTINGS_VIDEO:
                    parentMenu = MENU_SETTINGS;
                    parentSelectedOption = MENU_SETTINGS_VIDEO;
                    currentMenu = MENU_VIDEO;
                    selectedOption = 0;
                    break;

                case MENU_SETTINGS_CONTROLS:
                    parentMenu = MENU_SETTINGS;
                    parentSelectedOption = MENU_SETTINGS_CONTROLS;
                    currentMenu = MENU_CONTROLS;
                    selectedOption = 0;
                    break;

                case MENU_SETTINGS_BACK:
                    currentMenu = settingsHubReturnMenu;
                    selectedOption = settingsHubReturnSelectedOption;
                    break;

                default:
                    break;
            }
        }

        if (bJustPressed) {
            currentMenu = settingsHubReturnMenu;
            selectedOption = settingsHubReturnSelectedOption;
        }
    } else if (currentMenu == MENU_AUDIO) {
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
                    audio_toggle_mute();
                    break;

                case MENU_AUDIO_STEREO_MODE:
                    audio_toggle_stereo_mode();
                    break;

                case MENU_AUDIO_RUMBLE_TOGGLE:
                    joypad_set_rumble_enabled(!joypad_is_rumble_enabled());
                    (void)save_controller_save_settings();
                    break;

                default:
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

                case MENU_AUDIO_RUMBLE_TOGGLE:
                    joypad_set_rumble_enabled(!joypad_is_rumble_enabled());
                    (void)save_controller_save_settings();
                    break;

                case MENU_AUDIO_BACK:
                    currentMenu = parentMenu;
                    selectedOption = parentSelectedOption;
                    break;

                default:
                    break;
            }
        }

        if (bJustPressed) {
            currentMenu = parentMenu;
            selectedOption = parentSelectedOption;
        }
    } else if (currentMenu == MENU_VIDEO) {
        if (overscanCalibrating) {
            if (leftJustPressed) {
                uiOverscanX = clamp_overscan_x((int)uiOverscanX - 1);
                overscan_apply_and_save();
            }

            if (rightJustPressed) {
                uiOverscanX = clamp_overscan_x((int)uiOverscanX + 1);
                overscan_apply_and_save();
            }

            if (upJustPressed) {
                uiOverscanY = clamp_overscan_y((int)uiOverscanY + 1);
                overscan_apply_and_save();
            }

            if (downJustPressed) {
                uiOverscanY = clamp_overscan_y((int)uiOverscanY - 1);
                overscan_apply_and_save();
            }

            if (aJustPressed) {
                overscanCalibrating = false;
            } else if (bJustPressed) {
                uiOverscanX = overscanPrevX;
                uiOverscanY = overscanPrevY;
                overscan_apply_and_save();
                overscanCalibrating = false;
            }

            return;
        }

        if (leftJustPressed || rightJustPressed) {
            switch (selectedOption) {
                case MENU_VIDEO_ASPECT:
                    hdAspect = !hdAspect;
                    (void)save_controller_save_settings();
                    break;

                default:
                    break;
            }
        }

        if (aJustPressed) {
            switch (selectedOption) {
                case MENU_VIDEO_ASPECT:
                    hdAspect = !hdAspect;
                    (void)save_controller_save_settings();
                    break;

                case MENU_VIDEO_UI_OVERSCAN_CALIBRATE:
                    overscanPrevX = uiOverscanX;
                    overscanPrevY = uiOverscanY;
                    overscanCalibrating = true;
                    break;

                case MENU_VIDEO_BACK:
                    currentMenu = parentMenu;
                    selectedOption = parentSelectedOption;
                    break;

                default:
                    break;
            }
        }

        if (bJustPressed) {
            currentMenu = parentMenu;
            selectedOption = parentSelectedOption;
        }
    } else if (currentMenu == MENU_CONTROLS) {
        if (aJustPressed || bJustPressed) {
            currentMenu = parentMenu;
            selectedOption = parentSelectedOption;
        }
    } else if (currentMenu == MENU_CREDITS) {
        if (aJustPressed || bJustPressed) {
            currentMenu = parentMenu;
            selectedOption = parentSelectedOption;
        }
    } else if (currentMenu == MENU_LOAD_GAME) {
        if (aJustPressed) {
            if (selectedOption >= MENU_LOAD_GAME_SLOT_1 && selectedOption <= MENU_LOAD_GAME_SLOT_3) {
                save_controller_set_active_slot(selectedOption);
                save_controller_update_last_played_timestamp();

                menu_controller_close();
                pendingAction = MENU_ACTION_TITLE_START_GAME;
            } else if (selectedOption == MENU_LOAD_GAME_BACK) {
                currentMenu = parentMenu;
                selectedOption = parentSelectedOption;
            }
        }

        if (bJustPressed) {
            currentMenu = parentMenu;
            selectedOption = parentSelectedOption;
        }
    }
}

void menu_controller_update(void)
{
    menu_controller_update_for_state(
        scene_get_game_state(),
        scene_is_cutscene_active()
    );
}

void menu_controller_update_title(void)
{
    menu_controller_update_for_state(
        GAME_STATE_TITLE,
        false
    );
}

static void draw_pause_menu_background(int x, int y, int dialogWidth, int dialogHeight)
{
    if (!pauseMenuBg || pauseMenuBgSurf.width <= 0 || pauseMenuBgSurf.height <= 0) {
        rdpq_set_prim_color(RGBA32(40, 35, 30, 180));
        rdpq_fill_rectangle(x, y, x + dialogWidth, y + dialogHeight);
        return;
    }

    int imgW = pauseMenuBgSurf.width;
    int imgH = pauseMenuBgSurf.height;

    const float maxScaleX = (float)dialogWidth / (float)imgW;
    const float maxScaleY = (float)dialogHeight / (float)imgH;
    const float fitScale = (maxScaleX < maxScaleY) ? maxScaleX : maxScaleY;

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

    rdpq_mode_alphacompare(1);
    rdpq_tex_blit(&pauseMenuBgSurf, drawX, drawY, &(rdpq_blitparms_t){
        .scale_x = scaleX,
        .scale_y = scaleY,
    });
}

static void draw_menu_selection_highlight(int x, int y, int w, int h)
{
    const int padX = 6;
    const int padY = 2;
    const int x0 = x - padX;
    const int y0 = y - padY;
    const int x1 = x + w + padX;
    const int y1 = y + h + padY;

    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    rdpq_set_prim_color(RGBA32(0, 0, 0, 140));
    rdpq_fill_rectangle(x0, y0, x1, y1);

    rdpq_set_prim_color(RGBA32(255, 255, 255, 70));
    rdpq_fill_rectangle(x0, y0, x1, y0 + 1);
    rdpq_fill_rectangle(x0, y1 - 1, x1, y1);
    rdpq_fill_rectangle(x0, y0, x0 + 1, y1);
    rdpq_fill_rectangle(x1 - 1, y0, x1, y1);
}

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
    const int box = lineH;
    const int rowTop = rowBaselineY - lineH;

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
    rdpq_mode_alphacompare(0);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    if (icon) {
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

void menu_controller_draw(void)
{
    if (!menuActive) {
        return;
    }

    if (currentMenu == MENU_TITLE) {
        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);

        const int menuX = ui_safe_margin_x() + 4;
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
                draw_menu_selection_highlight_baseline(menuX, y, (menuW * 3) / 4, lineHeight);
                rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
            } else {
                rdpq_set_prim_color(RGBA32(220, 220, 220, 255));
            }

            const char *displayText = titleMenuOptions[i];
            if (i == MENU_TITLE_CONTINUE) {
                bool allSavesEmpty = true;
                for (int slot = 0; slot < 3; slot++) {
                    const SaveData *saveData = save_controller_get_slot_data(slot);
                    if (saveData && (saveData->run_count > 0 || saveData->best_boss_time_ms > 0)) {
                        allSavesEmpty = false;
                        break;
                    }
                }

                displayText = allSavesEmpty ? "Play" : "Continue";
            }

            rdpq_text_printf(&(rdpq_textparms_t){
                .align = ALIGN_LEFT,
                .width = menuW,
                .wrap = WRAP_WORD,
            }, FONT_UNBALANCED, menuX, y, "%s", displayText);

            y += lineHeight + 6;
        }

        return;
    }

    if (currentMenu == MENU_LOAD_GAME) {
        rdpq_sync_pipe();
        rdpq_set_mode_standard();
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

        const int dialogMarginX = 0;
        int dialogWidth = SCREEN_WIDTH - (dialogMarginX * 2);
        int dialogHeight = 230;

        dialogWidth = 260;

        int dialogX = (SCREEN_WIDTH - dialogWidth) / 2;
        int dialogY = (SCREEN_HEIGHT - dialogHeight) / 2;

        draw_pause_menu_background(dialogX, dialogY, dialogWidth, dialogHeight);

        rdpq_set_prim_color(RGBA32(0, 0, 0, 90));
        rdpq_fill_rectangle(dialogX, dialogY, dialogX + dialogWidth, dialogY + dialogHeight);

        rdpq_set_prim_color(RGBA32(180, 160, 120, 255));
        rdpq_fill_rectangle(dialogX, dialogY, dialogX + dialogWidth, dialogY + 3);
        rdpq_fill_rectangle(dialogX, dialogY + dialogHeight - 3, dialogX + dialogWidth, dialogY + dialogHeight);
        rdpq_fill_rectangle(dialogX, dialogY, dialogX + 3, dialogY + dialogHeight);
        rdpq_fill_rectangle(dialogX + dialogWidth - 3, dialogY, dialogX + dialogWidth, dialogY + dialogHeight);

        rdpq_set_prim_color(RGBA32(220, 200, 160, 255));
        rdpq_fill_rectangle(dialogX + 3, dialogY + 3, dialogX + dialogWidth - 3, dialogY + 4);
        rdpq_fill_rectangle(dialogX + 3, dialogY + 3, dialogX + 4, dialogY + dialogHeight - 3);

        const int paddingX = 15;
        const int paddingY = 20;
        const int contentX = dialogX + paddingX;
        const int contentY = dialogY + paddingY;
        const int contentW = dialogWidth - (paddingX * 2);
        const int titleYOffset = 8;

        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_CENTER,
            .width = contentW,
            .wrap = WRAP_WORD,
        }, FONT_UNBALANCED, contentX, contentY + titleYOffset, "LOAD GAME");

        int boxY = contentY + 50;
        const int boxHeight = 28;
        const int boxSpacing = 8;
        const int slotInnerPadX = 14;
        const int slotWidth = contentW / 2;
        const int boxLeft = contentX + (contentW - slotWidth) / 2;
        const int boxRight = boxLeft + slotWidth;
        const int boxWidth = boxRight - boxLeft;

        for (int slot = 0; slot < 3; slot++) {
            const SaveData *saveData = save_controller_get_slot_data(slot);
            int lastPlayedSlot = save_controller_get_last_played_slot();
            bool isLastPlayed = slot == lastPlayedSlot;
            bool isSelected = slot == selectedOption;

            if (isSelected) {
                rdpq_sync_pipe();
                rdpq_set_mode_standard();
                rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
                rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
                rdpq_set_prim_color(RGBA32(0, 0, 0, 100));
                rdpq_fill_rectangle(boxLeft, boxY, boxRight, boxY + boxHeight);
            }

            rdpq_sync_pipe();
            rdpq_set_mode_standard();
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_set_prim_color(RGBA32(40, 35, 30, 200));
            rdpq_fill_rectangle(boxLeft, boxY, boxRight, boxY + boxHeight);

            rdpq_sync_pipe();
            rdpq_set_mode_standard();
            rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
            rdpq_mode_combiner(RDPQ_COMBINER_FLAT);

            if (isSelected) {
                rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
            } else if (isLastPlayed) {
                rdpq_set_prim_color(RGBA32(255, 255, 0, 255));
            } else {
                rdpq_set_prim_color(RGBA32(180, 160, 120, 255));
            }

            rdpq_fill_rectangle(boxLeft, boxY, boxRight, boxY + 1);
            rdpq_fill_rectangle(boxLeft, boxY + boxHeight - 1, boxRight, boxY + boxHeight);
            rdpq_fill_rectangle(boxLeft, boxY, boxLeft + 1, boxY + boxHeight);
            rdpq_fill_rectangle(boxRight - 1, boxY, boxRight, boxY + boxHeight);

            rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
            rdpq_text_printf(&(rdpq_textparms_t){
                .align = ALIGN_CENTER,
                .width = boxWidth - (slotInnerPadX * 2),
                .wrap = WRAP_WORD,
            }, FONT_UNBALANCED, boxLeft + slotInnerPadX, boxY + 9, "%s", loadGameMenuOptions[slot]);

            if (saveData && (saveData->run_count > 0 || saveData->best_boss_time_ms > 0)) {
                rdpq_set_prim_color(RGBA32(200, 200, 200, 255));
                char statsText[64];
                snprintf(statsText, sizeof(statsText), "Runs: %lu", (unsigned long)saveData->run_count);

                rdpq_text_printf(&(rdpq_textparms_t){
                    .align = ALIGN_CENTER,
                    .width = boxWidth - (slotInnerPadX * 2),
                    .wrap = WRAP_WORD,
                }, FONT_UNBALANCED, boxLeft + slotInnerPadX, boxY + 25, "%s", statsText);
            } else {
                rdpq_set_prim_color(RGBA32(150, 150, 150, 255));

                rdpq_text_printf(&(rdpq_textparms_t){
                    .align = ALIGN_CENTER,
                    .width = boxWidth - (slotInnerPadX * 2),
                    .wrap = WRAP_WORD,
                }, FONT_UNBALANCED, boxLeft + slotInnerPadX, boxY + 25, "Empty");
            }

            boxY += boxHeight + boxSpacing;
        }

        int backY = boxY + 8;
        if (selectedOption == MENU_LOAD_GAME_BACK) {
            const int highlightH = 16;
            const int highlightY = backY - (highlightH / 2) - 2;
            draw_menu_selection_highlight_centered(contentX, highlightY, contentW, highlightH, 103);
            rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
        } else {
            rdpq_set_prim_color(RGBA32(220, 220, 220, 255));
        }

        const int backButtonWidth = contentW / 2;
        const int backButtonX = contentX + (contentW - backButtonWidth) / 2;

        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_CENTER,
            .width = backButtonWidth,
            .wrap = WRAP_WORD,
        }, FONT_UNBALANCED, backButtonX, backY, "%s", loadGameMenuOptions[MENU_LOAD_GAME_BACK]);

        return;
    }

    if (currentMenu == MENU_VIDEO && overscanCalibrating) {
        draw_overscan_corner_markers();
        return;
    }

    const int dialogMarginX = 0;
    int dialogWidth = SCREEN_WIDTH - (dialogMarginX * 2);
    int dialogHeight = 200;

    if (currentMenu == MENU_SETTINGS || currentMenu == MENU_AUDIO || currentMenu == MENU_VIDEO) {
        dialogHeight = 230;
    } else if (currentMenu == MENU_CONTROLS || currentMenu == MENU_CREDITS) {
        dialogHeight = 230;
    }

    if (currentMenu == MENU_CONTROLS) {
        dialogWidth = 320;
    } else if (
        currentMenu == MENU_SETTINGS ||
        currentMenu == MENU_AUDIO ||
        currentMenu == MENU_VIDEO ||
        currentMenu == MENU_CREDITS
    ) {
        dialogWidth = 260;
    }

    rdpq_sync_pipe();
    rdpq_set_mode_standard();
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    int x = (SCREEN_WIDTH - dialogWidth) / 2;
    int y = (SCREEN_HEIGHT - dialogHeight) / 2;

    draw_pause_menu_background(x, y, dialogWidth, dialogHeight);

    rdpq_set_prim_color(RGBA32(0, 0, 0, 90));
    rdpq_fill_rectangle(x, y, x + dialogWidth, y + dialogHeight);

    rdpq_set_prim_color(RGBA32(180, 160, 120, 255));
    rdpq_fill_rectangle(x, y, x + dialogWidth, y + 3);
    rdpq_fill_rectangle(x, y + dialogHeight - 3, x + dialogWidth, y + dialogHeight);
    rdpq_fill_rectangle(x, y, x + 3, y + dialogHeight);
    rdpq_fill_rectangle(x + dialogWidth - 3, y, x + dialogWidth, y + dialogHeight);

    rdpq_set_prim_color(RGBA32(220, 200, 160, 255));
    rdpq_fill_rectangle(x + 3, y + 3, x + dialogWidth - 3, y + 4);
    rdpq_fill_rectangle(x + 3, y + 3, x + 4, y + dialogHeight - 3);

    const int paddingX = 30;
    const int paddingY = 20;
    const int contentX = x + paddingX;
    const int contentY = y + paddingY;
    const int contentW = dialogWidth - (paddingX * 2);
    const int contentH = dialogHeight - (paddingY * 2);

    x = contentX;
    y = contentY;

    int lineHeight = 16;
    const int titleYOffset = 8;

    if (currentMenu == MENU_MAIN) {
        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_CENTER,
            .width = contentW,
            .height = contentH,
            .wrap = WRAP_WORD,
        }, FONT_UNBALANCED, x, y + titleYOffset, "PAUSE MENU");

        y += (lineHeight * 3) + titleYOffset;

        for (int i = 0; i < MENU_MAIN_COUNT; i++) {
            if (i == selectedOption) {
                draw_menu_selection_highlight_centered(x, y, contentW, lineHeight, 90);
                rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
            } else {
                rdpq_set_prim_color(RGBA32(220, 220, 220, 255));
            }

            rdpq_text_printf(&(rdpq_textparms_t){
                .align = ALIGN_CENTER,
                .width = contentW,
                .height = contentH,
                .wrap = WRAP_WORD,
            }, FONT_UNBALANCED, x, y, "%s", mainMenuOptions[i]);

            y += lineHeight + 4;
        }

        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    } else if (currentMenu == MENU_SETTINGS) {
        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_CENTER,
            .width = contentW,
            .height = contentH,
            .wrap = WRAP_WORD,
        }, FONT_UNBALANCED, x, y + titleYOffset, "SETTINGS");

        y += (lineHeight * 3) + titleYOffset;

        for (int i = 0; i < MENU_SETTINGS_COUNT; i++) {
            const char *optionText = settingsMenuOptions[i] ? settingsMenuOptions[i] : "";

            if (i == selectedOption) {
                draw_menu_selection_highlight_centered(x, y, contentW, lineHeight, contentW / 2);
                rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
            } else {
                rdpq_set_prim_color(RGBA32(220, 220, 220, 255));
            }

            rdpq_text_printf(&(rdpq_textparms_t){
                .align = ALIGN_CENTER,
                .width = contentW,
                .height = contentH,
                .wrap = WRAP_WORD,
            }, FONT_UNBALANCED, x, y, "%s", optionText);

            y += lineHeight + 2;
        }

        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    } else if (currentMenu == MENU_AUDIO) {
        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_CENTER,
            .width = contentW,
            .height = contentH,
            .wrap = WRAP_WORD,
        }, FONT_UNBALANCED, x, y + titleYOffset, "AUDIO");

        y += (lineHeight * 3) + titleYOffset;

        for (int i = 0; i < MENU_AUDIO_COUNT; i++) {
            char optionText[64];
            const char *fmt = audioMenuOptions[i] ? audioMenuOptions[i] : "";

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
            } else if (i == MENU_AUDIO_RUMBLE_TOGGLE) {
                bool rumbleEnabled = joypad_is_rumble_enabled();
                bool rumbleAvailable = joypad_is_connected(JOYPAD_PORT_1) && joypad_get_rumble_supported(JOYPAD_PORT_1);

                if (!rumbleAvailable) {
                    snprintf(optionText, sizeof(optionText), rumbleEnabled ? "Rumble: ON (No Pak)" : "Rumble: OFF (No Pak)");
                } else {
                    snprintf(optionText, sizeof(optionText), fmt, rumbleEnabled ? "ON" : "OFF");
                }
            } else {
                snprintf(optionText, sizeof(optionText), "%s", fmt);
            }

            if (i == selectedOption) {
                draw_menu_selection_highlight_centered(x, y, contentW, lineHeight, contentW / 2);
                rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
            } else {
                rdpq_set_prim_color(RGBA32(220, 220, 220, 255));
            }

            rdpq_text_printf(&(rdpq_textparms_t){
                .align = ALIGN_CENTER,
                .width = contentW,
                .height = contentH,
                .wrap = WRAP_WORD,
            }, FONT_UNBALANCED, x, y, "%s", optionText);

            y += lineHeight + 2;
        }

        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    } else if (currentMenu == MENU_VIDEO) {
        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_CENTER,
            .width = contentW,
            .height = contentH,
            .wrap = WRAP_WORD,
        }, FONT_UNBALANCED, x, y + titleYOffset, "VIDEO");

        y += (lineHeight * 3) + titleYOffset;

        for (int i = 0; i < MENU_VIDEO_COUNT; i++) {
            char optionText[64];
            const char *fmt = videoMenuOptions[i] ? videoMenuOptions[i] : "";

            if (i == MENU_VIDEO_ASPECT) {
                snprintf(optionText, sizeof(optionText), fmt, hdAspect ? "16:9" : "4:3");
            } else if (i == MENU_VIDEO_UI_OVERSCAN_CALIBRATE) {
                snprintf(optionText, sizeof(optionText), "%s", "Calibrate Overscan");
            } else {
                snprintf(optionText, sizeof(optionText), "%s", fmt);
            }

            if (i == selectedOption) {
                draw_menu_selection_highlight_centered(x, y, contentW, lineHeight, contentW / 2);
                rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
            } else {
                rdpq_set_prim_color(RGBA32(220, 220, 220, 255));
            }

            rdpq_text_printf(&(rdpq_textparms_t){
                .align = ALIGN_CENTER,
                .width = contentW,
                .height = contentH,
                .wrap = WRAP_WORD,
            }, FONT_UNBALANCED, x, y, "%s", optionText);

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

        draw_icon_line(iconCLeft, listX, y, lineHeight, listW, "Move Camera");
    } else if (currentMenu == MENU_CREDITS) {
        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_CENTER,
            .width = contentW,
            .height = contentH,
            .wrap = WRAP_WORD,
        }, FONT_UNBALANCED, x, y + titleYOffset, "CREDITS");

        y += (lineHeight * 3) + titleYOffset;

        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));

        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_CENTER,
            .width = contentW,
            .height = contentH,
            .wrap = WRAP_WORD,
        }, FONT_UNBALANCED, x, y, "FROMSCRATCH");

        y += lineHeight + 8;

        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_CENTER,
            .width = contentW,
            .height = contentH,
            .wrap = WRAP_WORD,
        }, FONT_UNBALANCED, x, y, "BoxingBruin");

        y += lineHeight + 6;

        rdpq_text_printf(&(rdpq_textparms_t){
            .align = ALIGN_CENTER,
            .width = contentW,
            .height = contentH,
            .wrap = WRAP_WORD,
        }, FONT_UNBALANCED, x, y, "HelloNewman");
    }
}

void menu_controller_free(void)
{
    rspq_wait();

    if (pauseMenuBg) {
        sprite_free(pauseMenuBg);
        pauseMenuBg = NULL;
        surface_free(&pauseMenuBgSurf);
    }

    if (iconA) {
        sprite_free(iconA);
        iconA = NULL;
    }

    if (iconB) {
        sprite_free(iconB);
        iconB = NULL;
    }

    if (iconZ) {
        sprite_free(iconZ);
        iconZ = NULL;
    }

    if (iconStart) {
        sprite_free(iconStart);
        iconStart = NULL;
    }

    if (iconStick) {
        sprite_free(iconStick);
        iconStick = NULL;
    }

    if (iconCLeft) {
        sprite_free(iconCLeft);
        iconCLeft = NULL;
    }
}

bool menu_controller_is_active(void)
{
    return menuActive;
}

bool menu_controller_is_title_submenu_active(void)
{
    return menuActive && menuIsTitleMenu && currentMenu != MENU_TITLE;
}

bool menu_controller_is_pause_menu_active(void)
{
    return menuActive && !menuIsTitleMenu;
}

void menu_controller_toggle(void)
{
    menuActive = !menuActive;

    if (menuActive) {
        joypad_rumble_stop();

        GameState state = scene_get_game_state();

        menuIsTitleMenu = state == GAME_STATE_TITLE;
        parentMenu = menuIsTitleMenu ? MENU_TITLE : MENU_MAIN;
        parentSelectedOption = 0;

        if (menuIsTitleMenu) {
            currentMenu = MENU_TITLE;
        } else {
            menuReturnState = state;

            if (state != GAME_STATE_VICTORY) {
                scene_set_game_state(GAME_STATE_MENU);
            }

            currentMenu = MENU_MAIN;
        }

        selectedOption = 0;

        if (!menuIsTitleMenu) {
            musicWasPaused = !audio_is_music_playing();
            if (!musicWasPaused) {
                audio_pause_music();
            }
        }
    } else {
        joypad_rumble_stop();

        overscanCalibrating = false;

        if (!menuIsTitleMenu) {
            scene_set_game_state(menuReturnState);

            if (!musicWasPaused) {
                audio_resume_music();
            }
        }
    }
}

void menu_controller_close(void)
{
    menuActive = false;
    joypad_rumble_stop();
    overscanCalibrating = false;

    if (!menuIsTitleMenu) {
        scene_set_game_state(menuReturnState);

        if (!musicWasPaused) {
            audio_resume_music();
        }
    }
}