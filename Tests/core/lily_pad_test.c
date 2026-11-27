#include "../game/boats.h"
#include "../game/drops.h"
#include "../game/fishing.h"
#include "../game/interaction.h"
#include "../game/item_icons.h"
#include "../game/mining_press.h"
#include "../world/internal.h"
#include "../world/space.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(e) do { if (!(e)) { printf("LILY TEST line %d failed: %s\n",__LINE__,#e); ++failures; } } while (0)

static int lily_drops(void) {
    int count=0;
    for (int i=0;i<MAXDROP;++i) if (drops[i].item.type==LILY_PAD_ITEM) count+=drops[i].item.count;
    return count;
}
static int natural_generation(void) {
    int failures=0,found=0;
    TerrainResult *old=malloc(sizeof *old),*now=malloc(sizeof *now);
    if (!old || !now) { free(old); free(now); return 1; }
    TerrainKey key=TERRAIN_KEY(196,GENERATOR_WETLANDS,OVERWORLD,WORLD_STANDARD,0);
    for (int cx=-21;cx<=-14 && !found;++cx) for (int cz=40;cz<=48 && !found;++cz) {
        terrain_prepare(key,cx,cz,now);
        for (unsigned i=0;i<now->wide.count;++i) if (now->wide.cell[i].cell==LILY_PAD) {
            int at=now->wide.cell[i].index;
            CHECK(at%WY>0 && now->cells[at]==AIR && now->cells[at-1]==WATER && now->biomes[at/WY]==BIOME_SWAMP);
            ++found;
        }
        if (found) {
            key.generation=GENERATOR_DESERT_PYRAMIDS;
            terrain_prepare(key,cx,cz,old);
            for (int at=0;at<CHUNK_CELLS;++at) if (old->cells[at]!=now->cells[at])
                CHECK(old->cells[at]==GRASS && now->cells[at]==WATER &&
                    old->biomes[at/WY]==BIOME_SWAMP && at%WY==old->tops[at/WY]);
            CHECK(!memcmp(old->biomes,now->biomes,sizeof old->biomes) &&
                old->wide.count+(unsigned)found==now->wide.count &&
                !memcmp(old->wide.cell,now->wide.cell,old->wide.count*sizeof *old->wide.cell));
            key.generation=GENERATOR_WETLANDS;
            terrain_prepare(key,cx,cz,old);
            CHECK(old->wide.count==now->wide.count &&
                !memcmp(old->wide.cell,now->wide.cell,now->wide.count*sizeof *now->wide.cell));
        }
    }
    CHECK(found>0);
    free(old); free(now);
    return failures;
}

int lily_pad_selftest(void) {
    int failures=natural_generation();
    enum { Y=180 };
    if (!world_begin_options(1712,GENERATOR_CURRENT,WORLD_SUPERFLAT,1)) return 1;
    player_reset_rules(SURVIVAL);
    inventory_reset(); drops_reset(); boats_reset();
    for (int x=-1;x<=1;++x) for (int z=-7;z<=3;++z) {
        CHECK(set_block(x,Y-1,z,STONE));
        CHECK(set_block(x,Y,z,z>=2 ? STONE : WATER));
    }
    ItemIcon icon;
    CHECK(item_icon(LILY_PAD_ITEM,&icon) && icon.path[0] && icon.path[0][0]);
    selected_slot=0; inventory[0]=(ItemStack){LILY_PAD_ITEM,4,0,0,0};
    px=.50f; py=Y+1.01f; pz=2.75f; yaw=0; pitch=atan2f(-1.56f,2.25f);
    const BlockEdit no_source[]={{0,Y,0,FLOW_BASE+2},{0,Y+1,0,LILY_PAD}};
    CHECK(!world_set_blocks(no_source,2) && world_cell(0,Y,0)==WATER);
    CHECK(world_set_blocks(no_source,1) && !game_tap().placed && inventory[0].count==4);
    CHECK(set_block(0,Y,0,WATER));
    CHECK(game_tap().placed && world_cell(0,Y+1,0)==LILY_PAD && inventory[0].count==3);
    CHECK(!game_tap().placed && inventory[0].count==3);
    CHECK(!solid(LILY_PAD) && world_point_blocked(.50f,Y+1.005f,.50f,1) &&
        !world_point_blocked(.50f,Y+1.04f,.50f,1));
    CHECK(world_box_blocked(body_box(.5f,Y+1.003f,.5f,.28f,1.72f),0));
    CHECK(!world_box_blocked(body_box(.5f,Y+1.02f,.5f,.28f,1.72f),0));
    px=.50f; py=Y+1.28f; pz=.50f; vy=0; grounded=0;
    for (int i=0;i<12;++i) game_step(.05f,0,0,0,0);
    CHECK(grounded && py>=Y+1.f && py<Y+1.07f);
    px=.50f; py=Y+1.01f; pz=2.75f; yaw=0; pitch=atan2f(-1.56f,2.25f);
    CHECK(player_mining_aim().raw==LILY_PAD);
    CHECK(mine()!=NULL && world_cell(0,Y+1,0)==AIR && world_cell(0,Y,0)==WATER && lily_drops()==1);
    CHECK(set_block(0,Y+1,0,LILY_PAD) && set_block(0,Y,0,AIR) &&
        world_cell(0,Y+1,0)==AIR && lily_drops()==2);
    CHECK(!set_block(0,Y+1,0,LILY_PAD));
    CHECK(set_block(0,Y,0,WATER) && set_block(0,Y+1,-2,LILY_PAD));
    CHECK(water_view_sample(.5f,Y+.5f,.5f).submerged &&
        !water_view_sample(.5f,Y+.97f,.5f).submerged);
    WaterView pad_water=water_view_sample(.5f,Y+.5f,-1.5f);
    CHECK(pad_water.submerged && pad_water.surface==Y+water_surface(WATER,AIR));
    CHECK(set_block(0,Y+3,0,STONE));
    CHECK(!water_view_sample(.5f,Y+.5f,.5f).open_sky);
    CHECK(set_block(0,Y+3,0,AIR) && water_view_sample(.5f,Y+.5f,.5f).open_sky);
    const Vec3 start={.5f,Y+.925f,.8f};
    CHECK(boat_restore(OVERWORLD,(BoatRecord){start,0,1}));
    px=start.x; py=start.y+.18f; pz=start.z; yaw=0;
    CHECK(boat_restore_rider(0));
    for (int n=0;n<19;++n) boats_step(.05f,0,1);
    CHECK(pz<-1.6f && world_cell(0,Y+1,-2)==AIR && lily_drops()==3);
    int caught=0;
    uint32_t rng=1712;
    for (int i=0;i<4000;++i) caught+=fishing_roll_catch(&rng,0).type==LILY_PAD_ITEM;
    CHECK(caught>0);
    world_reset();
    printf("LILY TEST new generation/legacy/source/shape/boat/fishing failures=%d\n",failures);
    return failures;
}
