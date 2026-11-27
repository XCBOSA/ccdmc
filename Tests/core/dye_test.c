#include "world_test.h"
#include "../game/drops.h"
#include "../game/dyes.h"
#include "../game/enchanting.h"
#include "../game/farming.h"
#include "../game/furnace.h"
#include "../game/interaction.h"
#include "../game/item_icons.h"
#include "../world/internal.h"
#include "../world/rails.h"
#include "../world/space.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { Y=158 };
#define CHECK(c) do { if (!(c)) { printf("DYE TEST failed line %d: %s\n",__LINE__,#c); ++bad; } } while (0)
static ItemStack stack(int item,int count) { return (ItemStack){(uint16_t)item,(uint8_t)count,0,0,0}; }
static void aim(float x,float y,float z) {
    float dx=x-px,dz=z-pz;
    yaw=atan2f(dx,-dz); pitch=atan2f(y-py-1.55f,hypotf(dx,dz));
}
static int inventory_count(int item) {
    int n=0;
    for (int i=0;i<SLOT_COUNT;++i) if (inventory[i].type==item) n+=inventory[i].count;
    return n;
}
static int drops_count(int item) {
    int n=0;
    for (int i=0;i<MAXDROP;++i) if (drops[i].item.type==item) n+=drops[i].item.count;
    return n;
}
static int crafts(void) {
    int bad=0;
    static const int inputs[]={INK_SAC,GRAY_DYE,LIGHT_GRAY_DYE,BONE_MEAL,ROSE_RED,CACTUS_GREEN,
        COCOA_BEANS,LAPIS_LAZULI,PURPLE_DYE,CYAN_DYE,PINK_DYE,LIME_DYE,DANDELION_YELLOW,
        LIGHT_BLUE_DYE,MAGENTA_DYE,ORANGE_DYE};
    const int ring[]={0,0,0,0,1,0,0,0,0};
    ItemIcon icon;
    for (unsigned i=0;i<sizeof inputs/sizeof *inputs;++i) {
        memset(inventory,0,sizeof inventory);
        inventory[0]=stack(GLASS,8); inventory[1]=stack(inputs[i],1);
        int item=BLACK_GLASS_ITEM+(int)i,raw=STAINED_GLASS_BASE+(int)i;
        CHECK(craft_preview(ring,3).type==item && craft_take(ring,3) && inventory_count(item)==8 &&
            !inventory_count(GLASS) && !inventory_count(inputs[i]) && item_icon(item,&icon));
        CHECK(glass_recipe_item(inputs[i])==item && stained_glass_item(raw)==item &&
            valid_cell(raw) && cell_type(raw)==GLASS);
    }
    const struct { uint16_t in[4]; int type,count; } mixes[]={
        {{LAPIS_LAZULI,0,ROSE_RED,0},PURPLE_DYE,2}, {{CACTUS_GREEN,LAPIS_LAZULI,0,0},CYAN_DYE,2},
        {{BONE_MEAL,ROSE_RED,0,0},PINK_DYE,2}, {{CACTUS_GREEN,BONE_MEAL,0,0},LIME_DYE,2},
        {{BONE_MEAL,LAPIS_LAZULI,0,0},LIGHT_BLUE_DYE,2}, {{PINK_DYE,PURPLE_DYE,0,0},MAGENTA_DYE,2},
        {{ROSE_RED,LAPIS_LAZULI,0,PINK_DYE},MAGENTA_DYE,3},
        {{ROSE_RED,LAPIS_LAZULI,ROSE_RED,BONE_MEAL},MAGENTA_DYE,4},
        {{DANDELION_YELLOW,0,0,ROSE_RED},ORANGE_DYE,2},
        {{BONE_MEAL,LAPIS_LAZULI,BONE_MEAL,ROSE_RED},AIR,0}
    };
    for (unsigned i=0;i<sizeof mixes/sizeof *mixes;++i) {
        ItemStack s=mix_dye(mixes[i].in,4);
        CHECK(s.type==mixes[i].type && s.count==mixes[i].count);
    }
    memset(inventory,0,sizeof inventory);
    inventory[0]=stack(DANDELION_ITEM,1); inventory[1]=stack(POPPY_ITEM,1);
    inventory[2]=stack(WHEAT,2); inventory[3]=stack(COCOA_BEANS,1); inventory[4]=stack(LAPIS_LAZULI,9);
    CHECK(craft_preview((int[]){0,-1,-1,-1},2).type==DANDELION_YELLOW);
    CHECK(craft_preview((int[]){1,-1,-1,-1},2).type==ROSE_RED);
    CHECK(craft_preview((int[]){2,3,2,-1,-1,-1,-1,-1,-1},3).count==8 &&
        craft_take((int[]){2,3,2,-1,-1,-1,-1,-1,-1},3) && inventory_count(COOKIE)==8);
    CHECK(craft_take((int[]){4,4,4,4,4,4,4,4,4},3) && inventory_count(LAPIS_BLOCK_ITEM)==1);
    for (int item=LAPIS_LAZULI;item<=ORANGE_DYE;++item) CHECK(item_icon(item,&icon));
    return bad;
}
static int natural(void) {
    enum { WANT_LAPIS=1,WANT_FLOWER=2,WANT_CACTUS=4,WANT_COCOA=8 };
    const uint32_t seed=992382978u;
    int bad=0,had=0;
    unsigned sampled=0;
    static TerrainResult old,newer;
    CHECK(world_begin_generated(seed,GENERATOR_DYES));
    for (int x=-125;x<=125 && had!=15;x+=9) for (int z=-125;z<=125 && had!=15;z+=9) {
        Biome b=terrain_biome(x*CHUNK_SIZE+8,z*CHUNK_SIZE+8);
        int targets=(WANT_LAPIS|WANT_FLOWER|(b==BIOME_DESERT || b==BIOME_MESA ? WANT_CACTUS : 0)|
            (b==BIOME_JUNGLE ? WANT_COCOA : 0));
        if ((had&targets)==targets) continue;
        ++sampled;
        terrain_prepare(TERRAIN_KEY(seed,GENERATOR_GOLEMS,OVERWORLD,WORLD_STANDARD,0),x,z,&old);
        terrain_prepare(TERRAIN_KEY(seed,GENERATOR_DYES,OVERWORLD,WORLD_STANDARD,0),x,z,&newer);
        CHECK(!memcmp(old.cells,newer.cells,sizeof old.cells) && !memcmp(old.tops,newer.tops,sizeof old.tops));
        for (unsigned i=0;i<newer.wide.count;++i) {
            TerrainWideCell c=newer.wide.cell[i];
            int y=c.index%WY,col=c.index/WY,a=col/CHUNK_SIZE,bz=col%CHUNK_SIZE;
            for (unsigned j=0;j<i;++j) CHECK(c.index!=newer.wide.cell[j].index);
            if (c.cell==LAPIS_ORE_CELL) {
                CHECK(y>=3 && y<=29 && old.cells[c.index]==STONE);
                had|=WANT_LAPIS;
            } else if (c.cell==DANDELION_CELL) {
                CHECK(y>0 && old.cells[c.index-1]==GRASS && old.cells[c.index]==AIR);
                had|=WANT_FLOWER;
            } else if (cactus_cell(c.cell)) {
                CHECK((newer.biomes[col]==BIOME_DESERT || newer.biomes[col]==BIOME_MESA) && y>0 && old.cells[c.index]==AIR);
                int below=y ? old.cells[c.index-1] : AIR;
                for (unsigned j=0;j<i;++j)
                    if (newer.wide.cell[j].index+1==c.index) below=cell_type(newer.wide.cell[j].cell);
                CHECK(below==SAND || below==RED_SAND || below==CACTUS);
                had|=WANT_CACTUS;
            } else if (cocoa_cell(c.cell)) {
                int d=cocoa_support_side(c.cell),support=((a+rail_dx(d))*CHUNK_SIZE+bz+rail_dz(d))*WY+y;
                CHECK(support>=0 && support<CHUNK_CELLS && old.cells[support]==JUNGLE_LOG && cocoa_stage(c.cell)==2);
                had|=WANT_COCOA;
            }
            if (dye_resource_cell(c.cell))
                CHECK(world_cell(x*CHUNK_SIZE+a,y,z*CHUNK_SIZE+bz)==c.cell);
        }
        for (unsigned i=0;i<old.wide.count;++i) CHECK(!dye_resource_cell(old.wide.cell[i].cell));
    }
    printf("DYE TEST natural mask=%d chunks=%u, old base cells unchanged\n",had,sampled);
    CHECK(had==15);
    return bad;
}
static int play(void) {
    int bad=0;
    world_begin(18281); generate(); player_reset_rules(SURVIVAL);
    for (int x=6;x<=11;++x) for (int z=6;z<=12;++z) CHECK(set_block(x,Y-1,z,STONE));
    px=8.5f; py=Y; pz=11.5f;
    selected_slot=0;
    inventory[0]=stack(WOOD_PICKAXE,1);
    aim(8.5f,Y+.53f,8.5f);
    drops_reset();
    CHECK(set_block(8,Y,8,LAPIS_ORE) && mine() && !drops_count(LAPIS_LAZULI));
    inventory[0]=stack(STONE_PICKAXE,1);
    CHECK(set_block(8,Y,8,LAPIS_ORE) && mine() && drops_count(LAPIS_LAZULI)>=4 && drops_count(LAPIS_LAZULI)<=8);
    drops_reset(); inventory[0]=stack(DIAMOND_PICKAXE,1);
    inventory[0].enchants=enchant_code(ENCH_FORTUNE,3);
    CHECK(set_block(8,Y,8,LAPIS_ORE) && mine() && drops_count(LAPIS_LAZULI)>=4 && drops_count(LAPIS_LAZULI)<=32);
    drops_reset(); inventory[0].enchants=enchant_code(ENCH_SILK_TOUCH,1);
    CHECK(set_block(8,Y,8,LAPIS_ORE) && mine() && drops_count(LAPIS_ORE_ITEM)==1 && !drops_count(LAPIS_LAZULI));
    drops_reset();
    CHECK(set_block(8,Y,8,STAINED_GLASS_END-1) && mine() && drops_count(ORANGE_GLASS_ITEM)==1);
    drops_reset(); inventory[0]=stack(CACTUS_ITEM,3);
    CHECK(set_block(8,Y-2,8,STONE) && set_block(8,Y-1,8,SAND) && set_block(9,Y,8,STONE));
    aim(8.5f,Y-.04f,8.5f);
    CHECK(game_tap().message && block(8,Y,8)==AIR && inventory[0].count==3);
    CHECK(set_block(9,Y,8,AIR) && game_tap().message && cactus_cell(world_cell(8,Y,8)) && inventory[0].count==2);
    CHECK(world_point_blocked(8.5f,Y+.5f,8.5f,0) && !world_point_blocked(8.02f,Y+.5f,8.5f,0));
    px=8.5f; py=Y+1.001f; pz=8.5f;
    CHECK(world_cactus_contact(body_box(px,py,pz,.28f,1.72f)));
    unsigned hp=player_rules.health;
    game_step(.05f,0,0,0,0);
    CHECK(player_rules.health+1u==hp);
    CHECK(world_set_farm_stage(8,Y,8,CACTUS_END-1));
    /* A growth tick must not create the next cactus block inside a player. */
    for (int i=0;i<700;++i) farm_step(.05f);
    CHECK(block(8,Y-1,8)==SAND && cactus_cell(world_cell(8,Y,8)) && block(8,Y+1,8)==AIR);
    px=8.5f; py=Y; pz=11.5f;
    for (int i=0;i<700 && block(8,Y+1,8)!=CACTUS;++i) farm_step(.05f);
    CHECK(block(8,Y+1,8)==CACTUS);
    aim(8.5f,Y+.45f,8.5f);
    CHECK(mine() && block(8,Y,8)==AIR && block(8,Y+1,8)==AIR && drops_count(CACTUS_ITEM)==2);
    CHECK(set_block(7,Y,8,FURNACE));
    inventory[1]=stack(COAL,1);
    CHECK(furnace_deposit(7,Y,8,FURNACE_INPUT,0) && furnace_deposit(7,Y,8,FURNACE_FUEL,1));
    for (int i=0;i<=SMELT_TICKS;++i) furnaces_advance(.0501f);
    const Furnace *f=furnace_at(7,Y,8);
    CHECK(f && f->slots[FURNACE_OUTPUT].type==CACTUS_GREEN && f->slots[FURNACE_OUTPUT].count==1);
    drops_reset();
    inventory[0]=stack(COCOA_BEANS,3);
    CHECK(set_block(8,Y,8,JUNGLE_LOG));
    aim(8.5f,Y+.45f,8.5f);
    CHECK(game_tap().message && cocoa_cell(world_cell(8,Y,9)) && inventory[0].count==2);
    int raw=world_cell(8,Y,9);
    CHECK(cocoa_stage(raw)==0 && cocoa_support_side(raw)==NORTH &&
        farm_bonemeal(8,Y,9)==1 && farm_bonemeal(8,Y,9)==1 && cocoa_stage(world_cell(8,Y,9))==2);
    CHECK(world_point_blocked(8.5f,Y+.49f,9.2f,0) && !world_point_blocked(8.5f,Y+.49f,9.85f,0));
    px=8.5f; pz=6.5f; aim(8.5f,Y+.45f,8.5f);
    CHECK(mine() && block(8,Y,9)==AIR && drops_count(COCOA_BEANS)==3);
    return bad;
}
int dye_selftest(void) {
    int bad=crafts();
    bad+=natural();
    bad+=play();
    printf("DYE TEST gathering, growth, melting, recipes, and sixteen glass colors failures=%d\n",bad);
    return bad;
}
