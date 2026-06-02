#include "title_scene.h"

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3danim.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dmodel.h>
#include <t3d/t3dskeleton.h>

#include <stdbool.h>

#include "../controllers/audio_controller.h"
#include "../controllers/camera_controller.h"
#include "../objects/character.h"
#include "../controllers/dialog_controller.h"
#include "../utilities/button_prompt_utility.h"
#include "../utilities/display_utility.h"
#include "../utilities/game_lighting.h"
#include "../utilities/general_utility.h"
#include "../utilities/game_time.h"
#include "../utilities/globals.h"
#include "../utilities/joypad_utility.h"
#include "../utilities/letterbox_utility.h"
#include "../utilities/menu_controller.h"
#include "../utilities/save_controller.h"
#include "../utilities/video_layout.h"

#include "../dev/dev.h"

#include "../managers/cutscene_manager.h"

typedef enum {
    TITLE_STATE_INACTIVE = 0,
    TITLE_STATE_IDLE,
    TITLE_STATE_TRANSITION_TO_GUARDIAN,
    TITLE_STATE_DONE,
} TitleSceneState;

typedef enum {
    TITLE_SFX_WALK = 0,
    TITLE_SFX_COUNT,
} TitleSceneSfx;

static const char *TITLE_SFX_PATHS[TITLE_SFX_COUNT] = {
    [TITLE_SFX_WALK] = "rom:/audio/sfx/title_screen_walk_effect-22k.wav64",
};

static const float TITLE_ROOM_Y = -1.0f;
static const float TITLE_CHARACTER_YAW = T3D_PI * 0.5f;
static const float TITLE_CAMERA_FORWARD_SPEED = 15.0f;

static const float TITLE_TEXT_ACTIVATION_TIME = 50.0f;
static const float TITLE_START_GAME_TIME = 10.0f;
static const float TITLE_FADE_TIME = 7.0f;

static const char *TITLE_DIALOGS[] = {
    ">The Demon\nking has\nforced\nthe land\ninto a\ncentury long\ndarkness.",
    ">The King\nhas trained\na legion\nof powerful\nknights\nsworn to\nprotect the\nthrone.",
    ">These\nbattle born\nknights are\ntaken from\ntheir\nfamilies and\ncast into\nservitude.",
    ">Enduring\nblade and\ntorment\nuntil nothing\nremains but\nhollow armor."
};

#define TITLE_DIALOG_COUNT ((int)(sizeof(TITLE_DIALOGS) / sizeof(TITLE_DIALOGS[0])))

static TitleSceneState s_state = TITLE_STATE_INACTIVE;
static TitleSceneResult s_result = TITLE_SCENE_RESULT_NONE;

static bool s_screen_transition = false;
static bool s_screen_breath = false;
static bool s_skip_button_visible = false;
static bool s_last_cutscene_a_pressed = false;
static bool s_last_a_pressed = false;
static bool s_last_start_pressed = false;

static int s_current_title_dialog = 0;
static float s_title_text_activation_timer = 0.0f;
static float s_title_start_game_timer = 0.0f;

static T3DModel *s_room_model = NULL;
static rspq_block_t *s_room_dpl = NULL;
static T3DMat4FP *s_room_matrix = NULL;

static T3DModel *s_fog_door_model = NULL;
static rspq_block_t *s_fog_door_dpl = NULL;
static T3DMat4FP *s_fog_door_matrix = NULL;

static ScrollParams s_fog_scroll_params = {
    .xSpeed = 0.0f,
    .ySpeed = 10.0f,
    .scale  = 64,
};

static T3DModel *s_dynamic_banner_model = NULL;
static rspq_block_t *s_dynamic_banner_dpl = NULL;
static T3DMat4FP *s_dynamic_banner_matrix = NULL;
static T3DSkeleton *s_dynamic_banner_skeleton = NULL;
static T3DAnim *s_dynamic_banner_wind_anim = NULL;

static void title_scene_free_model_asset(
    T3DModel **model,
    rspq_block_t **dpl,
    T3DMat4FP **matrix
) {
    if (*dpl) {
        rspq_block_free(*dpl);
        *dpl = NULL;
    }

    if (*model) {
        t3d_model_free(*model);
        *model = NULL;
    }

    if (*matrix) {
        free_uncached(*matrix);
        *matrix = NULL;
    }
}

