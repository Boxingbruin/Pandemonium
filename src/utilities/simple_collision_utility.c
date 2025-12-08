#include "simple_collision_utility.h"
#include <limits.h>   // for INT64_MAX

/* ----- basic fixed vec ops ----- */

int64_t scu_fixed_vec_dot(const FixedVec3 *a, const FixedVec3 *b)
{
    int64_t sum = 0;
    for (int i = 0; i < 3; ++i) {
        int64_t ai = a->v[i];
        int64_t bi = b->v[i];
        sum += (ai * bi) >> FIXED_SHIFT;   // Q16.16
    }
    return sum;
}

void scu_fixed_vec_sub(FixedVec3 *out, const FixedVec3 *a, const FixedVec3 *b)
{
    out->v[0] = a->v[0] - b->v[0];
    out->v[1] = a->v[1] - b->v[1];
    out->v[2] = a->v[2] - b->v[2];
}

int64_t scu_fixed_vec_len2(const FixedVec3 *a)
{
    return scu_fixed_vec_dot(a, a);
}

int64_t scu_fixed_vec_dist2(const FixedVec3 *a, const FixedVec3 *b)
{
    FixedVec3 d;
    scu_fixed_vec_sub(&d, a, b);
    return scu_fixed_vec_len2(&d);
}

/* Closest point on segment AB to P: result in fixed */
FixedVec3 scu_fixed_closest_point_on_segment(const FixedVec3 *A,
                                             const FixedVec3 *B,
                                             const FixedVec3 *P)
{
    FixedVec3 AB, AP;
    scu_fixed_vec_sub(&AB, B, A);
    scu_fixed_vec_sub(&AP, P, A);

    int64_t ab_dot_ab = scu_fixed_vec_dot(&AB, &AB); // Q16.16
    int64_t ap_dot_ab = scu_fixed_vec_dot(&AP, &AB); // Q16.16

    int32_t t = 0; // Q16.16
    if (ab_dot_ab != 0) {
        t = FIXED_DIV(ap_dot_ab, ab_dot_ab); // Q16.16 / Q16.16 -> Q16.16
        t = fixed_saturate(t);              // clamp [0, FIXED_ONE]
    }

    FixedVec3 res;
    for (int i = 0; i < 3; ++i) {
        int64_t tmp = ((int64_t)AB.v[i] * t) >> FIXED_SHIFT; // Q16.16
        res.v[i] = A->v[i] + (int32_t)tmp;
    }
    return res;
}

/* Segment–segment squared distance in fixed (Q16.16),
   standard closest-points algorithm (Ericson-style) */

int64_t scu_fixed_segment_segment_dist2(const FixedVec3 *p1,
                                        const FixedVec3 *q1,
                                        const FixedVec3 *p2,
                                        const FixedVec3 *q2)
{
    const int32_t EPS_FP = TO_FIXED(1e-4f); // small epsilon in Q16.16

    FixedVec3 d1, d2, r;
    scu_fixed_vec_sub(&d1, q1, p1); // direction of segment S1
    scu_fixed_vec_sub(&d2, q2, p2); // direction of segment S2
    scu_fixed_vec_sub(&r,  p1, p2);

    int64_t a = scu_fixed_vec_dot(&d1, &d1); // Q16.16
    int64_t e = scu_fixed_vec_dot(&d2, &d2); // Q16.16
    int64_t f = scu_fixed_vec_dot(&d2, &r);  // Q16.16

    int32_t s = 0, t = 0; // Q16.16

    if (a <= EPS_FP && e <= EPS_FP) {
        // both segments degenerate into points
        return scu_fixed_vec_dist2(p1, p2);
    }

    if (a <= EPS_FP) {
        // first segment degenerate
        s = 0;
        t = FIXED_DIV(f, e);
        t = fixed_saturate(t);
    } else {
        int64_t c = scu_fixed_vec_dot(&d1, &r);   // Q16.16
        if (e <= EPS_FP) {
            // second segment degenerate
            t = 0;
            s = -FIXED_DIV(c, a);
            s = fixed_saturate(s);
        } else {
            int64_t b = scu_fixed_vec_dot(&d1, &d2); // Q16.16

            // denom = a*e - b*b  (still Q16.16 after >> SHIFT)
            int64_t denom = ((a * e - b * b) >> FIXED_SHIFT);
            if (denom != 0) {
                // s = (b*f - c*e) / denom
                int64_t num_s = ((b * f - c * e) >> FIXED_SHIFT);
                s = FIXED_DIV(num_s, denom);
                s = fixed_saturate(s);
            } else {
                s = 0;
            }

            // t = (b*s + f)/e
            int64_t tmp = ((b * s) >> FIXED_SHIFT) + f; // Q16.16
            t = FIXED_DIV(tmp, e);

            if (t < 0) {
                t = 0;
                s = -FIXED_DIV(c, a);
                s = fixed_saturate(s);
            } else if (t > FIXED_ONE) {
                t = FIXED_ONE;
                int64_t num2 = (b - c); // Q16.16
                s = FIXED_DIV(num2, a);
                s = fixed_saturate(s);
            }
        }
    }

    // closest points: p1 + d1*s, p2 + d2*t
    FixedVec3 c1, c2;
    for (int i = 0; i < 3; ++i) {
        int64_t s_term = ((int64_t)d1.v[i] * s) >> FIXED_SHIFT;
        int64_t t_term = ((int64_t)d2.v[i] * t) >> FIXED_SHIFT;
        c1.v[i] = p1->v[i] + (int32_t)s_term;
        c2.v[i] = p2->v[i] + (int32_t)t_term;
    }

    return scu_fixed_vec_dist2(&c1, &c2);
}

