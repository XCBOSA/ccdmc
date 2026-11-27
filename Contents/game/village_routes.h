#ifndef CCDMC_VILLAGE_ROUTES_H
#define CCDMC_VILLAGE_ROUTES_H

#include "game.h"

enum { VILLAGE_WALK_RADIUS=19, VILLAGE_ROUTE_STEPS=76 };
typedef struct { int8_t x,z; } VillageStep;
typedef struct {
    int center_x,center_z,y,door_x,door_z;
} VillageWalk;

/* A bounded, resident-only search across the shared terrace. Coordinates
   in a returned path are relative to the village centre. */
int village_route(const VillageWalk *area,int x,int z,int goal_x,int goal_z,VillageStep *out);
int village_route_cell(const VillageWalk *area,int x,int z);

#endif
