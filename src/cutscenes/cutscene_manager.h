#ifndef CUTSCENE_MANAGER_H
#define CUTSCENE_MANAGER_H

#include <stdbool.h>
#include <t3d/t3d.h>

// All cutscenes in the game, in roughly the order they play. CUTSCENE_NONE means
// gameplay (or title) is active and no cutscene is running.
typedef enum {
    CUTSCENE_NONE,

    // Phase 1 boss intro chain
    CUTSCENE_PHASE1_INTRO,
    CUTSCENE_PHASE1_CHAIN_CLOSEUP,
    CUTSCENE_PHASE1_SWORDS_CLOSEUP,
    CUTSCENE_PHASE1_FILLER,
    CUTSCENE_PHASE1_LOYALTY,
    CUTSCENE_PHASE1_FEAR,
    CUTSCENE_PHASE1_BREAK_CHAINS,
    CUTSCENE_PHASE1_FACE_ZOOM_OUT,
    CUTSCENE_PHASE1_INTRO_END,

    // Phase 2 transition chain
    CUTSCENE_PHASE2_INTRO,
    CUTSCENE_PHASE2_KNEEL,
    CUTSCENE_PHASE2_BLURB,
    CUTSCENE_PHASE2_MIND,
    CUTSCENE_PHASE2_SHACKLED_SUN,
    CUTSCENE_PHASE2_BURN,
    CUTSCENE_PHASE2_BNW,
    CUTSCENE_PHASE2_END,

    // After the boss is defeated
    CUTSCENE_POST_BOSS_RESTORED,
} CutsceneState;

// ----------------------------------------------------------------------------
// Lifecycle. Called from scene_init / scene_cleanup / scene_reset.
// ----------------------------------------------------------------------------
void cutscene_manager_init(void);
void cutscene_manager_cleanup(void);
void cutscene_manager_reset(void);

// ----------------------------------------------------------------------------
// Queries used by callers across the codebase (scene.c, character.c, FMV, etc.)
// ----------------------------------------------------------------------------
CutsceneState cutscene_manager_get_state(void);
bool          cutscene_manager_is_active(void);
bool          cutscene_manager_is_dialog_active(void);
bool          cutscene_manager_is_skip_visible(void);

// Phase-2 trigger latch (boss AI checks this before kicking off the phase 2 cutscene)
bool cutscene_manager_phase2_triggered(void);
void cutscene_manager_mark_phase2_triggered(void);

// ----------------------------------------------------------------------------
// Drawing: the bits that are cleanly separable from scene.c's world render.
// scene.c keeps `scene_draw_cutscene()` (it renders the same room geometry
// as gameplay) and calls these helpers as needed.
// ----------------------------------------------------------------------------
// A-button + "skip" overlay. Drawn for both in-engine cutscenes and FMV playback.
void cutscene_manager_draw_skip_overlay(void);
// Per-cutscene fog ranges.
void cutscene_manager_draw_fog(void);

// ----------------------------------------------------------------------------
// Chain-break asset (PHASE1_BREAK_CHAINS). The model + skeleton + animation
// is cutscene-exclusive, so the manager owns its lifecycle and per-frame tick.
// scene.c renders it during the break-chains cutscene.
// ----------------------------------------------------------------------------
void cutscene_manager_chain_break_tick(float dt);
void cutscene_manager_chain_break_draw(void);

// Dialog string accessors used by the cutscene init in scene.c.
// (The arrays live in cutscene_manager.c since they are cutscene-only.)
const char *cutscene_manager_get_phase1_dialog(int idx);
const char *cutscene_manager_get_phase2_dialog(int idx);

#endif
