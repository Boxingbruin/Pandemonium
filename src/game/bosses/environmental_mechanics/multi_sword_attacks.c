#include "multi_sword_attacks.h"

#include <t3d/t3d.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

#include "character.h"
#include "simple_collision_utility.h"
#include "sword_trail.h"
#include "debug_draw.h"
#include "dev.h"

// ------------------------------------------------------------
// CONFIG (tune freely)
// ------------------------------------------------------------
#ifndef MSA_MAX_SWORDS
#define MSA_MAX_SWORDS 16
#endif

// default count
static int  g_count   = 5;
static bool g_enabled = true;
static MsaPattern g_pattern = MSA_PATTERN_GROUND_SWEEP;

// Ground plane used for the “stabbed into ground” effect.
static const float GROUND_Y = 0.0f;

// Sword body capsule
static const float SWORD_RADIUS          = 9.0f;
static const float SWORD_BLADE_LEN       = 90.0f;
static const float SWORD_HALF_IN_GROUND  = 25.0f;

// Trail collision uses MANY capsules along the curve (polyline segments).
static const float TRAIL_RADIUS    = 10.0f;
static const float TRAIL_LIFE      = 1.8f;
static const float TRAIL_MIN_STEP  = 7.0f;     // distance before adding a new trail point

// Motion (pattern A / ground sweep)
static const float SPEED        = 220.0f;
static const float TURN_RATE    = 2.6f;   // rad/sec scaled by noise
static const float NOISE_FREQ   = 1.35f;
static const float NOISE_AMPL   = 1.0f;
static const float HOME_RADIUS  = 700.0f;
static const float HOME_BIAS    = 1.8f;

// Damage gating (optional)
static const float DMG_BODY     = 22.0f;
static const float DMG_TRAIL    = 12.0f;
static const float HIT_COOLDOWN = 0.25f;

// ------------------------------------------------------------
// INTERNAL TYPES
// ------------------------------------------------------------
typedef struct {
    float pos[3];    // sword anchor on ground (y fixed)
    float dir[2];    // unit tangent in XZ
    float t;         // time for noise
    uint32_t seed;

    // Trail visuals
    SwordTrail trail;

    // Trail collision polyline
    float last_tip[3];
} MsaSword;

typedef struct {
    float a[3];
    float b[3];
    float age;
    float radius;
    bool  active;
} TrailSeg;

enum { MAX_TRAIL_SEGS = 512 };

static MsaSword g_swords[MSA_MAX_SWORDS];
static TrailSeg g_segs[MAX_TRAIL_SEGS];
static int g_seg_head = 0;

static float g_hit_cd = 0.0f;

// center used to keep system in view while always-on
static float g_center[3] = {0};

// ------------------------------------------------------------
// SMALL HELPERS
// ------------------------------------------------------------
static inline float clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline uint32_t xorshift32(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *s = x;
    return x;
}

static inline float frand01(uint32_t *s) {
    return (float)(xorshift32(s) & 0x00FFFFFF) / (float)0x01000000;
}

static inline float dist_xz(const float a[3], const float b[3]) {
    float dx = a[0] - b[0];
    float dz = a[2] - b[2];
    return sqrtf(dx*dx + dz*dz);
}

static inline void rotate_dir(float dir[2], float ang) {
    float c = cosf(ang);
    float s = sinf(ang);
    float x = dir[0], z = dir[1];
    dir[0] = x * c - z * s;
    dir[1] = x * s + z * c;

    float d = sqrtf(dir[0]*dir[0] + dir[1]*dir[1]);
    if (d > 1e-6f) { dir[0] /= d; dir[1] /= d; }
    else { dir[0] = 1.0f; dir[1] = 0.0f; }
}

static void seg_push(const float a[3], const float b[3], float radius) {
    TrailSeg *seg = &g_segs[g_seg_head];
    memcpy(seg->a, a, sizeof(float) * 3);
    memcpy(seg->b, b, sizeof(float) * 3);
    seg->age = 0.0f;
    seg->radius = radius;
    seg->active = true;
    g_seg_head = (g_seg_head + 1) % MAX_TRAIL_SEGS;
}

static void segs_update(float dt) {
    for (int i = 0; i < MAX_TRAIL_SEGS; i++) {
        if (!g_segs[i].active) continue;
        g_segs[i].age += dt;
        if (g_segs[i].age > TRAIL_LIFE) g_segs[i].active = false;
    }
}

