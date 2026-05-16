// Internal access to cutscene state for the heavy state-machine functions that
// still live in scene.c (scene_init_cutscene, scene_cutscene_update,
// scene_draw_cutscene). These routines mutate cutscene state while continuing
// to drive scene-owned geometry, camera, audio, etc. — moving them fully would
// require dozens of new scene.h accessors. Until that's untangled, scene.c
// gets direct access here. Outside callers should use the public API in
// cutscene_manager.h.

#ifndef CUTSCENE_MANAGER_INTERNAL_H
#define CUTSCENE_MANAGER_INTERNAL_H

#include "cutscene_manager.h"
#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>

extern CutsceneState cutsceneState;
extern float         cutsceneTimer;
extern float         cutsceneCameraTimer;

extern T3DVec3 cutsceneCamPosStart;
extern T3DVec3 cutsceneCamPosEnd;

extern bool cutsceneDialogActive;
extern bool phase2CutsceneTriggered;

extern bool skipButtonVisible;
extern bool lastCutsceneAPressed;

extern int  bossPostDefeatDialogStep;

// PHASE1_BREAK_CHAINS asset.
extern T3DModel*     cutsceneChainBreakModel;
extern rspq_block_t* cutsceneChainBreakDpl;
extern T3DMat4FP*    cutsceneChainBreakMatrix;
extern T3DSkeleton*  cutsceneChainBreakSkeleton;
extern T3DAnim**     cutsceneChainBreakAnimations;

#endif
