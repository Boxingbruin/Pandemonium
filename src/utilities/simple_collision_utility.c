#include "simple_collision_utility.h"
#include <math.h>

/* ------------------------------------------------------------------
 * Basic float vec3 helpers using float[3]
 * ------------------------------------------------------------------ */

static inline void v_sub(const float a[3], const float b[3], float out[3])
{
    out[0] = a[0] - b[0];
    out[1] = a[1] - b[1];
    out[2] = a[2] - b[2];
}

static inline void v_add(const float a[3], const float b[3], float out[3])
{
    out[0] = a[0] + b[0];
    out[1] = a[1] + b[1];
    out[2] = a[2] + b[2];
}

static inline void v_scale(const float v[3], float s, float out[3])
{
    out[0] = v[0] * s;
    out[1] = v[1] * s;
    out[2] = v[2] * s;
}

static inline float v_dot(const float a[3], const float b[3])
{
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}

static inline float v_len2(const float v[3])
{
    return v_dot(v, v);
}

static inline float v_dist2(const float a[3], const float b[3])
{
    float d[3];
    v_sub(a, b, d);
    return v_len2(d);
}

static inline float f_clamp(float x, float lo, float hi)
{
    return (x < lo) ? lo : (x > hi ? hi : x);
}

/* ------------------------------------------------------------------
 * Closest point on segment AB to P (float)
 * ------------------------------------------------------------------ */

static void closest_point_on_segment(
    const float A[3],
    const float B[3],
    const float P[3],
    float out[3])
{
    float AB[3], AP[3];
    v_sub(B, A, AB);
    v_sub(P, A, AP);

    float ab2 = v_dot(AB, AB);
    float t = 0.0f;

    if (ab2 > 0.0f) {
        t = v_dot(AP, AB) / ab2;
        t = f_clamp(t, 0.0f, 1.0f);
    }

    float scaled[3];
    v_scale(AB, t, scaled);
    v_add(A, scaled, out);
}

/* ------------------------------------------------------------------
 * Segment–segment squared distance (float, Ericson-style)
 * ------------------------------------------------------------------ */

static float segment_segment_dist2(
    const float p1[3], const float q1[3],
    const float p2[3], const float q2[3])
{
    const float EPS = 1e-4f;

    float d1[3], d2[3], r[3];
    v_sub(q1, p1, d1);  // S1 direction
    v_sub(q2, p2, d2);  // S2 direction
    v_sub(p1, p2, r);

    float a = v_dot(d1, d1); // |d1|^2
    float e = v_dot(d2, d2); // |d2|^2
    float f = v_dot(d2, r);

    float s, t;

    if (a <= EPS && e <= EPS) {
        // both segments degenerate to points
        return v_dist2(p1, p2);
    }

    if (a <= EPS) {
        // first segment degenerate
        s = 0.0f;
        t = f / e;
        t = f_clamp(t, 0.0f, 1.0f);
    } else {
        float c = v_dot(d1, r);
        if (e <= EPS) {
            // second segment degenerate
            t = 0.0f;
            s = -c / a;
            s = f_clamp(s, 0.0f, 1.0f);
        } else {
            float b = v_dot(d1, d2);
            float denom = a*e - b*b;

            if (fabsf(denom) > EPS) {
                s = (b*f - c*e) / denom;
                s = f_clamp(s, 0.0f, 1.0f);
            } else {
                s = 0.0f;
            }

            t = (b*s + f) / e;

            if (t < 0.0f) {
                t = 0.0f;
                s = -c / a;
                s = f_clamp(s, 0.0f, 1.0f);
            } else if (t > 1.0f) {
                t = 1.0f;
                float num2 = b - c;
                s = num2 / a;
                s = f_clamp(s, 0.0f, 1.0f);
            }
        }
    }

    float c1[3], c2[3];
    float tmp[3];

    v_scale(d1, s, tmp);
    v_add(p1, tmp, c1);

    v_scale(d2, t, tmp);
    v_add(p2, tmp, c2);

    return v_dist2(c1, c2);
}

/* ------------------------------------------------------------------
 * segment–AABB squared distance (float)
 * Approx: project segment onto AABB center, clamp to [0,1],
 * then compute point–AABB distance.
 * ------------------------------------------------------------------ */

static float segment_aabb_dist2(
    const float A[3],
    const float B[3],
    const float bmin[3],
    const float bmax[3])
{
    float AB[3];
    v_sub(B, A, AB);

    // Center of AABB
    float center[3] = {
        0.5f * (bmin[0] + bmax[0]),
        0.5f * (bmin[1] + bmax[1]),
        0.5f * (bmin[2] + bmax[2])
    };

    float A_to_C[3];
    v_sub(center, A, A_to_C);

    float t_num = v_dot(A_to_C, AB);
    float t_den = v_dot(AB, AB);
    if (t_den <= 1e-8f)
        t_den = 1.0f;

    float t = t_num / t_den;
    t = f_clamp(t, 0.0f, 1.0f);

    float closest[3];
    float scaled[3];
    v_scale(AB, t, scaled);
    v_add(A, scaled, closest);

    // Point vs AABB distance^2
    float dist2 = 0.0f;
    for (int i = 0; i < 3; ++i) {
        float c = closest[i];
        float mn = bmin[i];
        float mx = bmax[i];

        float d = 0.0f;
        if (c < mn)      d = mn - c;
        else if (c > mx) d = c - mx;

        dist2 += d*d;
    }
    return dist2;
}