static void title_scene_make_matrix(T3DMat4FP **matrix)
{
    if (!*matrix) {
        *matrix = malloc_uncached(sizeof(T3DMat4FP));
    }

    t3d_mat4fp_from_srt_euler(
        *matrix,
        (float[3]){MODEL_SCALE, MODEL_SCALE, MODEL_SCALE},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, TITLE_ROOM_Y, 0.0f}
    );
}

static void title_scene_load_room_assets(void)
{
    if (!s_room_model) {
        s_room_model = t3d_model_load("rom:/boss_room/room.t3dm");

        rspq_block_begin();
            t3d_model_draw(s_room_model);
        s_room_dpl = rspq_block_end();

        title_scene_make_matrix(&s_room_matrix);
    }

    if (!s_fog_door_model) {
        s_fog_door_model = t3d_model_load("rom:/boss_room/fog.t3dm");

        rspq_block_begin();
            t3d_model_draw(s_fog_door_model);
        s_fog_door_dpl = rspq_block_end();

        title_scene_make_matrix(&s_fog_door_matrix);
    }
}

static void title_scene_load_dynamic_banner_assets(void)
{
    if (s_dynamic_banner_model) {
        return;
    }

    s_dynamic_banner_model = t3d_model_load("rom:/title_screen/dynamic_banners.t3dm");

    s_dynamic_banner_skeleton = malloc_uncached(sizeof(T3DSkeleton));
    *s_dynamic_banner_skeleton = t3d_skeleton_create(s_dynamic_banner_model);

    s_dynamic_banner_wind_anim = malloc_uncached(sizeof(T3DAnim));
    *s_dynamic_banner_wind_anim = t3d_anim_create(s_dynamic_banner_model, "Wind");

    t3d_anim_set_looping(s_dynamic_banner_wind_anim, true);
    t3d_anim_set_playing(s_dynamic_banner_wind_anim, true);
    t3d_anim_attach(s_dynamic_banner_wind_anim, s_dynamic_banner_skeleton);

    rspq_block_begin();
        t3d_model_draw_skinned(s_dynamic_banner_model, s_dynamic_banner_skeleton);
    s_dynamic_banner_dpl = rspq_block_end();

    title_scene_make_matrix(&s_dynamic_banner_matrix);
}

static void title_scene_free_dynamic_banner_assets(void)
{
    if (s_dynamic_banner_dpl) {
        rspq_block_free(s_dynamic_banner_dpl);
        s_dynamic_banner_dpl = NULL;
    }

    if (s_dynamic_banner_wind_anim) {
        t3d_anim_destroy(s_dynamic_banner_wind_anim);
        free_uncached(s_dynamic_banner_wind_anim);
        s_dynamic_banner_wind_anim = NULL;
    }

    if (s_dynamic_banner_skeleton) {
        t3d_skeleton_destroy(s_dynamic_banner_skeleton);
        free_uncached(s_dynamic_banner_skeleton);
        s_dynamic_banner_skeleton = NULL;
    }

    if (s_dynamic_banner_model) {
        t3d_model_free(s_dynamic_banner_model);
        s_dynamic_banner_model = NULL;
    }

    if (s_dynamic_banner_matrix) {
        free_uncached(s_dynamic_banner_matrix);
        s_dynamic_banner_matrix = NULL;
    }
}

static void title_scene_reset_runtime_state(void)
{
    s_result = TITLE_SCENE_RESULT_NONE;
    s_state = TITLE_STATE_IDLE;

    s_screen_transition = false;
    s_screen_breath = false;
    s_skip_button_visible = false;
    s_last_cutscene_a_pressed = btn.a;
    s_last_a_pressed = btn.a;
    s_last_start_pressed = btn.start;

    s_current_title_dialog = 0;
    s_title_text_activation_timer = 0.0f;
    s_title_start_game_timer = 0.0f;
}

