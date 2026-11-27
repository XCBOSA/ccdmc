#include "world_test.h"
#include "../game/game.h"
#include "../game/drops.h"
#include "../game/interaction.h"
#include "../game/item_icons.h"
#include "../world/redstone.h"
#include "../world/space.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("SHAFT TEST line %d: %s\n",__LINE__,#c); ++bad; } } while (0)
static void aim(float x,float y,float z) {
    yaw=atan2f(x-px,-(z-pz)); pitch=atan2f(y-py-1.55f,hypotf(x-px,z-pz));
}
static int count_drops(int type) {
    int count=0;
    for (int i=0;i<MAXDROP;++i) if (drops[i].item.type==type) count+=drops[i].item.count;
    return count;
}
int shaft_selftest(void) {
    int bad=0;
    enum { Y=140 };
    if (!world_begin_options(2295,GENERATOR_CURRENT,WORLD_SUPERFLAT,1)) return 1;
    generate(); player_reset_rules(SURVIVAL);
    const GameDifficulty prior=game_selected_difficulty();
    CHECK(game_set_difficulty(NORMAL));
    memset(inventory,0,sizeof inventory);
    const int trap_grid[]={0,1,2,3,4,5,-1,-1,-1};
    const int ladder_grid[]={0,-1,1,2,3,4,5,-1,6};
    for (int i=0;i<7;++i) inventory[i]=(ItemStack){PLANKS,1,0,0,0};
    ItemStack result=craft_preview(trap_grid,3);
    CHECK(result.type==WOOD_TRAPDOOR_ITEM && result.count==2 && craft_take(trap_grid,3));
    for (int i=0;i<7;++i) inventory[i]=(ItemStack){STICK,1,0,0,0};
    result=craft_preview(ladder_grid,3);
    CHECK(result.type==LADDER_ITEM && result.count==3 && craft_take(ladder_grid,3));
    ItemIcon icon;
    CHECK(item_icon(LADDER_ITEM,&icon) && item_icon(WOOD_TRAPDOOR_ITEM,&icon));

    CHECK(!wall_attachment_place(0,Y,0,NORTH,1,0) && set_block(0,Y,-1,GLASS) &&
        !wall_attachment_place(0,Y,0,NORTH,0,1));
    for (int y=Y;y<=Y+6;++y) CHECK(set_block(0,y,-1,STONE));
    for (int z=0;z<4;++z) CHECK(set_block(0,Y-1,z,STONE));
    CHECK(set_block(-1,Y+5,0,STONE));
    px=.5f; py=Y+.01f; pz=2.8f; selected_slot=0;
    inventory[0]=(ItemStack){LADDER_ITEM,1,0,0,0};
    aim(.5f,Y+1.50f,0.f);
    CHECK(game_tap().placed && !inventory[0].count && world_cell(0,Y+1,0)==LADDER+NORTH);
    for (int y=Y;y<Y+6;++y) if (y!=Y+1) CHECK(wall_attachment_place(0,y,0,NORTH,1,0));
    py=Y+5.62f;
    inventory[0]=(ItemStack){WOOD_TRAPDOOR_ITEM,2,0,0,0};
    aim(.5f,Y+6.77f,0.f);
    CHECK(game_tap().placed && inventory[0].count==1);
    CHECK(world_cell(0,Y+6,0)==WOOD_TRAPDOOR+NORTH+TRAPDOOR_TOP);
    CHECK(world_point_blocked(.50f,Y+6.92f,.50f,1) && !world_point_blocked(.50f,Y+6.4f,.50f,1));
    aim(.5f,Y+6.89f,.55f);
    CHECK(game_tap().message && ((world_cell(0,Y+6,0)-WOOD_TRAPDOOR)&TRAPDOOR_OPEN));
    CHECK(!world_point_blocked(.5f,Y+6.92f,.5f,1) && world_point_blocked(.5f,Y+6.5f,.09f,1));
    CHECK(trapdoor_toggle(0,Y+6,0)==1);
    CHECK(set_block(1,Y+6,0,REDSTONE_BLOCK));
    for (int n=0;n<6;++n) redstone_step(0,0);
    CHECK(world_cell(0,Y+6,0)==WOOD_TRAPDOOR+NORTH+TRAPDOOR_TOP+TRAPDOOR_OPEN+TRAPDOOR_POWERED);
    CHECK(trapdoor_toggle(0,Y+6,0)==1); /* Manual override until the next edge. */
    for (int n=0;n<6;++n) redstone_step(0,0);
    CHECK(world_cell(0,Y+6,0)==WOOD_TRAPDOOR+NORTH+TRAPDOOR_TOP+TRAPDOOR_POWERED);
    CHECK(set_block(1,Y+6,0,AIR));
    for (int n=0;n<6;++n) redstone_step(0,0);
    CHECK(world_cell(0,Y+6,0)==WOOD_TRAPDOOR+NORTH+TRAPDOOR_TOP);

    px=.5f; py=Y+.01f; pz=1.9f; yaw=pitch=vy=0.f;
    for (int tick=0;tick<63;++tick) game_step(.05f,0,1,0,0);
    CHECK(player_on_ladder() && py>Y+4.4f && py<Y+5.2f); /* Head hits the closed cover. */
    CHECK(trapdoor_toggle(0,Y+6,0)==2);
    for (int tick=0;tick<12;++tick) game_step(.05f,0,1,0,0);
    CHECK(py>Y+5.9f && py<Y+6.5f);
    for (int tick=0;tick<5;++tick) game_step(.05f,-1,0,0,0);
    CHECK(px<0.f && py>Y+5.98f);
    px=.5f; pz=.48f; py=Y+7.6f; vy=-9.f;
    player_rules.fall_origin=Y+25.f;
    for (int tick=0;tick<45;++tick) game_step(.05f,0,0,0,0);
    CHECK(player_rules.health==MAX_HEALTH && player_on_ladder() && py>Y+3.5f && py<Y+5.8f);

    CHECK(set_block(-17,Y,2,STONE) && wall_attachment_place(-16,Y,2,WEST,1,0));
    CHECK(set_block(-17,Y,2,AIR) && world_cell(-16,Y,2)==AIR && count_drops(LADDER_ITEM)==1);
    CHECK(set_block(0,Y+6,-1,AIR) && world_cell(0,Y+6,0)==AIR && count_drops(WOOD_TRAPDOOR_ITEM)==1);
    CHECK(game_set_difficulty(prior));
    printf("SHAFT TEST crafting, touch, collision, signal edges, climbing, fall catch and supports failures=%d\n",bad);
    return bad;
}