/* ------------------------------------------------------------------
 * core collision tests (float)
 * ------------------------------------------------------------------ */

static bool sphere_vs_sphere(
    const float c1[3], float r1,
    const float c2[3], float r2)
{
    float dist2 = v_dist2(c1, c2);
    float r_sum = r1 + r2;
    float r2sum = r_sum * r_sum;
    return dist2 <= r2sum;
}

static bool sphere_vs_rect(
    const float center[3], float radius,
    const float rect_min[3], const float rect_max[3])
{
    float closest[3];
    for (int i = 0; i < 3; ++i) {
        float c  = center[i];
        float mn = rect_min[i];
        float mx = rect_max[i];

        if      (c < mn) closest[i] = mn;
        else if (c > mx) closest[i] = mx;
        else             closest[i] = c;
    }

    float dist2 = v_dist2(center, closest);
    return dist2 <= radius * radius;
}

static bool rect_vs_rect(
    const float amin[3], const float amax[3],
    const float bmin[3], const float bmax[3])
{
    for (int i = 0; i < 3; ++i) {
        if (amax[i] < bmin[i] || amin[i] > bmax[i])
            return false;
    }
    return true;
}

static bool capsule_vs_sphere(
    const float cap_a[3], const float cap_b[3], float cap_radius,
    const float sphere_center[3], float sphere_radius)
{
    float closest[3];
    closest_point_on_segment(cap_a, cap_b, sphere_center, closest);

    float dist2 = v_dist2(closest, sphere_center);
    float r_sum = cap_radius + sphere_radius;
    return dist2 <= r_sum * r_sum;
}

static bool capsule_vs_rect(
    const float cap_a[3], const float cap_b[3], float cap_radius,
    const float rect_min[3], const float rect_max[3])
{
    float dist2 = segment_aabb_dist2(cap_a, cap_b, rect_min, rect_max);
    return dist2 <= cap_radius * cap_radius;
}

static bool capsule_vs_capsule(
    const float a0[3], const float a1[3], float radiusA,
    const float b0[3], const float b1[3], float radiusB)
{
    float dist2 = segment_segment_dist2(a0, a1, b0, b1);
    float r_sum = radiusA + radiusB;
    return dist2 <= r_sum * r_sum;
}

/* ------------------------------------------------------------------
 * Fixed-point API implementation
 * ------------------------------------------------------------------ */

bool scu_fixed_capsule_vs_capsule(
    const SCU_CapsuleFixed *c1,
    const SCU_CapsuleFixed *c2)
{
    // Convert fixed-point to float
    float a0[3] = {FROM_FIXED(c1->a.v[0]), FROM_FIXED(c1->a.v[1]), FROM_FIXED(c1->a.v[2])};
    float a1[3] = {FROM_FIXED(c1->b.v[0]), FROM_FIXED(c1->b.v[1]), FROM_FIXED(c1->b.v[2])};
    float radiusA = FROM_FIXED(c1->radius);
    
    float b0[3] = {FROM_FIXED(c2->a.v[0]), FROM_FIXED(c2->a.v[1]), FROM_FIXED(c2->a.v[2])};
    float b1[3] = {FROM_FIXED(c2->b.v[0]), FROM_FIXED(c2->b.v[1]), FROM_FIXED(c2->b.v[2])};
    float radiusB = FROM_FIXED(c2->radius);
    
    // Use existing float-space capsule collision test
    return capsule_vs_capsule(a0, a1, radiusA, b0, b1, radiusB);
}

/* ------------------------------------------------------------------
 * Public float-space API (unchanged signatures)
 * ------------------------------------------------------------------ */

bool scu_sphere_vs_sphere_f(
    const float c1[3], float r1,
    const float c2[3], float r2)
{
    return sphere_vs_sphere(c1, r1, c2, r2);
}

bool scu_sphere_vs_rect_f(
    const float center[3], float radius,
    const float rect_min[3], const float rect_max[3])
{
    return sphere_vs_rect(center, radius, rect_min, rect_max);
}

bool scu_rect_vs_rect_f(
    const float amin[3], const float amax[3],
    const float bmin[3], const float bmax[3])
{
    return rect_vs_rect(amin, amax, bmin, bmax);
}

bool scu_capsule_vs_sphere_f(
    const float cap_a[3], const float cap_b[3], float cap_radius,
    const float sphere_center[3], float sphere_radius)
{
    return capsule_vs_sphere(cap_a, cap_b, cap_radius, sphere_center, sphere_radius);
}

bool scu_capsule_vs_rect_f(
    const float cap_a[3], const float cap_b[3], float cap_radius,
    const float rect_min[3], const float rect_max[3])
{
    return capsule_vs_rect(cap_a, cap_b, cap_radius, rect_min, rect_max);
}

bool scu_capsule_vs_capsule_f(
    const float a0[3], const float a1[3], float radiusA,
    const float b0[3], const float b1[3], float radiusB)
{
    return capsule_vs_capsule(a0, a1, radiusA, b0, b1, radiusB);
}
