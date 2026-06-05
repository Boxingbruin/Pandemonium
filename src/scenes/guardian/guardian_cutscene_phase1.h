#ifndef CUTSCENE_GUARDIAN_PHASE1_H
#define CUTSCENE_GUARDIAN_PHASE1_H

#include <t3d/t3d.h>
#include "../../managers/cutscene_manager.h"

bool cutscene_guardian_phase1_handles(CutsceneState state);

void cutscene_guardian_phase1_load(void);
void cutscene_guardian_phase1_unload(void);

void cutscene_guardian_phase1_enter(SceneContext *ctx, CutsceneState state);
void cutscene_guardian_phase1_update(SceneContext *ctx, float dt);
void cutscene_guardian_phase1_draw(SceneContext *ctx, T3DViewport *viewport);
void cutscene_guardian_phase1_draw_fog(void);

void cutscene_guardian_phase1_skip(SceneContext *ctx);

#endif