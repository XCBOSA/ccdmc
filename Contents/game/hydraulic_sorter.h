#ifndef VOXEL_HYDRAULIC_SORTER_H
#define VOXEL_HYDRAULIC_SORTER_H

#include "game.h"

/* One simulation tick; this utility doesn't request cold terrain. */
void hydraulic_sorters_step(int player_x,int player_z,unsigned tick);
const char *hydraulic_sorter_use(Hit target);

#endif
