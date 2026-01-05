#ifndef GENERAL_UTILITY_H
#define GENERAL_UTILITY_H

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmodel.h>

#define SCROLL_LIMIT 100000.0f
#define SCROLL_STEP 0.2f

typedef struct {
    sprite_t* spr;
    float xSpeed, ySpeed;
    float scale;
    float* offset;
} ScrollDyn;

typedef struct {
  float xSpeed;
  float ySpeed;
  float scale;
  float adv;

  float xSpeedTwo;
  float ySpeedTwo;
  float scaleTwo;
} ScrollParams;

typedef struct {
    int count;
    struct {
        const T3DObject* obj;
        ScrollParams*    sp;
    } *pairs;
    ScrollParams* active;
} ScrollCtx;

surface_t sprite_to_surface(sprite_t* spr);

void scroll_dyn_cb(void* userData, const T3DMaterial* material, rdpq_texparms_t* tp, rdpq_tile_t tile);
void dynamic_tex_cb(void* userData, const T3DMaterial* material, rdpq_texparms_t *tileParams, rdpq_tile_t tile);

void scroll_update(void);
void tile_scroll(void* userData, rdpq_texparms_t *tileParams, rdpq_tile_t tile);
void tile_double_scroll(void* user, rdpq_texparms_t *tp, rdpq_tile_t tile);

bool scroll_filter_tag(void* userData, const T3DObject* obj);
void tile_scroll_router(void* userData, rdpq_texparms_t* tileParams, rdpq_tile_t tile);

uint32_t rand_custom_u32(void);
float rand_custom_float(void);
float rand_custom_float_signed(void);

void free_if_not_null(void *ptr);

#endif