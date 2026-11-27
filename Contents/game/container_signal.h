#ifndef VOXEL_CONTAINER_SIGNAL_H
#define VOXEL_CONTAINER_SIGNAL_H
#include "game.h"

/* A full stack weighs STACK_MAX units, independent of its item capacity.
   Integer arithmetic preserves exact redstone thresholds across compiler modes. */
unsigned container_weight(const ItemStack *items,int count);
int container_fill_signal(unsigned weight,int slots);
/* Returns -1 for blocks that are not readable containers. */
int container_signal(int x,int y,int z,int type);
#endif
