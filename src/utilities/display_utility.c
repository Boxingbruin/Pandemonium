#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3ddebug.h>
#include <string.h>

#include "globals.h"

surface_t offscreenBuffer;

void draw_boss_health_bar(const char *name, float ratio, float flash)
{
	// Clamp ratio
	if (ratio < 0.0f) ratio = 0.0f;
	if (ratio > 1.0f) ratio = 1.0f;
	if (flash < 0.0f) flash = 0.0f;
	if (flash > 1.0f) flash = 1.0f;

	// Disable fog for UI elements to prevent them from taking on fog color
	t3d_fog_set_enabled(false);
	
	// Ensure we're in 2D rendering mode for UI
	rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
	rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);
	
	// Background bar
	float left = 20.0f;
	float right = SCREEN_WIDTH - 20.0f;
	float top = 10.0f;
	float bottom = 22.0f;
	rdpq_set_prim_color(RGBA32(100,40,40,255));
	rdpq_fill_rectangle(left, top, right, bottom);
	
	// Health fill
	int red = 200 + (int)(55.0f * flash);
	int green = 40 + (int)(20.0f * flash);
	int blue = 40 + (int)(20.0f * flash);
	rdpq_set_prim_color(RGBA32(red, green, blue, 255));
	float fillEnd = left + ((right - left) * ratio);
	rdpq_fill_rectangle(left, top, fillEnd, bottom);
	
	// Center the boss name text
	const char* displayName = name ? name : "Enemy";
	float barCenter = (left + right) * 0.5f;

	// Estimate character width (approximately 6 pixels for debug font)
	float estimatedTextWidth = strlen(displayName) * 6.0f;
	float textX = barCenter - (estimatedTextWidth * 0.5f);
	
	// Use rdpq_text_printf with proper text rendering setup
	rdpq_text_printf(NULL, FONT_UNBALANCED, (int)textX, (int)(bottom + 12.0f), "%s", displayName);
	
	// Re-enable fog after UI rendering
	t3d_fog_set_enabled(true);
}