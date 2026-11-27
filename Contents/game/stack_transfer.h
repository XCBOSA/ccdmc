#ifndef VOXEL_STACK_TRANSFER_H
#define VOXEL_STACK_TRANSFER_H
#include "game.h"

/* Shared by manual storage and automation. Preserve the complete item
   identity (wear, enchantments, custom name), never just its type. */
static inline int stack_receive(ItemStack *to,ItemStack from,int limit) {
    const ItemDef *def=from.count ? item_def(from.type) : NULL;
    if (!def || limit<=0 || (to->count && !item_same_kind(*to,from))) return 0;
    int count=def->max_stack-to->count;
    if (count>from.count) count=from.count;
    if (count>limit) count=limit;
    if (count<=0) return 0;
    from.count=(uint8_t)(to->count+count);
    *to=from;
    return count;
}
static inline void stack_deduct(ItemStack *from,int count) {
    from->count=(uint8_t)(from->count-count);
    if (!from->count) *from=(ItemStack){0,0,0,0,0};
}
static inline int storage_stack_move(ItemStack *slots,int count,int from,int to,int single) {
    if (from==to || (unsigned)from>=(unsigned)(count+SLOT_COUNT) ||
        (unsigned)to>=(unsigned)(count+SLOT_COUNT)) return 0;
    ItemStack *src=from<count ? &slots[from] : &inventory[from-count];
    ItemStack *dst=to<count ? &slots[to] : &inventory[to-count];
    if (!src->count) return 0;
    int n=stack_receive(dst,*src,single ? 1 : STACK_MAX);
    if (n) stack_deduct(src,n);
    else if (!single && dst->count && !item_same_kind(*src,*dst)) {
        ItemStack old=*src; *src=*dst; *dst=old;
    } else return 0;
    return 1;
}
#endif
