#ifndef VOXEL_MUSHROOM_SHAPE_H
#define VOXEL_MUSHROOM_SHAPE_H

#include "world.h"

/* Stem and cap geometry is shared by terrain and the atomic growth action.
   The level is measured from the small mushroom's cell. */
enum { MUSHROOM_RADIUS=3 };
static inline int huge_mushroom_part(int red,int height,int x,int level,int z) {
    if (level<0 || level>height) return AIR;
    if (x<0) x=-x;
    if (z<0) z=-z;
    if (!red && level==height)
        return x<=3 && z<=3 && (x!=3 || z!=3) ? BROWN_MUSHROOM_BLOCK : AIR;
    if (red && level==height) return x<=1 && z<=1 ? RED_MUSHROOM_BLOCK : AIR;
    if (red && level>=height-3 && x<=2 && z<=2 && ((x==2)!=(z==2))) return RED_MUSHROOM_BLOCK;
    return !x && !z ? MUSHROOM_STEM : AIR;
}
#endif
