#include "debug_draw.h"
#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <stdlib.h>  // abs()

#include "globals.h"
#include "game_math.h"
#include "display_utility.h"
#include "simple_collision_utility.h" 

#include <math.h> // for isfinite

uint16_t DEBUG_COLORS[6] = {
    0xf800, // Red     (#ff0000)
    0x0fe0, // Green   (#08ff00)
    0x001f, // Blue    (#0000ff)
    0xff80, // Yellow  (#fff300)
    0xf01f, // Magenta (#fa00ff)
    0xf8a5, // Orange  (#ffa500)
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

void debug_draw_aabb(
    T3DViewport *vp,
    const T3DVec3 *min,
    const T3DVec3 *max,
    uint16_t color)
{
    T3DVec3 points[8];

    // Corner world positions (no fixed / MODEL_SCALE stuff)
    T3DVec3 p0 = *min;
    T3DVec3 p1 = {{ max->v[0], min->v[1], min->v[2] }};
    T3DVec3 p2 = {{ min->v[0], max->v[1], min->v[2] }};
    T3DVec3 p3 = {{ max->v[0], max->v[1], min->v[2] }};
    T3DVec3 p4 = {{ min->v[0], max->v[1], max->v[2] }};
    T3DVec3 p5 = {{ max->v[0], max->v[1], max->v[2] }};
    T3DVec3 p6 = {{ min->v[0], min->v[1], max->v[2] }};
    T3DVec3 p7 = {{ max->v[0], min->v[1], max->v[2] }};

    // Project to view space
    t3d_viewport_calc_viewspace_pos(vp, &points[0], &p0);
    t3d_viewport_calc_viewspace_pos(vp, &points[1], &p1);
    t3d_viewport_calc_viewspace_pos(vp, &points[2], &p2);
    t3d_viewport_calc_viewspace_pos(vp, &points[3], &p3);
    t3d_viewport_calc_viewspace_pos(vp, &points[4], &p4);
    t3d_viewport_calc_viewspace_pos(vp, &points[5], &p5);
    t3d_viewport_calc_viewspace_pos(vp, &points[6], &p6);
    t3d_viewport_calc_viewspace_pos(vp, &points[7], &p7);

    const int indices[24] = {
        0, 1, 1, 3, 3, 2, 2, 0,
        4, 5, 5, 7, 7, 6, 6, 4,
        0, 6, 1, 7, 2, 4, 3, 5
    };

    for (int i = 0; i < 24; i += 2)
        debug_draw_line_vec3(&points[indices[i]], &points[indices[i+1]], color);
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

void debug_draw_tri_wire(
    T3DViewport *vp,
    const T3DVec3 *p0,
    const T3DVec3 *p1,
    const T3DVec3 *p2,
    uint16_t color)
{
    // Project world-space vertices to viewspace
    T3DVec3 sp0, sp1, sp2;
    t3d_viewport_calc_viewspace_pos(vp, &sp0, p0);
    t3d_viewport_calc_viewspace_pos(vp, &sp1, p1);
    t3d_viewport_calc_viewspace_pos(vp, &sp2, p2);
    
    // Draw three lines to form a triangle wireframe
    debug_draw_line_vec3(&sp0, &sp1, color);
    debug_draw_line_vec3(&sp1, &sp2, color);
    debug_draw_line_vec3(&sp2, &sp0, color);
}

void debug_draw_capsule(
    T3DViewport *vp,
    const T3DVec3 *a,
    const T3DVec3 *b,
    float radius,
    uint16_t color)
{
    // Draw end-cap spheres
    debug_draw_sphere(vp, a, radius, color);
    debug_draw_sphere(vp, b, radius, color);

    // Connect ends with a line
    T3DVec3 sp0, sp1;
    t3d_viewport_calc_viewspace_pos(vp, &sp0, a);
    t3d_viewport_calc_viewspace_pos(vp, &sp1, b);
    debug_draw_line_vec3(&sp0, &sp1, color);
}

void debug_draw_capsule_vs_aabb_list(
    T3DViewport   *vp,
    const T3DVec3 *capA,
    const T3DVec3 *capB,
    float          capRadius,
    const AABB    *aabbs,
    int            aabbCount,
    uint16_t       colorNoHit,
    uint16_t       colorHit)
{
    float capAF[3] = { capA->v[0], capA->v[1], capA->v[2] };
    float capBF[3] = { capB->v[0], capB->v[1], capB->v[2] };

    bool anyHit = false;

    for (int i = 0; i < aabbCount; ++i)
    {
        const AABB *box = &aabbs[i];

        float rectMin[3] = {
            box->min.v[0],
            box->min.v[1],
            box->min.v[2]
        };

        float rectMax[3] = {
            box->max.v[0],
            box->max.v[1],
            box->max.v[2]
        };

        bool hit = scu_capsule_vs_rect_f(
            capAF, capBF, capRadius,
            rectMin, rectMax);

        if (hit) anyHit = true;

        uint16_t c = hit ? colorHit : colorNoHit;
        debug_draw_aabb(vp, &box->min, &box->max, c);
    }

    uint16_t capColor = anyHit ? colorHit : colorNoHit;
    debug_draw_capsule(vp, capA, capB, capRadius, capColor);
}