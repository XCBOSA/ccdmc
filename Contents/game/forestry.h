#ifndef VOXEL_FORESTRY_H
#define VOXEL_FORESTRY_H

#include "game.h"

enum { TREE_BONE_STAGE=3,TREE_BONE_GROWN,TREE_BONE_TRIED };
int tree_soil(int type);
const char *tree_use(Hit hit);
int tree_bonemeal(int x,int y,int z);
void tree_tick(BlockEdit sapling,unsigned tick);
int tree_leaf_drop(int raw,int x,int y,int z,ItemStack tool);
int tree_sapling_drop(int raw);

#endif
