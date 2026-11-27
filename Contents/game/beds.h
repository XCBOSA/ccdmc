#ifndef VOXEL_BEDS_H
#define VOXEL_BEDS_H
#include "game.h"

typedef struct { int x,y,z; } BedPos;
typedef struct { int valid; BedPos head; } BedAnchor;
int bed_place(BedPos foot,int direction);
const char *bed_break(BedPos part);
const char *bed_use(Hit hit);
int bed_sleeping(void);
void bed_cancel_sleep(void);
void bed_step(unsigned ticks);
void bed_reset(void);
BedAnchor bed_anchor(void);
int bed_restore_anchor(BedAnchor anchor);
int bed_respawn_position(Vec3 *out);
const char *bed_take_message(void);

#endif
