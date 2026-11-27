#ifndef VOXEL_PISTONS_H
#define VOXEL_PISTONS_H

#include "game.h"
#include "../world/redstone.h"

enum { PISTON_PARTS=2 };
CircuitPos piston_offset(CircuitPos p,int facing,int distance);
/* Computes the expected other half of an extended piston without a world read. */
int piston_peer(CircuitPos p,int raw,CircuitPos *other,int *expected);
int piston_boxes(int x,int y,int z,int raw,BlockBox boxes[PISTON_PARTS]);
int piston_place(int x,int y,int z,int direction,int sticky);
const char *piston_break(Hit h);
/* A zero result asks the redstone queue to retry (unloaded terrain or memory). */
int piston_refresh(CircuitPos base,int raw,int powered);
int piston_nudge(BlockBox sweep,int dir,BlockBox body,Vec3 *shift);

#endif