static void title_scene_setup_camera_and_character(void)
{
    cameraState = CAMERA_CUSTOM;
    lastCameraState = CAMERA_CUSTOM;

    camera_mode(CAMERA_CUSTOM);
    camera_initialize(
        &(T3DVec3){{-580.6f, 75.0f, 0.0f}},
        &(T3DVec3){{-1.0f, 0.0f, 0.0f}},
        1.544792654048f,
        4.05f
    );

    customCamTarget.v[1] = 90.0f;

    character.pos[0] = -650.0f;
    character.pos[1] = 44.0f;
    character.pos[2] = 0.0f;

    character.scale[0] = MODEL_SCALE * 1.5f;
    character.scale[1] = MODEL_SCALE * 1.5f;
    character.scale[2] = MODEL_SCALE * 1.5f;

    character.rot[1] = TITLE_CHARACTER_YAW;

    character_update_position();
    character_set_state(CHAR_STATE_TITLE_IDLE);
    character_set_velocity_xz(0.0f, 0.0f);
}

static void title_scene_start_dialog(void)
{
    s_current_title_dialog = 0;
    s_title_text_activation_timer = 0.0f;

    dialog_controller_speak(TITLE_DIALOGS[0], 0, 9.0f, false, true);
}

void title_scene_enter(void)
{
    joypad_rumble_stop();

    audio_scene_load_paths(TITLE_SFX_PATHS, TITLE_SFX_COUNT);

    game_lighting_initialize();
    colorAmbient[0] = 0xFF;
    colorAmbient[1] = 0xFF;
    colorAmbient[2] = 0xFF;
    colorAmbient[3] = 0xFF;

    dialog_controller_init();
    button_prompt_init();
    letterbox_init();
    letterbox_show(false);

    title_scene_load_room_assets();
    title_scene_load_dynamic_banner_assets();

    title_scene_reset_runtime_state();
    title_scene_setup_camera_and_character();

    if (s_dynamic_banner_wind_anim) {
        t3d_anim_set_time(s_dynamic_banner_wind_anim, 0.0f);
        t3d_anim_set_playing(s_dynamic_banner_wind_anim, true);
    }

    audio_play_music("rom:/audio/music/demonous-22k.wav64", true);

    title_scene_start_dialog();

    startScreenFade = true;
}

void title_scene_exit(void)
{
    audio_stop_all_sfx();
    audio_stop_music();

    dialog_controller_reset();
    menu_controller_close();

    camera_breath_active(false);

    title_scene_free_dynamic_banner_assets();

    title_scene_free_model_asset(
        &s_fog_door_model,
        &s_fog_door_dpl,
        &s_fog_door_matrix
    );

    title_scene_free_model_asset(
        &s_room_model,
        &s_room_dpl,
        &s_room_matrix
    );

    s_state = TITLE_STATE_INACTIVE;
    s_result = TITLE_SCENE_RESULT_NONE;

    s_screen_transition = false;
    s_screen_breath = false;
    s_skip_button_visible = false;
}

void title_scene_begin_transition(void)
{
    if (s_state == TITLE_STATE_TRANSITION_TO_GUARDIAN) {
        return;
    }

    if (s_state != TITLE_STATE_IDLE) {
        return;
    }

    menu_controller_close();

    s_state = TITLE_STATE_TRANSITION_TO_GUARDIAN;

    character_set_state(CHAR_STATE_FOG_WALK);

    s_skip_button_visible = false;
    s_last_cutscene_a_pressed = btn.a;

    camera_breath_active(false);
    s_screen_breath = false;

    audio_stop_music_fade(6);

    audio_scene_load_paths(TITLE_SFX_PATHS, TITLE_SFX_COUNT);

    audio_play_scene_sfx_dist(
        TITLE_SFX_WALK,
        1.0f,
        0.0f
    );
}

static void title_scene_finish_transition(void)
{
    s_title_start_game_timer = 0.0f;
    s_skip_button_visible = false;
    s_state = TITLE_STATE_DONE;
    s_result = TITLE_SCENE_RESULT_START_GUARDIAN_INTRO;

    character_set_velocity_xz(0.0f, 0.0f);

    audio_stop_all_sfx();
}

