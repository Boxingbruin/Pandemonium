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
// Queries used by callers across the codebase.
// ----------------------------------------------------------------------------
CutsceneState cutscene_manager_get_state(void);
bool          cutscene_manager_is_active(void);
bool          cutscene_manager_is_dialog_active(void);
bool          cutscene_manager_is_skip_visible(void);

// Phase-2 trigger latch.
bool cutscene_manager_phase2_triggered(void);
void cutscene_manager_mark_phase2_triggered(void);

// ----------------------------------------------------------------------------
// Drawing helpers.
// ----------------------------------------------------------------------------
void cutscene_manager_draw_skip_overlay(void);
void cutscene_manager_draw_fog(void);

// Post-boss dialog.
typedef struct {
    const char *line1;
    const char *line2;
    float       holdSec;
} PostBossChat;

int                 cutscene_manager_post_boss_chat_count(void);
const PostBossChat *cutscene_manager_get_post_boss_chat(int idx);

#endif