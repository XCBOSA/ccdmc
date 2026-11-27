#ifndef VOXEL_TEMPLES_H
#define VOXEL_TEMPLES_H
#include "world.h"

/* Centre, main floor, and quarter turns clockwise from a south entrance. */
typedef struct { int x,y,z; unsigned rotation; } TemplePlan;
int temple_plan_for(int x,int z,TemplePlan *out);
BlockEdit temple_position(TemplePlan plan,int local_x,int local_y,int local_z,int raw);
#endif
