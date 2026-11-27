#ifndef VOXEL_PRESSURE_LAYOUT_H
#define VOXEL_PRESSURE_LAYOUT_H

/* Positions of the non-movable service equipment in each bank. */
enum { PRESSURE_FLOOR=38, PRESSURE_AQUIFER_HATCH_Z=-4, PRESSURE_LIFT_Z=3,
       PRESSURE_KILN_Z=-17, PRESSURE_VALVE_X=11, PRESSURE_VALVE_Z=-6 };
static inline int pressure_valve_site(int x,int y,int z) {
    return y==PRESSURE_FLOOR+1 && z==PRESSURE_VALVE_Z &&
        (x==-PRESSURE_VALVE_X || x==PRESSURE_VALVE_X);
}
#endif
