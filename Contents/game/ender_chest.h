#ifndef VOXEL_ENDER_CHEST_H
#define VOXEL_ENDER_CHEST_H

#include "chests.h"

typedef struct {
    Dimension dimension;
    ChestPos position;
    uint64_t epoch;
} EnderChestView;

/* One private inventory per player in this local world, not per block. */
void ender_chest_reset(void);
const ItemStack *ender_chest_items(void);
int ender_chest_restore(const ItemStack items[CHEST_SLOTS]);
ChestResult ender_chest_open(ChestPos p,EnderChestView *view);
int ender_chest_valid(const EnderChestView *view);
ItemStack ender_chest_item(const EnderChestView *view,int index);
/* Same slot numbering as the shared container UI: backpack starts at 54. */
ChestResult ender_chest_move(const EnderChestView *view,int from,int to,int single);

#endif
