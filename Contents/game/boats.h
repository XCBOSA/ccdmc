#ifndef VOXEL_BOATS_H
#define VOXEL_BOATS_H

#include "game.h"

enum { BOAT_CAPACITY = 24 };
#define BOAT_BODY_RADIUS .64f
#define BOAT_BODY_HEIGHT .42f
typedef struct {
    Vec3 position;
    float heading;
    int occupied;
} BoatRecord;

void boats_reset(void);
unsigned boats_revision(void);
const BoatRecord *boats_in(Dimension dimension);
int boat_restore(Dimension dimension, BoatRecord record);
int boat_rider(void);
int boat_restore_rider(int index);
void boat_unseat(void);
const char *boat_dismount(void);
const char *boat_use(void);
int boat_launch_on_water(int x,int y,int z,float offset_x,float offset_z,float heading);
const char *boat_take_down(void);
int boat_overlaps_cell(int x,int y,int z);
/* Move this dimension's hulls and drive the ridden boat. Return true if the
   rider position was supplied here instead of by normal player movement. */
int boats_step(float dt, int keyboard_side, int keyboard_front);

#endif
