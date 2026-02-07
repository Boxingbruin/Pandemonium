/*
logo_t3d under the same licensing as the rest of the code
logo_libdragon and logo_n64brew sourced from the N64brew-GameJam2024 repository

Copyright (c) 2024 N64brew

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmodel.h>
#include <math.h>

void logo_libdragon() {
  const color_t RED = RGBA32(221, 46, 26, 255);
  const color_t WHITE = RGBA32(255, 255, 255, 255);

  sprite_t *d1 = sprite_load("rom:/logos/libdragon/dragon1.sprite");
  sprite_t *d2 = sprite_load("rom:/logos/libdragon/dragon2.sprite");
  sprite_t *d3 = sprite_load("rom:/logos/libdragon/dragon3.sprite");
  sprite_t *d4 = sprite_load("rom:/logos/libdragon/dragon4.sprite");
  if (!d1 || !d2 || !d3 || !d4) {
    // If assets are missing, fail gracefully to avoid blocking boot.
    if (d1) sprite_free(d1);
    if (d2) sprite_free(d2);
    if (d3) sprite_free(d3);
    if (d4) sprite_free(d4);
    display_close();
    return;
  }
  wav64_t music;
  wav64_open(&music, "rom:/audio/sfx/dragon_22k.wav64");

  float angle1=0, angle2=0, angle3=0;
  float scale1=0, scale2=0, scale3=0, scroll4=0;
  uint32_t ms0=0;
  int anim_part=0;
  // This animation was authored for a 640x480 framebuffer.
  // Scale to the current display resolution (your game runs at 320x240).
  const int W = display_get_width();
  const int H = display_get_height();
  const float sx = (float)W / 640.0f;
  const float sy = (float)H / 480.0f;
  const float s = (sx < sy) ? sx : sy;

  // translation offset of the animation (simplify centering)
  const int X0 = (int)(10.0f * s);
  const int Y0 = (int)(30.0f * s);
  const float SCROLL4_START = 400.0f * s;

  void reset() {
    ms0 = get_ticks_ms();
    anim_part = 0;

    angle1 = 3.2f;
    angle2 = 1.9f;
    angle3 = 0.9f;
    scale1 = 0.0f;
    scale2 = 0.4f;
    scale3 = 0.8f;
    scroll4 = SCROLL4_START;
    wav64_play(&music, 0);
  }

  reset();
  while (1) {
    mixer_try_play();

    // Calculate animation part:
    // 0: rotate dragon head
    // 1: rotate dragon body and tail, scale up
    // 2: scroll dragon logo
    // 3: fade out
    uint32_t tt = get_ticks_ms() - ms0;
    if (tt < 1000) anim_part = 0;
    else if (tt < 1500) anim_part = 1;
    else if (tt < 4000) anim_part = 2;
    else if (tt < 5000) anim_part = 3;
    else break;

    // Update animation parameters using quadratic ease-out
    angle1 -= angle1 * 0.04f; if (angle1 < 0.010f) angle1 = 0;
    if (anim_part >= 1) {
      angle2 -= angle2 * 0.06f; if (angle2 < 0.01f) angle2 = 0;
      angle3 -= angle3 * 0.06f; if (angle3 < 0.01f) angle3 = 0;
      scale2 -= scale2 * 0.06f; if (scale2 < 0.01f) scale2 = 0;
      scale3 -= scale3 * 0.06f; if (scale3 < 0.01f) scale3 = 0;
    }
    if (anim_part >= 2) {
      scroll4 -= scroll4 * 0.08f;
    }

    // Update colors for fade out effect
    color_t red = RED;
    color_t white = WHITE;
    if (anim_part >= 3) {
      red.a = 255 - (tt-4000) * 255 / 1000;
      white.a = 255 - (tt-4000) * 255 / 1000;
    }

    surface_t *fb = display_get();
    rdpq_attach_clear(fb, NULL);

    // To simulate the dragon jumping out, we scissor the head so that
    // it appears as it moves.
    if (angle1 > 1.0f) {
      // Initially, also scissor horizontally, 
      // so that the head tail is not visible on the right.
      rdpq_set_scissor(0, 0, X0 + (int)(300.0f * s), Y0 + (int)(240.0f * s));
    } else {
      rdpq_set_scissor(0, 0, W, Y0 + (int)(240.0f * s));
    }

    // Draw dragon head
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(1);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    rdpq_mode_combiner(RDPQ_COMBINER1((0,0,0,PRIM),(TEX0,0,PRIM,0)));
    rdpq_set_prim_color(red);
    rdpq_sprite_blit(d1, X0 + (int)(216.0f * s), Y0 + (int)(205.0f * s), &(rdpq_blitparms_t){
      .theta = angle1, .scale_x = (scale1+1) * s, .scale_y = (scale1+1) * s,
      .cx = 176, .cy = 171,
    });

    // Restore scissor to standard
    rdpq_set_scissor(0, 0, W, H);

    // Draw a black rectangle with alpha gradient, to cover the head tail
    rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
    rdpq_mode_dithering(DITHER_NOISE_NOISE);
    float vtx[4][6] = {
      //  x,    y,  r,g,b,a
      { X0 + (float)(0.0f   * s),   Y0 + (float)(180.0f * s), 0,0,0,0 },
      { X0 + (float)(200.0f * s),   Y0 + (float)(180.0f * s), 0,0,0,0 },
      { X0 + (float)(200.0f * s),   Y0 + (float)(240.0f * s), 0,0,0,1 },
      { X0 + (float)(0.0f   * s),   Y0 + (float)(240.0f * s), 0,0,0,1 },
    };
    rdpq_triangle(&TRIFMT_SHADE, vtx[0], vtx[1], vtx[2]);
    rdpq_triangle(&TRIFMT_SHADE, vtx[0], vtx[2], vtx[3]);

    if (anim_part >= 1) {
      // Draw dragon body and tail
      rdpq_set_mode_standard();
      rdpq_mode_alphacompare(1);
      rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
      rdpq_mode_combiner(RDPQ_COMBINER1((0,0,0,PRIM),(TEX0,0,PRIM,0)));

      // Fade them in
      color_t color = red;
      color.r *= 1-scale3; color.g *= 1-scale3; color.b *= 1-scale3;
      rdpq_set_prim_color(color);

      rdpq_sprite_blit(d2, X0 + (int)(246.0f * s), Y0 + (int)(230.0f * s), &(rdpq_blitparms_t){
        .theta = angle2, .scale_x = (1-scale2) * s, .scale_y = (1-scale2) * s,
        .cx = 145, .cy = 113,
      });

      rdpq_sprite_blit(d3, X0 + (int)(266.0f * s), Y0 + (int)(256.0f * s), &(rdpq_blitparms_t){
        .theta = -angle3, .scale_x = (1-scale3) * s, .scale_y = (1-scale3) * s,
        .cx = 91, .cy = 24,
      });
    }

    // Draw scrolling logo
    if (anim_part >= 2) {
      rdpq_set_prim_color(white);
      rdpq_sprite_blit(d4,
        X0 + (int)(161.0f * s) + (int)scroll4,
        Y0 + (int)(182.0f * s),
        &(rdpq_blitparms_t){ .scale_x = s, .scale_y = s }
      );
    }

    rdpq_detach_show();
  }

  wait_ms(500); // avoid immediate switch to next screen
  rspq_wait();
  sprite_free(d1);
  sprite_free(d2);
  sprite_free(d3);
  sprite_free(d4);
  // Stop the logo sound channel before closing the wav.
  mixer_ch_stop(0);
  wav64_close(&music);
  // Original logo behavior: close display so the main app can re-init cleanly.
  display_close();
}

void logo_t3d() {
  // Asset lives under assets/logos/tiny3d/
  sprite_t *logo = sprite_load("rom:/logos/tiny3d/t3d-logo.sprite");
  if (!logo) {
    // Fallback: don't hang the boot sequence if the asset is missing.
    rdpq_attach_clear(display_get(), NULL);
    rdpq_detach_show();
    wait_ms(500);
    display_close();
    return;
  }

  // Fade in -> hold -> fade out.
  const uint32_t FADE_IN_MS  = 500;
  const uint32_t HOLD_MS     = 2000;
  const uint32_t FADE_OUT_MS = 500;
  const uint32_t TOTAL_MS    = FADE_IN_MS + HOLD_MS + FADE_OUT_MS;

  // Center on screen, scaled down to fit.
  const int W = display_get_width();
  const int H = display_get_height();

  // Fit logo within a comfortable portion of the screen.
  const float maxW = (float)W * 0.70f;
  const float maxH = (float)H * 0.45f;
  float scale = 1.0f;
  if (logo->width > 0 && logo->height > 0) {
    const float sx = maxW / (float)logo->width;
    const float sy = maxH / (float)logo->height;
    scale = fminf(1.0f, fminf(sx, sy));
    if (scale < 0.01f) scale = 0.01f;
  }

  const int drawW = (int)((float)logo->width * scale);
  const int drawH = (int)((float)logo->height * scale);
  int x = (W - drawW) / 2;
  int y = (H - drawH) / 2;
  if (x < 0) x = 0;
  if (y < 0) y = 0;

  const uint32_t t0 = get_ticks_ms();
  while (1) {
    const uint32_t t = get_ticks_ms() - t0;
    if (t >= TOTAL_MS) break;

    uint8_t a = 255;
    if (t < FADE_IN_MS) {
      a = (uint8_t)((t * 255) / FADE_IN_MS);
    } else if (t < (FADE_IN_MS + HOLD_MS)) {
      a = 255;
    } else {
      const uint32_t u = t - (FADE_IN_MS + HOLD_MS);
      a = (uint8_t)(255 - ((u * 255) / FADE_OUT_MS));
    }

    rdpq_attach_clear(display_get(), NULL);
    rdpq_set_mode_standard();
    rdpq_mode_alphacompare(0);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
    // Modulate sprite with prim alpha for fade.
    rdpq_mode_combiner(RDPQ_COMBINER1((0,0,0,PRIM),(TEX0,0,PRIM,0)));
    rdpq_set_prim_color(RGBA32(255, 255, 255, a));

    rdpq_sprite_blit(logo, x, y, &(rdpq_blitparms_t){ .scale_x = scale, .scale_y = scale });
    rdpq_detach_show();
  }

  rspq_wait();
  sprite_free(logo);
  // Original logo behavior: close display so the main app can re-init cleanly.
  display_close();
}
