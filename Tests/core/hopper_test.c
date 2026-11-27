#include "../game/hoppers.h"
#include "../game/brewing.h"
#include "../game/chests.h"
#include "../game/container_signal.h"
#include "../game/drops.h"
#include "../game/furnace.h"
#include "../game/interaction.h"
#include "../world/redstone.h"
#include <stdio.h>

#define CHECK_HOPPER(c) do { if (!(c)) { printf("HOPPER TEST failed line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
enum { Y=150 };
static int put(int x,int y,int z,int ax,int ay,int az) {
    return hopper_place((Hit){1,ax,ay,az,x,y,z});
}
static void ticks(int x,int z,int n) { while (n--) { redstone_step(x,z); hoppers_step(x,z); } }
static ItemStack stack(int item,int count) { return (ItemStack){(uint16_t)item,(uint8_t)count,0,0,0}; }
static ItemStack anything_at(int x,int y,int z,int slot) {
    const Hopper *h=hopper_at(x,y,z);
    return h ? h->slots[slot] : stack(AIR,0);
}
static ItemStack chest_contents(int x,int y,int z,int i) {
    ChestView box;
    return chest_automation_open((ChestPos){x,y,z},&box) ? chest_slot(&box,i) : stack(AIR,0);
}
static int load_hopper(int x,int y,int z,int slot,int item,int count) {
    inventory[0]=stack(item,count);
    return hopper_move(x,y,z,HOPPER_SLOTS,slot,0) && !inventory[0].count;
}
static int load_chest(int x,int y,int z,int slot,int item,int count) {
    ChestView box;
    inventory[0]=stack(item,count);
    return chest_open((ChestPos){x,y,z},&box)==CHEST_OK && chest_move(&box,DOUBLE_CHEST_SLOTS,slot,0)==CHEST_OK;
}
int hopper_selftest(void) {
    int failures=0;
    world_begin(882551);
    generate();
    inventory_reset();
    inventory[0]=stack(IRON_INGOT,5); inventory[1]=stack(CHEST,1);
    CHECK_HOPPER(craft_take((int[]){0,-1,0,0,1,0,-1,0,-1},3) && inventory[0].type==HOPPER_ITEM);
    CHECK_HOPPER(set_block(-16,Y,0,CHEST) && set_block(-17,Y+1,0,CHEST) &&
        load_chest(-17,Y+1,0,0,DIAMOND,3) && set_block(-17,Y+2,0,STONE));
    CHECK_HOPPER(put(-17,Y,0,-16,Y,0) && world_cell(-17,Y,0)==HOPPER_BASE+EAST);
    px=-16.5f; py=149.4f; pz=2.5f; yaw=0; pitch=-.07f;
    inventory_reset();
    CHECK_HOPPER(game_tap().panel==INTERACTION_HOPPER);
    CHECK_HOPPER(set_block(-17,Y,1,REDSTONE_BLOCK));
    ticks(-16,0,10);
    CHECK_HOPPER(!anything_at(-17,Y,0,0).count && chest_contents(-17,Y+1,0,0).count==3);
    CHECK_HOPPER(set_block(-17,Y,1,AIR));
    ticks(-16,0,1);
    CHECK_HOPPER(anything_at(-17,Y,0,0).count==1 && chest_contents(-17,Y+1,0,0).count==2 &&
        container_signal(-17,Y,0,HOPPER)==1 && !chest_contents(-16,Y,0,0).count);
    ticks(-16,0,7);
    CHECK_HOPPER(!chest_contents(-16,Y,0,0).count);
    ticks(-16,0,1);
    CHECK_HOPPER(chest_contents(-16,Y,0,0).count==1 && chest_contents(-17,Y+1,0,0).count==1);
    ticks(-16,0,16);
    CHECK_HOPPER(chest_contents(-16,Y,0,0).count==3 && !anything_at(-17,Y,0,0).count);
    CHECK_HOPPER(world_select_dimension(NETHER) && !hopper_at(-17,Y,0) &&
        world_select_dimension(OVERWORLD) && hopper_at(-17,Y,0));

    CHECK_HOPPER(set_block(0,Y,0,FURNACE) && set_block(0,Y-2,0,CHEST) &&
        put(0,Y+1,0,0,Y,0) && put(1,Y,0,0,Y,0) && put(0,Y-1,0,0,Y-2,0));
    CHECK_HOPPER(load_hopper(0,Y+1,0,0,IRON_ORE,1) && load_hopper(1,Y,0,0,COAL,1));
    ticks(0,0,1);
    const Furnace *f=furnace_at(0,Y,0);
    CHECK_HOPPER(f && f->slots[FURNACE_INPUT].type==IRON_ORE && f->slots[FURNACE_FUEL].type==COAL);
    for (int i=0;i<10;++i) { furnaces_advance(1.f); ticks(0,0,20); }
    CHECK_HOPPER(chest_contents(0,Y-2,0,0).type==IRON_INGOT && chest_contents(0,Y-2,0,0).count==1);

    CHECK_HOPPER(set_block(9,Y,0,BREWING_STAND) && set_block(9,Y-2,0,CHEST) &&
        put(9,Y+1,0,9,Y,0) && put(10,Y,0,9,Y,0) && put(9,Y-1,0,9,Y-2,0));
    CHECK_HOPPER(load_hopper(9,Y+1,0,0,LIMESTONE_ITEM,1) && load_hopper(10,Y,0,0,WATER_BOTTLE,1));
    ticks(9,0,1);
    const BrewingStand *b=brew_at(9,Y,0);
    CHECK_HOPPER(b && b->slots[BREW_INGREDIENT].type==LIMESTONE_ITEM && b->slots[0].type==WATER_BOTTLE);
    for (int i=0;i<5;++i) {
        brewing_advance(1.f);
        ticks(9,0,20);
    }
    CHECK_HOPPER(b && b->slots[0].type==WATER_BOTTLE && !chest_contents(9,Y-2,0,0).count);
    brewing_advance(1.f);
    ticks(9,0,10);
    CHECK_HOPPER(chest_contents(9,Y-2,0,0).type==POTION_FIRE);

    CHECK_HOPPER(set_block(3,Y-1,4,STONE) && put(3,Y,4,3,Y-1,4));
    CHECK_HOPPER(load_hopper(3,Y,4,0,STONE,63));
    for (int i=1;i<5;++i) CHECK_HOPPER(load_hopper(3,Y,4,i,PLANKS,64));
    drops_reset();
    drops[0]=make_drop(3.5f,Y+.8f,4.5f,stack(STONE,4));
    ticks(3,4,1);
    CHECK_HOPPER(drops[0].item.count==3 && anything_at(3,Y,4,0).count==64);
    CHECK_HOPPER(hopper_move(3,Y,4,1,HOPPER_SLOTS+2,0) && inventory[2].count==64);
    ticks(3,4,8);
    CHECK_HOPPER(!drops[0].item.count && anything_at(3,Y,4,1).count==3 && container_signal(3,Y,4,HOPPER)>10);
    player_reset_rules(SURVIVAL);
    CHECK_HOPPER(hopper_break(3,Y,4) && world_cell(3,Y,4)==AIR && !hopper_at(3,Y,4) &&
        drops[0].item.type==HOPPER_ITEM);
    world_reset();
    drops_reset();
    printf("HOPPER TEST craft/aim/lock/timing/seam/chest/furnace/brew/intake/capacity/break failures=%d\n",failures);
    return failures;
}
