#ifndef VOXEL_FARMING_H
#define VOXEL_FARMING_H
#include "game.h"

void farm_reset(void);
void farm_step(float dt);
const char *farm_use(Hit hit);
/* 1 = crop advanced, 2 = grass spread, 0 = unsuitable, -1 = unavailable. */
int farm_bonemeal(int x,int y,int z);
int mushroom_can_stay(int x,int y,int z);
const char *farm_break(Hit hit);
int farm_attached_cocoa(int x,int y,int z,BlockEdit edits[4],ItemStack loot[4]);
int farm_is_hoe(int type);
int farm_can_plant(int x,int y,int z);
void farm_trample(int x,int y,int z,float distance);

#endif
