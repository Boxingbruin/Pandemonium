#include <libdragon.h>
#include <t3d/t3d.h>

#include "game_lighting.h"

// Lighting
uint8_t colorAmbient[4];
uint8_t colorDir[4];
T3DVec3 lightDirVec;
color_t fogColor;

void game_lighting_initialize(void) 
{
    colorAmbient[0] = 100;
    colorAmbient[1] = 100;
    colorAmbient[2] = 100;
    colorAmbient[3] = 0xFF;

    colorDir[0] = 0xFF;
    colorDir[1] = 0xFF;
    colorDir[2] = 0xFF;
    colorDir[3] = 0xFF;

    lightDirVec = (T3DVec3){{0.0f, 0.0f, 0.0f}};
    t3d_vec3_norm(&lightDirVec);

    fogColor = (color_t){242, 218, 166, 0xFF};
}