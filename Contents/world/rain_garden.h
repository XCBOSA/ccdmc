#ifndef VOXEL_RAIN_GARDEN_H
#define VOXEL_RAIN_GARDEN_H

#include "world.h"

enum { RAIN_GARDEN_RADIUS=7, RAIN_GARDEN_BED_LEVEL=-3 };
typedef struct { int x,y,z; unsigned rotation; } RainGardenPlan;

int rain_garden_plan_for(int x,int z,RainGardenPlan *out);
int rain_garden_contains(RainGardenPlan plan,int x,int z);
BlockEdit rain_garden_position(RainGardenPlan plan,int local_x,int local_y,int local_z);

#endif
