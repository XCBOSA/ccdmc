#ifndef VOXEL_SILT_BED_H
#define VOXEL_SILT_BED_H
#include "game.h"

/* Uses resident cells only. An unbuffered neighbor is not a water source. */
int silt_bed_water(int x,int y,int z);
const char *silt_bed_tap(Hit hit);

#endif
