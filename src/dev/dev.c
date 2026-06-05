#include <stdbool.h>
#include <stdint.h>
#include <libdragon.h>
#include <rspq_profile.h>
#include <t3d/t3d.h>
#include <t3d/t3dmodel.h>
#include <t3d/t3ddebug.h>
#include <malloc.h>

#include "dev.h"
#include "debug_overlay.h"
#include "debug_draw.h"

#include "camera_controller.h"
#include "character/character.h"
#include "game/boss/boss.h"

#include "game_lighting.h"
#include "joypad_utility.h"

#include "globals.h"
#include "../scenes/guardian/guardian_scene.h"

extern int __boot_memsize;

bool debugDraw = true;

static bool displayMetrics = false;
static bool requestDisplayMetrics = false;
static float last3dFPS = 0.0f;

static float lightAzimuth = 0.0f;   // Rotation around Y axis (left/right)
static float lightElevation = 0.0f; // Rotation around X axis (up/down)

enum {
    DEV_NONE,
    DEV_FREECAM,
    DEV_LIGHTDIR,
    DEV_CAMPOS,
    DEV_COLLISION,
    DEV_RSPQ_PROFILER,
    DEV_MEMORY_DEBUG,
    DEV_SCENE_FLOW,
};

static int rowCount = 7;
static int controlling = DEV_NONE;

static bool toggleDevMenu = false;
static bool toggleSwitch = false;
static bool toggleSelectScene = false;

static int sidebarSelected = 0; // Sidebar menu selection
static int selected = 0;       // Submenu/category selection
static int columnCount = 5;

static const int sceneCount = 1;

// New: track if we are inside a category screen
static bool inCategoryScreen = false;

static bool toggleColliders = false;

// Button state tracking for C-pad Up+Down toggle
static bool lastCUpPressed = false;
static bool lastCDownPressed = false;

static T3DModel *devArrow;
static rspq_block_t *dplDevArrow;
static T3DMat4 devArrowMat;
static T3DMat4FP* devArrowMatFP;

// Live heap debug state
static heap_stats_t live_heap_stats;

static float bytes_to_mib(uint32_t bytes)
{
    return (float)bytes / (1024.0f * 1024.0f);
}

static void dev_update_live_heap_stats(void)
{
    sys_get_heap_stats(&live_heap_stats);
}

bool dev_menu_is_open(void)
{
    return toggleDevMenu;
}

void dev_tools_init()
{
    debug_init_isviewer();
    debug_init_usblog();
    console_init();
    console_set_debug(true);
    profile_data.frame_count = 0;
}

void dev_models_init()
{
    devArrow = t3d_model_load("rom:/arrow.t3dm");
    rspq_block_begin();
        t3d_model_draw(devArrow);
    dplDevArrow = rspq_block_end();

    devArrowMatFP = malloc_uncached(sizeof(T3DMat4FP));

    t3d_mat4_identity(&devArrowMat);
    t3d_mat4_scale(&devArrowMat, MODEL_SCALE, MODEL_SCALE, MODEL_SCALE);
    t3d_mat4_to_fixed(devArrowMatFP, &devArrowMat);
}

void dev_frame_update()
{
    rspq_profile_next_frame();
}

void dev_handle_camera_state()
{
    if(controlling == DEV_NONE)
    {
        cameraState = lastCameraState;
    }
    else
    {
        cameraState = CAMERA_FREECAM;
    }
}

