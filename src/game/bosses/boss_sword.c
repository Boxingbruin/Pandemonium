/*
 * boss_sword.c
 *
 * Boss sword module — owns the detached-sword resources and rendering.
 * The throw choreography lives in boss_attacks.c and drives this module
 * through boss_sword_release / boss_sword_land / boss_sword_reattach.
 */

#include "boss_sword.h"

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmath.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3dmodel.h>
#include <math.h>
#include <stdlib.h>

#include "globals.h"     // MODEL_SCALE
#include "game_math.h"   // mat4fp_mul_point_f32_row3_colbasis

// --------------------------------------------------------------------------
// Resource lifecycle
// --------------------------------------------------------------------------
void boss_sword_init(Boss* boss) {
    if (!boss) return;

    // Separate sword model used while the sword is detached from the hand.
    T3DModel* swordModel = t3d_model_load("rom:/boss/bossSword.t3dm");
    boss->swordModel = swordModel;

    rspq_block_begin();
    t3d_model_draw(swordModel);
    boss->swordDpl = rspq_block_end();

    // Local transform relative to the hand bone (kept for completeness; the
    // bone-attached visual is actually the baked z_sword1 mesh in the boss model).
    T3DMat4FP* swordMatFP = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_from_srt_euler(swordMatFP,
        (float[3]){1.0f, 1.0f, 1.0f},
        (float[3]){0.0f, 0.0f, 0.0f},
        (float[3]){0.0f, 0.0f, 0.0f});
    boss->swordMatFP = swordMatFP;

    // World-space matrix for the in-flight / stuck sword.
    T3DMat4FP* swordFlightMatFP = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_identity(swordFlightMatFP);
    boss->swordFlightMatFP = swordFlightMatFP;

    // Chain strip matrix. The chain mesh itself is an optional asset; until one
    // is provided (boss->chainModel / chainDpl), the chain simply isn't drawn.
    T3DMat4FP* chainMatFP = malloc_uncached(sizeof(T3DMat4FP));
    t3d_mat4fp_identity(chainMatFP);
    boss->chainMatFP = chainMatFP;
    boss->chainModel = NULL;
    boss->chainDpl   = NULL;

    boss_sword_reset(boss);
}

void boss_sword_reset(Boss* boss) {
    if (!boss) return;
    boss->swordDrawMode   = BOSS_SWORD_DRAW_ATTACHED;
    boss->swordSpinYaw    = 0.0f;
    boss->swordStuckPitch = 0.0f;
    boss->swordStuckYaw   = 0.0f;
    boss->swordStuckRoll  = 0.0f;
}

void boss_sword_free(Boss* boss) {
    if (!boss) return;

    if (boss->swordModel) {
        t3d_model_free((T3DModel*)boss->swordModel);
        boss->swordModel = NULL;
    }
    if (boss->swordDpl) {
        rspq_wait();
        rspq_block_free((rspq_block_t*)boss->swordDpl);
        boss->swordDpl = NULL;
    }
    if (boss->swordMatFP) {
        rspq_wait();
        free_uncached(boss->swordMatFP);
        boss->swordMatFP = NULL;
    }
    if (boss->swordFlightMatFP) {
        rspq_wait();
        free_uncached((T3DMat4FP*)boss->swordFlightMatFP);
        boss->swordFlightMatFP = NULL;
    }
    if (boss->chainMatFP) {
        rspq_wait();
        free_uncached((T3DMat4FP*)boss->chainMatFP);
        boss->chainMatFP = NULL;
    }
    if (boss->chainDpl) {
        rspq_wait();
        rspq_block_free((rspq_block_t*)boss->chainDpl);
        boss->chainDpl = NULL;
    }
    if (boss->chainModel) {
        t3d_model_free((T3DModel*)boss->chainModel);
        boss->chainModel = NULL;
    }
}

// --------------------------------------------------------------------------
// Queries
// --------------------------------------------------------------------------
bool boss_sword_is_attached(const Boss* boss) {
    return boss && boss->swordDrawMode == BOSS_SWORD_DRAW_ATTACHED;
}

