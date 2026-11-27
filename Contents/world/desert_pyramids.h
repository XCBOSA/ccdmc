#ifndef VOXEL_DESERT_PYRAMIDS_H
#define VOXEL_DESERT_PYRAMIDS_H

#include "world.h"

enum { PYRAMID_REGION=192, PYRAMID_RADIUS=10, PYRAMID_TREASURE=-11, PYRAMID_TNT=-13 };
typedef struct { int x,y,z; unsigned rotation; } DesertPyramidPlan;

int desert_pyramid_plan_for(int x,int z,DesertPyramidPlan *out);
BlockEdit desert_pyramid_position(DesertPyramidPlan p,int dx,int dy,int dz,int raw);

#endif