void dev_controller_update()
{
    // C-pad Up+Down combination to toggle dev menu as z button is used for targeting
    // bool bothPressed = btn.c_up && btn.c_down;
    // bool justPressed = bothPressed && (!lastCUpPressed || !lastCDownPressed);

    if(btn.r)
    {
        toggleDevMenu = !toggleDevMenu;
        inCategoryScreen = false; // Always return to main menu when toggling
    }

    if(!toggleDevMenu)
    {
        requestDisplayMetrics = false;
        displayMetrics = false;
        toggleSwitch = false;
        inCategoryScreen = false;
    }
    else
    {
        requestDisplayMetrics = true;

        // --- Sidebar menu navigation ---
        if(!inCategoryScreen)
        {
            // Move up/down the main menu
            if(btn.d_up)
            {
                sidebarSelected--;
                if(sidebarSelected < 0) sidebarSelected = rowCount;
            }

            if(btn.d_down)
            {
                sidebarSelected++;
                if(sidebarSelected > rowCount) sidebarSelected = 0;
            }

            // Enter category screen with d right
            if(btn.d_right)
            {
                controlling = sidebarSelected;
                inCategoryScreen = true;
                selected = 0;
                dev_handle_camera_state();
            }
        }
        else
        {
            // --- Inside a category screen ---
            // Exit to main menu with B
            if(btn.d_left)
            {
                inCategoryScreen = false;
            }

            // Category-specific controls
            switch(controlling)
            {
                case DEV_COLLISION:
                    if(selected == 0 && (btn.d_left || btn.d_right))
                    {
                        toggleColliders = !toggleColliders;
                    }
                    break;

                case DEV_LIGHTDIR:
                    // Update light direction based on joystick input
                    if (joypad.btn.d_up) lightElevation += 0.01f;    // Increase elevation (tilt up)
                    if (joypad.btn.d_down) lightElevation -= 0.01f;  // Decrease elevation (tilt down)
                    if (joypad.btn.d_left) lightAzimuth += 0.01f;    // Rotate left around Y-axis
                    if (joypad.btn.d_right) lightAzimuth -= 0.01f;   // Rotate right around Y-axis

                    // Constrain the elevation to avoid flipping
                    if (lightElevation > M_PI_2) lightElevation = M_PI_2;
                    if (lightElevation < -M_PI_2) lightElevation = -M_PI_2;

                    lightDirVec.v[0] = cos(lightElevation) * sin(lightAzimuth); // X component
                    lightDirVec.v[1] = sin(lightElevation);                     // Y component (up/down)
                    lightDirVec.v[2] = cos(lightElevation) * cos(lightAzimuth); // Z component
                    t3d_vec3_norm(&lightDirVec); // Normalize the direction vector

                    // Set the arrow's transformation matrix based on the direction and up vector
                    t3d_mat4_rot_from_dir(&devArrowMat, &lightDirVec, &(T3DVec3){{0,1,0}});
                    t3d_mat4_scale(&devArrowMat, MODEL_SCALE, MODEL_SCALE, MODEL_SCALE);
                    t3d_mat4_to_fixed(devArrowMatFP, &devArrowMat);

                    float targetPos[3] = {
                        camPos.v[0] + camDir.v[0] * distanceInFrontOfCamera,
                        camPos.v[1] + camDir.v[1] * distanceInFrontOfCamera,
                        camPos.v[2] + camDir.v[2] * distanceInFrontOfCamera
                    };

                    t3d_mat4fp_set_pos(devArrowMatFP, (float[]){targetPos[0], targetPos[1], targetPos[2]});

                    t3d_light_set_directional(0, colorDir, &lightDirVec);
                    break;

                case DEV_CAMPOS:
                    // Place the arrow object at the camera target
                    t3d_mat4_identity(&devArrowMat);
                    t3d_mat4_scale(&devArrowMat, MODEL_SCALE, MODEL_SCALE, MODEL_SCALE);
                    t3d_mat4_to_fixed(devArrowMatFP, &devArrowMat);

                    float targetPos2[3] = {
                        camPos.v[0] + camDir.v[0] * distanceInFrontOfCamera,
                        camPos.v[1] + camDir.v[1] * distanceInFrontOfCamera,
                        camPos.v[2] + camDir.v[2] * distanceInFrontOfCamera
                    };

                    t3d_mat4fp_set_pos(devArrowMatFP, (float[]){targetPos2[0], targetPos2[1], targetPos2[2]});
                    break;

                case DEV_MEMORY_DEBUG:
                    // Live memory overlay is controlled by the global DEBUG_MEMORY.
                    // No per-frame snapshot input needed anymore.
                    break;

                case DEV_SCENE_FLOW:
                    if (btn.d_up) {
                        selected--;
                        if (selected < 0) selected = 1;
                    }

                    if (btn.d_down) {
                        selected++;
                        if (selected > 1) selected = 0;
                    }

                    if (btn.a && DEV_MODE) {
                        if (selected == 0) {
                            scene_dev_warp_to_fight();
                        } else {
                            scene_dev_warp_to_pre_phase2();
                        }

                        toggleDevMenu = false;
                        inCategoryScreen = false;
                        controlling = DEV_NONE;
                        dev_handle_camera_state();
                    }
                    break;

                default:
                    break;
            }
        }
    }

    // Update angles when buttons are pressed
    if (controlling == DEV_LIGHTDIR)
    {
        // Update light direction based on joystick input
        if (joypad.btn.d_up) lightElevation += 0.01f;    // Increase elevation (tilt up)
        if (joypad.btn.d_down) lightElevation -= 0.01f;  // Decrease elevation (tilt down)
        if (joypad.btn.d_left) lightAzimuth += 0.01f;    // Rotate left around Y-axis
        if (joypad.btn.d_right) lightAzimuth -= 0.01f;   // Rotate right around Y-axis

        // Constrain the elevation to avoid flipping
        if (lightElevation > M_PI_2) lightElevation = M_PI_2;
        if (lightElevation < -M_PI_2) lightElevation = -M_PI_2;

        lightDirVec.v[0] = cos(lightElevation) * sin(lightAzimuth); // X component
        lightDirVec.v[1] = sin(lightElevation);                     // Y component (up/down)
        lightDirVec.v[2] = cos(lightElevation) * cos(lightAzimuth); // Z component
        t3d_vec3_norm(&lightDirVec); // Normalize the direction vector

        // Set the arrow's transformation matrix based on the direction and up vector
        t3d_mat4_rot_from_dir(&devArrowMat, &lightDirVec, &(T3DVec3){{0,1,0}});
        t3d_mat4_scale(&devArrowMat, MODEL_SCALE, MODEL_SCALE, MODEL_SCALE);
        t3d_mat4_to_fixed(devArrowMatFP, &devArrowMat);

        float targetPos[3] = {
            camPos.v[0] + camDir.v[0] * distanceInFrontOfCamera,
            camPos.v[1] + camDir.v[1] * distanceInFrontOfCamera,
            camPos.v[2] + camDir.v[2] * distanceInFrontOfCamera
        };

        t3d_mat4fp_set_pos(devArrowMatFP, (float[]){targetPos[0], targetPos[1], targetPos[2]});

        t3d_light_set_directional(0, colorDir, &lightDirVec);
    }

    if(controlling == DEV_CAMPOS)
    {
        // Place the arrow object at the camera target
        t3d_mat4_identity(&devArrowMat);
        t3d_mat4_scale(&devArrowMat, MODEL_SCALE, MODEL_SCALE, MODEL_SCALE);
        t3d_mat4_to_fixed(devArrowMatFP, &devArrowMat);

        float targetPos[3] = {
            camPos.v[0] + camDir.v[0] * distanceInFrontOfCamera,
            camPos.v[1] + camDir.v[1] * distanceInFrontOfCamera,
            camPos.v[2] + camDir.v[2] * distanceInFrontOfCamera
        };

        t3d_mat4fp_set_pos(devArrowMatFP, (float[]){targetPos[0], targetPos[1], targetPos[2]});
    }

    if(controlling == DEV_COLLISION)
    {
        bool buttonPressed = false;

        if(btn.d_up)
        {
            selected--;
            buttonPressed = true;
        }

        if(btn.d_down)
        {
            selected++;
            buttonPressed = true;
        }

        if(btn.d_left || btn.d_right)
        {
            buttonPressed = true;
        }

        if(selected <= -1)
        {
            selected = columnCount;
        }

        if(selected >= columnCount)
        {
            selected = 0;
        }

        if(buttonPressed)
        {
            if(controlling == DEV_COLLISION)
            {
                if(toggleColliders)
                {
                    debugDraw = true;
                }
            }

            buttonPressed = false;
        }
    }

    // Update button state tracking for next frame
    lastCUpPressed = btn.c_up;
    lastCDownPressed = btn.c_down;
}