// --------------------------------------------------------------------------
// Throw transitions (called by the attack handler)
// --------------------------------------------------------------------------
void boss_sword_release(Boss* boss) {
    if (!boss) return;

    T3DSkeleton* skel = (T3DSkeleton*)boss->skeleton;
    if (skel && boss->modelMat && boss->handRightBoneIndex >= 0) {
        const T3DMat4FP* B = &skel->boneMatricesFP[boss->handRightBoneIndex];
        const T3DMat4FP* M = (const T3DMat4FP*)boss->modelMat;
        const float handLocal[3] = { 0.0f, 0.0f, 0.0f };
        float handModel[3];
        mat4fp_mul_point_f32_row3_colbasis(B, handLocal,  handModel);
        mat4fp_mul_point_f32_row3_colbasis(M, handModel, boss->swordThrowReleasePos);
    } else {
        boss->swordThrowReleasePos[0] = boss->pos[0];
        boss->swordThrowReleasePos[1] = boss->pos[1] + 40.0f;
        boss->swordThrowReleasePos[2] = boss->pos[2];
    }

    // Start the projectile exactly at the unparented (hand) world position.
    boss->swordProjectilePos[0] = boss->swordThrowReleasePos[0];
    boss->swordProjectilePos[1] = boss->swordThrowReleasePos[1];
    boss->swordProjectilePos[2] = boss->swordThrowReleasePos[2];

    // Re-aim at the player's CURRENT position at the moment of release (the
    // target captured at attack-selection is stale by the windup + freeze).
    // Land a little PAST the player along the throw direction and at GROUND
    // level: the boss reels itself to the sword and slams, so a landing spot
    // behind the player makes the slide-in pass through them.
    const float BEHIND_DIST = 35.0f;  // how far past the player the sword lands
    float adx = character.pos[0] - boss->swordThrowReleasePos[0];
    float adz = character.pos[2] - boss->swordThrowReleasePos[2];
    float alen = sqrtf(adx*adx + adz*adz);
    float adirX = (alen > 0.001f) ? (adx / alen) : 0.0f;
    float adirZ = (alen > 0.001f) ? (adz / alen) : 0.0f;
    boss->swordThrowTargetPos[0] = character.pos[0] + adirX * BEHIND_DIST;
    boss->swordThrowTargetPos[1] = character.pos[1];  // ground level (player's feet)
    boss->swordThrowTargetPos[2] = character.pos[2] + adirZ * BEHIND_DIST;

    // Orient the sword along the throw direction and hold that pose for the
    // whole flight (no spin — a spinning flat blade reads as edge-on/invisible).
    // The model's blade points along its local -X (tip away from the hilt), so
    // we aim the -X axis down the flight path: +PI yaw and flipped pitch versus
    // aiming +X. Without this the sword flies hilt-first ("backwards").
    float fdx = boss->swordThrowTargetPos[0] - boss->swordThrowReleasePos[0];
    float fdy = boss->swordThrowTargetPos[1] - boss->swordThrowReleasePos[1];
    float fdz = boss->swordThrowTargetPos[2] - boss->swordThrowReleasePos[2];
    float fxz = sqrtf(fdx*fdx + fdz*fdz);
    boss->swordStuckYaw   = atan2f(fdz, fdx) + T3D_PI;
    boss->swordStuckPitch = atan2f(fdy, (fxz > 0.001f) ? fxz : 0.001f);
    boss->swordStuckRoll  = 0.0f;
    boss->swordSpinYaw    = 0.0f;

    boss->swordDrawMode = BOSS_SWORD_DRAW_IN_FLIGHT;
}

void boss_sword_land(Boss* boss) {
    if (!boss) return;
    boss->swordProjectilePos[0] = boss->swordThrowTargetPos[0];
    boss->swordProjectilePos[1] = boss->swordThrowTargetPos[1];
    boss->swordProjectilePos[2] = boss->swordThrowTargetPos[2];
    boss->swordDrawMode = BOSS_SWORD_DRAW_HIDDEN;
}

void boss_sword_reattach(Boss* boss) {
    if (!boss) return;
    boss->swordDrawMode = BOSS_SWORD_DRAW_ATTACHED;
}

// --------------------------------------------------------------------------
// Rendering
// --------------------------------------------------------------------------
// Bone-attached draw. Called from boss_render_draw, INSIDE the boss's matrix
// slot (boss->modelMat is the active matrix), so we push the hand bone + local
// offset on top: modelMat * handBone * swordLocal. The hand bone carries the
// armature scale, so the sword comes out the right size automatically.
void boss_sword_draw(Boss* boss) {
    if (!boss) return;
    if (boss->handRightBoneIndex < 0 || !boss->swordDpl) return;
    if (boss->swordDrawMode != BOSS_SWORD_DRAW_ATTACHED) return;

    if (boss->skeleton && boss->swordMatFP) {
        T3DSkeleton* skel = (T3DSkeleton*)boss->skeleton;
        t3d_matrix_push(&skel->boneMatricesFP[boss->handRightBoneIndex]);
        t3d_matrix_push((T3DMat4FP*)boss->swordMatFP);
        rspq_block_run((rspq_block_t*)boss->swordDpl);
        t3d_matrix_pop(2);
    }
}

