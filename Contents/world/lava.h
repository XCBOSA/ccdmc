#ifndef CCDMC_LAVA_H
#define CCDMC_LAVA_H

#include "world.h"

enum { LAVA_NETHER_TICKS=10, LAVA_SURFACE_TICKS=30 };

void lava_step(void);
void lava_reset(void);
void lava_notify_edit(int x,int y,int z,int before,int after);
/* The visitor's final argument is the remaining delay in game ticks. */
int lava_visit(Dimension dim,WorldRecord visit,void *context);
int lava_restore(int x,int y,int z,int delay);

static inline int lava_quenched_block(int raw) {
    if (raw==LAVA) return OBSIDIAN;
    return raw>=LAVA_FLOW_BASE+3 && raw<LAVA_FALLING ? COBBLESTONE : AIR;
}

#endif
