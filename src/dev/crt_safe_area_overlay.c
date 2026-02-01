#include "crt_safe_area_overlay.h"

static void DrawRectOutline(int x0, int y0, int x1, int y1, color_t color)
{
    if (x1 < x0 || y1 < y0) return;

    rdpq_set_prim_color(color);

    // Treat (x0,y0)->(x1,y1) as inclusive coordinates.
    const int x2 = x1 + 1;
    const int y2 = y1 + 1;

    // Top / bottom (1px)
    rdpq_fill_rectangle(x0, y0, x2, y0 + 1);
    rdpq_fill_rectangle(x0, y1, x2, y1 + 1);

    // Left / right (1px)
    rdpq_fill_rectangle(x0, y0, x0 + 1, y2);
    rdpq_fill_rectangle(x1, y0, x1 + 1, y2);
}

void DrawCrtSafeAreaOverlay(int screenW, int screenH)
{
    if (screenW < 2 || screenH < 2) return;

    // Make the overlay self-contained and stable regardless of prior render state.
    rdpq_set_mode_standard();
    rdpq_mode_combiner(RDPQ_COMBINER_FLAT);

    // Margin math (integer safe)
    const int marginX_action = (screenW * 50) / 1000; // 5.0%
    const int marginY_action = (screenH * 50) / 1000;
    const int marginX_title  = (screenW * 75) / 1000; // 7.5%
    const int marginY_title  = (screenH * 75) / 1000;

    // Rect endpoints (inclusive)
    const int x0_full = 0;
    const int y0_full = 0;
    const int x1_full = screenW - 1;
    const int y1_full = screenH - 1;

    const int x0_action = marginX_action;
    const int y0_action = marginY_action;
    const int x1_action = (screenW - 1) - marginX_action;
    const int y1_action = (screenH - 1) - marginY_action;

    const int x0_title = marginX_title;
    const int y0_title = marginY_title;
    const int x1_title = (screenW - 1) - marginX_title;
    const int y1_title = (screenH - 1) - marginY_title;

    // Distinct, high-contrast colors (no alpha needed)
    const color_t col_full   = RGBA32(0xFF, 0xFF, 0xFF, 0xFF); // White
    const color_t col_action = RGBA32(0x00, 0xFF, 0x00, 0xFF); // Green
    const color_t col_title  = RGBA32(0xFF, 0xFF, 0x00, 0xFF); // Yellow

    // Draw in a fixed order every frame (avoid flicker)
    DrawRectOutline(x0_full,   y0_full,   x1_full,   y1_full,   col_full);
    DrawRectOutline(x0_action, y0_action, x1_action, y1_action, col_action);
    DrawRectOutline(x0_title,  y0_title,  x1_title,  y1_title,  col_title);
}

