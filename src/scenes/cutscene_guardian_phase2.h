#ifndef CUTSCENE_GUARDIAN_PHASE2_H
#define CUTSCENE_GUARDIAN_PHASE2_H

#include <stdbool.h>

#include <t3d/t3d.h>

#include "../managers/cutscene_manager.h"
#include "scene_context.h"

bool cutscene_guardian_phase2_handles(CutsceneState state);

void cutscene_guardian_phase2_load(void);
void cutscene_guardian_phase2_unload(void);

void cutscene_guardian_phase2_enter(SceneContext *ctx, CutsceneState state);
void cutscene_guardian_phase2_update(SceneContext *ctx, float dt);
void cutscene_guardian_phase2_draw(SceneContext *ctx, T3DViewport *viewport);
void cutscene_guardian_phase2_draw_fog(void);

void cutscene_guardian_phase2_skip(SceneContext *ctx);

#endif