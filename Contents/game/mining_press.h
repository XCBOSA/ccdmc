#ifndef CCDMC_MINING_PRESS_H
#define CCDMC_MINING_PRESS_H

#include <stdint.h>

typedef struct { int hit,x,y,z,raw; unsigned duration; } MiningAim;
typedef struct { MiningAim aim; uint64_t due; int held,armed; } MiningPress;

MiningAim player_mining_aim(void);
void mining_press_begin(MiningPress *p,MiningAim aim,uint64_t now,unsigned elapsed);
void mining_press_aim(MiningPress *p,MiningAim aim,uint64_t now);
void mining_press_finish_attempt(MiningPress *p,MiningAim next,uint64_t now,int removed);
int mining_press_ready(const MiningPress *p,uint64_t now);
void mining_press_release(MiningPress *p);

#endif
