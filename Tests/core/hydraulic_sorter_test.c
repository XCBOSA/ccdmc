#include "world_test.h"
#include "../game/chests.h"
#include "../game/hydraulic_sorter.h"
#include "../game/item_custom.h"
#include "../game/item_icons.h"
#include <stdio.h>
#include <string.h>

static int total(const ChestView *view,int type) {
    int count=0;
    for (int i=0;i<view->count*CHEST_SLOTS;++i) {
        const ItemStack item=chest_slot(view,i);
        if (item.type==type) count+=item.count;
    }
    return count;
}
static int status_has(Hit h,const char *needle) {
    const char *status=hydraulic_sorter_use(h);
    return status && strstr(status,needle)!=NULL;
}

int hydraulic_sorter_selftest(void) {
    int failures=0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"HYDRAULIC SORTER line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
    CHECK(world_begin_profile(1862,GENERATOR_CURRENT,WORLD_SUPERFLAT));
    player_reset_rules(SURVIVAL);
    memset(inventory,0,sizeof inventory);
    selected_slot=0;
    const ChestPos feed={6,182,6},light={7,181,6},heavy={6,180,6};
    const Hit washer={.hit=1,.x=6,.y=181,.z=6};
    CHECK(set_block(6,181,6,HYDRAULIC_SORTER) && set_block(feed.x,feed.y,feed.z,CHEST) &&
        set_block(light.x,light.y,light.z,CHEST) && set_block(heavy.x,heavy.y,heavy.z,CHEST));
    BlockEdit sites[4];
    CHECK(world_has_sorters() && world_sorter_batch(0,0,sites,4)==1 && sites[0].cell==HYDRAULIC_SORTER);
    CHECK(world_restore_edit(170,181,167,HYDRAULIC_SORTER) && !world_sorter_batch(10,10,sites,4));
    ChestView input={0},light_box={0},heavy_box={0};
    CHECK(chest_automation_open_resident(feed,&input) && chest_automation_open_resident(light,&light_box) &&
        chest_open(heavy,&heavy_box)==CHEST_OK);
    /* Seed full rock output. The wood lane still works; the named tool never moves. */
    ChestRecord blocked={.at=heavy};
    for (int i=0;i<CHEST_SLOTS;++i) blocked.items[i]=(ItemStack){STONE,64,0,0,0};
    CHECK(chest_restore(&blocked));
    ItemStack tool={IRON_PICKAXE,1,3,0,0};
    CHECK(item_set_custom(&tool,2,"矿工的工具"));
    CHECK(chest_automation_insert(&input,tool));
    for (int i=0;i<8;++i) CHECK(chest_automation_insert(&input,(ItemStack){PLANKS,1,0,0,0}));
    for (int i=0;i<4;++i) CHECK(chest_automation_insert(&input,(ItemStack){LIMESTONE_ITEM,1,0,0,0}));
    CHECK(status_has(washer,"需要单侧"));
    hydraulic_sorters_step(6,6,0);
    CHECK(total(&input,PLANKS)==8);
    CHECK(set_block(5,181,6,WATER));
    CHECK(status_has(washer,"静水"));
    hydraulic_sorters_step(6,6,16);
    CHECK(total(&input,PLANKS)==8);
    const BlockEdit weak={5,181,6,FLOW_BASE},strong={5,181,6,FLOW_BASE+WATER_LEVELS-2};
    CHECK(world_set_blocks(&weak,1));
    hydraulic_sorters_step(6,6,32);
    CHECK(total(&light_box,PLANKS)==1 && total(&input,LIMESTONE_ITEM)==4);
    CHECK(world_set_blocks(&strong,1) && set_block(6,181,5,WATER));
    CHECK(status_has(washer,"一个入水面"));
    hydraulic_sorters_step(6,6,48);
    CHECK(total(&light_box,PLANKS)==1 && total(&input,LIMESTONE_ITEM)==4);
    CHECK(set_block(6,181,5,AIR));
    hydraulic_sorters_step(6,6,64);
    CHECK(total(&light_box,PLANKS)==5 && total(&input,LIMESTONE_ITEM)==4);
    for (int i=0;i<64;++i) chest_automation_remove(&heavy_box,0);
    hydraulic_sorters_step(6,6,80);
    CHECK(total(&light_box,PLANKS)==6 && total(&heavy_box,LIMESTONE_ITEM)==1);
    for (unsigned t=96;t<=224;t+=16) hydraulic_sorters_step(6,6,t);
    CHECK(total(&light_box,PLANKS)==8 && total(&heavy_box,LIMESTONE_ITEM)==4 &&
        !total(&input,PLANKS) && !total(&input,LIMESTONE_ITEM) && total(&input,IRON_PICKAXE)==1 &&
        item_same_kind(chest_slot(&input,0),tool));

    CHECK(set_block(13,181,6,CHEST) && set_block(14,181,6,CHEST));
    CHECK(chest_automation_open_resident((ChestPos){14,181,6},&input) && input.count==2 &&
        input.halves[0].x==13 && input.halves[1].x==14);
    /* Probe through the paired half into a cold neighbour, without warming it. */
    CHECK(set_block(13,181,6,AIR) && set_block(15,181,6,CHEST));
    int cold;
    CHECK(!world_resident_cell(16,181,6,&cold) && !chest_automation_open_resident((ChestPos){14,181,6},&input) &&
        !world_resident_cell(16,181,6,&cold));
    CHECK(set_block(14,181,6,AIR) && set_block(15,181,6,AIR));
    CHECK(world_restore_edit(7,181,7,DESERT_PYRAMID_CHEST_CELL) &&
        !chest_automation_open_resident(light,&input) &&
        world_recorded_cell(OVERWORLD,7,181,7)==DESERT_PYRAMID_CHEST_CELL);
    CHECK(set_block(6,181,6,CISTERN_EMPTY) && !world_sorter_batch(0,0,sites,4) &&
        world_cistern_batch(0,0,sites,4)==1 && sites[0].cell==CISTERN_EMPTY);
    CHECK(world_restore_edit(170,181,167,AIR) && !world_has_sorters());

    const int craft[]={0,1,2,3};
    inventory[0]=(ItemStack){SINTERED_TILE_ITEM,1,0,0,0};
    inventory[1]=(ItemStack){STRIATED_GLASS,1,0,0,0};
    inventory[2]=(ItemStack){IRON_INGOT,1,0,0,0};
    inventory[3]=(ItemStack){PUMP_GASKET,1,0,0,0};
    ItemIcon icon;
    CHECK(craft_preview(craft,2).type==HYDRAULIC_SORTER_ITEM && craft_take(craft,2) &&
        item_icon(HYDRAULIC_SORTER_ITEM,&icon) && icon.path[3] &&
        item_def(HYDRAULIC_SORTER_ITEM)->placed_block==HYDRAULIC_SORTER &&
        mining_drop(HYDRAULIC_SORTER,0)==HYDRAULIC_SORTER_ITEM);
    printf("HYDRAULIC SORTER weak/strong, blocked lane, stock conservation, resident boundaries and recipe: failures=%d\n",failures);
    world_reset();
#undef CHECK
    return failures;
}
