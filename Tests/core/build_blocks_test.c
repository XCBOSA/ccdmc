#include "../game/game.h"
#include "../game/drops.h"
#include "../game/interaction.h"
#include "../game/item_icons.h"
#include "../world/build_blocks.h"
#include "../world/redstone.h"
#include "../world/space.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("BUILD BLOCKS TEST line %d: %s\n",__LINE__,#c); ++bad; } } while (0)
static void look(float x,float y,float z) {
    yaw=atan2f(x-px,-(z-pz)); pitch=atan2f(y-py-1.55f,hypotf(x-px,z-pz));
}
static int edit(int x,int y,int z,int raw) {
    const BlockEdit e={x,y,z,raw};
    return world_set_blocks(&e,1);
}
int build_blocks_selftest(void) {
    int bad=0;
    enum { Y=124 };
    if (!world_begin_options(1614,GENERATOR_CURRENT,WORLD_SUPERFLAT,1)) return 1;
    generate(); player_reset_rules(SURVIVAL);
    const GameDifficulty prior=game_selected_difficulty();
    CHECK(game_set_difficulty(NORMAL));
    const int stone=build_slab_raw(BUILD_STONE,BUILD_SLAB_BOTTOM), top=stone+1,dbl=stone+2;
    const int oak=build_stair_raw(BUILD_OAK,NORTH,0),acacia=build_stair_raw(BUILD_ACACIA,EAST,1);
    const int in[]={PLANKS,BIRCH_PLANK_ITEM,SPRUCE_PLANK_ITEM,JUNGLE_PLANK_ITEM,ACACIA_PLANK_ITEM,
        DARK_OAK_PLANK_ITEM,STONE,SANDSTONE,COBBLESTONE,BRICK_BLOCK_ITEM,STONE_BRICKS,NETHER_BRICKS,QUARTZ_BLOCK_ITEM};
    const int row[]={-1,-1,-1,0,0,0,-1,-1,-1};
    const int left[]={0,-1,-1,0,0,-1,0,0,0},right[]={-1,-1,0,-1,0,0,0,0,0};
    for (int mat=0;mat<BUILD_MATERIALS;++mat) {
        memset(inventory,0,sizeof inventory); inventory[0]=(ItemStack){(uint16_t)in[mat],12,0,0,0};
        ItemStack slab=craft_preview(row,3);
        CHECK(slab.count==6 && slab.type==BUILD_SLAB_ITEMS+mat);
        CHECK(craft_take(row,3) && inventory[0].count==9);
        if (mat==BUILD_STONE) CHECK(!craft_preview(left,3).count && !build_stair_raw(mat,NORTH,0));
        else {
            ItemStack a=craft_preview(left,3),b=craft_preview(right,3);
            ItemIcon icon;
            CHECK(a.count==4 && a.type==building_stair_item(mat) && a.type==b.type && b.count==4 && item_icon(a.type,&icon));
        }
    }
    inventory[0]=(ItemStack){ACACIA_LOG,1,0,0,0};
    const int single[]={-1,0,-1,-1};
    CHECK(craft_preview(single,2).type==ACACIA_PLANK_ITEM);
    inventory[0]=(ItemStack){BIRCH_PLANK_ITEM,3,0,0,0}; inventory[1]=(ItemStack){PLANKS,1,0,0,0};
    const int mixed[]={0,1,0,0};
    CHECK(craft_preview(mixed,2).type==CRAFTING_TABLE);
    for (int d=0;d<4;++d) {
        unsigned tall[]={3,10,12,5};
        int north=build_stair_raw(BUILD_BIRCH,d,0),turn=(d+1)&3;
        int other=build_stair_raw(BUILD_QUARTZ,turn,0),near[4]={0};
        CHECK(build_mask(north,NULL)==(15u|tall[d]<<4));
        near[d]=other;
        CHECK(build_mask(north,near)==(15u|(tall[d]&tall[turn])<<4));
        near[(turn+2)&3]=north;
        CHECK(build_mask(north,near)==build_mask(north,NULL));
        memset(near,0,sizeof near); near[(d+2)&3]=other;
        CHECK(build_mask(north,near)==(15u|(tall[d]|tall[turn])<<4));
        near[(d+2)&3]=build_stair_raw(BUILD_QUARTZ,turn,1);
        CHECK(build_mask(north,near)==build_mask(north,NULL));
    }
    CHECK(build_mask(stone,NULL)==15u && build_mask(top,NULL)==240u && build_mask(dbl,NULL)==255u &&
        build_partial(oak) && !build_partial(dbl) && full_top_surface(acacia));
    CHECK(set_block(-17,Y-1,2,STONE) && set_block(-16,Y-1,2,STONE));
    CHECK(edit(-17,Y,2,build_stair_raw(BUILD_BRICK,EAST,0)) && edit(-16,Y,2,build_stair_raw(BUILD_QUARTZ,SOUTH,0)));
    CHECK(world_point_blocked(-16.24f,Y+.75f,2.75f,1) &&
        !world_point_blocked(-16.24f,Y+.75f,2.25f,1));
    CHECK(edit(-16,Y,2,AIR) && world_point_blocked(-16.24f,Y+.75f,2.25f,1));

    for (int z=-5;z<=5;++z) CHECK(set_block(0,Y-1,z,STONE));
    CHECK(set_block(0,Y,-1,STONE) && edit(0,Y,0,stone));
    px=.5f; py=Y+.01f; pz=2.8f; selected_slot=0;
    inventory[0]=(ItemStack){BUILD_SLAB_ITEMS+BUILD_STONE,3,0,0,0};
    look(.5f,Y+.5f,.55f);
    Hit hit=target();
    CHECK(hit.hit && hit.y==Y && hit.ay==Y+1 && hit.z==0);
    CHECK(game_tap().placed && world_cell(0,Y,0)==dbl && inventory[0].count==2);
    inventory[0]=(ItemStack){WOOD_PICKAXE,1,0,0,0};
    CHECK(mine() && world_cell(0,Y,0)==AIR);
    int slabs=0;
    for (int i=0;i<MAXDROP;++i) if (drops[i].item.type==BUILD_SLAB_ITEMS+BUILD_STONE) slabs+=drops[i].item.count;
    CHECK(slabs==2);
    inventory[0]=(ItemStack){BUILD_SLAB_ITEMS+BUILD_STONE,3,0,0,0};
    look(.5f,Y+.78f,0.f);
    CHECK(game_tap().placed && world_cell(0,Y,0)==top && inventory[0].count==2);
    CHECK(world_point_blocked(.5f,Y+.75f,.5f,1) && !world_point_blocked(.5f,Y+.25f,.5f,1));
    look(.5f,Y+.09f,0.f);
    CHECK(edit(0,Y,0,AIR) && game_tap().placed && world_cell(0,Y,0)==stone);
    CHECK(!redstone_place_wire(0,Y+1,0));
    CHECK(edit(0,Y,0,top) && redstone_place_wire(0,Y+1,0));
    CHECK(edit(0,Y,0,stone));
    for (int n=0;n<8;++n) redstone_step(0,0);
    CHECK(world_cell(0,Y+1,0)==AIR);
    CHECK(edit(0,Y,0,oak) && edit(0,Y,-1,AIR) && edit(0,Y+1,-1,oak));
    for (int z=-3;z<=-2;++z) CHECK(set_block(0,Y,z,STONE) && set_block(0,Y+1,z,STONE));
    px=.5f; pz=1.9f; py=Y+.01f; vy=0.f; yaw=pitch=0;
    float max_vy=0.f;
    for (int n=0;n<85 && pz>-2.4f;++n) { game_step(.015f,0,1,0,0); max_vy=fmaxf(max_vy,vy); }
    printf("BUILD BLOCKS WALK x=%.3f y=%.3f z=%.3f max-upward=%.3f\n",px,py,pz,max_vy);
    CHECK(pz< -2.f && py>Y+1.98f && py<Y+2.03f && max_vy<.1f);
    CHECK(game_set_difficulty(prior));
    printf("BUILD BLOCKS TEST all materials, crafting, touching faces, corner seams, drops, circuits and walking failures=%d\n",bad);
    return bad;
}
