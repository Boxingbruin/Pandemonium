#include "opening_credits.h"

#include <libdragon.h>

#include "../../utilities/globals.h"
#include "logo.h"

static void opening_credits_reinit_display_rdpq(void)
{
    if (DITHER_ENABLED) {
        display_init(
            RESOLUTION_320x240,
            DEPTH_16_BPP,
            FRAME_BUFFER_COUNT,
            GAMMA_NONE,
            FILTERS_RESAMPLE_ANTIALIAS
        );
    } else {
        if (ARES_AA_ENABLED) {
            display_init(
                RESOLUTION_320x240,
                DEPTH_32_BPP,
                FRAME_BUFFER_COUNT,
                GAMMA_NONE,
                FILTERS_RESAMPLE_ANTIALIAS
            );
        } else {
            display_init(
                RESOLUTION_320x240,
                DEPTH_32_BPP,
                FRAME_BUFFER_COUNT,
                GAMMA_NONE,
                FILTERS_DISABLED
            );
        }
    }

    rdpq_init();
}

void opening_credits_play(void)
{
    if (DEV_MODE) return;

    logo_libdragon();
    opening_credits_reinit_display_rdpq();

    logo_t3d();
    opening_credits_reinit_display_rdpq();
}