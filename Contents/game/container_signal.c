#include "container_signal.h"
#include "chests.h"
#include "furnace.h"
#include "brewing.h"
#include "hoppers.h"
#include "droppers.h"

unsigned container_weight(const ItemStack *items,int count) {
    unsigned filled=0;
    for (int i=0;i<count;++i) {
        ItemStack item=items[i];
        const ItemDef *def=item.count ? item_def(item.type) : NULL;
        if (def && def->max_stack) filled+=(unsigned)item.count*(STACK_MAX/def->max_stack);
    }
    return filled;
}
int container_fill_signal(unsigned weight,int slots) {
    if (!weight || slots<=0) return 0;
    unsigned capacity=(unsigned)slots*STACK_MAX;
    return weight>=capacity ? 15 : 1+(int)(14u*weight/capacity);
}
int container_signal(int x,int y,int z,int type) {
    if (is_chest(type)) return chest_signal((ChestPos){x,y,z});
    if (type==FURNACE) {
        const Furnace *f=furnace_at(x,y,z);
        return f ? container_fill_signal(container_weight(f->slots,FURNACE_SLOTS),FURNACE_SLOTS) : 0;
    }
    if (type==BREWING_STAND) {
        const BrewingStand *b=brew_at(x,y,z);
        return b ? container_fill_signal(container_weight(b->slots,BREW_SLOTS),BREW_SLOTS) : 0;
    }
    if (type==HOPPER) {
        const Hopper *h=hopper_at(x,y,z);
        return h ? container_fill_signal(container_weight(h->slots,HOPPER_SLOTS),HOPPER_SLOTS) : 0;
    }
    if (ejector_type(type)) {
        const Dropper *d=dropper_at(x,y,z);
        return d ? container_fill_signal(container_weight(d->slots,DROPPER_SLOTS),DROPPER_SLOTS) : 0;
    }
    return -1;
}
