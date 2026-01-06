/*
 * boss_render.c
 * 
 * Render module - handles drawing and debug visualization
 * Read-only access to Boss state
 */

#include "boss_render.h"
#include "boss.h"

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3ddebug.h>
#include <t3d/t3dmodel.h>
#include <math.h>

#include "dev.h"
#include "dev/debug_draw.h"
#include "display_utility.h"
#include "character.h"
#include "scene.h"
#include "game_time.h"
#include "globals.h"
#include "general_utility.h"

ScrollDyn bossScrollDyn = {
    .xSpeed = 0.0f,
    .ySpeed = 30.0f,
    .scale  = 64.0f,
    .spr = NULL,
};

void boss_draw_init(void)
{
    bossScrollDyn.spr = sprite_load("rom:/boss_room/fog.i8.sprite");
}

static void boss_draw_scrolling(Boss* boss)
{
    T3DSkeleton* skel = (T3DSkeleton*)boss->skeleton;

    t3d_matrix_set(boss->modelMat, true);
    t3d_model_draw_custom(boss->model, (T3DModelDrawConf){
        .userData     = &bossScrollDyn,
        .tileCb       = NULL,
        .filterCb     = NULL,
        .dynTextureCb = scroll_dyn_cb,
        .matrices = (skel && skel->bufferCount == 1)
          ? skel->boneMatricesFP
          : (const T3DMat4FP*)t3d_segment_placeholder(T3D_SEGMENT_SKELETON)
    });
}

void boss_render_draw(Boss* boss) {
    if (!boss || !boss->visible) return;
    
    boss_draw_scrolling(boss);
    
    // Draw sword attached to Hand-Right bone
    if (boss->handRightBoneIndex >= 0 && boss->swordDpl && boss->swordMatFP) {
        T3DSkeleton* skel = (T3DSkeleton*)boss->skeleton;
        if (skel) {
            // Push bone matrix, then sword's local transform matrix
            t3d_matrix_push(&skel->boneMatricesFP[boss->handRightBoneIndex]); // double matrix push and pop, it's already in a push pop.
                t3d_matrix_push((T3DMat4FP*)boss->swordMatFP);
                rspq_block_run((rspq_block_t*)boss->swordDpl);
            t3d_matrix_pop(2);
        }
    }
}

void boss_render_debug(Boss* boss, void* viewport) {
    if (!boss || !viewport) return;
    
    T3DViewport* vp = (T3DViewport*)viewport;
    
    // Show health bar when boss is active
    if (boss->health <= 0 || !scene_is_boss_active() || scene_is_cutscene_active()) {
        return;
    }
    
    // Top health bar
    float ratio = boss->maxHealth > 0.0f ? fmaxf(0.0f, fminf(1.0f, boss->health / boss->maxHealth)) : 0.0f;
    float flash = 0.0f;
    if (boss->damageFlashTimer > 0.0f) {
        flash = fminf(1.0f, boss->damageFlashTimer / 0.3f);
        boss->damageFlashTimer -= deltaTime;
        if (boss->damageFlashTimer < 0.0f) boss->damageFlashTimer = 0.0f;
    }
    draw_boss_health_bar(boss->name, ratio, flash);
    
    if (!DEV_MODE || !debugDraw) {
        return;
    }
    
    // Display debug info
    float dx = character.pos[0] - boss->pos[0];
    float dy = character.pos[1] - boss->pos[1];
    float dz = character.pos[2] - boss->pos[2];
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    
    rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    int y = 48;
    int listSpacing = 12;
    
    const char* stateNames[] = {
        "Intro", "Neutral", "Chase", "Strafe", "Recover", "Stagger", "Dead",
        "Charge", "Power Jump", "Combo", "Combo Starter", "Roar Stomp", "Tracking Slam", "Flip Attack"
    };
    const char* stateName = (boss->state < 14) ? stateNames[boss->state] : "Unknown";
    
    rdpq_text_printf(NULL, FONT_UNBALANCED, 20, y, "Boss State: %s", stateName);
    y += listSpacing;
    rdpq_text_printf(NULL, FONT_UNBALANCED, 20, y, "Boss Dist: %.1f", dist);
    y += listSpacing;
    
    if (boss->attackNameDisplayTimer > 0.0f && boss->currentAttackName) {
        rdpq_text_printf(NULL, FONT_UNBALANCED, 20, y, "Attack: %s", boss->currentAttackName);
        y += listSpacing;
    }
    
    // Animation blending stats
    if (boss->isBlending) {
        y += listSpacing;
        rdpq_set_prim_color(RGBA32(0x39, 0xBF, 0x1F, 0xFF));
        rdpq_text_printf(NULL, FONT_UNBALANCED, 20, y, "Blending: ON");
        y += listSpacing;
        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
        rdpq_text_printf(NULL, FONT_UNBALANCED, 20, y, "Blend Factor: %.2f (%.0f%%)", 
                        boss->blendFactor, boss->blendFactor * 100.0f);
        y += listSpacing;
        rdpq_text_printf(NULL, FONT_UNBALANCED, 20, y, "Blend Timer: %.3fs / %.3fs", 
                        boss->blendTimer, boss->blendDuration);
    } else {
        y += listSpacing;
        rdpq_set_prim_color(RGBA32(0x66, 0x66, 0x66, 0xFF));
        rdpq_text_printf(NULL, FONT_UNBALANCED, 20, y, "Blending: OFF");
        rdpq_set_prim_color(RGBA32(255, 255, 255, 255));
    }
    
    // Draw boss targeting debug visualization
    if (scene_is_boss_active()) {
        T3DVec3 targetPos = {{boss->debugTargetingPos[0], boss->debugTargetingPos[1], boss->debugTargetingPos[2]}};
        debug_draw_sphere(vp, &targetPos, 4.0f, DEBUG_COLORS[5]);
        debug_draw_cross(vp, &targetPos, 4.0f, DEBUG_COLORS[5]);
    }
}