// Compute the effective world scale of the sword by measuring how far a 1-unit
// offset along the hand bone maps in world space. The separate bossSword model
// and the baked z_sword1 are the same ~17-unit mesh; the baked one is blown up
// by the armature scale, so we reproduce that here.
static float boss_sword_world_scale(const Boss* boss) {
    float scale = MODEL_SCALE;
    if (boss->skeleton && boss->modelMat && boss->handRightBoneIndex >= 0) {
        T3DSkeleton* skel = (T3DSkeleton*)boss->skeleton;
        const T3DMat4FP* B = &skel->boneMatricesFP[boss->handRightBoneIndex];
        const T3DMat4FP* M = (const T3DMat4FP*)boss->modelMat;
        float a_m[3], b_m[3], a_w[3], b_w[3];
        mat4fp_mul_point_f32_row3_colbasis(B, (float[3]){0.0f, 0.0f, 0.0f}, a_m);
        mat4fp_mul_point_f32_row3_colbasis(B, (float[3]){1.0f, 0.0f, 0.0f}, b_m);
        mat4fp_mul_point_f32_row3_colbasis(M, a_m, a_w);
        mat4fp_mul_point_f32_row3_colbasis(M, b_m, b_w);
        float sdx = b_w[0]-a_w[0], sdy = b_w[1]-a_w[1], sdz = b_w[2]-a_w[2];
        float s = sqrtf(sdx*sdx + sdy*sdy + sdz*sdz);
        if (s > 0.001f) scale = s;
    }
    return scale;
}

// World-space blade segment of the flying sword (along the flight direction),
// centred on swordProjectilePos. Used for the in-flight hitbox — the sword's
// reach is along its blade.
bool boss_sword_flight_segment(const Boss* boss, float outBase[3], float outTip[3]) {
    if (!boss || boss->swordDrawMode != BOSS_SWORD_DRAW_IN_FLIGHT) return false;

    float dx = boss->swordThrowTargetPos[0] - boss->swordThrowReleasePos[0];
    float dy = boss->swordThrowTargetPos[1] - boss->swordThrowReleasePos[1];
    float dz = boss->swordThrowTargetPos[2] - boss->swordThrowReleasePos[2];
    float len = sqrtf(dx*dx + dy*dy + dz*dz);
    if (len < 0.001f) return false;
    float dirx = dx/len, diry = dy/len, dirz = dz/len;

    float half = boss_sword_world_scale(boss) * 8.0f;

    outTip[0]  = boss->swordProjectilePos[0] + dirx * half;
    outTip[1]  = boss->swordProjectilePos[1] + diry * half;
    outTip[2]  = boss->swordProjectilePos[2] + dirz * half;
    outBase[0] = boss->swordProjectilePos[0] - dirx * half;
    outBase[1] = boss->swordProjectilePos[1] - diry * half;
    outBase[2] = boss->swordProjectilePos[2] - dirz * half;
    return true;
}

// World-space segment for the sword TRAIL. Unlike the hitbox, this must be
// PERPENDICULAR to the flight direction: the trail ribbon is swept between
// consecutive base->tip segments, so a segment running along the velocity
// would produce colinear samples and a zero-width (invisible) ribbon. We use
// the horizontal perpendicular to the velocity, which sweeps a visible streak
// as the sword translates — the same geometry that makes the melee swing trail
// work (blade perpendicular to the tip's motion).
bool boss_sword_flight_trail_segment(const Boss* boss, float outBase[3], float outTip[3]) {
    if (!boss || boss->swordDrawMode != BOSS_SWORD_DRAW_IN_FLIGHT) return false;

    float dx = boss->swordThrowTargetPos[0] - boss->swordThrowReleasePos[0];
    float dz = boss->swordThrowTargetPos[2] - boss->swordThrowReleasePos[2];
    float len = sqrtf(dx*dx + dz*dz);
    float dirx = (len > 0.001f) ? dx/len : 1.0f;
    float dirz = (len > 0.001f) ? dz/len : 0.0f;

    // perp = normalize(dir x up) for up=(0,1,0) -> (-dirz, 0, dirx), already unit.
    float px = -dirz, pz = dirx;

    float half = boss_sword_world_scale(boss) * 7.0f;

    outTip[0]  = boss->swordProjectilePos[0] + px * half;
    outTip[1]  = boss->swordProjectilePos[1];
    outTip[2]  = boss->swordProjectilePos[2] + pz * half;
    outBase[0] = boss->swordProjectilePos[0] - px * half;
    outBase[1] = boss->swordProjectilePos[1];
    outBase[2] = boss->swordProjectilePos[2] - pz * half;
    return true;
}

