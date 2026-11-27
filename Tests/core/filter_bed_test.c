#include "world_test.h"
#include "../game/farming.h"
#include "../game/drops.h"
#include "../game/item_icons.h"
#include <stdio.h>
#include <string.h>

static void filter_ticks(void) {
    for (int i=0;i<30;++i) farm_step(.1f);
}
int filter_bed_selftest(void) {
    int failures=0;
#define CHECK(test) do { if (!(test)) { fprintf(stderr,"FILTER BED line %d: %s\n",__LINE__,#test); ++failures; } } while (0)
    const int x=-1,y=70,z=0;
    const Hit floor={1,x,y,z,x,y+1,z};
    const Hit mat={1,x,y+1,z,x,y+2,z};
    CHECK(world_begin_options(9117,GENERATOR_CURRENT,WORLD_SUPERFLAT,1));
    inventory_reset(); player_reset_rules(SURVIVAL); farm_reset();
    memset(drops,0,sizeof drops);
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){SAND,1,0,0,0};
    inventory[1]=(ItemStack){GRAVEL,1,0,0,0};
    inventory[2]=(ItemStack){CLAY_BALL,1,0,0,0};
    const int bed_recipe[]={0,1,2,-1};
    CHECK(craft_preview(bed_recipe,2).type==ASH_SAND && craft_take(bed_recipe,2) && inventory[0].count==3);
    inventory[0]=(ItemStack){SILT_GEL,1,0,0,0};
    inventory[1]=(ItemStack){BROWN_MUSHROOM_ITEM,1,0,0,0};
    const int moss_recipe[]={0,1,-1,-1};
    ItemIcon icon;
    CHECK(craft_preview(moss_recipe,2).type==FILTER_MOSS && craft_take(moss_recipe,2) &&
        inventory[0].count==2 && item_icon(FILTER_MOSS,&icon) && icon.path[3]);
    px=-1.5f;py=y+1.01f;pz=2.50f;
    CHECK(set_block(x,y,z,ASH_SAND));
    (void)world_cell(x+1,y,z); (void)world_cell(x,y,z-1);
    CHECK(farm_use(floor) && world_cell(x,y+1,z)==FILTER_MAT_SEEDED && inventory[0].count==1);
    filter_ticks();
    CHECK(world_cell(x,y+1,z)==FILTER_MAT_SEEDED);
    CHECK(set_block(x+1,y,z,WATER));
    filter_ticks();
    CHECK(world_cell(x,y+1,z)==FILTER_MAT_SEEDED);
    const BlockEdit flow={x+1,y,z,FLOW_BASE+4};
    CHECK(world_set_blocks(&flow,1) && set_block(x-1,y,z,STONE));
    filter_ticks();
    CHECK(world_cell(x,y+1,z)==FILTER_MAT_SEEDED);
    CHECK(set_block(x-1,y,z,AIR) && set_block(x,y,z+1,WATER));
    filter_ticks();
    CHECK(world_cell(x,y+1,z)==FILTER_MAT_SEEDED);
    CHECK(set_block(x,y,z+1,AIR) && set_block(x,y+2,z,GLASS));
    filter_ticks();
    CHECK(world_cell(x,y+1,z)==FILTER_MAT_SEEDED);
    CHECK(set_block(x,y+2,z,AIR));
    filter_ticks();
    CHECK(world_cell(x,y+1,z)==FILTER_MAT_TWO && world_cell(x+1,y,z)==FLOW_BASE+4);
    CHECK(set_block(x+1,y,z,AIR)); /* Disconnected: only stored water is available. */
    inventory[0]=(ItemStack){FIELD_FLASK_FULL,1,0,0,0};
    CHECK(farm_use(mat) && inventory[0].type==FIELD_FLASK_FULL && world_cell(x,y+1,z)==FILTER_MAT_TWO);
    inventory[0].type=FIELD_FLASK_EMPTY;
    CHECK(farm_use(mat) && inventory[0].type==FIELD_FLASK_ONE && world_cell(x,y+1,z)==FILTER_MAT_ONE);
    CHECK(farm_use(mat) && inventory[0].type==FIELD_FLASK_TWO && world_cell(x,y+1,z)==FILTER_MAT_PRIMED);
    filter_ticks();
    CHECK(farm_use(mat) && inventory[0].type==FIELD_FLASK_TWO && world_cell(x,y+1,z)==FILTER_MAT_PRIMED);
    CHECK(farm_break(floor) && world_cell(x,y+1,z)==AIR && drops[0].item.type==ASH_SAND &&
        drops[1].item.type==FILTER_MOSS && drops[1].item.count==1);
    printf("FILTER BED TEST recipes, static, flow, two inlets, covered, stored sips and one clump drop failures=%d\n",failures);
    return failures;
#undef CHECK
}
