#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3ddebug.h>
#include <string.h>
#include <math.h>

#include "globals.h"
#include "game_time.h"

surface_t offscreenBuffer;
static int fadeBlackAlpha = 255; // Default alpha value for the rectangle
bool startScreenFade = false;

void draw_boss_health_bar(const char *name, float ratio, float flash)
{
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
	
	// Background bar (darker so the red fill is clearly visible as it shrinks)
	float margin = 10.0f;
	int left = margin;
	int right = SCREEN_WIDTH - margin;
	int top = 5;
	int bottom = 20;
	rdpq_set_prim_color(RGBA32(50, 50, 50, 255));
	rdpq_fill_rectangle(left, top, right, bottom);
	
	// Health fill (solid red; no debug markers)
	int red = 200 + (int)(55.0f * flash);
	int green = 30 + (int)(20.0f * flash);
	int blue = 30 + (int)(20.0f * flash);
	rdpq_set_prim_color(RGBA32(red, green, blue, 255));
	int fillEnd = left + (int)((right - left) * ratio);
	rdpq_fill_rectangle(left, top, fillEnd, bottom);
	
	// Center the boss name text
	const char* displayName = name ? name : "Enemy";
	float barCenter = (left + right) * 0.5f;

	// Estimate character width (approximately 6 pixels for debug font)
	float estimatedTextWidth = strlen(displayName) * 6.0f;
	float textX = barCenter - (estimatedTextWidth * 0.5f);
	
	// Use rdpq_text_printf with proper text rendering setup
	rdpq_text_printf(NULL, FONT_UNBALANCED, (int)textX, (int)(bottom + 12.0f), "%s", displayName);
}

void draw_player_health_bar(const char *name, float ratio, float flash)
{
	// Clamp ratio
	if (ratio < 0.0f) ratio = 0.0f;
	if (ratio > 1.0f) ratio = 1.0f;
	if (flash < 0.0f) flash = 0.0f;
	if (flash > 1.0f) flash = 1.0f;

	// Add a built-in low-health flash so the bar pulses when critical
	const float LOW_HEALTH_THRESHOLD = 0.25f;
	float warningFlash = 0.0f;
	if (ratio <= LOW_HEALTH_THRESHOLD) {
		// 4 Hz sine pulse; normalize to 0..1
		float pulse = sinf(gameTime * 8.0f);
		warningFlash = 0.5f * (pulse + 1.0f);
	}
	float combinedFlash = fmaxf(flash, warningFlash);

	// Set up UI rendering mode (no fog manipulation needed)
	rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
	rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
	
	// Smaller background bar positioned at bottom-right
	float barWidth = 120.0f;  // Smaller than boss bar
	float barHeight = 8.0f;   // Smaller height
	float margin = 10.0f;
	float left = margin;
	float right = left + barWidth;
	float bottom = SCREEN_HEIGHT - margin;
	float top = bottom - barHeight;

	rdpq_set_prim_color(RGBA32(60, 20, 20, 255));
	rdpq_fill_rectangle(left, top, right, bottom);
	
	// Health fill - using green/yellow/red gradient based on health ratio
	int red, green, blue;
	if (ratio > 0.6f) {
		// Green to yellow
		float t = (ratio - 0.6f) / 0.4f;
		red = 200 - (int)(100.0f * t) + (int)(55.0f * combinedFlash);
		green = 200 + (int)(20.0f * combinedFlash);
		blue = 40 + (int)(20.0f * combinedFlash);
	} else if (ratio > 0.3f) {
		// Yellow to orange
		float t = (ratio - 0.3f) / 0.3f;
		red = 200 + (int)(30.0f * (1.0f - t)) + (int)(55.0f * combinedFlash);
		green = 200 - (int)(50.0f * (1.0f - t)) + (int)(20.0f * combinedFlash);
		blue = 40 + (int)(20.0f * combinedFlash);
	} else {
		// Red
		red = 200 + (int)(55.0f * combinedFlash);
		green = 40 + (int)(20.0f * combinedFlash);
		blue = 40 + (int)(20.0f * combinedFlash);
	}
	
	rdpq_set_prim_color(RGBA32(red, green, blue, 255));
	float fillEnd = left + ((right - left) * ratio);
	rdpq_fill_rectangle(left, top, fillEnd, bottom);
	
	// Player name text below the health bar
	// const char* displayName = name ? name : "Player";
	
	// Estimate character width (approximately 6 pixels for debug font)
	// float estimatedTextWidth = strlen(displayName) * 6.0f;
	// float textX = right - estimatedTextWidth;  // Right-align text
	
	// Use rdpq_text_printf with proper text rendering setup
	// rdpq_text_printf(NULL, FONT_UNBALANCED, (int)textX, (int)(bottom + 4.0f), "%s", displayName);
}

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