static void title_scene_update_transition(void)
{
    if (s_title_start_game_timer >= TITLE_START_GAME_TIME) {
        title_scene_finish_transition();
        return;
    }

    bool a_currently_pressed = btn.a;
    bool a_just_pressed = a_currently_pressed && !s_last_cutscene_a_pressed;

    if (a_just_pressed) {
        if (!s_skip_button_visible) {
            s_skip_button_visible = true;
        } else {
            s_last_cutscene_a_pressed = a_currently_pressed;
            title_scene_finish_transition();
            return;
        }
    }

    s_last_cutscene_a_pressed = a_currently_pressed;

    if (btn.start) {
        title_scene_finish_transition();
        return;
    }

    audio_update_fade(deltaTime);

    character_set_state(CHAR_STATE_FOG_WALK);

    s_title_start_game_timer += deltaTime;

    const float targetDropSpeed = 1.0f;

    customCamDir.v[0] = customCamTarget.v[0] - customCamPos.v[0];
    customCamDir.v[1] = customCamTarget.v[1] - customCamPos.v[1];
    customCamDir.v[2] = customCamTarget.v[2] - customCamPos.v[2];
    t3d_vec3_norm(&customCamDir);

    for (int i = 0; i < 3; i++) {
        customCamPos.v[i]    += customCamDir.v[i] * TITLE_CAMERA_FORWARD_SPEED * deltaTime;
        customCamTarget.v[i] += customCamDir.v[i] * TITLE_CAMERA_FORWARD_SPEED * deltaTime;
    }

    customCamTarget.v[1] -= targetDropSpeed * deltaTime;
}

static void title_scene_update_idle(void)
{
    s_last_start_pressed = btn.start;
    s_last_a_pressed = btn.a;

    if (!s_screen_breath) {
        camera_breath_active(true);
        s_screen_breath = true;
    }

    camera_breath_update(deltaTime);

    if (!menu_controller_is_title_submenu_active()) {
        if (s_title_text_activation_timer >= TITLE_TEXT_ACTIVATION_TIME) {
            dialog_controller_update();

            if (!dialog_controller_speaking()) {
                s_current_title_dialog++;

                if (s_current_title_dialog >= TITLE_DIALOG_COUNT) {
                    s_title_text_activation_timer = 0.0f;
                    s_current_title_dialog = -1;
                } else {
                    dialog_controller_speak(
                        TITLE_DIALOGS[s_current_title_dialog],
                        0,
                        9.0f,
                        false,
                        true
                    );
                }
            }
        } else {
            s_title_text_activation_timer += deltaTime;
        }
    }
}

void title_scene_update(void)
{
    if (s_state == TITLE_STATE_INACTIVE || s_state == TITLE_STATE_DONE) {
        return;
    }

    audio_update_fade(deltaTime);
    scroll_update();

    if (s_state == TITLE_STATE_TRANSITION_TO_GUARDIAN) {
        title_scene_update_transition();
    } else {
        menu_controller_update_title();

        MenuAction action = menu_controller_consume_action();
        if (action == MENU_ACTION_TITLE_START_GAME) {
            title_scene_begin_transition();
        } else {
            title_scene_update_idle();
        }
    }

    character_update_cinematic();

    if (s_dynamic_banner_wind_anim && s_dynamic_banner_skeleton) {
        t3d_anim_update(s_dynamic_banner_wind_anim, deltaTime);
        t3d_skeleton_update(s_dynamic_banner_skeleton);
    }

    letterbox_update();
}

static void title_scene_draw_3d(void)
{
    rdpq_sync_pipe();
    rdpq_mode_zbuf(false, false);

    t3d_matrix_push_pos(1);
        if (s_room_matrix && s_room_dpl) {
            t3d_matrix_set(s_room_matrix, true);
            rspq_block_run(s_room_dpl);
        }

        if (s_dynamic_banner_matrix && s_dynamic_banner_dpl) {
            t3d_matrix_set(s_dynamic_banner_matrix, true);
            rspq_block_run(s_dynamic_banner_dpl);
        }
    t3d_matrix_pop(1);

    rdpq_sync_pipe();
    rdpq_mode_zbuf(true, true);

    t3d_matrix_push_pos(1);
        character_draw();

        if (s_fog_door_matrix && s_fog_door_model) {
            t3d_matrix_set(s_fog_door_matrix, true);

            t3d_model_draw_custom(s_fog_door_model, (T3DModelDrawConf){
                .userData = &s_fog_scroll_params,
                .tileCb = tile_scroll,
            });
        }
    t3d_matrix_pop(1);
}

