#ifndef VOXEL_DROPS_H
#define VOXEL_DROPS_H
#include "game.h"
#include "../world/barriers.h"
#include <stddef.h>

enum { DROP_EXPIRE_TICKS=6000, DROP_PICKUP_TICKS=10, DEATH_DROP_PICKUP_TICKS=40, DROP_SIM_RADIUS=5,
       DEATH_DROP_FILE_LIMIT=65536 };

Drop make_drop(float x,float y,float z,ItemStack item);
int drop_loose_item(Vec3 position,ItemStack item);
int drop_valid(Drop drop);
void drops_reset(void);
int drops_step(float dt,unsigned ticks);
int death_keep_inventory(void);
int death_set_keep_inventory(int keep);
/* Commits all items or none. Allocation failure leaves the inventory intact. */
int death_drop_inventory(Vec3 at);
void death_discard_inventory(void);
size_t death_drop_count(Dimension dimension);
int death_drop_restore(Dimension dimension,Drop drop);
typedef int (*DropVisitor)(const Drop *drop,void *ctx);
int death_drop_visit(Dimension dimension,DropVisitor visit,void *ctx);
void drops_visible(int cx,int cz,DropVisitor visit,void *ctx);
typedef int (*DropIntake)(const Drop *drop,void *ctx);
/* Callback returns the number absorbed. Unaccepted remainder stays in its entity. */
void drops_collect_automatic(int cx,int cz,DropIntake intake,void *ctx);
/* The dropped falling anvil itself may occupy the exempt ordinary slot. */
void drops_anvil_impact(BlockBox strike,int exempt);
#endif
