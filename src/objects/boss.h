#ifndef BOSS_H
#define BOSS_H

#include <libdragon.h>
#include <t3d/t3d.h>
#include <t3d/t3dskeleton.h>
#include <t3d/t3danim.h>

#include "general_utility.h"

typedef struct {
    T3DVec3 localCapA;
    T3DVec3 localCapB;
    float radius;
} CapsuleCollider;

// Structure for holding boss data
typedef struct {
    T3DModel *model;
    float pos[3];
    float rot[3];
    float scale[3];

    ScrollParams *scrollParams;
    T3DSkeleton *skeleton;
    //T3DSkeleton *skeletonBlend;
    T3DAnim **animations;
    int currentAnimation;
    int animationCount;

    CapsuleCollider capsuleCollider;

    T3DMat4FP *modelMat;
    rspq_block_t *dpl;

    bool visible;
} Boss;

extern Boss boss;

void boss_init(void);

void boss_update_position(void);

void boss_draw();
void boss_update();
void boss_delete();

void boss_free(void);


#endif