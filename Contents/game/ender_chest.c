#include "ender_chest.h"
#include "stack_transfer.h"
#include "../world/light.h"
#include <string.h>

static ItemStack slots[CHEST_SLOTS];
static uint64_t epoch=1;

void ender_chest_reset(void) {
    memset(slots,0,sizeof slots);
    if (!++epoch) ++epoch;
}
const ItemStack *ender_chest_items(void) { return slots; }
int ender_chest_restore(const ItemStack items[CHEST_SLOTS]) {
    if (!items) return 0;
    for (int i=0;i<CHEST_SLOTS;++i) if (!item_stack_valid(items[i])) return 0;
    memcpy(slots,items,sizeof slots);
    return 1;
}

static ChestResult access(ChestPos p) {
    if (!inworld(p.x,p.y,p.z) || !ender_chest_cell(world_cell(p.x,p.y,p.z))) return CHEST_MISSING;
    if (p.y+1<WY && light_opacity(world_cell(p.x,p.y+1,p.z))>=16) return CHEST_BLOCKED;
    return CHEST_OK;
}
ChestResult ender_chest_open(ChestPos p,EnderChestView *view) {
    if (!view) return CHEST_MISSING;
    memset(view,0,sizeof *view);
    ChestResult ok=access(p);
    if (ok==CHEST_OK) *view=(EnderChestView){world_dimension(),p,epoch};
    return ok;
}
int ender_chest_valid(const EnderChestView *view) {
    return view && view->epoch==epoch && view->dimension==world_dimension() &&
        access(view->position)==CHEST_OK;
}
static int mapped(int index) {
    if ((unsigned)index<CHEST_SLOTS) return index;
    return index>=DOUBLE_CHEST_SLOTS && index<DOUBLE_CHEST_SLOTS+SLOT_COUNT ?
        index-DOUBLE_CHEST_SLOTS+CHEST_SLOTS : -1;
}
ItemStack ender_chest_item(const EnderChestView *view,int index) {
    int i=mapped(index);
    return i<0 || !ender_chest_valid(view) ? (ItemStack){0,0,0,0,0} :
        i<CHEST_SLOTS ? slots[i] : inventory[i-CHEST_SLOTS];
}
ChestResult ender_chest_move(const EnderChestView *view,int from,int to,int single) {
    if (!ender_chest_valid(view)) return CHEST_MISSING;
    return storage_stack_move(slots,CHEST_SLOTS,mapped(from),mapped(to),single) ? CHEST_OK : CHEST_NO_MOVE;
}
