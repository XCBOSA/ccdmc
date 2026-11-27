#include "dispenser_actions.h"
#include "actors.h"
#include "armor.h"
#include "boats.h"
#include "blast.h"
#include "fire.h"
#include "fireworks.h"
#include "spawn_eggs.h"
#include "drops.h"
#include "enchanting.h"
#include "combat.h"
#include "farming.h"
#include "minecarts.h"
#include "portals.h"
#include "stack_transfer.h"
#include "../world/space.h"
#include <math.h>

static DispenseResult used_if(ItemStack *item,int committed) {
    if (!committed) return DISPENSE_BLOCKED;
    stack_deduct(item,1);
    return DISPENSE_USED;
}
static DispenseResult bucket(ItemStack *slots,int count,int index,DispenseOutlet out,int raw) {
    ItemStack *held=&slots[index];
    if (held->type!=BUCKET) {
        int liquid=held->type==WATER_BUCKET ? WATER : LAVA;
        if (raw!=AIR && raw!=FIRE_CELL &&
            !(liquid==LAVA ? lava_flow_cell(raw) : water_strength(raw) && raw!=WATER)) return DISPENSE_EJECT;
        if (!(liquid==WATER && world_dimension()==NETHER) && !set_block(out.x,out.y,out.z,liquid))
            return DISPENSE_BLOCKED;
        *held=(ItemStack){BUCKET,1,0,0,0};
        return DISPENSE_USED;
    }
    if (raw!=WATER && raw!=LAVA) return DISPENSE_EJECT;
    ItemStack result={(uint16_t)(raw==WATER ? WATER_BUCKET : LAVA_BUCKET),1,0,0,0};
    int freed=held->count==1 ? index : -1;
    for (int i=0;freed<0 && i<count;++i) if (!slots[i].count) freed=i;
    int drop_slot=-1;
    Drop drop=make_drop(out.muzzle.x,out.muzzle.y,out.muzzle.z,result);
    if (freed<0 && drop_valid(drop))
        for (int i=0;i<MAXDROP;++i) if (!drops[i].item.count) { drop_slot=i; break; }
    if ((freed<0 && drop_slot<0) || !set_block(out.x,out.y,out.z,AIR)) return DISPENSE_BLOCKED;
    stack_deduct(held,1);
    if (freed>=0) slots[freed]=result;
    else { drops[drop_slot]=drop; drops_dirty=1; }
    return DISPENSE_USED;
}
DispenseResult dispenser_action(ItemStack *slots,int count,int index,DispenseOutlet out) {
    if (!slots || index<0 || index>=count || !slots[index].count) return DISPENSE_BLOCKED;
    ItemStack *held=&slots[index];
    int raw=world_cell(out.x,out.y,out.z);
    Vec3 direction={(float)out.dx,(float)out.dy,(float)out.dz};
    if (spawn_egg_item(held->type))
        return used_if(held,spawn_egg_at(held->type,(Vec3){out.x+.5f,out.y+.025f,out.z+.5f}));
    /* Put the projectile inside its first free path, not the machine. A
       blocked mouth will still collide before an entity behind the wall. */
    switch (held->type) {
    case FIREWORK_ROCKET:
        return used_if(held,firework_launch(*held,out.muzzle,atan2f((float)out.dx,(float)-out.dz)));
    case FIRE_CHARGE:
        return used_if(held,fire_use_igniter(out.x,out.y,out.z));
    case ARROW: return used_if(held,combat_dispenser_arrow(out.muzzle,direction));
    case EGG: return used_if(held,actors_launch_egg(out.muzzle,direction));
    case SNOWBALL: return used_if(held,actors_launch_snowball(out.muzzle,direction));
    case SPLASH_WEAKNESS: case SPLASH_WEAKNESS_LONG:
    case SPLASH_WATER_BREATHING: case SPLASH_WATER_BREATHING_LONG:
    case SPLASH_NIGHT_VISION: case SPLASH_NIGHT_VISION_LONG:
        return used_if(held,actors_launch_splash(out.muzzle,direction,held->type));
    case BUCKET: case WATER_BUCKET: case LAVA_BUCKET: return bucket(slots,count,index,out,raw);
    case BOAT: {
        int water_y=raw==AIR ? out.y-1 : out.y;
        if (!water_strength(world_cell(out.x,water_y,out.z))) return DISPENSE_EJECT;
        return used_if(held,boat_launch_on_water(out.x,water_y,out.z,.125f*out.dx,.125f*out.dz,
            atan2f((float)out.dx,(float)-out.dz)));
    }
    case MINECART: case TNT_MINECART: case CHEST_MINECART: case HOPPER_MINECART: case FURNACE_MINECART: {
        RailCell rail={out.x,out.y-(raw==AIR),out.z};
        if (!rail_type(block(rail.x,rail.y,rail.z))) return DISPENSE_EJECT;
        return used_if(held,held->type==FURNACE_MINECART ? cart_launch_furnace_on_rail(rail) :
            held->type==HOPPER_MINECART ? cart_launch_hopper_on_rail(rail) :
            held->type==CHEST_MINECART ? cart_launch_chest_on_rail(rail) :
            held->type==TNT_MINECART ? cart_launch_tnt_on_rail(rail) : cart_launch_on_rail(rail));
    }
    case BONE_MEAL: return used_if(held,farm_bonemeal(out.x,out.y,out.z)>0);
    case TNT_ITEM:
        return solid(cell_type(raw)) ? DISPENSE_EJECT : used_if(held,blast_dispense(out.x,out.y,out.z));
    case FLINT_AND_STEEL:
        if (!fire_use_igniter(out.x,out.y,out.z)) return DISPENSE_BLOCKED;
        enchant_wear(held,1,0);
        return DISPENSE_USED;
    default: {
        int gear=armor_slot(held->type);
        if (gear<0 || !player_rules.health || armor_worn(gear).count) return DISPENSE_EJECT;
        BlockBox front={(float)out.x,(float)out.y,(float)out.z,out.x+1.f,out.y+1.f,out.z+1.f};
        if (!box_overlap(front,body_box(px,py,pz,.28f,1.72f))) return DISPENSE_EJECT;
        return used_if(held,armor_restore(gear,*held));
    }
    }
}