void dev_draw_memory_debug(void)
{
    if (!DEV_MODE || !DEBUG_MEMORY) {
        return;
    }

    dev_update_live_heap_stats();

    uint32_t rdram_total = (uint32_t)__boot_memsize;

    uint32_t heap_total = (uint32_t)live_heap_stats.total;
    uint32_t heap_used  = (uint32_t)live_heap_stats.used;
    uint32_t heap_free  = heap_total - heap_used;

    /*
     * Everything that is not currently free heap is either:
     * - static/pre-heap memory
     * - reserved runtime memory
     * - display/audio/system allocations
     * - active heap allocations
     *
     * This is the closest live "whole RDRAM pressure" number available from
     * the runtime side without instrumenting every allocator/load path.
     */
    uint32_t rdram_used_or_reserved = rdram_total - heap_free;

    /*
     * Non-heap/reserved baseline:
     * detected RDRAM minus the total heap arena.
     *
     * This includes static linked memory, stack/runtime reservations, and any
     * memory not exposed to the malloc heap.
     */
    uint32_t non_heap_or_reserved = rdram_total - heap_total;

    const int x = 8;
    const int lineHeight = 12;
    const int lines = 6;
    const int y = display_get_height() - 8 - (lineHeight * lines);

    t3d_debug_print_start();

    t3d_debug_printf(x, y + lineHeight * 0, "RAM:   %.2f MB", bytes_to_mib(rdram_total));
    t3d_debug_printf(x, y + lineHeight * 1, "USED+: %.2f MB", bytes_to_mib(rdram_used_or_reserved));
    t3d_debug_printf(x, y + lineHeight * 2, "FREE:  %.2f MB", bytes_to_mib(heap_free));
    t3d_debug_printf(x, y + lineHeight * 3, "HUSED: %.2f MB", bytes_to_mib(heap_used));
    t3d_debug_printf(x, y + lineHeight * 4, "HTOT:  %.2f MB", bytes_to_mib(heap_total));
    t3d_debug_printf(x, y + lineHeight * 5, "NONHP: %.2f MB", bytes_to_mib(non_heap_or_reserved));
}

