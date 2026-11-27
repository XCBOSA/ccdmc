#include "world_test.h"
#include "../game/furnace.h"
#include "../game/heat_bench.h"
#include "../game/item_custom.h"
#include "../game/item_icons.h"
#include <stdio.h>
#include <string.h>

static int has(Hit hit,const char *wanted) {
    const char *msg=heat_bench_use(hit);
    return msg && strstr(msg,wanted);
}
int heat_bench_selftest(void) {
    int failures=0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"HEAT BENCH line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
    const Hit bench={1,6,176,6,6,176,7};
    const int x=bench.x,y=bench.y,z=bench.z;
    furnaces_reset();
    CHECK(world_begin_profile(1906,GENERATOR_CURRENT,WORLD_SUPERFLAT));
    player_reset_rules(SURVIVAL);
    memset(inventory,0,sizeof inventory); selected_slot=0;
    CHECK(set_block(x,y,z,HEAT_BENCH_EMPTY) && set_block(x+1,y,z,FURNACE));
    Furnace burning={0}; burning.x=x+1; burning.y=y; burning.z=z;
    burning.slots[FURNACE_INPUT]=(ItemStack){IRON_ORE,1,0,0,0};
    burning.slots[FURNACE_FUEL]=(ItemStack){COAL,2,0,0,0};
    burning.burn=1001; burning.fuel_duration=FURNACE_MAX_FUEL_TICKS;
    burning.cook=SMELT_TICKS-1;
    CHECK(furnace_restore(&burning));
    CHECK(has(bench,"炉在烧制") && world_recorded_cell(OVERWORLD,x,y,z)==HEAT_BENCH_EMPTY);
    const Furnace *f=furnace_at(x+1,y,z);
    CHECK(f && f->burn==1001 && f->slots[FURNACE_FUEL].count==2);
    furnaces_advance(.05f);
    f=furnace_at(x+1,y,z);
    CHECK(f && f->burn==1000 && f->slots[FURNACE_OUTPUT].type==IRON_INGOT);
    CHECK(set_block(x-1,y,z,FURNACE) && has(bench,"只接一座熔炉") && set_block(x-1,y,z,AIR));
    CHECK(has(bench,"回收余火 +4") && world_recorded_cell(OVERWORLD,x,y,z)==HEAT_BENCH_FULL);
    f=furnace_at(x+1,y,z);
    CHECK(f && f->burn==1000-4*HEAT_BENCH_TICKS_PER_UNIT && f->slots[FURNACE_FUEL].count==2 &&
        f->slots[FURNACE_OUTPUT].count==1);
    CHECK(has(bench,"4/4") && furnace_store_idle_heat(x+1,y,z,x,z)==0 &&
        !world_set_heat_bench_level(x,y,z,HEAT_BENCH_EMPTY,HEAT_BENCH_FULL));

    inventory[0]=(ItemStack){IRON_PICKAXE,1,17,0,0};
    CHECK(item_set_custom(&inventory[0],4,"矿工的铁镐"));
    const uint32_t custom=inventory[0].custom;
    CHECK(has(bench,"修复 12 点") && inventory[0].damage==5 && inventory[0].custom==custom &&
        world_recorded_cell(OVERWORLD,x,y,z)==HEAT_BENCH_FULL-1);
    CHECK(has(bench,"修复 5 点") && !inventory[0].damage && inventory[0].custom==custom &&
        world_recorded_cell(OVERWORLD,x,y,z)==HEAT_BENCH_EMPTY+2);
    CHECK(has(bench,"无需修复") && world_recorded_cell(OVERWORLD,x,y,z)==HEAT_BENCH_EMPTY+2);
    CHECK(world_restore_edit(x+1,y,z,AIR)); /* Can use banked heat without a furnace. */
    for (int i=1;i<SLOT_COUNT;++i) inventory[i]=(ItemStack){STONE,64,0,0,0};
    inventory[1]=(ItemStack){GLASS_PANE_ITEM,8,0,0,0};
    selected_slot=1;
    CHECK(has(bench,"背包无法装入") && inventory[1].count==8 &&
        world_recorded_cell(OVERWORLD,x,y,z)==HEAT_BENCH_EMPTY+2);
    inventory[1].count=4; /* The outgoing stack frees a slot even with a full bag. */
    CHECK(has(bench,"熔回 1 块玻璃") && inventory[1].type==GLASS && inventory[1].count==1 &&
        world_recorded_cell(OVERWORLD,x,y,z)==HEAT_BENCH_EMPTY);
    inventory[1]=(ItemStack){GLASS_PANE_ITEM,8,0,0,0};
    CHECK(has(bench,"需要 2 格蓄热") && inventory[1].count==8);
    CHECK(!furnace_store_idle_heat(x+1,y,z,x,z)); /* No invalid source. */
    /* A cold overflow neighbour must not be read through the streaming cache. */
    Hit edge=bench; edge.x=15;
    int raw;
    memset(inventory,0,sizeof inventory); selected_slot=0;
    CHECK(set_block(15,y,z,HEAT_BENCH_EMPTY) && !world_resident_cell(16,y,z,&raw));
    CHECK(has(edge,"等待邻区载入") && !world_resident_cell(16,y,z,&raw));
    for (int i=0;i<=HEAT_BENCH_CAP;++i)
        CHECK(heat_bench_cell(HEAT_BENCH_EMPTY+i) && cell_type(HEAT_BENCH_EMPTY+i)==HEAT_BENCH_EMPTY &&
            mining_drop(HEAT_BENCH_EMPTY+i,0)==HEAT_BENCH_ITEM);

    const int craft[]={0,1,2,3};
    inventory[0]=(ItemStack){LAMINATE_ITEM,1,0,0,0};
    inventory[1]=(ItemStack){STRIATED_GLASS,1,0,0,0};
    inventory[2]=(ItemStack){BRICK_ITEM,1,0,0,0};
    inventory[3]=(ItemStack){IRON_INGOT,1,0,0,0};
    ItemIcon ico;
    CHECK(craft_preview(craft,2).type==HEAT_BENCH_ITEM && craft_take(craft,2) &&
        item_icon(HEAT_BENCH_ITEM,&ico) && ico.path[3] &&
        item_def(HEAT_BENCH_ITEM)->placed_block==HEAT_BENCH_EMPTY);
    printf("HEAT BENCH busy furnace, fuel accounting, finite heat, repair metadata, full bag, cold boundary, recipe: failures=%d\n",failures);
    furnaces_reset();
    world_reset();
#undef CHECK
    return failures;
}
