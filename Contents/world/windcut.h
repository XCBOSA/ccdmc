#ifndef VOXEL_WINDCUT_H
#define VOXEL_WINDCUT_H

#include "world.h"

enum { ARID_SITE_REGION=192, WINDCUT_RADIUS=10 };
typedef struct { int x,y,z; unsigned rotation; } WindcutPlan;

int windcut_plan_for(int x,int z,WindcutPlan *out);
int windcut_contains(WindcutPlan plan,int x,int z);

#endif
