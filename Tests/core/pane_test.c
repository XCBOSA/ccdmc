#include "world_test.h"
#include "../game/drops.h"
#include "../game/enchanting.h"
#include "../game/item_icons.h"
#include "../world/light.h"
#include "../world/space.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("PANE TEST failed line %d: %s\n",__LINE__,#c); ++bad; } } while (0)
enum { Y=158 };

int pane_selftest(void) {
    int bad=0;
    world_begin(18281); generate(); player_reset_rules(SURVIVAL);
    CHECK(set_block(-1,Y,0,GLASS_PANE_CELL));
    CHECK(world_point_blocked(-.5f,Y+.5f,.12f,1) && world_point_blocked(-.12f,Y+.5f,.5f,1) &&
        !world_point_blocked(-.12f,Y+.5f,.12f,1) && !world_point_blocked(-.5f,Y+1.04f,.5f,1));
    CHECK(set_block(0,Y,0,STAINED_PANE_BASE+7) && set_block(-1,Y,-1,IRON_BARS_CELL) &&
        set_block(-2,Y,0,WOOD_FENCE));
    CHECK(world_point_blocked(-.08f,Y+.5f,.5f,1) && world_point_blocked(-.5f,Y+.5f,.10f,1) &&
        !world_point_blocked(-.90f,Y+.5f,.5f,1) && !world_point_blocked(-.5f,Y+.5f,.90f,1));
    CHECK(world_point_blocked(.05f,Y+.5f,.5f,1) &&
        light_opacity(STAINED_PANE_BASE+7)==light_opacity(GLASS) && !solid(block(-1,Y,0)));
    CHECK(set_block(-1,Y,-1,AIR) && !world_point_blocked(-.5f,Y+.5f,.10f,1));
    CHECK(set_block(0,Y,0,AIR) && world_point_blocked(-.5f,Y+.5f,.90f,1));
    CHECK(pane_connections(GLASS_PANE_CELL,(int[]){STONE,GLASS,CACTUS_BASE,IRON_BARS_CELL})==
        ((1u<<NORTH)|(1u<<EAST)|(1u<<WEST)));

    memset(inventory,0,sizeof inventory);
    const int lower[] = {-1,-1,-1,0,0,0,0,0,0};
    const int mixed[] = {0,0,0,0,0,1,-1,-1,-1};
    inventory[1]=(ItemStack){GLASS,8,0,0,0};
    for (int color=-2;color<STAINED_PANE_END-STAINED_PANE_BASE;++color) {
        int type=color==-2 ? GLASS : color==-1 ? IRON_INGOT : BLACK_GLASS_ITEM+color;
        int result=color==-2 ? GLASS_PANE_ITEM : color==-1 ? IRON_BARS_ITEM : BLACK_PANE_ITEM+color;
        int raw=color==-2 ? GLASS_PANE_CELL : color==-1 ? IRON_BARS_CELL : STAINED_PANE_BASE+color;
        inventory[0]=(ItemStack){(uint16_t)type,5,0,0,0};
        CHECK(!craft_preview(lower,3).count);
        ++inventory[0].count;
        ItemIcon icon;
        CHECK(item_icon(result,&icon) && icon.path[0][0] && pane_item(raw)==result &&
            item_def(result)->placed_block==raw && valid_cell(raw));
        ItemStack out=craft_preview(lower,3);
        CHECK(out.type==result && out.count==16);
        CHECK(!craft_preview(mixed,3).count || color==-2);
    }
    CHECK(craft_take(lower,3) && inventory[0].type==ORANGE_PANE_ITEM && inventory[0].count==16);

    /* Stone at the end of the aim line accepts a real survival placement. */
    CHECK(set_block(1,Y,3,STONE) && set_block(1,Y,4,AIR));
    selected_slot=0;
    px=1.5f; py=Y-1.05f; pz=6.5f; yaw=pitch=0;
    CHECK(target().hit && target().z==3);
    CHECK(place() && inventory[0].count==15 && world_cell(1,Y,4)==STAINED_PANE_END-1);
    CHECK(target().hit && target().z==4 && mine() && world_cell(1,Y,4)==AIR && !drops[0].item.count);
    CHECK(set_block(1,Y,4,STAINED_PANE_END-1));
    inventory[0]=(ItemStack){DIAMOND_PICKAXE,1,0,enchant_code(ENCH_SILK_TOUCH,1),0};
    CHECK(mine() && world_cell(1,Y,4)==AIR && drops[0].item.type==ORANGE_PANE_ITEM);
    drops_reset();
    CHECK(set_block(1,Y,4,IRON_BARS_CELL));
    inventory[0]=(ItemStack){WOOD_PICKAXE,1,0,0,0};
    CHECK(mine() && world_cell(1,Y,4)==AIR && drops[0].item.type==IRON_BARS_ITEM);
    CHECK(mining_drop(IRON_BARS,0)==AIR && mining_drop(GLASS_PANE,4)==AIR);
    printf("PANE TEST connections/seam/shapes/craft/harvest/placement failures=%d\n",bad);
    return bad;
}
