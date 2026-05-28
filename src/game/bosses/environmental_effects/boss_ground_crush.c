#include "boss_ground_crush.h"

#include <stdbool.h>
#include <string.h>

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmodel.h>

#include "../../../utilities/globals.h"

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

void boss_ground_crush_init(void)
{
    memset(&groundCrush, 0, sizeof(groundCrush));

    groundCrush.mat = malloc_uncached(sizeof(T3DMat4FP));
    groundCrush.scale = MODEL_SCALE;

    groundCrushModel = t3d_model_load("rom:/boss/ground_crush.t3dm");
}

void boss_ground_crush_reset(void)
{
    groundCrush.active = false;

    groundCrush.pos[0] = 0.0f;
    groundCrush.pos[1] = 0.0f;
    groundCrush.pos[2] = 0.0f;

    groundCrush.age = 0.0f;
    groundCrush.life = 0.0f;
    groundCrush.scale = MODEL_SCALE;
}

void boss_ground_crush_spawn(float x, float y, float z)
{
    groundCrush.active = true;

    groundCrush.pos[0] = x;
    groundCrush.pos[1] = y;
    groundCrush.pos[2] = z;

    groundCrush.age = 0.0f;
    groundCrush.life = 3.0f;
    groundCrush.scale = MODEL_SCALE;
}

void boss_ground_crush_update(float dt)
{
    if (!groundCrush.active) return;

    groundCrush.age += dt;

    if (groundCrush.age > groundCrush.life) {
        groundCrush.active = false;
    }
}

void boss_ground_crush_draw(void)
{
    if (!groundCrush.active) return;
    if (!groundCrush.mat || !groundCrushModel) return;

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

    t3d_matrix_set(groundCrush.mat, true);

    rdpq_set_prim_color(RGBA32(255, 255, 255, a));

    t3d_model_draw(groundCrushModel);
}

void boss_ground_crush_cleanup(void)
{
    if (groundCrush.mat) {
        free_uncached(groundCrush.mat);
        groundCrush.mat = NULL;
    }

    if (groundCrushModel) {
        t3d_model_free(groundCrushModel);
        groundCrushModel = NULL;
    }

    memset(&groundCrush, 0, sizeof(groundCrush));
}