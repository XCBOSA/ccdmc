#ifndef VOXEL_HEAT_BENCH_H
#define VOXEL_HEAT_BENCH_H

#include "game.h"

enum { HEAT_BENCH_TICKS_PER_UNIT=120, HEAT_BENCH_REPAIR=12,
       HEAT_BENCH_PANES=4, HEAT_BENCH_RECYCLE_COST=2 };

/* Empty hand collects idle heat; damaged iron tools and ordinary glass
   panes each choose a distinct use of the same finite stored resource. */
const char *heat_bench_use(Hit hit);
/* Supply through a mole with clay, or charge/discharge reusable cores. */
const char *heat_bench_core(Hit hit,int input);

#endif
