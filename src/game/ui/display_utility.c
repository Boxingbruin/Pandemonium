#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3ddebug.h>
#include <string.h>
#include <math.h>

#include "globals.h"
#include "game_time.h"
#include "video_layout.h"

static int fadeBlackAlpha = 255; // Default alpha value for the rectangle
bool startScreenFade = false;
static sprite_t* bossHealthBarBackgroundSprite = NULL;
static bool bossHealthBarBackgroundLoadAttempted = false;
static sprite_t* bossHealthBarNameSprite = NULL;
static bool bossHealthBarNameLoadAttempted = false;
//static sprite_t* playerShieldHealthSprites[5] = { NULL, NULL, NULL, NULL, NULL };
//static bool playerShieldHealthSpritesLoadAttempted = false;
//static float playerShieldDeathAnimTime = 0.0f;

// UI intro progress values (0.0 = off-screen, 1.0 = fully visible)
static float boss_ui_intro = 1.0f;
static float player_ui_intro = 1.0f;

// Trailing indicators for each bar. After a change, the "loss" trail lingers
// above the current ratio (recent damage/use, shown yellow) and the "gain"
// trail lingers below it (recent heal/regen, shown as a white highlight).
#define TRAIL_HOLD_TIME 0.35f
#define TRAIL_DECAY_RATE 0.6f // ratio per second

typedef struct {
    float lossTrail;
    float gainTrail;
    float lastRatio;
    float lossHold;
    float gainHold;
} BarTrailState;

static BarTrailState playerHealthBarState  = { 1.0f, 1.0f, 1.0f, 0.0f, 0.0f };
static BarTrailState playerStaminaBarState = { 1.0f, 1.0f, 1.0f, 0.0f, 0.0f };
static BarTrailState bossHealthBarState    = { 1.0f, 1.0f, 1.0f, 0.0f, 0.0f };

static void update_bar_trails(float ratio, BarTrailState *s)
{
    if (ratio < s->lastRatio) {
        // Just lost — refresh loss hold, snap gain trail forward so a stale
        // healing highlight doesn't reappear behind new damage.
        s->lossHold = TRAIL_HOLD_TIME;
        if (s->gainTrail < ratio) s->gainTrail = ratio;
    } else if (ratio > s->lastRatio) {
        // Just gained — refresh gain hold, snap loss trail up to current.
        s->gainHold = TRAIL_HOLD_TIME;
        if (s->lossTrail < ratio) s->lossTrail = ratio;
    }
    s->lastRatio = ratio;

    // Clamp trails to valid sides of the current ratio.
    if (s->lossTrail < ratio) s->lossTrail = ratio;
    if (s->gainTrail > ratio) s->gainTrail = ratio;

    if (s->lossHold > 0.0f) {
        s->lossHold -= deltaTime;
        if (s->lossHold < 0.0f) s->lossHold = 0.0f;
    } else if (s->lossTrail > ratio) {
        s->lossTrail -= TRAIL_DECAY_RATE * deltaTime;
        if (s->lossTrail < ratio) s->lossTrail = ratio;
    }

    if (s->gainHold > 0.0f) {
        s->gainHold -= deltaTime;
        if (s->gainHold < 0.0f) s->gainHold = 0.0f;
    } else if (s->gainTrail < ratio) {
        s->gainTrail += TRAIL_DECAY_RATE * deltaTime;
        if (s->gainTrail > ratio) s->gainTrail = ratio;
    }
}

void display_utility_set_boss_ui_intro(float progress)
{
	if (progress < 0.0f) progress = 0.0f;
	if (progress > 1.0f) progress = 1.0f;
	boss_ui_intro = progress;
}

void display_utility_set_player_ui_intro(float progress)
{
	if (progress < 0.0f) progress = 0.0f;
	if (progress > 1.0f) progress = 1.0f;
	player_ui_intro = progress;
}

void display_utility_snap_boss_health_trail(float ratio)
{
	if (ratio < 0.0f) ratio = 0.0f;
	if (ratio > 1.0f) ratio = 1.0f;
	bossHealthBarState.lossTrail = ratio;
	bossHealthBarState.gainTrail = ratio;
	bossHealthBarState.lastRatio = ratio;
	bossHealthBarState.lossHold  = 0.0f;
	bossHealthBarState.gainHold  = 0.0f;
}

