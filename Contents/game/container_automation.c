#include "container_automation.h"
#include "brewing.h"
#include "chests.h"
#include "droppers.h"
#include "furnace.h"
#include "hoppers.h"
#include "minecarts.h"

int container_insert_one(int x,int y,int z,int motion_y,ItemStack item) {
    int type=block(x,y,z);
    if (is_chest(type)) {
        ChestView chest;
        return chest_automation_open((ChestPos){x,y,z},&chest) && chest_automation_insert(&chest,item);
    }
    if (type==HOPPER) return hopper_insert(x,y,z,item);
    if (ejector_type(type)) return dropper_insert(x,y,z,item);
    if (type==FURNACE) return motion_y<=0 && furnace_automation_insert(x,y,z,motion_y<0 ?
        FURNACE_INPUT : FURNACE_FUEL,item);
    if (type==BREWING_STAND) return motion_y<=0 && brew_automation_insert(x,y,z,motion_y<0,item);
    int cart=cart_container_at(x,y,z);
    return cart<0 ? NO_INVENTORY : cart_container_insert(cart,item);
}

static int accepted(ContainerIntake accept,ItemStack item,void *ctx) {
    if (!item.count) return 0;
    item.count=1;
    return accept(item,ctx);
}

int container_extract_one(int x,int y,int z,ContainerIntake accept,void *ctx) {
    if (!accept) return INVENTORY_REJECTED;
    int type=block(x,y,z);
    if (is_chest(type)) {
        ChestView chest;
        if (chest_automation_open((ChestPos){x,y,z},&chest))
            for (int i=0;i<chest.count*CHEST_SLOTS;++i)
                if (accepted(accept,chest_slot(&chest,i),ctx)) {
                    chest_automation_remove(&chest,i); return INVENTORY_INSERTED;
                }
    } else if (type==HOPPER) {
        const Hopper *h=hopper_at(x,y,z);
        if (h) for (int i=0;i<HOPPER_SLOTS;++i)
            if (accepted(accept,h->slots[i],ctx)) {
                hopper_extract(x,y,z,i); return INVENTORY_INSERTED;
            }
    } else if (ejector_type(type)) {
        const Dropper *d=dropper_at(x,y,z);
        if (d) for (int i=0;i<DROPPER_SLOTS;++i)
            if (accepted(accept,d->slots[i],ctx)) {
                dropper_extract(x,y,z,i); return INVENTORY_INSERTED;
            }
    } else if (type==FURNACE) {
        const Furnace *f=furnace_at(x,y,z);
        if (f && accepted(accept,f->slots[FURNACE_OUTPUT],ctx)) {
            furnace_automation_take_output(x,y,z);
            return INVENTORY_INSERTED;
        }
    } else if (type==BREWING_STAND) {
        const BrewingStand *s=brew_at(x,y,z);
        if (s) for (int i=0;i<BREW_BOTTLES;++i)
            if (s->slots[i].type!=WATER_BOTTLE && accepted(accept,s->slots[i],ctx)) {
                brew_automation_take_bottle(x,y,z,i); return INVENTORY_INSERTED;
            }
    } else {
        Dimension d=world_dimension();
        int cart=cart_container_at(x,y,z);
        if (cart<0) return NO_INVENTORY;
        const ItemStack *items=cart_cargo_in(d,cart);
        int n=cart_cargo_count(d,cart);
        if (items) for (int i=0;i<n;++i)
            if (accepted(accept,items[i],ctx)) {
                cart_container_extract(cart,i); return INVENTORY_INSERTED;
            }
    }
    return INVENTORY_REJECTED;
}
