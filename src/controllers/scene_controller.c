#include "scene_controller.h"

#include "../scenes/title/title_scene.h"
#include "../scenes/guardian/guardian_scene.h"

static SceneControllerSceneId s_active_scene = SCENE_CONTROLLER_SCENE_TITLE;

void scene_controller_switch_to_title(void)
{
    if (s_active_scene == SCENE_CONTROLLER_SCENE_TITLE) {
        title_scene_exit();
    } else if (s_active_scene == SCENE_CONTROLLER_SCENE_GUARDIAN) {
        scene_cleanup();
    }

    s_active_scene = SCENE_CONTROLLER_SCENE_TITLE;
    title_scene_enter();
}

static void scene_controller_switch_to_guardian_intro(void)
{
    title_scene_exit();

    scene_init();

    s_active_scene = SCENE_CONTROLLER_SCENE_GUARDIAN;
}

void scene_controller_init(void)
{
    s_active_scene = SCENE_CONTROLLER_SCENE_TITLE;
    title_scene_enter();
}

void scene_controller_update(void)
{
    switch (s_active_scene) {
        case SCENE_CONTROLLER_SCENE_TITLE:
            title_scene_update();

            if (title_scene_get_result() == TITLE_SCENE_RESULT_START_GUARDIAN_INTRO) {
                scene_controller_switch_to_guardian_intro();
            }
            break;

        case SCENE_CONTROLLER_SCENE_GUARDIAN:
            scene_update();
            break;

        default:
            break;
    }
}

void scene_controller_draw(T3DViewport *viewport)
{
    switch (s_active_scene) {
        case SCENE_CONTROLLER_SCENE_TITLE:
            title_scene_draw(viewport);
            break;

        case SCENE_CONTROLLER_SCENE_GUARDIAN:
            scene_draw(viewport);
            break;

        default:
            break;
    }
}

void scene_controller_restart(void)
{
    switch (s_active_scene) {
        case SCENE_CONTROLLER_SCENE_TITLE:
            scene_controller_switch_to_title();
            break;

        case SCENE_CONTROLLER_SCENE_GUARDIAN:
            scene_restart();
            break;

        default:
            break;
    }
}

SceneControllerSceneId scene_controller_get_active_scene(void)
{
    return s_active_scene;
}