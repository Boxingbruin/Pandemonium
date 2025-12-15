#include "debug_draw.h"
#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <stdlib.h>  // abs()

#include "globals.h"
#include "game_math.h"
#include "display_utility.h"

#include <math.h> // for isfinite

uint16_t DEBUG_COLORS[5] = {
    0xf800, // Red     (#ff0000)
    0x0fe0, // Green   (#08ff00)
    0x001f, // Blue    (#0000ff)
    0xff80, // Yellow  (#fff300)
    0xf01f, // Magenta (#fa00ff)
};

static void debug_draw_line(int px0, int py0, int px1, int py1, uint16_t color)
{
    uint16_t *fb = offscreenBuffer.buffer;

    uint32_t width = display_get_width();
    uint32_t height = display_get_height();
    if((px0 > width + 200) || (px1 > width + 200) ||
        (py0 > height + 200) || (py1 > height + 200)) {
        return;
    }

    float pos[2] = {px0, py0};
    int dx = px1 - px0;
    int dy = py1 - py0;
    int steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    if(steps <= 0)return;
    float xInc = dx / (float)steps;
    float yInc = dy / (float)steps;

    for(int i=0; i<steps; ++i)
    {
        if(pos[1] >= 0 && pos[1] < height && pos[0] >= 0 && pos[0] < width) {
        fb[(int)pos[1] * width + (int)pos[0]] = color;
        }
        pos[0] += xInc;
        pos[1] += yInc;
    }
}

static inline void debug_draw_line_vec3(const T3DVec3 *p0, const T3DVec3 *p1, uint16_t color)
{
    if(p0->v[2] < 1.0 && p1->v[2] < 1.0) 
    {
        debug_draw_line((int)p0->v[0], (int)p0->v[1], (int)p1->v[0], (int)p1->v[1], color);
    }
}

void debug_draw_aabb(T3DViewport *vp, const FixedVec3 *min, const FixedVec3 *max, uint16_t color)
{
    // rspq_wait();
    // uint16_t *fb = offscreenBuffer.buffer;

    // transform min/max to screen space
    T3DVec3 points[8];
    T3DVec3 pt0 = {{FROM_FIXED(min->v[0])*MODEL_SCALE, FROM_FIXED(min->v[1])*MODEL_SCALE, FROM_FIXED(min->v[2])*MODEL_SCALE}};
    T3DVec3 pt1 = {{FROM_FIXED(max->v[0])*MODEL_SCALE, FROM_FIXED(min->v[1])*MODEL_SCALE, FROM_FIXED(min->v[2])*MODEL_SCALE}};
    t3d_viewport_calc_viewspace_pos(vp, &points[0], &pt0);
    t3d_viewport_calc_viewspace_pos(vp, &points[1], &pt1); pt0.v[1] = FROM_FIXED(max->v[1])*MODEL_SCALE;
    t3d_viewport_calc_viewspace_pos(vp, &points[2], &pt0); pt1.v[1] = FROM_FIXED(max->v[1])*MODEL_SCALE;
    t3d_viewport_calc_viewspace_pos(vp, &points[3], &pt1); pt0.v[2] = FROM_FIXED(max->v[2])*MODEL_SCALE;
    t3d_viewport_calc_viewspace_pos(vp, &points[4], &pt0); pt1.v[2] = FROM_FIXED(max->v[2])*MODEL_SCALE;
    t3d_viewport_calc_viewspace_pos(vp, &points[5], &pt1); pt0.v[1] = FROM_FIXED(min->v[1])*MODEL_SCALE;
    t3d_viewport_calc_viewspace_pos(vp, &points[6], &pt0); pt1.v[1] = FROM_FIXED(min->v[1])*MODEL_SCALE;
    t3d_viewport_calc_viewspace_pos(vp, &points[7], &pt1);

    // draw min/max as wireframe cube
    const int indices[24] = {
        0, 1, 1, 3, 3, 2, 2, 0,
        4, 5, 5, 7, 7, 6, 6, 4,
        0, 6, 1, 7, 2, 4, 3, 5
    };

    for(int i=0; i<24; i+=2) 
    {
        debug_draw_line_vec3(&points[indices[i]], &points[indices[i+1]], color);
    }
}