void draw_boss_health_bar(const char *name, float ratio, float flash)
{
	(void)name;

	// Clamp ratio
	if (ratio < 0.0f) ratio = 0.0f;
	if (ratio > 1.0f) ratio = 1.0f;
	if (flash < 0.0f) flash = 0.0f;
	if (flash > 1.0f) flash = 1.0f;

	// Reset pipeline so UI colors are not affected by 3D fog/lighting state
	rdpq_sync_pipe();
	rdpq_set_mode_standard();
	#ifdef RDPQ_FOG_DISABLED
	rdpq_mode_fog(RDPQ_FOG_DISABLED);
	#else
	rdpq_mode_fog(0); // older libdragon: 0 disables fog
	#endif
	rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
	rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
	
	// Background bar (darker so the red fill is clearly visible as it shrinks).
	// Boss HUD is anchored to the bottom-center of the screen, narrower than full width.
	const int marginY = ui_safe_margin_y();
	const int bossBarWidth = 220;
	int left = (SCREEN_WIDTH - bossBarWidth) / 2;
	int right = left + bossBarWidth;
	const int fillInsetX = 18;
	int barLeft = left + fillInsetX;
	int barRight = right - fillInsetX;
	if (barRight <= barLeft) {
		barLeft = left;
		barRight = right;
	}

	// Decorative frame overlay (lazy load once).
	if (!bossHealthBarBackgroundLoadAttempted) {
		bossHealthBarBackgroundLoadAttempted = true;
		bossHealthBarBackgroundSprite = sprite_load("rom:/ui/healthbars/boss/boss_background_healthbar.sprite");
	}

	// Taller red/dark fill only; frame sprite scale/position below is unchanged.
	const int fillHeight = 8;
	float frameScale = 1.0f;
	int frameHeight = 24;
	int frameY = SCREEN_HEIGHT - marginY - 24;
	int fillCenterY = frameY + 12;
	if (bossHealthBarBackgroundSprite) {
		frameScale = (float)(right - left) / (float)bossHealthBarBackgroundSprite->width;
		frameHeight = (int)((float)bossHealthBarBackgroundSprite->height * frameScale);
		frameY = SCREEN_HEIGHT - marginY - frameHeight;
		fillCenterY = frameY + frameHeight / 2;
	}

	// Load the name plate now so we can include its height in the slide distance.
	if (!bossHealthBarNameLoadAttempted) {
		bossHealthBarNameLoadAttempted = true;
		bossHealthBarNameSprite = sprite_load("rom:/ui/healthbars/boss/guardian_of_the_shackled_sun.sprite");
	}
	const float nameScale = 0.5f;
	int nameHeight = bossHealthBarNameSprite ? (int)((float)bossHealthBarNameSprite->height * nameScale) : 0;
	int nameY = frameY - nameHeight;

	// Slide the whole boss HUD (name plate, frame, fill) up from below the
	// bottom of the screen into position, matching the player health bar intro.
	float p = boss_ui_intro;
	int slideDist = SCREEN_HEIGHT - nameY + 8;
	int yOffset = (int)((1.0f - p) * (float)slideDist);
	frameY += yOffset;
	fillCenterY += yOffset;
	nameY += yOffset;

	int top = fillCenterY - fillHeight / 2 + 2;
	int bottom = top + fillHeight;
	int alpha = 170;

	// Background bar
	rdpq_set_prim_color(RGBA32(50, 50, 50, 130));
	rdpq_fill_rectangle(barLeft, top, barRight, bottom);

	update_bar_trails(ratio, &bossHealthBarState);

	int fillEnd  = barLeft + (int)((barRight - barLeft) * ratio);
	int lossEnd  = barLeft + (int)((barRight - barLeft) * bossHealthBarState.lossTrail);
	int gainEnd  = barLeft + (int)((barRight - barLeft) * bossHealthBarState.gainTrail);

	// Recent-damage segment (yellow) between the live fill and the previous value.
	if (lossEnd > fillEnd) {
		rdpq_set_prim_color(RGBA32(230, 200, 60, alpha));
		rdpq_fill_rectangle(fillEnd, top, lossEnd, bottom);
	}

	// Health fill (solid red; no debug markers)
	int red = 200 + (int)(55.0f * flash);
	int green = 30 + (int)(20.0f * flash);
	int blue = 30 + (int)(20.0f * flash);
	rdpq_set_prim_color(RGBA32(red, green, blue, alpha));
	if (fillEnd > barLeft) {
		rdpq_fill_rectangle(barLeft, top, fillEnd, bottom);
	}

	// Recent-heal segment (white highlight) overlaid on top of the fill.
	if (gainEnd < fillEnd) {
		rdpq_set_prim_color(RGBA32(240, 240, 240, alpha));
		rdpq_fill_rectangle(gainEnd, top, fillEnd, bottom);
	}

	// Decorative frame overlay
	if (bossHealthBarBackgroundSprite) {
		// Render the sprite in standard textured mode to preserve authored colors.
		rdpq_sync_pipe();
		rdpq_set_mode_standard();
		rdpq_mode_alphacompare(1);
		rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

		rdpq_sprite_blit(bossHealthBarBackgroundSprite, left, frameY, &(rdpq_blitparms_t){
			.scale_x = frameScale,
			.scale_y = frameScale,
		});
	}

	// Boss name plate
	if (bossHealthBarNameSprite) {
		rdpq_sync_pipe();
		rdpq_set_mode_standard();
		rdpq_mode_alphacompare(1);
		rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

		rdpq_sprite_blit(bossHealthBarNameSprite, left + 12, nameY + 12, &(rdpq_blitparms_t){
			.scale_x = nameScale,
			.scale_y = nameScale,
		});
	}
}

