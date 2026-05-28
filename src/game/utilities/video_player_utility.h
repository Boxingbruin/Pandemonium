#ifndef VIDEO_PLAYER_UTILITY_H
#define VIDEO_PLAYER_UTILITY_H

#include <stdbool.h>
#include <t3d/t3d.h>

// Call from anywhere (eg scene.c) to request a video.
void video_player_request(const char *rom_video_path);

// Optional: cancel a pending request (rarely needed)
void video_player_cancel(void);

// Returns true if a video is currently pending.
bool video_player_is_pending(void);

// Call once per frame from main loop *before* rdpq_attach(...).
// If it plays a video, it returns true (and your frame should usually `continue;`).
bool video_player_pump_and_play(T3DViewport *viewport);

#endif