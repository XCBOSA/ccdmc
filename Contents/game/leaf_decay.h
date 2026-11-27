#ifndef VOXEL_LEAF_DECAY_H
#define VOXEL_LEAF_DECAY_H

#include "game.h"
#include <stddef.h>

enum { LEAF_TILE=8, LEAF_WORK_LIMIT=65536, LEAF_DELAY_LIMIT=28 };
typedef struct {
    int x,y,z;
    unsigned cursor,delay;
    uint64_t layers[LEAF_TILE];
} LeafWork;
typedef int (*LeafWorkVisitor)(const LeafWork *work,void *context);

/* Allocate before any world write, mark only after a successful transaction,
   then finish before invoking other edits (fluids, rails, redstone). */
int leaves_prepare_edit(int x,int y,int z,int before,int after);
void leaves_commit_edit(int x,int y,int z,int before,int after);
void leaves_finish_edits(void);
void leaves_reset(void);
void leaves_step(int player_x,int player_z);
size_t leaves_pending(Dimension d);
int leaves_visit(Dimension d,LeafWorkVisitor visitor,void *context);
int leaves_restore(Dimension d,const LeafWork *work);

#endif
