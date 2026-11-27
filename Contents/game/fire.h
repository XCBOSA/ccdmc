#ifndef VOXEL_FIRE_H
#define VOXEL_FIRE_H
#include "game.h"
#include "../world/barriers.h"

enum { FIRE_FILE_LIMIT=65536, FIRE_MAX_AGE=15, FIRE_MAX_DELAY=40 };
enum { IGNITE_BLOCKED, IGNITE_FLAME, IGNITE_TNT };
typedef struct { int x,y,z; uint16_t attempts; uint8_t age,delay; } FireCell;
typedef int (*FireVisitor)(const FireCell *fire,void *context);

void fires_reset(void);
void fires_step(int x,int z,unsigned ticks);
void fire_after_edit(int x,int y,int z,int before,int after);
int fire_ignite(int x,int y,int z);
int fire_use_igniter(int x,int y,int z);
int fire_restore(const FireCell *state);
int fire_visit(Dimension d,FireVisitor visitor,void *context);
int fire_contact(BlockBox body);
#endif
