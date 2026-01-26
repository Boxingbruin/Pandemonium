#include "boss.h"
#include "boss_sfx.h"

#include "character.h"
#include "scene_sfx.h"

#include "game_time.h"

static bool bossAttackSfxPlayed = false; 

static float audioTimer = 0.0f;

MultiSfx bossComboAttack1Sfx[] = {
    { SCENE1_SFX_BOSS_SWING4, 0.0f, false },
    { SCENE1_SFX_BOSS_SWING4, 1.6f, false },
    { SCENE1_SFX_BOSS_LAND1, 3.2f, false },
};

MultiSfx bossFlipAttackSfx[] = { // Remove 2 seconds because of idle preparation being outside attack function loop.
    { SCENE1_SFX_BOSS_SWING4, 2.0f, false },
    { SCENE1_SFX_BOSS_SMASH2, 2.8f, false },
    { SCENE1_SFX_BOSS_LAND2, 3.5f, false },
};

MultiSfx bossJumpForwardSfx[] = {
    { SCENE1_SFX_BOSS_LAND2, 0.0f, false },
    { SCENE1_SFX_BOSS_SMASH3, 1.0f, false },
};

MultiSfx bossSlowAttackSfx[] = { // also called TrackingSlam
    { SCENE1_SFX_BOSS_STEP1, 0.8f, false },
    { SCENE1_SFX_BOSS_SMASH1, 2.5f, false },
};

static float get_distance_to_player(Boss *boss)
{
    float dx = character.pos[0] - boss->pos[0];
    float dz = character.pos[2] - boss->pos[2];
    return sqrtf(dx*dx + dz*dz);
}

static void reset_multi_sfx(MultiSfx *list, int count)
{
    for (int i = 0; i < count; i++) {
        list[i].played = false;
    }
}

void boss_reset_sfx()
{
    bossAttackSfxPlayed = false;
    audioTimer = 0.0f; // guards against early exit animations

    reset_multi_sfx(bossComboAttack1Sfx, 3);
    reset_multi_sfx(bossFlipAttackSfx, 3);
    reset_multi_sfx(bossJumpForwardSfx, 2);
    reset_multi_sfx(bossSlowAttackSfx, 2);
}

void boss_play_attack_sfx(Boss *boss, int sfxIndex, float audioTime)
{
    if (bossAttackSfxPlayed)
        return;

    if(audioTimer >= audioTime)
    {
        bossAttackSfxPlayed = true;
        audioTimer = 0.0f;

        audio_play_scene_sfx_dist(sfxIndex, 1.0f, get_distance_to_player(boss));
    }
    else
    {
        audioTimer += deltaTime;
    }
}

void boss_multi_attack_sfx(Boss *boss, MultiSfx *sfxList, int sfxCount)
{
    if (bossAttackSfxPlayed) 
        return;

    audioTimer += deltaTime;

    for (int i = 0; i < sfxCount; i++) 
    {
        if (sfxList[i].played) continue;

        if (audioTimer >= sfxList[i].triggerTime) 
        {
            audio_play_scene_sfx_dist(sfxList[i].sfxIndex, 1.0f, get_distance_to_player(boss));
            sfxList[i].played = true;
        }
    }

    // recompute completion after we flip played flags
    bool allPlayed = true;
    for (int i = 0; i < sfxCount; i++) 
    {
        if (!sfxList[i].played) 
        { 
            allPlayed = false; break; 
        }
    }

    if (allPlayed) 
    {
        bossAttackSfxPlayed = true;
        audioTimer = 0.0f;
    }
}