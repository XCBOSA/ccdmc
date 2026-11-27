#include "../game/chests.h"
#include "../game/droppers.h"
#include "../game/drops.h"
#include "../game/hoppers.h"
#include "../game/interaction.h"
#include "../world/redstone.h"
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { printf("DROPPER TEST failed line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
enum { Y=150 };
static ItemStack stack(int type,int count) { return (ItemStack){(uint16_t)type,(uint8_t)count,0,0,0}; }
static ItemStack at_slot(int x,int y,int z,int slot) {
    const Dropper *d=dropper_at(x,y,z);
    return d ? d->slots[slot] : stack(AIR,0);
}
static int load(int x,int y,int z,int slot,int type,int count) {
    inventory[0]=stack(type,count);
    return dropper_move(x,y,z,DROPPER_SLOTS,slot,0) && !inventory[0].count;
}
static void ticks(int x,int z,int ticks_left) { while (ticks_left--) redstone_step(x,z); }
static int emitted(int type) {
    int count=0;
    for (int i=0;i<MAXDROP;++i) if (drops[i].item.type==type) count+=drops[i].item.count;
    return count;
}
int dropper_selftest(void) {
    int failures=0;
    world_begin(3071923); generate();
    player_reset_rules(SURVIVAL); inventory_reset();
    inventory[0]=stack(COBBLESTONE,7); inventory[1]=stack(REDSTONE_DUST,1);
    CHECK(craft_take((int[]){0,0,0,0,-1,0,0,1,0},3) && inventory[0].type==DROPPER_ITEM);

    /* Player faces north; the opening must point back south, not north. */
    CHECK(set_block(-17,Y,-1,STONE));
    px=-16.5f; py=149.01f; pz=2.6f; yaw=0; pitch=0; selected_slot=0;
    CHECK(game_tap().panel==INTERACTION_NONE && world_cell(-17,Y,0)==DROPPER_BASE+SOUTH && !inventory[0].count);
    CHECK(game_tap().panel==INTERACTION_EJECTOR);
    CHECK(load(-17,Y,0,8,ARROW,3));
    CHECK(set_block(-18,Y-1,0,STONE) && redstone_place_lever((CircuitPos){-18,Y,0},(CircuitPos){-18,Y-1,0},1));
    CHECK(redstone_toggle_lever(-18,Y,0)==2);
    ticks(-17,0,4);
    CHECK(at_slot(-17,Y,0,8).count==3 && !emitted(ARROW));
    CHECK(redstone_toggle_lever(-18,Y,0)==1);
    ticks(-17,0,1);
    CHECK(at_slot(-17,Y,0,8).count==2 && emitted(ARROW)==1 && drops[0].z>1.f);
    ticks(-17,0,9);
    CHECK(emitted(ARROW)==1);
    CHECK(redstone_toggle_lever(-18,Y,0)==2);
    ticks(-17,0,30);
    CHECK(emitted(ARROW)==2 && at_slot(-17,Y,0,8).count==1);

    /* The upper signal is not a direct neighbour of the dropper. The
       front output still crosses a chunk seam into an ordinary chest. */
    CHECK(dropper_place(-17,Y,7,EAST) && set_block(-16,Y,7,CHEST));
    CHECK(load(-17,Y,7,0,IRON_INGOT,3) && set_block(-17,Y+2,7,REDSTONE_BLOCK));
    ticks(-17,7,5);
    ChestView receiver;
    CHECK(chest_open((ChestPos){-16,Y,7},&receiver)==CHEST_OK &&
        chest_slot(&receiver,0).type==IRON_INGOT && chest_slot(&receiver,0).count==1);
    CHECK(set_block(-18,Y-1,7,STONE) && redstone_place_comparator(-18,Y,7,WEST));
    ticks(-17,7,4);
    CHECK(redstone_comparator_power(-18,Y,7)==1);

    /* A full nine-slot receiver must not turn an unsuccessful insert into
       a world drop, and the source must not lose one. An upward chain works. */
    CHECK(dropper_place(4,Y,4,DROPPER_UP-DROPPER_BASE) && dropper_place(4,Y+1,4,SOUTH));
    for (int i=0;i<DROPPER_SLOTS;++i) CHECK(load(4,Y+1,4,i,PLANKS,64));
    CHECK(load(4,Y,4,0,DIAMOND,2) && set_block(5,Y,4,REDSTONE_BLOCK));
    ticks(4,4,5);
    CHECK(at_slot(4,Y,4,0).count==2 && !emitted(DIAMOND));
    CHECK(dropper_move(4,Y+1,4,8,DROPPER_SLOTS+1,0));
    CHECK(set_block(5,Y,4,AIR)); ticks(4,4,1);
    CHECK(set_block(5,Y,4,REDSTONE_BLOCK)); ticks(4,4,5);
    CHECK(at_slot(4,Y,4,0).count==1 && at_slot(4,Y+1,4,8).type==DIAMOND && !emitted(DIAMOND));

    CHECK(dropper_place(9,Y,0,SOUTH) && set_block(9,Y-2,0,CHEST));
    CHECK(hopper_place((Hit){1,9,Y-2,0,9,Y-1,0}) && hopper_place((Hit){1,9,Y,0,9,Y+1,0}));
    inventory[0]=stack(QUARTZ,2);
    CHECK(hopper_move(9,Y+1,0,HOPPER_SLOTS,0,0));
    hoppers_step(9,0);
    const Hopper *below=hopper_at(9,Y-1,0);
    CHECK(at_slot(9,Y,0,0).type==QUARTZ || (below && below->slots[0].type==QUARTZ));
    for (int i=0;i<30;++i) hoppers_step(9,0);
    CHECK(chest_open((ChestPos){9,Y-2,0},&receiver)==CHEST_OK && chest_slot(&receiver,0).count==2);

    /* A pickaxe harvests the block and the remaining stack, after removing
       the latched device. Reset must also remove the second dimension bank. */
    px=-16.5f; py=149.01f; pz=2.6f; yaw=0; pitch=0; selected_slot=0;
    inventory[0]=stack(WOOD_PICKAXE,1);
    drops_reset();
    CHECK(mine() && world_cell(-17,Y,0)==AIR && !dropper_at(-17,Y,0) &&
        emitted(DROPPER_ITEM)==1 && emitted(ARROW)==1 && inventory[0].damage==1);
    CHECK(world_select_dimension(NETHER) && !droppers_present() && !dropper_at(4,Y,4));
    CHECK(world_select_dimension(OVERWORLD) && dropper_at(4,Y,4));
    world_reset(); drops_reset();
    printf("DROPPER TEST craft/aim/pulse/latch/quasi/vertical/chunk seam/comparator/hopper/backpressure/break failures=%d\n",failures);
    return failures;
}