static void get_character_capsule_world(float outA[3], float outB[3], float *outR) {
    outA[0] = character.pos[0] + character.capsuleCollider.localCapA.v[0];
    outA[1] = character.pos[1] + character.capsuleCollider.localCapA.v[1];
    outA[2] = character.pos[2] + character.capsuleCollider.localCapA.v[2];

    outB[0] = character.pos[0] + character.capsuleCollider.localCapB.v[0];
    outB[1] = character.pos[1] + character.capsuleCollider.localCapB.v[1];
    outB[2] = character.pos[2] + character.capsuleCollider.localCapB.v[2];

    *outR = character.capsuleCollider.radius;
}

// Sword body capsule: mostly vertical, slight forward tilt so “blade faces tangent”
static void sword_body_capsule(const MsaSword *s, float outA[3], float outB[3]) {
    float fx = s->dir[0];
    float fz = s->dir[1];

    const float tilt = 0.25f;

    outA[0] = s->pos[0] - fx * 6.0f;
    outA[1] = GROUND_Y + (SWORD_BLADE_LEN * 0.85f);
    outA[2] = s->pos[2] - fz * 6.0f;

    outB[0] = s->pos[0] + fx * (SWORD_BLADE_LEN * tilt);
    outB[1] = GROUND_Y - SWORD_HALF_IN_GROUND;
    outB[2] = s->pos[2] + fz * (SWORD_BLADE_LEN * tilt);
}

// “tip” point for trail sampling: sits on ground plane
static void sword_trail_tip(const MsaSword *s, float outTip[3]) {
    outTip[0] = s->pos[0];
    outTip[1] = GROUND_Y;
    outTip[2] = s->pos[2];
}

// Pattern A turn signal (organic S-curve-ish)
static float noise_turn_signal(const MsaSword *s) {
    float t = s->t;
    float ph1 = (float)((s->seed >>  0) & 1023) * 0.006135923f;
    float ph2 = (float)((s->seed >> 10) & 1023) * 0.006135923f;
    float ph3 = (float)((s->seed >> 20) & 1023) * 0.006135923f;
    float w = NOISE_FREQ;

    float n =
        0.75f * sinf(w * t + ph1) +
        0.35f * sinf((w * 2.07f) * t + ph2) +
        0.20f * sinf((w * 3.63f) * t + ph3);

    return (n * (NOISE_AMPL / 1.3f));
}

static void apply_home_bias(MsaSword *s, float *inout_turn) {
    float dx = s->pos[0] - g_center[0];
    float dz = s->pos[2] - g_center[2];
    float d = sqrtf(dx*dx + dz*dz);
    if (d <= HOME_RADIUS) return;

    float tx = -dx / d;
    float tz = -dz / d;

    float cx = s->dir[0];
    float cz = s->dir[1];

    // sign of cross to know which way to turn toward center
    float cross = cx * tz - cz * tx;
    float excess = clampf((d - HOME_RADIUS) / 250.0f, 0.0f, 1.0f);
    *inout_turn += cross * (HOME_BIAS * excess);
}

static bool trail_hits_character(const float charA[3], const float charB[3], float charR) {
    // IMPORTANT: this is NOT “one capsule for the whole trail”.
    // It is many short capsule segments. That’s how it bends/curves.
    for (int i = 0; i < MAX_TRAIL_SEGS; i++) {
        const TrailSeg *s = &g_segs[i];
        if (!s->active) continue;

        if (scu_capsule_vs_capsule_f(
                s->a, s->b, s->radius,
                charA, charB, charR))
            return true;
    }
    return false;
}

// ------------------------------------------------------------
// PUBLIC API
// ------------------------------------------------------------
void msa_set_enabled(bool enabled) { g_enabled = enabled; }

void msa_set_sword_count(int count) {
    if (count < 1) count = 1;
    if (count > MSA_MAX_SWORDS) count = MSA_MAX_SWORDS;
    g_count = count;
}

void msa_set_pattern(MsaPattern p) { g_pattern = p; }

void msa_init(void) {
    memset(g_swords, 0, sizeof(g_swords));
    memset(g_segs,   0, sizeof(g_segs));
    g_seg_head = 0;
    g_hit_cd = 0.0f;

    // Keep it centered on the player for always-on testing
    g_center[0] = character.pos[0];
    g_center[1] = GROUND_Y;
    g_center[2] = character.pos[2];

    // Spawn swords in a ring around player
    uint32_t seed = 0xA123BEEF;
    for (int i = 0; i < MSA_MAX_SWORDS; i++) {
        MsaSword *s = &g_swords[i];
        s->seed = xorshift32(&seed) ^ (uint32_t)(i * 0x9E3779B9u);

        float ang = ((float)i / (float)MSA_MAX_SWORDS) * (2.0f * (float)M_PI);
        float r   = 120.0f + 80.0f * frand01(&s->seed);

        s->pos[0] = g_center[0] + cosf(ang) * r;
        s->pos[1] = GROUND_Y;
        s->pos[2] = g_center[2] + sinf(ang) * r;

        float yaw = ang + (frand01(&s->seed) - 0.5f) * 1.2f;
        s->dir[0] = cosf(yaw);
        s->dir[1] = sinf(yaw);

        s->t = frand01(&s->seed) * 10.0f;

        sword_trail_instance_init(&s->trail);
        sword_trail_tip(s, s->last_tip);
    }
}

