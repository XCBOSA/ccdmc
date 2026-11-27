#ifndef VOXEL_SPILLWAYS_H
#define VOXEL_SPILLWAYS_H
#include "internal.h"

enum { SPILLWAY_REGION=128, SPILLWAY_BEGIN=-4, SPILLWAY_END=7, SPILLWAY_NEAR=-4, SPILLWAY_FAR=2, SPILLWAY_ROOF=5 };
typedef struct {
    int x,z,floor,rotated;
    /* First lane (v) of the two-cell mouth at the downstream end. */
    int mouth;
} SpillwayPlan;
int spillway_plan_for(int x,int z,SpillwayPlan *out);
BlockEdit spillway_cell(SpillwayPlan plan,int u,int v,int dy,int cell);
void spillway_generate_chunk(SpillwayPlan plan,int cx,int cz,uint8_t *cells,TerrainWide *wide);

#endif
