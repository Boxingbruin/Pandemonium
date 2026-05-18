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
#include <ctype.h>
#include <string.h>

#include "dev.h"
#include "dev/debug_draw.h"
#include "display_utility.h"
#include "character.h"
#include "scene.h"
#include "game_time.h"
#include "globals.h"
#include "general_utility.h"

// Shadow tuning (duplicated from boss.c for rendering alpha)
static const float BOSS_SHADOW_GROUND_Y = -1.0f;  // Match roomY floor level
static const float BOSS_JUMP_REF_HEIGHT = 120.0f;
static const float BOSS_SHADOW_BASE_ALPHA = 120.0f;

// Damage flash material tint cache: t3d sets prim color per material from the
// model asset, so an external rdpq_set_prim_color() gets clobbered. We mutate
// each material's primColor while flashing and restore on flash end.
static T3DMaterial** s_flashMats = NULL;
static color_t*      s_flashOrigPrim = NULL;
static uint8_t*      s_flashOrigFlags = NULL;
static int           s_flashMatCount = 0;
static const T3DModel* s_flashCachedModel = NULL;
static bool          s_flashTintApplied = false;

static void boss_flash_cache_init(const T3DModel* model) {
    if (s_flashCachedModel == model && s_flashMats) return;

    // Free any previous cache (model changed).
    if (s_flashMats)      { free(s_flashMats);      s_flashMats = NULL; }
    if (s_flashOrigPrim)  { free(s_flashOrigPrim);  s_flashOrigPrim = NULL; }
    if (s_flashOrigFlags) { free(s_flashOrigFlags); s_flashOrigFlags = NULL; }
    s_flashMatCount = 0;
    s_flashCachedModel = NULL;
    s_flashTintApplied = false;
    if (!model) return;

    int count = 0;
    T3DModelIter it = t3d_model_iter_create(model, T3D_CHUNK_TYPE_MATERIAL);
    while (t3d_model_iter_next(&it)) count++;
    if (count <= 0) return;

    s_flashMats      = (T3DMaterial**)malloc(sizeof(T3DMaterial*) * count);
    s_flashOrigPrim  = (color_t*)malloc(sizeof(color_t) * count);
    s_flashOrigFlags = (uint8_t*)malloc(sizeof(uint8_t) * count);
    if (!s_flashMats || !s_flashOrigPrim || !s_flashOrigFlags) {
        free(s_flashMats);      s_flashMats = NULL;
        free(s_flashOrigPrim);  s_flashOrigPrim = NULL;
        free(s_flashOrigFlags); s_flashOrigFlags = NULL;
        return;
    }

    int i = 0;
    it = t3d_model_iter_create(model, T3D_CHUNK_TYPE_MATERIAL);
    while (t3d_model_iter_next(&it)) {
        T3DMaterial* m = it.material;
        s_flashMats[i]      = m;
        s_flashOrigPrim[i]  = m->primColor;
        s_flashOrigFlags[i] = m->setColorFlags;
        i++;
    }
    s_flashMatCount = i;
    s_flashCachedModel = model;
}

// tintAmount in [0..1]: 0 = fully original color, 1 = fully red.
// Each material lerps from its own original primColor toward red, so the
// fade returns smoothly to per-material colors (e.g. yellow swords, red cloth)
// instead of routing through a shared white and popping back at the end.
static void boss_flash_apply_tint(float tintAmount) {
    if (!s_flashMats) return;
    if (tintAmount < 0.0f) tintAmount = 0.0f;
    if (tintAmount > 1.0f) tintAmount = 1.0f;
    const float ta = tintAmount;
    const float ka = 1.0f - tintAmount;
    // Muted dark red target so the flash reads as "hurt" without blowing out.
    const float tr = 100.0f, tg = 20.0f, tb = 20.0f;
    for (int i = 0; i < s_flashMatCount; i++) {
        T3DMaterial* m = s_flashMats[i];
        color_t o = s_flashOrigPrim[i];
        uint8_t r = (uint8_t)(o.r * ka + tr * ta);
        uint8_t g = (uint8_t)(o.g * ka + tg * ta);
        uint8_t b = (uint8_t)(o.b * ka + tb * ta);
        m->primColor = (color_t){ r, g, b, o.a };
        m->setColorFlags = s_flashOrigFlags[i] | 0b001;
    }
    s_flashTintApplied = true;
}

static void boss_flash_restore(void) {
    if (!s_flashMats || !s_flashTintApplied) return;
    for (int i = 0; i < s_flashMatCount; i++) {
        T3DMaterial* m = s_flashMats[i];
        m->primColor     = s_flashOrigPrim[i];
        m->setColorFlags = s_flashOrigFlags[i];
    }
    s_flashTintApplied = false;
}

ScrollDyn bossScrollDyn = {
    .xSpeed = 0.0f,
    .ySpeed = 30.0f,
    .scale  = 64.0f,
    .spr = NULL,
};

typedef struct {
    ScrollDyn* scroll;
    Boss* boss;
} BossDrawUserData;

// Hide any sword-related objects once the boss is dead.
// This is name-based and depends on object names embedded in the .t3dm.
static bool boss_filter_hide_swords_when_dead(void* userData, const T3DObject* obj) {
    BossDrawUserData* ud = (BossDrawUserData*)userData;
    Boss* boss = ud ? ud->boss : NULL;
    if (!boss) return true;
    if (boss->state != BOSS_STATE_DEAD) return true;
    if (!obj || !obj->name) return true;

    // Filter either by object name OR by material name.
    // In Tiny3D, each draw "object" has exactly one material, so this cleanly hides sub-materials
    // even if the source asset was a single mesh with multiple material slots.
    const char* objName = obj->name;
    const char* matName = (obj->material && obj->material->name) ? obj->material->name : NULL;

    // Specific Fast64 material names you showed (keep this list tight to avoid accidental hiding).
    if (matName) {
        // NOTE: intentionally do NOT hide "z_sword1" (main sword).
        if (strcmp(matName, "z_temp_swords") == 0 ||
            strcmp(matName, "z_temp_swords_decal") == 0) {
            return false;
        }
    }
    return true;
}

