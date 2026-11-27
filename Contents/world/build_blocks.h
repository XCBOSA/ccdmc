#ifndef VOXEL_BUILD_BLOCKS_H
#define VOXEL_BUILD_BLOCKS_H

#include "barriers.h"

enum { BUILD_BOXES=8 };
int build_material_block(int material);
int build_slab_raw(int material,int height);
int build_stair_raw(int material,int direction,int inverted);
int build_wooden(int raw);
/* Bit index x + 2*z + 4*y. Each bit represents half a voxel on each axis. */
unsigned build_mask(int raw,const int neighbors[4]);
void build_neighbors(int x,int y,int z,int out[4]);
unsigned build_world_mask(int x,int y,int z,int raw);
int build_boxes(int x,int y,int z,unsigned mask,BlockBox out[BUILD_BOXES]);
int build_world_boxes(int x,int y,int z,int raw,BlockBox out[BUILD_BOXES]);

#endif
