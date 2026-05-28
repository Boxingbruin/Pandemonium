#include "scene_bounds.h"

#include <math.h>
#include <stdbool.h>

#include <libdragon.h>
#include <t3d/t3d.h>

#include "../dev/debug_draw.h"
#include "../utilities/simple_collision_utility.h"

// ------------------------------------------------------------
// Static scene bounds / room OBBs
// ------------------------------------------------------------

#define WALL_THICKNESS 20.0f
#define WALL_HEIGHT   200.0f

static SCU_OBB g_roomOBBs[] = {

    // right wall
    // (345, 595) -> (-430, 595)
    {
        .center = { (-430.0f + 345.0f) * 0.5f, 0.0f, 595.0f },
        .half   = { (345.0f - (-430.0f)) * 0.5f, WALL_HEIGHT * 0.5f, WALL_THICKNESS * 0.5f },
        .yaw    = 3.1415926f
    },

    // front wall
    // (-430, 595) -> (-430, -595)
    {
        .center = { -430.0f, 0.0f, (595.0f + -595.0f) * 0.5f },
        .half   = { (595.0f - (-595.0f)) * 0.5f, WALL_HEIGHT * 0.5f, WALL_THICKNESS * 0.5f },
        .yaw    = -1.5707963f
    },

    // left wall
    // (-458, -595) -> (345, -595)
    {
        .center = { (-458.0f + 345.0f) * 0.5f, 0.0f, -595.0f },
        .half   = { (345.0f - (-458.0f)) * 0.5f, WALL_HEIGHT * 0.5f, WALL_THICKNESS * 0.5f },
        .yaw    = 0.0f
    },

    // left wall bend in
    // (345, -595) -> (420, -420)
    {
        .center = { (345.0f + 420.0f) * 0.5f, 0.0f, (-595.0f + -420.0f) * 0.5f },
        .half   = { 95.52f, WALL_HEIGHT * 0.5f, WALL_THICKNESS * 0.5f },
        .yaw    = 1.1659045f
    },

    // right wall bend in
    // (345, 595) -> (420, 420)
    {
        .center = { (345.0f + 420.0f) * 0.5f, 0.0f, (595.0f + 420.0f) * 0.5f },
        .half   = { 95.52f, WALL_HEIGHT * 0.5f, WALL_THICKNESS * 0.5f },
        .yaw    = -1.1659045f
    },

    // left wall continued
    // (420, -415) -> (600, -415)
    {
        .center = { (420.0f + 600.0f) * 0.5f, 0.0f, -415.0f },
        .half   = { (600.0f - 420.0f) * 0.5f, WALL_HEIGHT * 0.5f, WALL_THICKNESS * 0.5f },
        .yaw    = 0.0f
    },

    // right wall continued
    // (420, 415) -> (600, 415)
    {
        .center = { (420.0f + 600.0f) * 0.5f, 0.0f, 415.0f },
        .half   = { (600.0f - 420.0f) * 0.5f, WALL_HEIGHT * 0.5f, WALL_THICKNESS * 0.5f },
        .yaw    = 0.0f
    },

    // back wall
    // (600, 420) -> (600, -420)
    {
        .center = { 600.0f, 0.0f, (420.0f + -420.0f) * 0.5f },
        .half   = { (420.0f - (-420.0f)) * 0.5f, WALL_HEIGHT * 0.5f, WALL_THICKNESS * 0.5f },
        .yaw    = -1.5707963f
    },

    // pillar 1
    // depth X=100, width Z=80
    // center (x=553, z=-238)
    {
        .center = { 553.0f, 0.0f, -238.0f },
        .half   = { 50.0f, WALL_HEIGHT * 0.5f, 40.0f },
        .yaw    = 0.0f
    },

    // pillar 2
    // depth X=100, width Z=80
    // center (x=553, z=238)
    {
        .center = { 553.0f, 0.0f, 238.0f },
        .half   = { 50.0f, WALL_HEIGHT * 0.5f, 40.0f },
        .yaw    = 0.0f
    },
};

static const int g_roomOBBCount = sizeof(g_roomOBBs) / sizeof(g_roomOBBs[0]);

// ------------------------------------------------------------
// Public accessors
// ------------------------------------------------------------

const SCU_OBB *scene_bounds_get_room_obbs(void)
{
    return g_roomOBBs;
}

int scene_bounds_get_room_obb_count(void)
{
    return g_roomOBBCount;
}

// ------------------------------------------------------------
// Broad room check
// ------------------------------------------------------------

bool scene_bounds_check_room(float posX, float posY, float posZ)
{
    (void)posY;

    if (posX < -458.0f) return false;
    if (posX >  600.0f) return false;
    if (posZ < -595.0f) return false;
    if (posZ >  595.0f) return false;

    return true;
}

// ------------------------------------------------------------
// Debug draw
// ------------------------------------------------------------

void scene_bounds_draw_obb_debug(
    T3DViewport *viewport,
    const SCU_OBB *obb,
    float y,
    uint16_t color
)
{
    if (!viewport || !obb) return;

    float c = cosf(obb->yaw);
    float s = sinf(obb->yaw);

    float hx = obb->half[0];
    float hz = obb->half[2];

    float lx[4] = { -hx,  hx,  hx, -hx };
    float lz[4] = { -hz, -hz,  hz,  hz };

    T3DVec3 p[4];

    for (int i = 0; i < 4; i++) {
        float wx = obb->center[0] + (c * lx[i] - s * lz[i]);
        float wz = obb->center[2] + (s * lx[i] + c * lz[i]);

        p[i] = (T3DVec3){{ wx, y, wz }};
    }

    debug_draw_tri_wire(viewport, &p[0], &p[1], &p[2], color);
    debug_draw_tri_wire(viewport, &p[0], &p[2], &p[3], color);
}

void scene_bounds_draw_debug(T3DViewport *viewport)
{
    if (!viewport) return;

    for (int i = 0; i < g_roomOBBCount; i++) {
        scene_bounds_draw_obb_debug(
            viewport,
            &g_roomOBBs[i],
            g_roomOBBs[i].center[1],
            DEBUG_COLORS[2]
        );
    }
}