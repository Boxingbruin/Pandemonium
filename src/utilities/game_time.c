#include <libdragon.h>

#include "game_time.h"

static float TIME_SPEED = 1.0f;

double nowS = 0.0;
float gameTime = 0.0f;
float lastTime = 0.0f;
float deltaTime = 0.0f;

static float get_time_s() 
{
    return (float)((double)get_ticks_us() / 1000000.0);
}

void game_time_init(void) 
{
    lastTime = get_time_s() - (1.0f / 60.0f);
}
  
void game_time_update(void)
{
    // Get current time in seconds
    nowS = get_time_s();

    // Calculate the time difference between this frame and the last frame
    deltaTime = nowS - lastTime;

    // Update game time with the adjusted deltaTime
    gameTime += deltaTime;

    // Update lastTime for the next frame
    lastTime = nowS;
}

void game_time_reset(void)
{
    gameTime = 0.0f;
    lastTime = 0.0;
    deltaTime = 0.0f;
}
