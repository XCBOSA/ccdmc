#ifndef VOXEL_SPAWN_EGGS_H
#define VOXEL_SPAWN_EGGS_H
#include "game.h"
const ItemDef *spawn_egg_def(int item);
unsigned spawn_egg_spot(int item);
int spawn_egg_at(int item,Vec3 point);
const char *spawn_egg_use(Hit hit);
#endif