void msa_update(float dt) {
    if (!g_enabled) return;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.05f) dt = 0.05f;

    // always-on center follows player for now
    g_center[0] = character.pos[0];
    g_center[2] = character.pos[2];

    if (g_hit_cd > 0.0f) g_hit_cd -= dt;

    segs_update(dt);

    float charA[3], charB[3], charR;
    get_character_capsule_world(charA, charB, &charR);

    bool hit_body  = false;

    // Pattern-specific update (only one for now)
    if (g_pattern == MSA_PATTERN_GROUND_SWEEP) {
        for (int i = 0; i < g_count; i++) {
            MsaSword *s = &g_swords[i];
            s->t += dt;

            float turn = noise_turn_signal(s);
            apply_home_bias(s, &turn);

            float dYaw = turn * TURN_RATE * dt;
            dYaw = clampf(dYaw, -0.22f, 0.22f);
            rotate_dir(s->dir, dYaw);

            s->pos[0] += s->dir[0] * SPEED * dt;
            s->pos[2] += s->dir[1] * SPEED * dt;
            s->pos[1] = GROUND_Y;

            // ---- Trail visuals (your existing system)
            float tip[3];
            sword_trail_tip(s, tip);

            float base[3] = {
                tip[0] - s->dir[0] * 26.0f,
                tip[1] + 1.0f,
                tip[2] - s->dir[1] * 26.0f
            };

            sword_trail_instance_update(&s->trail, dt, true, base, tip);

            // ---- Trail collision (polyline -> many capsules)
            float moved = dist_xz(tip, s->last_tip);
            if (moved >= TRAIL_MIN_STEP) {
                float a[3] = { s->last_tip[0], GROUND_Y + 2.0f, s->last_tip[2] };
                float b[3] = { tip[0],        GROUND_Y + 2.0f, tip[2]        };
                seg_push(a, b, TRAIL_RADIUS);

                memcpy(s->last_tip, tip, sizeof(float) * 3);
            }

            // ---- Sword body collision
            float swordA[3], swordB[3];
            sword_body_capsule(s, swordA, swordB);

            if (scu_capsule_vs_capsule_f(
                    swordA, swordB, SWORD_RADIUS,
                    charA, charB, charR))
            {
                hit_body = true;
            }
        }
    }

    bool hit_trail = trail_hits_character(charA, charB, charR);

    if (g_hit_cd <= 0.0f) {
        if (hit_body) {
            character_apply_damage(DMG_BODY);
            g_hit_cd = HIT_COOLDOWN;
        } else if (hit_trail) {
            character_apply_damage(DMG_TRAIL);
            g_hit_cd = HIT_COOLDOWN;
        }
    }
}

void msa_draw(T3DViewport *viewport) {
    if (!g_enabled) return;

    // Draw visuals
    for (int i = 0; i < g_count; i++) {
        sword_trail_instance_draw(&g_swords[i].trail, viewport);
    }

    if (!debugDraw) return;

    // Debug: sword body capsules + trail segment capsules
    for (int i = 0; i < g_count; i++) {
        float A[3], B[3];
        sword_body_capsule(&g_swords[i], A, B);

        T3DVec3 a = {{ A[0], A[1], A[2] }};
        T3DVec3 b = {{ B[0], B[1], B[2] }};
        debug_draw_capsule(viewport, &a, &b, SWORD_RADIUS, DEBUG_COLORS[5]);

        T3DVec3 tip = {{ g_swords[i].pos[0], GROUND_Y, g_swords[i].pos[2] }};
        debug_draw_cross(viewport, &tip, 10.0f, DEBUG_COLORS[0]);
    }

    for (int i = 0; i < MAX_TRAIL_SEGS; i++) {
        if (!g_segs[i].active) continue;
        T3DVec3 a = {{ g_segs[i].a[0], g_segs[i].a[1], g_segs[i].a[2] }};
        T3DVec3 b = {{ g_segs[i].b[0], g_segs[i].b[1], g_segs[i].b[2] }};
        debug_draw_capsule(viewport, &a, &b, g_segs[i].radius, DEBUG_COLORS[1]);
    }
}