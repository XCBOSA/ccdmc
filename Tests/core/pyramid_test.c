#include "../game/blast.h"
#include "../game/chests.h"
#include "../game/drops.h"
#include "../game/furnace.h"
#include "../game/interaction.h"
#include "../game/item_icons.h"
#include "../world/internal.h"
#include "../world/redstone.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { printf("PYRAMID TEST line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
static int part(int x) { return x/CHUNK_SIZE-(x%CHUNK_SIZE<0); }
static int block_at(BlockEdit p) { return world_cell(p.x,p.y,p.z); }
static void walk(BlockEdit p) {
    px=p.x+.5f; py=p.y+.01f; pz=p.z+.5f;
    player_rules.fall_origin=py;
}
static BlockEdit plate(DesertPyramidPlan plan) {
    return desert_pyramid_position(plan,0,PYRAMID_TREASURE,0,PRESSURE_STONE);
}
static int find_crossing(DesertPyramidPlan *out) {
    for (int a=-24;a<=24;++a) for (int b=-24;b<=24;++b) {
        DesertPyramidPlan p={0};
        if (desert_pyramid_plan_for(a*PYRAMID_REGION,b*PYRAMID_REGION,&p) &&
            (part(p.x-1)!=part(p.x+1) || part(p.z-1)!=part(p.z+1))) {
            *out=p;
            return 1;
        }
    }
    return 0;
}
static int tnt_left(DesertPyramidPlan p) {
    int count=0;
    for (int x=-1;x<=1;++x) for (int z=-1;z<=1;++z)
        count+=block_at(desert_pyramid_position(p,x,PYRAMID_TNT,z,0))==TNT_BLOCK;
    return count;
}
static unsigned setup_trap(DesertPyramidPlan p) {
    int a=part(p.x),b=part(p.z);
    for (int i=a-1;i<=a+1;++i) for (int j=b-1;j<=b+1;++j)
        (void)world_cell(i*CHUNK_SIZE,p.y,j*CHUNK_SIZE);
    walk(desert_pyramid_position(p,1,PYRAMID_TREASURE,1,AIR));
    for (int i=0;i<4;++i) redstone_step(p.x,p.z);
    BlockEdit at=plate(p);
    CHECK(block_at(at)==PRESSURE_STONE && tnt_left(p)==9 &&
        !redstone_plate_power(at.x,at.y,at.z));
    size_t primed=0;
    (void)blasts_in(OVERWORLD,&primed);
    CHECK(!primed);
    return (unsigned)primed;
}
static void snapshots(TerrainKey key,DesertPyramidPlan p) {
    static TerrainResult first,again;
    unsigned markers=0,device_chunks=0,chests=0,ink=0;
    for (int cx=part(p.x-PYRAMID_RADIUS);cx<=part(p.x+PYRAMID_RADIUS);++cx)
        for (int cz=part(p.z-PYRAMID_RADIUS);cz<=part(p.z+PYRAMID_RADIUS);++cz) {
            terrain_prepare(key,cx,cz,&first);
            terrain_prepare(key,cx+6,cz+5,&again);
            terrain_prepare(key,cx,cz,&again);
            CHECK(first.devices.count==again.devices.count && first.wide.count==again.wide.count &&
                !memcmp(first.devices.cell,again.devices.cell,first.devices.count*sizeof(TerrainWideCell)) &&
                !memcmp(first.wide.cell,again.wide.cell,first.wide.count*sizeof(TerrainWideCell)) &&
                !memcmp(first.cells,again.cells,sizeof first.cells));
            markers+=first.devices.count;
            device_chunks+=first.devices.count!=0;
            for (unsigned j=0;j<first.wide.count;++j) {
                chests+=first.wide.cell[j].cell==DESERT_PYRAMID_CHEST_CELL;
                ink+=stained_clay_cell(first.wide.cell[j].cell);
            }
        }
    CHECK(markers==10 && device_chunks>=2 && chests==4 && ink>=20);
}
static void safe(DesertPyramidPlan p) {
    player_reset_rules(SURVIVAL);
    CHECK(game_set_difficulty(NORMAL));
    (void)setup_trap(p);
    BlockEdit at=plate(p);
    px=at.x+1.52f; py=at.y+.01f; pz=at.z+.50f;
    yaw=-1.5707963f; pitch=-.98f; selected_slot=0;
    inventory[0]=(ItemStack){IRON_PICKAXE,1,0,0,0};
    Hit target_plate=target();
    CHECK(target_plate.hit && target_plate.x==at.x && target_plate.y==at.y && target_plate.z==at.z);
    CHECK(mine()!=NULL && block_at(at)==AIR);
    walk(at);
    for (int i=0;i<6;++i) redstone_step(p.x,p.z);
    CHECK(tnt_left(p)==9);
    yaw=1.5707963f; pitch=-.49f;
    const Interaction opened=game_tap();
    CHECK(opened.panel==INTERACTION_CHEST && opened.hit.x==p.x+2 &&
        opened.hit.y==at.y && opened.hit.z==p.z);
    int total=0;
    for (int i=0;i<4;++i) {
        int dx=i==0 ? 2 : i==1 ? -2 : 0,dz=i==2 ? 2 : i==3 ? -2 : 0;
        BlockEdit q=desert_pyramid_position(p,dx,PYRAMID_TREASURE,dz,0);
        ChestPos pos={q.x,q.y,q.z};
        ChestView view={OVERWORLD,{{0,0,0},{0,0,0}},0,0};
        CHECK(block_at(q)==DESERT_PYRAMID_CHEST_CELL && chest_open(pos,&view)==CHEST_OK && view.count==1);
        int each=0;
        for (int j=0;j<CHEST_SLOTS;++j) {
            int count=chest_slot(&view,j).count;
            each+=count;
            while (count-->0) chest_automation_remove(&view,j);
        }
        CHECK(each>=2 && chest_open(pos,&view)==CHEST_OK && block_at(q)==CHEST);
        total+=each;
    }
    CHECK(world_select_dimension(NETHER) && world_select_dimension(OVERWORLD));
    CHECK(block_at(at)==AIR && tnt_left(p)==9);
    printf("PYRAMID TEST safely disarmed, four independent chests, total loot=%d\n",total);
}
static void trap(DesertPyramidPlan p) {
    player_reset_rules(SURVIVAL);
    (void)setup_trap(p);
    walk(plate(p));
    for (int i=0;i<10;++i) redstone_step(p.x,p.z);
    size_t n=0,flashes=0;
    const PrimedBlast *charges=blasts_in(OVERWORLD,&n);
    CHECK(n>=1 && charges[0].kind==BLAST_TNT && tnt_left(p)<9);
    walk(desert_pyramid_position(p,0,1,2,AIR));
    int maximum=0;
    for (int i=0;i<TNT_FUSE+2*CHAIN_FUSE_MAX+20;++i) {
        blasts_step(1);
        (void)blast_flashes(OVERWORLD,&flashes);
        if ((int)flashes>maximum) maximum=(int)flashes;
    }
    CHECK(maximum>=2 && tnt_left(p)==0);
    printf("PYRAMID TEST pressure plate lit TNT, peak chain flashes=%d, remaining=%d\n",maximum,tnt_left(p));
}
static void stained_clay(void) {
    CHECK(world_begin_profile(1712,GENERATOR_CURRENT,WORLD_SUPERFLAT));
    furnaces_reset();
    CHECK(set_block(5,4,5,FURNACE));
    CHECK(furnace_automation_insert(5,4,5,FURNACE_INPUT,(ItemStack){CLAY,1,0,0,0}));
    CHECK(furnace_automation_insert(5,4,5,FURNACE_FUEL,(ItemStack){COAL,1,0,0,0}));
    for (int n=0;n<=SMELT_TICKS+2;++n) furnaces_advance(.05f);
    const Furnace *f=furnace_at(5,4,5);
    CHECK(f && f->slots[FURNACE_OUTPUT].type==TERRACOTTA && f->slots[FURNACE_OUTPUT].count==1);
    const int dye[]={INK_SAC,GRAY_DYE,LIGHT_GRAY_DYE,BONE_MEAL,ROSE_RED,CACTUS_GREEN,
        COCOA_BEANS,LAPIS_LAZULI,PURPLE_DYE,CYAN_DYE,PINK_DYE,LIME_DYE,DANDELION_YELLOW,
        LIGHT_BLUE_DYE,MAGENTA_DYE,ORANGE_DYE};
    int sources[9];
    inventory_reset();
    for (int n=0;n<9;++n) {
        sources[n]=n;
        inventory[n]=(ItemStack){TERRACOTTA,1,0,0,0};
    }
    for (unsigned i=0;i<sizeof dye/sizeof *dye;++i) {
        inventory[4]=(ItemStack){(uint16_t)dye[i],1,0,0,0};
        ItemStack result=craft_preview(sources,3);
        const ItemDef *def=item_def(result.type);
        ItemIcon icon;
        CHECK(result.type==BLACK_CLAY_ITEM+i && result.count==8 && def && item_icon(result.type,&icon));
        if (!def) continue;
        int placed=def->placed_block;
        CHECK(stained_clay_cell(placed) && set_block(4+(int)i,4,8,placed) &&
            block(4+(int)i,4,8)==placed && mining_drop(placed,1)==result.type &&
            mining_drop(placed,0)==AIR);
    }
    furnaces_reset();
    inventory_reset();
}
int pyramid_selftest(void) {
    failures=0;
    enum { SEED=1712 };
    TerrainKey key=TERRAIN_KEY(SEED,GENERATOR_DESERT_PYRAMIDS,OVERWORLD,WORLD_STANDARD,0);
    DesertPyramidPlan p={0};
    CHECK(world_begin_generated(SEED,key.generation));
    if (!find_crossing(&p)) { CHECK(0 && "need deterministic seam fixture"); return failures; }
    printf("PYRAMID TEST natural seed=%u centre=%d,%d,%d rotation=%u\n",SEED,p.x,p.y,p.z,p.rotation);
    CHECK(terrain_biome(p.x,p.z)==BIOME_DESERT);
    snapshots(key,p);
    CHECK(block_at(desert_pyramid_position(p,0,0,0,0))==BLUE_STAINED_CLAY);
    CHECK(block_at(desert_pyramid_position(p,-8,5,10,0))==ORANGE_STAINED_CLAY);
    CHECK(block_at(desert_pyramid_position(p,8,5,10,0))==ORANGE_STAINED_CLAY);
    CHECK(block_at(desert_pyramid_position(p,0,1,10,0))==AIR);
    CHECK(block_at(desert_pyramid_position(p,6,4,2,0))==SANDSTONE &&
        block_at(desert_pyramid_position(p,6,5,2,0))==SANDSTONE);
    safe(p);
    CHECK(world_begin_generated(SEED,key.generation));
    trap(p);
    CHECK(world_begin_generated(SEED,GENERATOR_MINESHAFTS) && !desert_pyramid_plan_for(p.x,p.z,&p));
    CHECK(world_begin_options(SEED,key.generation,WORLD_STANDARD,1) && !desert_pyramid_plan_for(p.x,p.z,&p));
    FlatLayout flat={0};
    CHECK(!flat_parse("bedrock,3*stone,52*sandstone,8*sand | dryland | outposts",&flat));
    CHECK(world_begin_layout(SEED,key.generation,WORLD_SUPERFLAT,0,&flat) &&
        desert_pyramid_plan_for(0,0,&p) && p.y==flat.height &&
        block_at(desert_pyramid_position(p,0,0,0,0))==BLUE_STAINED_CLAY);
    flat.features=0;
    CHECK(world_begin_layout(SEED,key.generation,WORLD_SUPERFLAT,0,&flat) && !desert_pyramid_plan_for(0,0,&p));
    stained_clay();
    drops_reset();
    blasts_reset();
    world_reset();
    printf("PYRAMID TEST failures=%d\n",failures);
    return failures;
}
