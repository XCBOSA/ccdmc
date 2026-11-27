#ifndef VOXEL_DUNGEONS_H
#define VOXEL_DUNGEONS_H

#include "world.h"

enum { DUNGEON_REGION=32, DUNGEON_ROOF=5 };
typedef struct {
    int x, floor, z, radius_x, radius_z, spawner, chest_count;
    uint32_t entrances;
} DungeonPlan;
/* Pure terrain query for the region containing (x,z). Does not load chunks. */
int dungeon_plan_for(int x,int z,DungeonPlan *out);
int strata_gallery_plan_for(int x,int z,DungeonPlan *out);
void strata_gallery_sample(DungeonPlan plan,int upper,int *x,int *y,int *z);

#endif