// Thrown-sword draw. Called from scene.c at the ROOT matrix level (after the
// boss's matrix slot is popped), the same place msa_draw_visuals renders its
// world-space swords. This is why it uses set(..., true): at root the parent is
// identity, so the sword renders at its own world-space matrix. Drawing this
// inside the boss's matrix slot (with the boss model matrix active) was the bug
// that anchored the sword to the boss instead of placing it in the world.
void boss_sword_draw_thrown(Boss* boss) {
    if (!boss || !boss->swordDpl) return;

    const bool detached = (boss->swordDrawMode == BOSS_SWORD_DRAW_IN_FLIGHT
                        || boss->swordDrawMode == BOSS_SWORD_DRAW_STUCK);

    if (detached && boss->swordFlightMatFP) {
        float s = boss_sword_world_scale(boss);
        // Fixed orientation aimed along the throw direction (captured at release).
        t3d_mat4fp_from_srt_euler(
            (T3DMat4FP*)boss->swordFlightMatFP,
            (float[3]){ s, s, s },
            (float[3]){ boss->swordStuckPitch, boss->swordStuckYaw, boss->swordStuckRoll },
            (float[3]){
                boss->swordProjectilePos[0],
                boss->swordProjectilePos[1],
                boss->swordProjectilePos[2]
            }
        );
        t3d_matrix_push_pos(1);
        t3d_matrix_set((T3DMat4FP*)boss->swordFlightMatFP, true);
        rspq_block_run((rspq_block_t*)boss->swordDpl);
        t3d_matrix_pop(1);
    }

    // Chain strip between the hand bone and the sword while detached. Only drawn
    // when a chain mesh exists (boss->chainDpl is NULL in V1).
    if (boss->chainDpl && boss->chainMatFP && boss->modelMat
        && boss->skeleton && boss->handRightBoneIndex >= 0
        && boss->swordDrawMode != BOSS_SWORD_DRAW_ATTACHED)
    {
        T3DSkeleton* skel = (T3DSkeleton*)boss->skeleton;
        const T3DMat4FP* B = &skel->boneMatricesFP[boss->handRightBoneIndex];
        const T3DMat4FP* M = (const T3DMat4FP*)boss->modelMat;

        float handLocal[3] = { 0.0f, 0.0f, 0.0f };
        float handModel[3], handWorld[3];
        mat4fp_mul_point_f32_row3_colbasis(B, handLocal,  handModel);
        mat4fp_mul_point_f32_row3_colbasis(M, handModel, handWorld);

        float dx = boss->swordProjectilePos[0] - handWorld[0];
        float dy = boss->swordProjectilePos[1] - handWorld[1];
        float dz = boss->swordProjectilePos[2] - handWorld[2];
        float lenXZ  = sqrtf(dx*dx + dz*dz);
        float length = sqrtf(dx*dx + dy*dy + dz*dz);
        if (length > 0.001f) {
            float midX = (handWorld[0] + boss->swordProjectilePos[0]) * 0.5f;
            float midY = (handWorld[1] + boss->swordProjectilePos[1]) * 0.5f;
            float midZ = (handWorld[2] + boss->swordProjectilePos[2]) * 0.5f;
            float yaw   =  atan2f(dz, dx);
            float pitch = -atan2f(dy, lenXZ > 0.001f ? lenXZ : 0.001f);
            const float thickness = 1.5f;
            t3d_mat4fp_from_srt_euler(
                (T3DMat4FP*)boss->chainMatFP,
                (float[3]){ length, thickness, thickness },
                (float[3]){ 0.0f, yaw, pitch },
                (float[3]){ midX, midY, midZ }
            );
            t3d_matrix_push_pos(1);
            t3d_matrix_set((T3DMat4FP*)boss->chainMatFP, true);
            rspq_block_run((rspq_block_t*)boss->chainDpl);
            t3d_matrix_pop(1);
        }
    }
}
