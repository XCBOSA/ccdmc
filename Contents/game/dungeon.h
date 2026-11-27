#ifndef VOXEL_DUNGEON_GAME_H
#define VOXEL_DUNGEON_GAME_H

#include "game.h"

enum { DUNGEON_CLOCKS=256, DUNGEON_INITIAL_DELAY=20,
    DUNGEON_MIN_DELAY=200, DUNGEON_MAX_DELAY=799, DUNGEON_MOB_LIMIT=6 };
typedef struct {
    int x,y,z;
    uint32_t random;
    uint16_t delay;
} DungeonClock;

void dungeon_reset(void);
void dungeon_step(unsigned elapsed,Vec3 player);
const DungeonClock *dungeon_clocks(void);
int dungeon_restore_clock(DungeonClock clock);

#endif
