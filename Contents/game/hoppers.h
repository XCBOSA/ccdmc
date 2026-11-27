#ifndef VOXEL_HOPPERS_H
#define VOXEL_HOPPERS_H
#include "game.h"
#include "../world/barriers.h"
#include <stddef.h>

enum { HOPPER_SLOTS=5, HOPPER_TICKS=8, HOPPER_RADIUS=4, HOPPER_LIMIT=65536, HOPPER_PARTS=6 };
typedef struct { int x,y,z; ItemStack slots[HOPPER_SLOTS]; uint8_t cooldown; } Hopper;
const Hopper *hopper_at(int x,int y,int z);
int hopper_place(Hit hit);
const char *hopper_break(int x,int y,int z);
void hoppers_step(int player_x,int player_z);
void hoppers_reset(void);
void hopper_forget(int x,int y,int z);
int hopper_restore(const Hopper *h);
typedef int (*HopperVisitor)(const Hopper *,void *);
int hoppers_visit(Dimension dimension,HopperVisitor visit,void *context);
/* Exactly one index space for manual movement, starting backpack slots at 5. */
int hopper_move(int x,int y,int z,int from,int to,int single);
int hopper_insert(int x,int y,int z,ItemStack item);
int hopper_extract(int x,int y,int z,int slot);
int hopper_boxes(int x,int y,int z,int raw,BlockBox out[HOPPER_PARTS]);
#endif
