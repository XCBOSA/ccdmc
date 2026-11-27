#include "mining_press.h"

static uint64_t deadline(uint64_t now,unsigned delay) {
    return now>UINT64_MAX-delay ? UINT64_MAX : now+delay;
}

void mining_press_begin(MiningPress *p,MiningAim aim,uint64_t now,unsigned elapsed) {
    *p=(MiningPress){aim,deadline(now,aim.duration>elapsed ? aim.duration-elapsed : 0),1,1};
}

void mining_press_aim(MiningPress *p,MiningAim aim,uint64_t now) {
    if (!p->held || (p->aim.hit==aim.hit &&
        (!aim.hit || (p->aim.x==aim.x && p->aim.y==aim.y && p->aim.z==aim.z &&
            p->aim.raw==aim.raw && p->aim.duration==aim.duration)))) return;
    p->aim=aim;
    p->armed=aim.hit;
    p->due=deadline(now,aim.duration);
}

void mining_press_finish_attempt(MiningPress *p,MiningAim next,uint64_t now,int removed) {
    /* Successful blocks can advance the ray to the next block. An actor or
       blocked placement does not repeat without selecting a new target. */
    p->armed=p->held && removed && next.hit;
    p->aim=next;
    p->due=deadline(now,next.duration);
}

int mining_press_ready(const MiningPress *p,uint64_t now) { return p->held && p->armed && now>=p->due; }
void mining_press_release(MiningPress *p) { *p=(MiningPress){0}; }
