// src/utilities/path_ribbon.h
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <sprite.h> 

#ifndef PR_MAX_POINTS_DRAW
#define PR_MAX_POINTS_DRAW 64
#endif

typedef struct { uint8_t r,g,b,a; } PRColor;

typedef struct {
    // points (xyz), y is forced to floor in try_add
    float pts[PR_MAX_POINTS_DRAW][3];
    uint8_t count;
    uint8_t max_points;
    uint8_t sealed;

    float min_step;

    float floor_y;
    float floor_eps;

    // wall
    float wall_height;
    float wall_w_mult;
    PRColor wall_color_bot;
    PRColor wall_color_top;

    // crack
    float  crack_w_start;   // half-width at t=0 (before tip taper)
    float  crack_w_end;     // half-width at t=1 (before tip taper)
    float  crack_w_noise;   // 0..1 relative jitter (per-point, deterministic)
    float  crack_tip_taper; // 0..0.49 portion of length tapered to 0 at ends
    PRColor crack_color;

    uint32_t seed;

    // fading
    float alpha_mul;
    float fade_t;
    float fade_dur;
    uint8_t fading;
    uint8_t dead;
} PathRibbon;

void path_ribbon_set_wall_texture(sprite_t *spr);

void path_ribbon_init(PathRibbon* pr, uint8_t max_points, float min_step);
void path_ribbon_clear(PathRibbon* pr);

void path_ribbon_set_floor(PathRibbon* pr, float floor_y);
void path_ribbon_set_seed(PathRibbon* pr, uint32_t seed);

bool path_ribbon_try_add(PathRibbon* pr, float x, float z);

void path_ribbon_start_fade(PathRibbon* pr, float seconds);
void path_ribbon_update(PathRibbon* pr, float dt);

void path_ribbon_draw_crack(const PathRibbon* pr);
void path_ribbon_draw_wall (const PathRibbon* pr);