void draw_player_health_bar(const char *name, float ratio, float flash)
{
    (void)name;

    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    if (flash < 0.0f) flash = 0.0f;
    if (flash > 1.0f) flash = 1.0f;

    rdpq_sync_pipe();
    rdpq_set_mode_standard();

#ifdef RDPQ_FOG_DISABLED
    rdpq_mode_fog(RDPQ_FOG_DISABLED);
#else
    rdpq_mode_fog(0);
#endif

    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    const int marginX = ui_safe_margin_x();
    const int marginY = ui_safe_margin_y();

    const int barWidth = 120;
    const int barHeight = 10;

    float p = player_ui_intro;
    int slideDist = 40;
    int yOffset = (int)((1.0f - p) * (float)slideDist);

    int left = marginX;
    int top = marginY + 4 - yOffset;
    int bottom = top + barHeight;
    int right = left + barWidth;

    // Background
    rdpq_set_prim_color(RGBA32(35, 35, 35, 160));
    rdpq_fill_rectangle(left, top, right, bottom);

    // Optional darker inner border/empty region
    rdpq_set_prim_color(RGBA32(10, 10, 10, 190));
    rdpq_fill_rectangle(left + 1, top + 1, right - 1, bottom - 1);

    update_bar_trails(ratio, &playerHealthBarState);

    // Health fill
    int red = 200 + (int)(55.0f * flash);
    int green = 35 + (int)(45.0f * flash);
    int blue = 35 + (int)(45.0f * flash);

    int fillRight  = left + 2 + (int)((barWidth - 4) * ratio);
    int lossRight  = left + 2 + (int)((barWidth - 4) * playerHealthBarState.lossTrail);
    int gainLeft   = left + 2 + (int)((barWidth - 4) * playerHealthBarState.gainTrail);

    // Recent-damage segment (yellow) between the live fill and the previous value.
    if (lossRight > fillRight) {
        rdpq_set_prim_color(RGBA32(230, 200, 60, 220));
        rdpq_fill_rectangle(fillRight, top + 2, lossRight, bottom - 2);
    }

    if (fillRight > left + 2) {
        rdpq_set_prim_color(RGBA32(red, green, blue, 220));
        rdpq_fill_rectangle(left + 2, top + 2, fillRight, bottom - 2);
    }

    // Recent-heal segment (white highlight) overlaid on the top of the fill.
    if (gainLeft < fillRight) {
        rdpq_set_prim_color(RGBA32(240, 240, 240, 220));
        rdpq_fill_rectangle(gainLeft, top + 2, fillRight, bottom - 2);
    }

    // Light frame
    rdpq_set_prim_color(RGBA32(210, 210, 210, 180));
    rdpq_fill_rectangle(left, top, right, top + 1);
    rdpq_fill_rectangle(left, bottom - 1, right, bottom);
    rdpq_fill_rectangle(left, top, left + 1, bottom);
    rdpq_fill_rectangle(right - 1, top, right, bottom);
}

