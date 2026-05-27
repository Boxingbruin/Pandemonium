#include "boss_ground_crush.h"

#include <stdbool.h>
#include <string.h>

#include <libdragon.h>
#include <t3d/t3d.h>
#include <rdpq.h>
#include <rspq.h>

typedef struct {
    bool active;

    float pos[3];
    float age;
    float life;
    float scale;

    T3DMat4FP *mat;
} BossGroundCrush;

static BossGroundCrush groundCrush;

static T3DModel *groundCrushModel = NULL;
static rspq_block_t *dplGroundCrush = NULL;

void boss_ground_crush_init(void)
{
    memset(&groundCrush, 0, sizeof(groundCrush));

    groundCrush.mat = malloc_uncached(sizeof(T3DMat4FP));

    groundCrushModel = t3d_model_load("rom:/ground_crush/ground_crush.t3dm");

    rspq_block_begin();
        t3d_model_draw(groundCrushModel);
    dplGroundCrush = rspq_block_end();
}

void boss_ground_crush_reset(void)
{
    groundCrush.active = false;
    groundCrush.age = 0.0f;
    groundCrush.life = 0.0f;
    groundCrush.scale = 1.0f;

    groundCrush.pos[0] = 0.0f;
    groundCrush.pos[1] = 0.0f;
    groundCrush.pos[2] = 0.0f;
}

void boss_ground_crush_spawn(float x, float y, float z)
{
    groundCrush.active = true;

    groundCrush.pos[0] = x;
    groundCrush.pos[1] = y;
    groundCrush.pos[2] = z;

    groundCrush.age = 0.0f;
    groundCrush.life = 3.0f;
    groundCrush.scale = 1.0f;
}

void boss_ground_crush_update(float dt)
{
    if (!groundCrush.active) return;

    groundCrush.age += dt;

    if (groundCrush.age >= groundCrush.life) {
        groundCrush.active = false;
    }
}

void boss_ground_crush_draw(void)
{
    if (!groundCrush.active) return;
    if (!groundCrush.mat || !dplGroundCrush) return;

    float alpha = 1.0f;

    const float fadeTime = 0.5f;
    const float fadeStart = groundCrush.life - fadeTime;

    if (groundCrush.age >= fadeStart) {
        alpha = (groundCrush.life - groundCrush.age) / fadeTime;

        if (alpha < 0.0f) alpha = 0.0f;
        if (alpha > 1.0f) alpha = 1.0f;
    }

    uint8_t a = (uint8_t)(alpha * 255.0f);

    t3d_mat4fp_from_srt_euler(
        groundCrush.mat,
        (float[3]){ groundCrush.scale, groundCrush.scale, groundCrush.scale },
        (float[3]){ 0.0f, 0.0f, 0.0f },
        (float[3]){ groundCrush.pos[0], groundCrush.pos[1], groundCrush.pos[2] }
    );

    rdpq_set_prim_color(RGBA32(255, 255, 255, a));

    t3d_matrix_set(groundCrush.mat, true);
    rspq_block_run(dplGroundCrush);
}

void boss_ground_crush_cleanup(void)
{
    if (groundCrush.mat) {
        free_uncached(groundCrush.mat);
        groundCrush.mat = NULL;
    }

    if (dplGroundCrush) {
        rspq_block_free(dplGroundCrush);
        dplGroundCrush = NULL;
    }

    if (groundCrushModel) {
        t3d_model_free(groundCrushModel);
        groundCrushModel = NULL;
    }

    memset(&groundCrush, 0, sizeof(groundCrush));
}