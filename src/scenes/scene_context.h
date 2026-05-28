// scene_context.h

// TODO: This is temporary until we port over the model manager + scene controller, we also need memory management.
// Cutscenes need to be their own scenes.
// We dont currently have memory management because up until this point we rushed it all into one scene file.
// Ideally each cutscene will be its own scene, managing its own memory.  It would be too huge of a task to take that on
// with 0 memory management at the moment, so this is a stepping stone.

#ifndef SCENE_CONTEXT_H
#define SCENE_CONTEXT_H

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dmodel.h>

#include "scene.h"
#include "../game/bosses/boss.h"

typedef struct SceneContext {
    Boss *boss;

    GameState *gameState;
    bool *bossActivated;

    float roomY;

    // Core room assets borrowed from scene.
    T3DModel *mapModel;
    rspq_block_t *mapDpl;
    T3DMat4FP *mapMatrix;

    T3DModel *windowsModel;
    rspq_block_t *windowsDpl;
    T3DMat4FP *windowsMatrix;

    T3DModel *roomFloorModel;
    rspq_block_t *roomFloorDpl;
    T3DMat4FP *roomFloorMatrix;

    T3DModel *roomLedgeModel;
    rspq_block_t *roomLedgeDpl;
    T3DMat4FP *roomLedgeMatrix;

    T3DModel *pillarsModel;
    rspq_block_t *pillarsDpl;
    T3DMat4FP *pillarsMatrix;

    T3DModel *pillarsFrontModel;
    rspq_block_t *pillarsFrontDpl;
    T3DMat4FP *pillarsFrontMatrix;

    T3DModel *chainsModel;
    rspq_block_t *chainsDpl;
    T3DMat4FP *chainsMatrix;

    // Shared UI/runtime flags owned by scene.
    bool *screenTransition;
    bool *cutsceneDialogActive;
    bool *skipButtonVisible;

    float *bossUiIntro;
    float *playerUiIntro;

    bool *cameraLockOnActive;
    bool *pendingBossLoopMusic;
} SceneContext;

#endif