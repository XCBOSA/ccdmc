#ifndef VOXEL_RAILS_H
#define VOXEL_RAILS_H

#include "world.h"

enum { NORTH, EAST, SOUTH, WEST };
enum { STRAIGHT_NS, STRAIGHT_EW, RISE_E, RISE_W, RISE_N, RISE_S, BEND_NE, BEND_ES, BEND_SW, BEND_WN };
typedef struct { int x,y,z; } RailCell;
typedef struct { int dir,up; } RailEnd;
typedef enum { RAIL_NORMAL, RAIL_BOOSTER, RAIL_DETECTOR, RAIL_ACTIVATOR } RailKind;
typedef struct { RailEnd ends[2]; RailKind kind; int shape; } RailPiece;
typedef struct { float x,y,z,dx,dy,dz; } RailSample;

static inline int rail_dx(int dir) { return dir==EAST ? 1 : dir==WEST ? -1 : 0; }
static inline int rail_dz(int dir) { return dir==SOUTH ? 1 : dir==NORTH ? -1 : 0; }
int rail_decode(int cell,RailPiece *piece);
int rail_at(RailCell p,RailPiece *piece);
int rail_link(RailCell p,int end,RailCell *next,int *entry);
RailSample rail_sample(RailCell p,RailPiece rail,float t);
int rail_is_energized(RailCell p);
int rail_place(RailCell p,RailKind kind,int east_west);
/* A change under a minecart is deferred. Redstone queues the retry. */
int rail_refresh_junction(RailCell p);
void rail_after_edit(int x,int y,int z,int old_cell,int new_cell);

#endif