void debug_draw_circle(T3DViewport *vp, const T3DVec3 *center, float radius, const T3DVec3 *normal, uint16_t color)
{
    // Generate orthonormal basis (u, v) in circle's plane
    T3DVec3 u = {{1, 0, 0}};
    if (fabsf(normal->v[0]) > 0.9f) u.v[1] = 1, u.v[0] = 0;

    // u = normalize(cross(normal, u))
    T3DVec3 u_cross = {{
        normal->v[1]*u.v[2] - normal->v[2]*u.v[1],
        normal->v[2]*u.v[0] - normal->v[0]*u.v[2],
        normal->v[0]*u.v[1] - normal->v[1]*u.v[0]
    }};
    float len = sqrtf(u_cross.v[0]*u_cross.v[0] + u_cross.v[1]*u_cross.v[1] + u_cross.v[2]*u_cross.v[2]);
    if (len < 1e-6f) return; // Avoid division by zero
    for (int i = 0; i < 3; i++) u.v[i] = u_cross.v[i] / len;

    // v = cross(normal, u)
    T3DVec3 v = {{
        normal->v[1]*u.v[2] - normal->v[2]*u.v[1],
        normal->v[2]*u.v[0] - normal->v[0]*u.v[2],
        normal->v[0]*u.v[1] - normal->v[1]*u.v[0]
    }};

    const int segments = 32;
    for (int i = 0; i < segments; ++i)
    {
        float angle0 = (float)i / segments * 2.0f * T3D_PI;
        float angle1 = (float)(i + 1) / segments * 2.0f * T3D_PI;

        T3DVec3 p0, p1, sp0, sp1;
        for (int j = 0; j < 3; ++j) {
            p0.v[j] = center->v[j] + radius * (cosf(angle0) * u.v[j] + sinf(angle0) * v.v[j]);
            p1.v[j] = center->v[j] + radius * (cosf(angle1) * u.v[j] + sinf(angle1) * v.v[j]);
        }

        t3d_viewport_calc_viewspace_pos(vp, &sp0, &p0);
        t3d_viewport_calc_viewspace_pos(vp, &sp1, &p1);
        debug_draw_line_vec3(&sp0, &sp1, color);
    }
}

void debug_draw_sphere(T3DViewport *vp, const T3DVec3 *center, float radius, uint16_t color)
{
    rspq_wait();
    // uint16_t *fb = offscreenBuffer.buffer;

    T3DVec3 up = {{0, 1, 0}};     // Y is up
    T3DVec3 right = {{1, 0, 0}};  // Horizontal ring
    T3DVec3 forward = {{0, 0, 1}}; // Forward direction
    debug_draw_circle(vp, center, radius, &up, color);     // XZ plane
    debug_draw_circle(vp, center, radius, &right, color);  // YZ plane
    debug_draw_circle(vp, center, radius, &forward, color); // XY plane
}

void debug_draw_cross(T3DViewport *vp, const T3DVec3 *center, float half_length, uint16_t color)
{
    // X axis
    T3DVec3 p_x0 = *center, p_x1 = *center;
    p_x0.v[0] -= half_length;
    p_x1.v[0] += half_length;

    // Y axis
    T3DVec3 p_y0 = *center, p_y1 = *center;
    p_y0.v[1] -= half_length;
    p_y1.v[1] += half_length;

    // Z axis
    T3DVec3 p_z0 = *center, p_z1 = *center;
    p_z0.v[2] -= half_length;
    p_z1.v[2] += half_length;

    // Project and draw
    T3DVec3 sp0, sp1;
    t3d_viewport_calc_viewspace_pos(vp, &sp0, &p_x0);
    t3d_viewport_calc_viewspace_pos(vp, &sp1, &p_x1);
    debug_draw_line_vec3(&sp0, &sp1, color);

    t3d_viewport_calc_viewspace_pos(vp, &sp0, &p_y0);
    t3d_viewport_calc_viewspace_pos(vp, &sp1, &p_y1);
    debug_draw_line_vec3(&sp0, &sp1, color);

    t3d_viewport_calc_viewspace_pos(vp, &sp0, &p_z0);
    t3d_viewport_calc_viewspace_pos(vp, &sp1, &p_z1);
    debug_draw_line_vec3(&sp0, &sp1, color);
}

void debug_draw_dot(T3DViewport *vp, const T3DVec3 *center, float radius, uint16_t color)
{
    // Small sphere as a dot
    debug_draw_sphere(vp, center, radius, color);
}

void debug_draw_capsule_from_fixed(T3DViewport *vp, const FixedVec3 *a, const FixedVec3 *b, int32_t radius_fixed, uint16_t color)
{
    T3DVec3 aworld = {{
        FROM_FIXED(a->v[0]) * MODEL_SCALE,
        FROM_FIXED(a->v[1]) * MODEL_SCALE,
        FROM_FIXED(a->v[2]) * MODEL_SCALE
    }};
    T3DVec3 bworld = {{
        FROM_FIXED(b->v[0]) * MODEL_SCALE,
        FROM_FIXED(b->v[1]) * MODEL_SCALE,
        FROM_FIXED(b->v[2]) * MODEL_SCALE
    }};

    float radius_world = FROM_FIXED(radius_fixed) * MODEL_SCALE;

    // end spheres
    debug_draw_sphere(vp, &aworld, radius_world, color);
    debug_draw_sphere(vp, &bworld, radius_world, color);

    // connect ends with a line
    T3DVec3 sp0, sp1;
    t3d_viewport_calc_viewspace_pos(vp, &sp0, &aworld);
    t3d_viewport_calc_viewspace_pos(vp, &sp1, &bworld);
    debug_draw_line_vec3(&sp0, &sp1, color);  // static inline, same file
}
