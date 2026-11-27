#include "../game/brewing.h"
#include "../game/chests.h"
#include "../game/furnace.h"
#include "../world/redstone.h"
#include <stdio.h>
#include <string.h>

#define CHECK_COMPARE(c) do { if (!(c)) { printf("COMPARATOR TEST failed line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
enum { Y=150, MAIN=-17 };
static void step(int n) { while (n--) redstone_step(MAIN,0); }
static int output(int x,int z) { return redstone_comparator_power(x,Y,z); }
static int lit(void) {
    int n=0;
    for (int i=1;i<=15;++i) n+=world_cell(MAIN+i,Y-1,0)==LAMP_ON;
    return n;
}
static int fill(const ChestView *view,int from,int to) {
    for (int i=from;i<to;++i) {
        inventory[5]=(ItemStack){STONE,64,0,0,0};
        if (chest_move(view,DOUBLE_CHEST_SLOTS+5,i,0)!=CHEST_OK) return 0;
    }
    return 1;
}
int comparator_selftest(void) {
    int failures=0;
    world_begin(397451);
    generate();
    inventory_reset();
    inventory[0]=(ItemStack){STONE,3,0,0,0};
    inventory[1]=(ItemStack){REDSTONE_TORCH_ITEM,3,0,0,0};
    inventory[2]=(ItemStack){QUARTZ,1,0,0,0};
    CHECK_COMPARE(!craft_preview((int[]){-1,1,-1,1,2,1,0,0,0},3).count &&
        !craft_take((int[]){-1,1,-1,1,2,1,0,0,0},3) && mining_drop(COMPARATOR,0)==COMPARATOR_ITEM);
    player_reset_rules(CREATIVE);
    CHECK_COMPARE(!inventory_creative_pick(COMPARATOR_ITEM));
    player_reset_rules(SURVIVAL);
    for (int x=-22;x<=0;++x) for (int z=-6;z<=9;++z) CHECK_COMPARE(set_block(x,Y-1,z,STONE));
    int chest_x=MAIN-1;
    ChestView box={OVERWORLD,{{0,0,0},{0,0,0}},0,0};
    CHECK_COMPARE(set_block(chest_x,Y,0,CHEST) && chest_open((ChestPos){chest_x,Y,0},&box)==CHEST_OK);
    CHECK_COMPARE(redstone_place_comparator(MAIN,Y,0,EAST));
    for (int i=1;i<=15;++i)
        CHECK_COMPARE(set_block(MAIN+i,Y-1,0,REDSTONE_LAMP) && redstone_place_wire(MAIN+i,Y,0));
    step(8);
    CHECK_COMPARE(output(MAIN,0)==0 && lit()==0);
    /* A non-stackable item fills one slot, and empty storage is truly zero. */
    inventory[5]=(ItemStack){WOOD_SWORD,1,0,0,0};
    CHECK_COMPARE(chest_move(&box,DOUBLE_CHEST_SLOTS+5,0,0)==CHEST_OK && chest_signal((ChestPos){chest_x,Y,0})==1);
    step(1);
    CHECK_COMPARE(output(MAIN,0)==0);
    step(2);
    CHECK_COMPARE(output(MAIN,0)==1 && lit()==1);
    CHECK_COMPARE(chest_move(&box,0,DOUBLE_CHEST_SLOTS+5,0)==CHEST_OK);
    CHECK_COMPARE(fill(&box,0,14));
    step(8);
    CHECK_COMPARE(output(MAIN,0)==8 && lit()==8 && redstone_wire_power(-16,Y,0)==8 &&
        redstone_wire_power(-15,Y,0)==7 && redstone_wire_power(-9,Y,0)==1 && redstone_wire_power(-8,Y,0)==0);
    CHECK_COMPARE(fill(&box,14,27));
    step(8);
    CHECK_COMPARE(output(MAIN,0)==15 && lit()==15);

    /* Joining an empty half doubles the denominator. The far half's
       inventory edits must wake the comparator reading the near half. */
    CHECK_COMPARE(set_block(chest_x-1,Y,0,CHEST));
    CHECK_COMPARE(chest_open((ChestPos){chest_x,Y,0},&box)==CHEST_OK && box.count==2);
    step(8);
    CHECK_COMPARE(output(MAIN,0)==8 && lit()==8);
    CHECK_COMPARE(fill(&box,0,27));
    step(8);
    CHECK_COMPARE(output(MAIN,0)==15 && lit()==15);
    for (int i=0;i<27;++i) {
        inventory[5]=(ItemStack){0,0,0,0,0};
        CHECK_COMPARE(chest_move(&box,i,DOUBLE_CHEST_SLOTS+5,0)==CHEST_OK);
    }
    step(8);
    CHECK_COMPARE(output(MAIN,0)==8 && lit()==8);

    /* A south-facing view of a full furnace input gives five at the side
       of the main comparator. Switch and verify the actual downstream lamps. */
    CHECK_COMPARE(set_block(MAIN,Y,2,FURNACE) && redstone_place_comparator(MAIN,Y,1,NORTH));
    inventory[5]=(ItemStack){COBBLESTONE,64,0,0,0};
    CHECK_COMPARE(furnace_deposit(MAIN,Y,2,FURNACE_INPUT,5));
    step(8);
    CHECK_COMPARE(output(MAIN,1)==5 && output(MAIN,0)==8 && lit()==8);
    CHECK_COMPARE(redstone_toggle_comparator(MAIN,Y,0)==2);
    step(8);
    CHECK_COMPARE(output(MAIN,0)==3 && lit()==3);
    CHECK_COMPARE(set_block(MAIN,Y,-1,REDSTONE_BLOCK));
    step(8);
    CHECK_COMPARE(output(MAIN,0)==0 && lit()==0);
    CHECK_COMPARE(set_block(MAIN,Y,-1,AIR) && redstone_toggle_comparator(MAIN,Y,0)==1);
    step(8);
    CHECK_COMPARE(output(MAIN,0)==8 && lit()==8);
    CHECK_COMPARE(furnace_withdraw(MAIN,Y,2,FURNACE_INPUT));
    inventory[5]=(ItemStack){COBBLESTONE,32,0,0,0};
    inventory[6]=(ItemStack){COAL,64,0,0,0};
    CHECK_COMPARE(furnace_deposit(MAIN,Y,2,FURNACE_INPUT,5) && furnace_deposit(MAIN,Y,2,FURNACE_FUEL,6));
    step(6);
    CHECK_COMPARE(output(MAIN,1)==8 && output(MAIN,0)==8); /* Equality passes in compare mode. */
    furnaces_advance(.05f);
    step(6);
    CHECK_COMPARE(output(MAIN,1)==7);

    /* A container a block farther away is read through stone, not glass. */
    int through=-15;
    CHECK_COMPARE(set_block(through-1,Y,6,STONE) && set_block(through-2,Y,6,CHEST) &&
        redstone_place_comparator(through,Y,6,EAST));
    CHECK_COMPARE(chest_open((ChestPos){through-2,Y,6},&box)==CHEST_OK && fill(&box,0,7));
    step(6);
    CHECK_COMPARE(output(through,6)==4);
    CHECK_COMPARE(set_block(through-1,Y,6,GLASS));
    step(6);
    CHECK_COMPARE(output(through,6)==0);
    CHECK_COMPARE(set_block(through-1,Y,6,STONE));
    step(6);
    CHECK_COMPARE(output(through,6)==4);

    CHECK_COMPARE(set_block(-21,Y,8,BREWING_STAND) && redstone_place_comparator(-20,Y,8,EAST) &&
        set_block(-19,Y,8,STONE) && redstone_place_wire(-18,Y,8));
    inventory[5]=(ItemStack){WATER_BOTTLE,1,0,0,0};
    inventory[6]=(ItemStack){LIMESTONE_ITEM,5,0,0,0};
    CHECK_COMPARE(brew_deposit(-21,Y,8,0,5) && brew_deposit(-21,Y,8,BREW_INGREDIENT,6));
    step(6);
    CHECK_COMPARE(output(-20,8)==5);
    for (int i=0;i<6;++i) brewing_advance(1.f);
    step(6);
    CHECK_COMPARE(output(-20,8)==4 && redstone_wire_power(-18,Y,8)==4);
    player_reset_rules(SURVIVAL);
    memset(drops,0,sizeof dimension_drops[0]);
    for (int i=0;i<MAXDROP;++i) drops[i].item=(ItemStack){COAL,1,0,0,0};
    CHECK_COMPARE(set_block(-20,Y-1,8,AIR));
    step(6);
    CHECK_COMPARE(block(-20,Y,8)==COMPARATOR && !output(-20,8) && !redstone_wire_power(-18,Y,8));
    drops[0].item=(ItemStack){0,0,0,0,0};
    step(6);
    CHECK_COMPARE(block(-20,Y,8)==AIR && !output(-20,8) &&
        drops[0].item.type==COMPARATOR_ITEM && drops[0].item.count==1);
    printf("COMPARATOR TEST legacy-only, analogue/15 lamps/container/seam/side/compare/subtract/furnace/brew/drop failures=%d\n",failures);
    return failures;
}
