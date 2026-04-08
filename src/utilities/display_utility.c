#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3ddebug.h>
#include <string.h>
#include <math.h>

#include "globals.h"
#include "game_time.h"
#include "video_layout.h"

surface_t offscreenBuffer;
static int fadeBlackAlpha = 255; // Default alpha value for the rectangle
bool startScreenFade = false;
static sprite_t* bossHealthBarBackgroundSprite = NULL;
static bool bossHealthBarBackgroundLoadAttempted = false;
//static sprite_t* playerShieldHealthSprites[5] = { NULL, NULL, NULL, NULL, NULL };
//static bool playerShieldHealthSpritesLoadAttempted = false;
//static float playerShieldDeathAnimTime = 0.0f;

// UI intro progress values (0.0 = off-screen, 1.0 = fully visible)
static float boss_ui_intro = 1.0f;
static float player_ui_intro = 1.0f;

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
	// Keep the entire boss HUD inside the UI-safe / overscan rectangle.
	const int marginX = ui_safe_margin_x();
	const int marginY = ui_safe_margin_y();
	int left = marginX;
	int right = SCREEN_WIDTH - marginX;
	const int fillInsetX = 24;
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
	const int fillHeight = 16;
	float frameScale = 1.0f;
	int frameY = marginY;
	int fillCenterY = marginY + 12;
	if (bossHealthBarBackgroundSprite) {
		frameScale = (float)(right - left) / (float)bossHealthBarBackgroundSprite->width;
		frameY = marginY;
		int frameHeight = (int)((float)bossHealthBarBackgroundSprite->height * frameScale);
		fillCenterY = frameY + frameHeight / 2;
	}
	int top = fillCenterY - fillHeight / 2;
	int bottom = top + fillHeight;

	// center-out growth only (no vertical slide)
	float p = boss_ui_intro;
	int alpha = 170;

	// Width reveal from center outward based on intro progress
	int center = (barLeft + barRight) / 2;
	int halfWidth = (barRight - barLeft) / 2;
	int revealLeft = center - (int)(halfWidth * p);
	int revealRight = center + (int)(halfWidth * p);
	if (revealRight > revealLeft) {
		rdpq_set_prim_color(RGBA32(50, 50, 50, 130));
		rdpq_fill_rectangle(revealLeft, top, revealRight, bottom);
	}
	
	// Health fill (solid red; no debug markers)
	int red = 200 + (int)(55.0f * flash);
	int green = 30 + (int)(20.0f * flash);
	int blue = 30 + (int)(20.0f * flash);
	rdpq_set_prim_color(RGBA32(red, green, blue, alpha));
	int fillEnd = barLeft + (int)((barRight - barLeft) * ratio);
	// Clip fill to revealed width region
	int clipLeft = (revealLeft > barLeft) ? revealLeft : barLeft;
	int clipRight = (revealRight < barRight) ? revealRight : barRight;
	int fillClipRight = (fillEnd < clipRight) ? fillEnd : clipRight;
	if (fillClipRight > clipLeft) {
		rdpq_fill_rectangle(clipLeft, top, fillClipRight, bottom);
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
	
	// Center the boss name text
	//const char* displayName = name ? name : "Enemy";
	//float barCenter = (left + right) * 0.5f;

	// Estimate character width (approximately 6 pixels for debug font)
	// float estimatedTextWidth = strlen(displayName) * 6.0f;
	// float textX = barCenter - (estimatedTextWidth * 0.5f);
	
	// // Use rdpq_text_printf with proper text rendering setup
	// rdpq_text_printf(NULL, FONT_UNBALANCED, (int)textX, (int)(bottom + 12.0f), "%s", displayName);
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
    int slideDistBottom = 40;
    int yOffset = (int)((1.0f - p) * (float)slideDistBottom);

    int left = marginX + 12;
    int bottom = SCREEN_HEIGHT - marginY - 14 + yOffset;
    int top = bottom - barHeight;
    int right = left + barWidth;

    // Background
    rdpq_set_prim_color(RGBA32(35, 35, 35, 160));
    rdpq_fill_rectangle(left, top, right, bottom);

    // Optional darker inner border/empty region
    rdpq_set_prim_color(RGBA32(10, 10, 10, 190));
    rdpq_fill_rectangle(left + 1, top + 1, right - 1, bottom - 1);

    // Health fill
    int red = 200 + (int)(55.0f * flash);
    int green = 35 + (int)(45.0f * flash);
    int blue = 35 + (int)(45.0f * flash);

    int fillRight = left + 2 + (int)((barWidth - 4) * ratio);

    if (fillRight > left + 2) {
        rdpq_set_prim_color(RGBA32(red, green, blue, 220));
        rdpq_fill_rectangle(left + 2, top + 2, fillRight, bottom - 2);
    }

    // Light frame
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