void dev_update()
{
    if(displayMetrics)
    {
        rdpq_set_mode_standard();
        rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
        rdpq_set_prim_color(RGBA32(0,0,0,120));
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_fill_rectangle(0, 0, display_get_width(), display_get_height());
        t3d_debug_print_start();

        // Sidebar menu labels
        const char *sidebarLabels[] = {
            "None",
            "Free Camera",
            "Light Direction",
            "Camera Position",
            "Collision",
            "Profiler",
            "Memory Debug",
            "Scene Flow"
        };

        int sidebarCount = rowCount + 1;
        int sidebarX = 10;
        int sidebarY = 12;
        int sidebarW = 160;
        int sidebarH = 10;

        // Draw sidebar background
        rdpq_set_prim_color(RGBA32(0, 0, 0, 180));
        rdpq_fill_rectangle(0, 0, sidebarW, display_get_height());
        t3d_debug_print_start();

        // Draw sidebar items
        for(int i = 0; i < sidebarCount; i++)
        {
            if(i == sidebarSelected && !inCategoryScreen)
            {
                // Highlight selected
                rdpq_set_prim_color(RGBA32(80, 80, 200, 220));
                rdpq_fill_rectangle(sidebarX - 4, sidebarY - 2 + i * sidebarH, sidebarW - 10, sidebarY - 2 + i * sidebarH + sidebarH);
                t3d_debug_print_start();
                rdpq_set_prim_color(RGBA32(255, 255, 255, 255)); // White text for selected
            }
            else
            {
                rdpq_set_prim_color(RGBA32(180, 180, 180, 255)); // Dimmer text for unselected
            }

            t3d_debug_printf(sidebarX, sidebarY + i * sidebarH, "%s", sidebarLabels[i]);
        }

        rdpq_set_prim_color(RGBA32(255, 255, 255, 255)); // Restore color for right pane

        // Draw right pane (category screen) if inCategoryScreen
        if(inCategoryScreen)
        {
            int paneX = sidebarW - 10;

            switch(controlling)
            {
                case DEV_NONE:
                    t3d_debug_printf(paneX, 24, "No dev tools active.");
                    break;

                case DEV_FREECAM:
                    t3d_debug_printf(paneX, 24, "Free Camera Controls");
                    t3d_debug_printf(paneX, 36, "CamX: %.4f", camPos.v[0]);
                    t3d_debug_printf(paneX, 48, "CamY: %.4f", camPos.v[1]);
                    t3d_debug_printf(paneX, 60, "CamZ: %.4f", camPos.v[2]);
                    break;

                case DEV_LIGHTDIR:
                    t3d_debug_printf(paneX, 24, "Light Direction Controls");
                    t3d_debug_printf(paneX, 36, "DirLight: %.4f, %.4f, %.4f", lightDirVec.v[0], lightDirVec.v[1], lightDirVec.v[2]);
                    break;

                case DEV_CAMPOS:
                    t3d_debug_printf(paneX, 24, "Camera Position");

                    float targetPos[3] = {
                        camPos.v[0] + camDir.v[0] * distanceInFrontOfCamera,
                        camPos.v[1] + camDir.v[1] * distanceInFrontOfCamera,
                        camPos.v[2] + camDir.v[2] * distanceInFrontOfCamera
                    };

                    t3d_debug_printf(paneX, 36, "CamX: %.4f", targetPos[0]);
                    t3d_debug_printf(paneX, 48, "CamY: %.4f", targetPos[1]);
                    t3d_debug_printf(paneX, 60, "CamZ: %.4f", targetPos[2]);
                    break;

                case DEV_COLLISION:
                    rdpq_set_prim_color(RGBA32(0, 0, 0, 200));
                    rdpq_fill_rectangle(paneX-8, 30 + (selected * 12) - 6, display_get_width(), 30 + (selected * 12) + 6);
                    t3d_debug_print_start();

                    if(toggleColliders)
                        t3d_debug_printf(paneX, 24, "Toggle Colliders On");
                    else
                        t3d_debug_printf(paneX, 24, "Toggle Colliders Off");

                    t3d_debug_printf(paneX, 48, "Show BVH Leaf Node Intersections");
                    t3d_debug_printf(paneX, 60, "Show BVH");
                    break;

                case DEV_RSPQ_PROFILER:
                    if(profile_data.frame_count == 0)
                        t3d_debug_printf(paneX, 24, "%s", "See wiki/profiling.md");

                    debug_draw_perf_overlay(last3dFPS);
                    break;

                case DEV_MEMORY_DEBUG:
                    t3d_debug_print_start();
                    t3d_debug_printf(paneX, 28, "Live RDRAM overlay");
                    t3d_debug_printf(paneX, 44, "DEV_MODE:     %s", DEV_MODE ? "ON" : "OFF");
                    t3d_debug_printf(paneX, 56, "DEBUG_MEMORY: %s", DEBUG_MEMORY ? "ON" : "OFF");
                    t3d_debug_printf(paneX, 72, "Overlay requires both ON");

                    if (DEV_MODE && DEBUG_MEMORY) {
                        uint32_t rdram_total = (uint32_t)__boot_memsize;
                        uint32_t heap_total = (uint32_t)live_heap_stats.total;
                        uint32_t heap_used = (uint32_t)live_heap_stats.used;
                        uint32_t heap_free = heap_total - heap_used;
                        uint32_t rdram_used_or_reserved = rdram_total - heap_free;
                        uint32_t non_heap_or_reserved = rdram_total - heap_total;

                        t3d_debug_printf(paneX, 96, "RAM:   %.2f MB", bytes_to_mib(rdram_total));
                        t3d_debug_printf(paneX, 108, "USED+: %.2f MB", bytes_to_mib(rdram_used_or_reserved));
                        t3d_debug_printf(paneX, 120, "FREE:  %.2f MB", bytes_to_mib(heap_free));
                        t3d_debug_printf(paneX, 132, "HUSED: %.2f MB", bytes_to_mib(heap_used));
                        t3d_debug_printf(paneX, 144, "HTOT:  %.2f MB", bytes_to_mib(heap_total));
                        t3d_debug_printf(paneX, 156, "NONHP: %.2f MB", bytes_to_mib(non_heap_or_reserved));
                    }
                    break;

                case DEV_SCENE_FLOW:
                    t3d_debug_printf(paneX, 24, "DEV_MODE warps (A = run)");
                    t3d_debug_printf(paneX, 40, "%s Skip to boss fight",
                        selected == 0 ? ">" : " ");
                    t3d_debug_printf(paneX, 56, "%s Pre phase-2 (~42%% HP)",
                        selected == 1 ? ">" : " ");
                    t3d_debug_printf(paneX, 80, "Up/Down: choose");
                    break;
            }
        }
    }

    dev_draw_memory_debug();
}

