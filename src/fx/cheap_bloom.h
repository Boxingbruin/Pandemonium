#ifndef CHEAP_BLOOM_H
#define CHEAP_BLOOM_H

#include <stdbool.h>
#include <stdint.h>
#include <surface.h>

/*
 * Cheap bloom pass.
 *
 * This is intentionally not the Tiny3D/RSPFX HDR bloom path. It is an RDPQ-only,
 * in-place framebuffer post-process:
 *
 *   active 320x240 RGBA32 framebuffer
 *     -> downscale to 80x60 RGBA32
 *     -> stretch/blend 80x60 back over the same framebuffer
 *
 * Requirements:
 *   - display size: 320x240
 *   - framebuffer: RGBA32 / 32-bit
 *   - caller must pass the currently active display framebuffer surface
 *
 * Call after all scene geometry has been drawn, before detaching/showing frame.
 */

void cheap_bloom_init(void);
void cheap_bloom_cleanup(void);

void cheap_bloom_set_enabled(bool enabled);
bool cheap_bloom_is_enabled(void);

void cheap_bloom_set_alpha(uint8_t alpha);
uint8_t cheap_bloom_get_alpha(void);

void cheap_bloom_apply(surface_t *framebuffer);

#endif
