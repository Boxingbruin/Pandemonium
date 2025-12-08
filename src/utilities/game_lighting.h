#ifndef GAME_LIGHT_H
#define GAME_LIGHT_H

extern uint8_t colorAmbient[4];
extern uint8_t colorDir[4];
extern T3DVec3 lightDirVec;

void game_lighting_initialize(void);

#endif