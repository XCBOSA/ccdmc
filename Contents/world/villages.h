#ifndef CCDMC_VILLAGES_H
#define CCDMC_VILLAGES_H

#include "world.h"

enum { VILLAGE_REGION_EDGE=256, VILLAGE_RADIUS=22, VILLAGE_HOMES=5 };
typedef struct { int x,z,floor; Biome style; unsigned turn; } VillagePlan;

/* A deterministic plan for the 256-block grid region containing (x,z).
   This does not load or mutate any chunk. Coordinates must be in save range. */
int village_plan_for(int x,int z,VillagePlan *out);
/* Local village coordinates before seed-dependent quarter-turn rotation. */
void village_global(const VillagePlan *plan,int a,int b,int *x,int *z);
/* Location just outside the door, with floor at plan->floor. */
int village_home_entry(const VillagePlan *plan,int home,int *x,int *z);
int village_home_door(const VillagePlan *plan,int home,int *x,int *z,int *facing);
int village_home_contains(const VillagePlan *plan,int home,int x,int z);
int village_smith_chest(const VillagePlan *plan,int *x,int *y,int *z);
/* Natural wall attachments, for support changes across an unloaded seam. */
int village_natural_ladder(int x,int y,int z);

/* Apply only the structure overlay to one already-generated terrain chunk. */
struct TerrainWide;
void village_generate_chunk(const VillagePlan *plan,int cx,int cz,uint8_t *cells,uint8_t *tops,int smith_loot,
    struct TerrainWide *wide);

#endif
