#ifndef VOXEL_AIR_STATION_H
#define VOXEL_AIR_STATION_H
#include "game.h"

enum { AIR_PIPE_LIMIT=24, AIR_REFILL_COST=2 };
/* These are direct actions, not a persistent panel or automatic area effect. */
const char *air_station_tap(Hit hit);
#endif
