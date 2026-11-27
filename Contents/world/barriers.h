#ifndef VOXEL_BARRIERS_H
#define VOXEL_BARRIERS_H

#include "rails.h"

typedef struct { float x0,y0,z0,x1,y1,z1; } BlockBox;
typedef struct { int facing,open,powered; } GateState;
enum { BARRIER_PARTS=12 };
enum { BARRIER_VISUAL, BARRIER_BODY, BARRIER_PICK };
#define BARRIER_HEIGHT 1.5f

int gate_decode(int raw,GateState *out);
int gate_place(int x,int y,int z,int facing);
int gate_toggle(int x,int y,int z,int approach);
void barrier_after_edit(int x,int y,int z,int before,int after);
void gate_refresh_redstone(int x,int y,int z);
int door_support(int raw);
int door_place(int x,int y,int z,int facing,int right,int iron);
int door_toggle(int x,int y,int z);
int door_refresh_redstone(int x,int y,int z);
int door_item(int raw);
/* Returns the matching, closed wooden door's lower cell and normalizes y. */
int door_closed_wood(int x,int *y,int z);
int door_break_closed_wood(int x,int y,int z,int expected);
int wall_attachment_support(int raw);
int wall_attachment_place(int x,int y,int z,int side,int ladder,int top);
int trapdoor_toggle(int x,int y,int z);
int trapdoor_refresh_redstone(int x,int y,int z);

/* Neighbours are N,E,S,W. Give the mesher its already-borrowed columns;
   physics and selection pass live cells through the very same connection rule. */
unsigned fence_connections(int raw,const int adjacent[4]);
unsigned pane_connections(int raw,const int adjacent[4]);
enum { PANE_CENTER=4 };
#define PANE_HALF_WIDTH .0625f
/* Center is permanent; arm positions are N,E,S,W. A zero mask gives the
   classic isolated cross. Each arm stops at the edge of the center. */
static inline unsigned pane_parts(unsigned links) { return (links ? links : 15u)|(1u<<PANE_CENTER); }
BlockBox pane_part(int dir);
int barrier_boxes(int raw,unsigned links,int purpose,BlockBox out[BARRIER_PARTS]);
int barrier_world_boxes(int x,int y,int z,int raw,int purpose,BlockBox out[BARRIER_PARTS]);
int box_overlap(BlockBox a,BlockBox b);
int box_contains(BlockBox box,float x,float y,float z);

#endif