void dev_draw_update(T3DViewport *viewport)
{
    if(controlling != 0 && dplDevArrow != NULL)
    {
        t3d_matrix_push_pos(1);
            t3d_matrix_set(devArrowMatFP, true);
            rspq_block_run(dplDevArrow);
            //rdpq_mode_zbuf(true, false);
        t3d_matrix_pop(1);

        rdpq_mode_zbuf(true, true);
    }
}

// FOR COLLISION SYSTEM REFERENCE ONLY
void dev_draw_debug_update(T3DViewport *viewport)
{
    if(!debugDraw)
        return;

    rspq_wait();

    // Build character capsule in game space = character.pos + local offsets
    float charScale = character.scale[0];

    T3DVec3 charCapA = {{
        character.pos[0] + character.capsuleCollider.localCapA.v[0],
        character.pos[1] + character.capsuleCollider.localCapA.v[1],
        character.pos[2] + character.capsuleCollider.localCapA.v[2],
    }};

    T3DVec3 charCapB = {{
        character.pos[0] + character.capsuleCollider.localCapB.v[0],
        character.pos[1] + character.capsuleCollider.localCapB.v[1],
        character.pos[2] + character.capsuleCollider.localCapB.v[2],
    }};

    float charRadius = character.capsuleCollider.radius;

    AABB debugAabbs[] = {
        {
            .min = {{-64.0f, 0.0f, -64.0f}},
            .max = {{ 64.0f, 64.0f, 64.0f}}
        },
        {
            .min = {{ 32.0f, 0.0f, 32.0f}},
            .max = {{ 96.0f, 32.0f, 96.0f}}
        }
    };

    int debugAabbCount = sizeof(debugAabbs) / sizeof(debugAabbs[0]);

    // Also draw character capsule vs AABBs for collision testing
    debug_draw_capsule_vs_aabb_list(
        viewport,
        &charCapA,
        &charCapB,
        charRadius,
        debugAabbs,
        debugAabbCount,
        DEBUG_COLORS[1],
        DEBUG_COLORS[0]);

    // Draw boss capsule collider, if needed for future debug drawing.
    Boss* boss = boss_get_instance();
    if (boss) {
        float bossScale = boss->scale[0];

        T3DVec3 bossCapA = {{
            boss->pos[0] + boss->capsuleCollider.localCapA.v[0] * bossScale,
            boss->pos[1] + boss->capsuleCollider.localCapA.v[1] * bossScale,
            boss->pos[2] + boss->capsuleCollider.localCapA.v[2] * bossScale,
        }};

        T3DVec3 bossCapB = {{
            boss->pos[0] + boss->capsuleCollider.localCapB.v[0] * bossScale,
            boss->pos[1] + boss->capsuleCollider.localCapB.v[1] * bossScale,
            boss->pos[2] + boss->capsuleCollider.localCapB.v[2] * bossScale,
        }};

        float bossRadius = boss->capsuleCollider.radius * bossScale;

        (void)bossCapA;
        (void)bossCapB;
        (void)bossRadius;
    }

    (void)charScale;
}

void dev_frames_end_update()
{
    if(!displayMetrics)
    {
        last3dFPS = display_get_fps();
        rspq_wait();
        rspq_profile_get_data(&profile_data);

        if(requestDisplayMetrics)
            displayMetrics = true;
    }

    rspq_profile_reset();
}

void dev_free()
{
    t3d_model_free(devArrow);
    free_uncached(devArrowMatFP);
    rspq_block_free(dplDevArrow);

    devArrow = NULL;
    devArrowMatFP = NULL;
    dplDevArrow = NULL;
}