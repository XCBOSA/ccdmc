#ifndef VOXEL_CISTERN_H
#define VOXEL_CISTERN_H

#include "game.h"

enum { CISTERN_RAIN_PERIOD=120, CISTERN_RAIN_BATCH=12 };
/* Called only by the live weather clock. No terrain generation or catch-up. */
void cistern_collect_rain(int x,int z,unsigned wet_ticks_left);
const char *cistern_use(Hit hit);

#endif
