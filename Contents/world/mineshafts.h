#ifndef VOXEL_MINESHAFTS_H
#define VOXEL_MINESHAFTS_H

#include "internal.h"

enum { MINESHAFT_REGION=128, MINESHAFT_BRANCHES=2 };
typedef struct {
    int x,z,floor,rotated,left,right;
    int branch[MINESHAFT_BRANCHES],north,south;
    unsigned key;
} MineshaftPlan;

/* One candidate per region. Everything, including the block variants, is
   reconstructed from the immutable terrain job key, not loaded chunks. */
int mineshaft_plan(TerrainKey key,int x,int z,MineshaftPlan *out);
void mineshaft_generate_chunk(const MineshaftPlan *plan,int cx,int cz,uint8_t *cells,TerrainWide *wide);

#endif
