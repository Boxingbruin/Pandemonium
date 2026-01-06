#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmodel.h>

#include "general_utility.h"
#include "game_time.h"

uint32_t seed = 12345;
static uint32_t rng_s[2] = {123456789, 362436069};  // must not both be 0

static float scrollOffset = 0.0f;

// Pseudo-random unsigned integer
void rng_seed(uint32_t a, uint32_t b) 
{
    rng_s[0] = a ? a : 1;
    rng_s[1] = b ? b : 2;
}

uint32_t rand_xorshift64(void)
{
    uint32_t s1 = rng_s[0];
    uint32_t s0 = rng_s[1];

    rng_s[0] = s0;
    s1 ^= s1 << 23;
    rng_s[1] = s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26);

    return rng_s[1] + s0;
}

uint32_t rand_custom_u32(void)
{
	seed = seed * 1664525 + 1013904223;
	return seed >> 16;
}

float rand_custom_float(void) 
{
    return (rand_custom_u32() >> 8) * (1.0f / 16777216.0f);
}

// Pseudo-random float between -1 and 1
float rand_custom_float_signed(void)
{
    return (rand_custom_float() * 2.0f) - 1.0f; // Adjust to [-1, 1]
}

// fast modulo
static inline float wrap_mod(float v, float scale) 
{
    const float inv = (scale == 64.0f) ? 0.015625f : 0.03125f;

    // trunc toward 0
    float q = (float)((int)(v * inv));
    v -= q * scale;
    if (v < 0.f) v += scale;
    return v;
}

surface_t sprite_to_surface(sprite_t* spr)
{
    tex_format_t fmt = sprite_get_format(spr);
    return surface_make_linear((void*)spr->data, fmt, spr->width, spr->height);
}

void dynamic_tex_cb(void* userData, const T3DMaterial* material, rdpq_texparms_t *tileParams, rdpq_tile_t tile) 
{
	if(tile != TILE0)return;

	surface_t* surface = (surface_t*)userData;

	rdpq_sync_tile();
	rdpq_mode_tlut(TLUT_NONE);
	rdpq_tex_upload(TILE0, surface, NULL);
}

void scroll_dyn_cb(void* userData, const T3DMaterial* material, rdpq_texparms_t* tp, rdpq_tile_t tile)
{
    if (tile != TILE0) return;

    ScrollDyn* s = (ScrollDyn*)userData;

    tp->s.translate = wrap_mod(scrollOffset * s->xSpeed, s->scale);
    tp->t.translate = wrap_mod(scrollOffset * s->ySpeed, s->scale);

    surface_t surf = sprite_to_surface(s->spr);

    rdpq_sync_tile();
    rdpq_mode_tlut(TLUT_NONE);
    rdpq_tex_upload(TILE0, &surf, tp);

}

void scroll_update(void)
{
	if (scrollOffset > SCROLL_LIMIT || scrollOffset < -SCROLL_LIMIT) 
		scrollOffset = 0.0f;

	scrollOffset += deltaTime;
}

void tile_scroll(void* userData, rdpq_texparms_t *tp, rdpq_tile_t tile) 
{
    if (tile != TILE0) return;

    ScrollParams* p = (ScrollParams*)userData;

    tp->s.translate = wrap_mod(scrollOffset * p->xSpeed, p->scale);
    tp->t.translate = wrap_mod(scrollOffset * p->ySpeed, p->scale);
}

void tile_double_scroll(void* userData, rdpq_texparms_t *tp, rdpq_tile_t tile) 
{
	ScrollParams* p = (ScrollParams*)userData;

	if (tile == TILE0)
	{ 
		tp->s.translate = wrap_mod(scrollOffset * p->xSpeed, p->scale);
		tp->t.translate = wrap_mod(scrollOffset * p->ySpeed, p->scale);
	}
	else if (tile == TILE1)
	{ 
		tp->s.translate = wrap_mod(scrollOffset * p->xSpeedTwo, p->scaleTwo);
		tp->t.translate = wrap_mod(scrollOffset * p->ySpeedTwo, p->scaleTwo);
	}
}

bool scroll_filter_tag(void* userData, const T3DObject* obj)
{
	ScrollCtx* ctx = (ScrollCtx*)userData;
	ctx->active = NULL;
	for (int i = 0; i < ctx->count; i++) {
		if (obj == ctx->pairs[i].obj) {
		ctx->active = ctx->pairs[i].sp;
		ctx->active->adv = 0;
		break;
		}
	}
	return true;
}

// Router: forwards existing tile_scroll with the active params
void tile_scroll_router(void* userData, rdpq_texparms_t* tileParams, rdpq_tile_t tile)
{
	ScrollCtx* ctx = (ScrollCtx*)userData;
	if (!ctx || !ctx->active) return;
	tile_scroll(ctx->active, tileParams, tile);
}

void free_if_not_null(void *ptr) 
{
	if (ptr != NULL) 
	{
		free(ptr);
		ptr = NULL;
	}
}