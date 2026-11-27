#ifndef VOXEL_DROPPERS_H
#define VOXEL_DROPPERS_H
#include "game.h"

/* Historical storage name; dropper and dispenser records share this bank.
   Their raw world cells carry the type and outlet direction. */
enum { DROPPER_SLOTS=9, DROPPER_DELAY=4, DROPPER_LIMIT=65536 };
typedef struct { int x,y,z; ItemStack slots[DROPPER_SLOTS]; uint32_t rolls; } Dropper;
typedef int (*DropperVisitor)(const Dropper *d,void *context);
const Dropper *dropper_at(int x,int y,int z);
/* Called by terrain activation just before the corresponding block edit. */
int dropper_seed_temple(int x,int y,int z);
int dropper_place(int x,int y,int z,int face);
int ejector_place(int x,int y,int z,int face,int type);
int dropper_move(int x,int y,int z,int from,int to,int single);
int dropper_insert(int x,int y,int z,ItemStack item);
int dropper_extract(int x,int y,int z,int slot);
/* Returns zero only if the receiving chunk is not ready. */
int dropper_fire(int x,int y,int z,int raw);
int droppers_present(void);
void dropper_forget(int x,int y,int z);
void droppers_reset(void);
int dropper_restore(const Dropper *d);
int droppers_visit(Dimension d,DropperVisitor visit,void *context);
#endif