/* ----- collision tests ----- */

bool scu_fixed_sphere_vs_sphere(const SCU_SphereFixed *s1,
                                const SCU_SphereFixed *s2)
{
    int64_t dist2 = scu_fixed_vec_dist2(&s1->center, &s2->center); // Q16.16
    int64_t r_sum = (int64_t)s1->radius + s2->radius;              // Q16.16
    int64_t r2    = (r_sum * r_sum) >> FIXED_SHIFT;                // Q16.16
    return dist2 <= r2;
}

bool scu_fixed_sphere_vs_rect(const SCU_SphereFixed *s,
                              const SCU_RectFixed   *r)
{
    FixedVec3 closest;
    for (int i = 0; i < 3; ++i) {
        int32_t c  = s->center.v[i];
        int32_t mn = r->min.v[i];
        int32_t mx = r->max.v[i];

        if (c < mn)      closest.v[i] = mn;
        else if (c > mx) closest.v[i] = mx;
        else             closest.v[i] = c;
    }

    int64_t dist2 = scu_fixed_vec_dist2(&s->center, &closest);
    int64_t r2    = ((int64_t)s->radius * s->radius) >> FIXED_SHIFT;
    return dist2 <= r2;
}

bool scu_fixed_rect_vs_rect(const SCU_RectFixed *a,
                            const SCU_RectFixed *b)
{
    // AABBs overlap if all axes overlap
    for (int i = 0; i < 3; ++i) {
        if (a->max.v[i] < b->min.v[i] || a->min.v[i] > b->max.v[i])
            return false;
    }
    return true;
}

bool scu_fixed_capsule_vs_sphere(const SCU_CapsuleFixed *cap,
                                 const SCU_SphereFixed  *s)
{
    FixedVec3 closest = scu_fixed_closest_point_on_segment(&cap->a, &cap->b, &s->center);
    int64_t dist2 = scu_fixed_vec_dist2(&closest, &s->center);

    int64_t r_sum = (int64_t)cap->radius + s->radius; // Q16.16
    int64_t r2    = (r_sum * r_sum) >> FIXED_SHIFT;
    return dist2 <= r2;
}

/* segment–AABB squared distance in fixed, similar to your BVH helper */

static int64_t scu_fixed_segment_aabb_dist2(const FixedVec3 *A,
                                            const FixedVec3 *B,
                                            const FixedVec3 *bmin,
                                            const FixedVec3 *bmax)
{
    int32_t ab[3];
    for (int i = 0; i < 3; ++i)
        ab[i] = B->v[i] - A->v[i]; // Q16.16

    int32_t center[3];
    for (int i = 0; i < 3; ++i)
        center[i] = (bmin->v[i] + bmax->v[i]) / 2;

    int32_t a_to_c[3];
    for (int i = 0; i < 3; ++i)
        a_to_c[i] = center[i] - A->v[i];

    int64_t t_num = 0, t_den = 0;
    for (int i = 0; i < 3; ++i) {
        int64_t at  = a_to_c[i];
        int64_t abi = ab[i];
        t_num += (at * abi) >> FIXED_SHIFT;   // Q16.16
        t_den += (abi * abi) >> FIXED_SHIFT;  // Q16.16
    }

    if (t_den == 0) t_den = 1;

    int32_t t = FIXED_DIV(t_num, t_den); // Q16.16
    t = fixed_saturate(t);

    int32_t closest[3];
    for (int i = 0; i < 3; ++i) {
        closest[i] = A->v[i] + (int32_t)(((int64_t)ab[i] * t) >> FIXED_SHIFT);
    }

    int64_t dist2 = 0;
    for (int i = 0; i < 3; ++i) {
        int32_t c  = closest[i];
        int32_t mn = bmin->v[i];
        int32_t mx = bmax->v[i];

        int32_t d = 0;
        if (c < mn)      d = mn - c;
        else if (c > mx) d = c - mx;

        dist2 += ((int64_t)d * d) >> FIXED_SHIFT;
    }
    return dist2;
}

bool scu_fixed_capsule_vs_rect(const SCU_CapsuleFixed *cap,
                               const SCU_RectFixed    *r)
{
    int64_t dist2 = scu_fixed_segment_aabb_dist2(&cap->a, &cap->b, &r->min, &r->max);
    int64_t r2    = ((int64_t)cap->radius * cap->radius) >> FIXED_SHIFT;
    return dist2 <= r2;
}

bool scu_fixed_capsule_vs_capsule(const SCU_CapsuleFixed *c1,
                                  const SCU_CapsuleFixed *c2)
{
    int64_t dist2 = scu_fixed_segment_segment_dist2(&c1->a, &c1->b, &c2->a, &c2->b);

    int64_t r_sum = (int64_t)c1->radius + c2->radius;
    int64_t r2    = (r_sum * r_sum) >> FIXED_SHIFT;
    return dist2 <= r2;
}
