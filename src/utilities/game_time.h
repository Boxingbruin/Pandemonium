#ifndef GAME_TIME_H
#define GAME_TIME_H

extern float gameTime;
extern float deltaTime;
extern double nowS;

void game_time_init(void);
void game_time_reset(void);
void game_time_update(void);

#endif