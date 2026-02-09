
#ifndef MULTI_SWORD_ATTACKS
#define MULTI_SWORD_ATTACKS

#include <t3d/t3d.h> 
#include <stdbool.h>

// Generic “always running” driver for multi-sword attacks.
// Later you can switch patterns / enable only during boss attack windows.
void msa_init(void);
void msa_update(float dt);
void msa_draw(T3DViewport *viewport);

// Runtime controls
void msa_set_enabled(bool enabled);
void msa_set_sword_count(int count);

// Extendibility: patterns are just modes inside this module (for now).
typedef enum {
    MSA_PATTERN_GROUND_SWEEP = 0,   // stabbed swords carving curvy ground trails (your “option A”)
    // future:
    // MSA_PATTERN_CHASE_PLAYER,
    // MSA_PATTERN_SPIRAL,
} MsaPattern;

void msa_set_pattern(MsaPattern p);

#endif