void draw_player_stamina_bar(float ratio)
{
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;

    rdpq_sync_pipe();
    rdpq_set_mode_standard();

#ifdef RDPQ_FOG_DISABLED
    rdpq_mode_fog(RDPQ_FOG_DISABLED);
#else
    rdpq_mode_fog(0);
#endif

    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    const int marginX = ui_safe_margin_x();
    const int marginY = ui_safe_margin_y();

    const int healthBarWidth = 120;
    const int healthBarHeight = 10;
    const int barWidth = (healthBarWidth * 3) / 4;
    const int barHeight = 6;

    float p = player_ui_intro;
    int slideDist = 40;
    int yOffset = (int)((1.0f - p) * (float)slideDist);

    // Sit flush against the bottom of the health bar so the borders share a line.
    int healthBarBottom = marginY + 4 + healthBarHeight - yOffset;

    int left = marginX;
    int top = healthBarBottom;
    int bottom = top + barHeight;
    int right = left + barWidth;

    // Background
    rdpq_set_prim_color(RGBA32(35, 35, 35, 160));
    rdpq_fill_rectangle(left, top, right, bottom);

    // Inner empty region
    rdpq_set_prim_color(RGBA32(10, 10, 10, 190));
    rdpq_fill_rectangle(left + 1, top + 1, right - 1, bottom - 1);

    update_bar_trails(ratio, &playerStaminaBarState);

    int fillWidth = (int)((barWidth - 4) * ratio);
    int lossWidth = (int)((barWidth - 4) * playerStaminaBarState.lossTrail);
    int gainWidth = (int)((barWidth - 4) * playerStaminaBarState.gainTrail);

    // Recently-used segment (yellow) between live fill and previous value.
    if (lossWidth > fillWidth) {
        rdpq_set_prim_color(RGBA32(230, 200, 60, 220));
        rdpq_fill_rectangle(left + 2 + fillWidth, top + 2, left + 2 + lossWidth, bottom - 2);
    }

    // Green fill — require at least 2px before rendering so sub-pixel residue
    // doesn't leave a sliver when the bar is empty.
    if (fillWidth >= 2) {
        rdpq_set_prim_color(RGBA32(60, 200, 80, 220));
        rdpq_fill_rectangle(left + 2, top + 2, left + 2 + fillWidth, bottom - 2);
    }

    // Recently-regenerated segment (white highlight) overlaid on top of the fill.
    if (gainWidth < fillWidth) {
        rdpq_set_prim_color(RGBA32(240, 240, 240, 220));
        rdpq_fill_rectangle(left + 2 + gainWidth, top + 2, left + 2 + fillWidth, bottom - 2);
    }

    // Light frame (shares its top edge with the health bar's bottom edge)
    rdpq_set_prim_color(RGBA32(210, 210, 210, 180));
    rdpq_fill_rectangle(left, top, right, top + 1);
    rdpq_fill_rectangle(left, bottom - 1, right, bottom);
    rdpq_fill_rectangle(left, top, left + 1, bottom);
    rdpq_fill_rectangle(right - 1, top, right, bottom);
}