static void boss_scroll_dyn_cb_wrapper(void* userData, const T3DMaterial* material, rdpq_texparms_t* tp, rdpq_tile_t tile) {
    BossDrawUserData* ud = (BossDrawUserData*)userData;
    ScrollDyn* scroll = ud ? ud->scroll : NULL;
    scroll_dyn_cb(scroll, material, tp, tile);
}

void boss_draw_init(void)
{
    // This texture is only needed for the scrolling/fog material. If it isn't
    // available yet (e.g. different scene), keep it NULL and fall back to
    // regular drawing.
    if (!bossScrollDyn.spr) {
        bossScrollDyn.spr = sprite_load("rom:/boss_room/fog.i8.sprite");
    }
}

static void boss_draw_scrolling(Boss* boss)
{
    BossDrawUserData ud = {
        .scroll = &bossScrollDyn,
        .boss = boss,
    };

    // If the scrolling texture wasn't loaded, avoid the custom draw path.
    // This prevents startup crashes if the sprite isn't present in the current ROM.
    if (!bossScrollDyn.spr) {
        T3DSkeleton* skel = (T3DSkeleton*)boss->skeleton;
        t3d_matrix_set(boss->modelMat, true);
        t3d_model_draw_custom(boss->model, (T3DModelDrawConf){
            .userData     = &ud,
            .tileCb       = NULL,
            .filterCb     = boss_filter_hide_swords_when_dead,
            .dynTextureCb = NULL,
            .matrices = (skel && skel->bufferCount == 1)
              ? skel->boneMatricesFP
              : (const T3DMat4FP*)t3d_segment_placeholder(T3D_SEGMENT_SKELETON)
        });
        return;
    }

    T3DSkeleton* skel = (T3DSkeleton*)boss->skeleton;

    t3d_matrix_set(boss->modelMat, true);
    t3d_model_draw_custom(boss->model, (T3DModelDrawConf){
        .userData     = &ud,
        .tileCb       = NULL,
        .filterCb     = boss_filter_hide_swords_when_dead,
        .dynTextureCb = boss_scroll_dyn_cb_wrapper,
        .matrices = (skel && skel->bufferCount == 1)
          ? skel->boneMatricesFP
          : (const T3DMat4FP*)t3d_segment_placeholder(T3D_SEGMENT_SKELETON)
    });
}

// Draw only the shadow - should be called in a batched shadow pass with zbuf(false, false)
void boss_draw_shadow(Boss* boss) {
    if (!boss || !boss->visible) return;
    if (!boss->dpl_shadow || !boss->shadowMat) return;

    // Compute alpha like character: fade with height
    float h = boss->pos[1] - BOSS_SHADOW_GROUND_Y;
    if (h < 0.0f) h = 0.0f;
    float t = (BOSS_JUMP_REF_HEIGHT > 0.0f) ? (h / BOSS_JUMP_REF_HEIGHT) : 0.0f;
    if (t > 1.0f) t = 1.0f;
    float fade = 1.0f - t; fade *= fade;
    uint8_t a = (uint8_t)(BOSS_SHADOW_BASE_ALPHA * fade);

    if (a > 0) {
        rdpq_set_prim_color(RGBA32(0, 0, 0, a));
        t3d_matrix_set((T3DMat4FP*)boss->shadowMat, true);
        rspq_block_run((rspq_block_t*)boss->dpl_shadow);
    }
}

void boss_render_draw(Boss* boss) {
    if (!boss || !boss->visible) return;

    // Be defensive: render might be called before init is fully complete.
    if (!boss->model || !boss->modelMat) return;

    // Shadow is now drawn separately via boss_draw_shadow() in a batched pass
    // This avoids expensive mode changes per boss

    boss_flash_cache_init((const T3DModel*)boss->model);
    if (boss->damageFlashTimer > 0.0f) {
        // 0.25s window: solid red for the first half, then fade out over the second.
        float f = boss->damageFlashTimer / 0.25f;
        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;
        float tint = (f >= 0.5f) ? 1.0f : (f * 2.0f);
        boss_flash_apply_tint(tint);
    } else if (s_flashTintApplied) {
        boss_flash_restore();
    }

    boss_draw_scrolling(boss);
    
    // Draw sword attached to Hand-Right bone
    if (boss->handRightBoneIndex >= 0 && boss->swordDpl && boss->swordMatFP) {
        T3DSkeleton* skel = (T3DSkeleton*)boss->skeleton;
        if (skel) {
            // Push bone matrix, then sword's local transform matrix
            // t3d_matrix_push(&skel->boneMatricesFP[boss->handRightBoneIndex]); // double matrix push and pop, it's already in a push pop.
            //     t3d_matrix_push((T3DMat4FP*)boss->swordMatFP);
            //     rspq_block_run((rspq_block_t*)boss->swordDpl);
            // t3d_matrix_pop(2);
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
        flash = fminf(1.0f, boss->damageFlashTimer / 0.25f);
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
        "Lunge", "Power Jump", "Combo", "Combo Starter", "Tracking Slam", "Flip Attack", "Lunge Starter", "Smash", "Quick Attack", "Aerial Sword Barrage"
    };
    const char* stateName = (boss->state < 17) ? stateNames[boss->state] : "Unknown";
    
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

