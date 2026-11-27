#ifndef VOXEL_CONTAINER_AUTOMATION_H
#define VOXEL_CONTAINER_AUTOMATION_H
#include "game.h"

enum { NO_INVENTORY=-1, INVENTORY_REJECTED, INVENTORY_INSERTED };
/* Vertical motion, not the target face: -1 = insert into top. Caller owns
   the source and must have checked that the target chunk is available. */
int container_insert_one(int x,int y,int z,int motion_y,ItemStack item);
/* Offer one item from the bottom; deduct only if the destination accepted it.
   Return NO_INVENTORY, INVENTORY_REJECTED, or INVENTORY_INSERTED. */
typedef int (*ContainerIntake)(ItemStack item,void *ctx);
int container_extract_one(int x,int y,int z,ContainerIntake accept,void *ctx);
#endif
