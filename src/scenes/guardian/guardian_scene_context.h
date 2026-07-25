// TODO: This is temporary until we port over the model manager + scene controller, we also need memory management.
// Cutscenes need to be their own scenes.
// We dont currently have memory management because up until this point we rushed it all into one scene file.
// Ideally each cutscene will be its own scene, managing its own memory.  It would be too huge of a task to take that on
// with 0 memory management at the moment, so this is a stepping stone.

#ifndef SCENE_CONTEXT_H
#define SCENE_CONTEXT_H

#include <libdragon.h>
#include <t3d/t3dmodel.h>

#include "guardian_scene.h"
#include "../../game/boss/boss.h"
#include "../../utilities/general_utility.h"

/*
 * The optimized room remains owned by guardian_scene.c. These functions expose
 * its stable render passes to the existing cutscene files without copying raw
 * frozen-block state into SceneContext.
 */
void scene_draw_environment_walls(void);
void scene_draw_environment_floor(void);
void scene_draw_environment_niches_windows(void);
void scene_draw_environment_decals(void);
void scene_draw_environment_pillars_statue(void);
void scene_draw_environment_sunshafts(void);
void scene_draw_environment_fog_door(void);

typedef struct SceneContext {
    Boss *boss;

    float roomY;

    GameState *gameState;
    bool *screenTransition;

    // Persistent ceiling chains remain separate from the room replacement.
    T3DModel *chainsModel;
    rspq_block_t *chainsDpl;
    T3DMat4FP *chainsMatrix;

    // TODO: should be moved into the boss.c
    T3DModel *bossChainsGlowModel;
    rspq_block_t *bossChainsGlowDpl;
    T3DMat4FP *bossChainsGlowMatrix;
    ScrollParams *bossChainsGlowScrollParams;

    // Callbacks into scene.c.  TODO: there should be no callbacks in this file.
    void (*init_playing)(bool skippedCutscene);
    void (*finish_phase2_cutscene)(void);
} SceneContext;

#endif