//Shield version
/*void draw_player_health_bar(const char *name, float ratio, float flash)
{
	(void)name;

	// Clamp ratio
	if (ratio < 0.0f) ratio = 0.0f;
	if (ratio > 1.0f) ratio = 1.0f;
	if (flash < 0.0f) flash = 0.0f;
	if (flash > 1.0f) flash = 1.0f;

	// Reset pipeline so UI doesn't inherit a leftover render mode from the 3D pass
	// or conditional draws (eg: sword trails / lock-on marker).
	rdpq_sync_pipe();
	rdpq_set_mode_standard();
	#ifdef RDPQ_FOG_DISABLED
	rdpq_mode_fog(RDPQ_FOG_DISABLED);
	#else
	rdpq_mode_fog(0); // older libdragon: 0 disables fog
	#endif

	// Lazy load shield health sprites from healthiest to broken.
	if (!playerShieldHealthSpritesLoadAttempted) {
		playerShieldHealthSpritesLoadAttempted = true;
		playerShieldHealthSprites[0] = sprite_load("rom:/ui/healthbars/player/tile000.sprite");
		playerShieldHealthSprites[1] = sprite_load("rom:/ui/healthbars/player/tile001.sprite");
		playerShieldHealthSprites[2] = sprite_load("rom:/ui/healthbars/player/tile002.sprite");
		playerShieldHealthSprites[3] = sprite_load("rom:/ui/healthbars/player/tile003.sprite");
		playerShieldHealthSprites[4] = sprite_load("rom:/ui/healthbars/player/tile004.sprite");
	}

	// Slide in from the bottom so it matches the existing player UI intro behavior.
	int slideDistBottom = 40; // pixels downward off-screen at start
	float p = player_ui_intro; // 0..1
	float yOffset = (1.0f - p) * (float)slideDistBottom;

	// Map health to one of 5 shield stages.
	// tile004 is reserved for exactly 0% health.
	int stage = 0;
	if (ratio <= 0.0f) {
		stage = 4;
	} else {
		stage = (int)floorf((1.0f - ratio) * 4.0f);
		if (stage < 0) stage = 0;
		if (stage > 3) stage = 3;
	}

	sprite_t *shieldSprite = playerShieldHealthSprites[stage];
	if (!shieldSprite) return;

	// Keep shield compact and anchored to the bottom-left safe area.
	const float targetHeight = 48.0f;
	float baseScale = targetHeight / (float)shieldSprite->height;
	float scale = baseScale;
	int drawX = ui_safe_margin_x() + 4;
	int drawY = SCREEN_HEIGHT - ui_safe_margin_y() - (int)(shieldSprite->height * scale) + (int)yOffset;

	// Tiny impact shake while damage flash is active.
	float shakeAmplitude = 2.8f * flash;
	// At low health, add a subtle "heartbeat" shake pulse once per second.
	if (ratio > 0.0f && ratio <= 0.25f) {
		const float beatDuration = 0.14f; // first 140ms of each second
		float beatPhase = fmodf(gameTime, 1.0f);
		if (beatPhase < beatDuration) {
			float beatT = beatPhase / beatDuration;
			float envelope = 1.0f - fabsf((beatT * 2.0f) - 1.0f); // 0->1->0
			float lowHealthShake = 1.8f * envelope;
			if (lowHealthShake > shakeAmplitude) shakeAmplitude = lowHealthShake;
		}
	}
	drawX += (int)(sinf(gameTime * 65.0f) * shakeAmplitude);
	drawY += (int)(cosf(gameTime * 83.0f) * shakeAmplitude);

	// At 0% health, grow and fade out the shattered shield.
	// Reset immediately if player is alive again (eg: restart/revive).
	float alpha01 = 1.0f;
	if (ratio <= 0.0f) {
		const float DEATH_ANIM_DURATION = 0.55f;
		playerShieldDeathAnimTime += deltaTime;
		if (playerShieldDeathAnimTime > DEATH_ANIM_DURATION) playerShieldDeathAnimTime = DEATH_ANIM_DURATION;

		float t = playerShieldDeathAnimTime / DEATH_ANIM_DURATION;
		scale = baseScale * (1.0f + 0.40f * t);
		alpha01 = 1.0f - t;

		// Keep expansion centered around the original sprite placement.
		int baseW = (int)((float)shieldSprite->width * baseScale);
		int baseH = (int)((float)shieldSprite->height * baseScale);
		int grownW = (int)((float)shieldSprite->width * scale);
		int grownH = (int)((float)shieldSprite->height * scale);
		drawX -= (grownW - baseW) / 2;
		drawY -= (grownH - baseH) / 2;
	} else {
		playerShieldDeathAnimTime = 0.0f;
	}

	rdpq_sync_pipe();
	rdpq_set_mode_standard();
	rdpq_mode_alphacompare(0);
	rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
	// Keep authored sprite colors and modulate with primitive color/alpha.
	rdpq_mode_combiner(RDPQ_COMBINER_TEX_FLAT);
	rdpq_set_prim_color(RGBA32(255, 255, 255, (uint8_t)(255.0f * alpha01)));
	rdpq_sprite_blit(shieldSprite, drawX, drawY, &(rdpq_blitparms_t){
		.scale_x = scale,
		.scale_y = scale,
	});
}*/

void display_manager_draw_rectangle(int x, int y, int width, int height, color_t color)
{

    rdpq_set_mode_standard();
    rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);

    rdpq_set_prim_color(color);
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
    
    rdpq_sync_pipe();

    rdpq_fill_rectangle(x, y, width, height);
}


void display_utility_solid_black_transition(bool fadeIn, float speed)
{
    if(startScreenFade)
    {
        if(fadeIn)
            fadeBlackAlpha = 255;
        else
            fadeBlackAlpha = 0;

        startScreenFade = false;
    }
    
    if(fadeIn)
    {
		fadeBlackAlpha -= deltaTime * speed;
		if (fadeBlackAlpha <= 0.0f)
			return;

		display_manager_draw_rectangle(0, 0, display_get_width(), display_get_height(), RGBA32(0, 0, 0, fadeBlackAlpha));

    }
    else
    {
		fadeBlackAlpha += deltaTime * speed;
		
		if(fadeBlackAlpha >= 255.0f)
		{
			display_manager_draw_rectangle(0, 0, display_get_width(), display_get_height(), RGBA32(0, 0, 0, 255));
		}
		else
		{
			display_manager_draw_rectangle(0, 0, display_get_width(), display_get_height(), RGBA32(0, 0, 0, fadeBlackAlpha));
		}
    }
}