static void title_scene_draw_run_counter_panel(void)
{
    const bool savesOn = save_controller_is_enabled();
    const uint32_t savedRuns = save_controller_get_run_count();
    const uint32_t bestMs = save_controller_get_best_boss_time_ms();

    if (menu_controller_is_title_submenu_active()) {
        return;
    }

    if (savesOn && savedRuns == 0 && bestMs == 0) {
        return;
    }

    const int margin = ui_safe_margin_x();
    const int panelW = 120;
    const bool showBest = savesOn && bestMs > 0;
    const int lineCount = 1 + (showBest ? 1 : 0);
    const int panelH = 6 + (lineCount * 13);
    const int panelX0 = margin;
    const int panelY0 = SCREEN_HEIGHT - ui_safe_margin_y() - panelH;

    rdpq_set_prim_color(RGBA32(0, 0, 0, 120));
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_fill_rectangle(panelX0, panelY0, panelX0 + panelW, panelY0 + panelH);

    rdpq_set_prim_color(RGBA32(255, 255, 255, 255));

    int lineY = panelY0 + 13;

    if (savesOn) {
        rdpq_text_printf(
            NULL,
            FONT_UNBALANCED,
            panelX0 + 6,
            lineY,
            "Runs: %lu",
            (unsigned long)savedRuns
        );
    } else {
        rdpq_text_printf(
            NULL,
            FONT_UNBALANCED,
            panelX0 + 6,
            lineY,
            "Runs: --"
        );
    }

    lineY += 13;

    if (showBest) {
        const uint32_t minutes = bestMs / 60000u;
        const uint32_t seconds = (bestMs / 1000u) % 60u;
        const uint32_t millis  = bestMs % 1000u;

        rdpq_text_printf(
            NULL,
            FONT_UNBALANCED,
            panelX0 + 6,
            lineY,
            "Best: %lu:%02lu.%03lu",
            (unsigned long)minutes,
            (unsigned long)seconds,
            (unsigned long)millis
        );
    }
}

static void title_scene_draw_idle_2d(void)
{
    rdpq_set_mode_standard();
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    title_scene_draw_run_counter_panel();

    if (
        s_title_text_activation_timer >= TITLE_TEXT_ACTIVATION_TIME &&
        !menu_controller_is_title_submenu_active()
    ) {
        const int dlgW = 120;
        const int dlgH = 180;
        const int dlgX = SCREEN_WIDTH - ui_safe_margin_x() - dlgW;
        const int dlgY = ui_safe_margin_y();

        dialog_controller_draw(true, dlgX, dlgY, dlgW, dlgH);
    }

    display_utility_solid_black_transition(true, 200.0f);
}

static void title_scene_draw_transition_2d(void)
{
    if (s_title_start_game_timer >= TITLE_FADE_TIME && !s_screen_transition) {
        startScreenFade = true;
        s_screen_transition = true;
    }

    if (s_screen_transition) {
        display_utility_solid_black_transition(false, 200.0f);
    }

    cutscene_manager_draw_skip_overlay(s_skip_button_visible);
}

void title_scene_draw(T3DViewport *viewport)
{
    if (s_state == TITLE_STATE_INACTIVE) {
        return;
    }

    t3d_frame_start();

    if (!DITHER_ENABLED && !debugDraw) {
        rdpq_mode_dithering(DITHER_NONE_BAYER);
    }

    t3d_viewport_attach(viewport);

    color_t fogColor = (color_t){0, 0, 0, 0xFF};
    rdpq_mode_fog(RDPQ_FOG_STANDARD);
    rdpq_set_fog_color(fogColor);

    t3d_screen_clear_color(RGBA32(0, 0, 0, 0xFF));
    t3d_screen_clear_depth();

    t3d_fog_set_range(450.0f, 800.0f);
    t3d_fog_set_enabled(true);

    t3d_light_set_ambient(colorAmbient);

    title_scene_draw_3d();

    rdpq_sync_pipe();

    if (s_state == TITLE_STATE_TRANSITION_TO_GUARDIAN || s_state == TITLE_STATE_DONE) {
        title_scene_draw_transition_2d();
    } else {
        title_scene_draw_idle_2d();
    }
}

TitleSceneResult title_scene_get_result(void)
{
    return s_result;
}

void title_scene_clear_result(void)
{
    s_result = TITLE_SCENE_RESULT_NONE;
}

bool title_scene_is_active(void)
{
    return s_state != TITLE_STATE_INACTIVE;
}

bool title_scene_is_transitioning(void)
{
    return s_state == TITLE_STATE_TRANSITION_TO_GUARDIAN;
}