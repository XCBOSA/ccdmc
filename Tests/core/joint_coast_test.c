#include "world_test.h"
#include "../world/internal.h"
#include "../game/survey.h"
#include "../game/item_icons.h"
#include "../game/actors.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static float walk_on(int x,int y,int flying) {
    px=x+.5f; pz=12.5f; py=y+1.015f; vy=0.f;
    yaw=0.f; pitch=0.f; mx=my=0.f; grounded=1;
    player_reset_rules(CREATIVE); player_rules.flying=flying;
    for (int i=0;i<12;++i) (void)game_step(.05f,0,1,0,0);
    return 12.5f-pz;
}
int joint_coast_selftest(void) {
    const uint64_t seed=91825;
    const int x=470,y=67,z=-49;
    int failures=0,exposed=0,covered=0,high=-1,low=WY;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"JOINT COAST line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
    CHECK(world_begin_generated(seed,GENERATOR_RAIN_GARDEN) && terrain_biome(x,z)==BIOME_MUSHROOM);
    CHECK(world_begin_generated(seed,GENERATOR_JOINT_COAST) && terrain_biome(x,z)==BIOME_JOINT_COAST);
    CHECK(!strcmp(biome_name(BIOME_JOINT_COAST),"柱岩海岸") && terrain_height(x,z)==y);
    CHECK(world_cell(x,y,z)==STONE && world_cell(x,y-1,z)==JOINT_BASALT && !world_has_edit(x,y-1,z));
    const Hit summit={1,x,y,z,x,y+1,z},under={1,x,y-1,z,x,y,z};
    const SurveyReading seam=survey_read(summit);
    CHECK(seam.state==SURVEY_SEAM && seam.depth==1 && seam.rock==JOINT_BASALT &&
        set_block(x,y,z,AIR) && survey_read(under).state==SURVEY_EXPOSED);
    CHECK(mining_drop(JOINT_BASALT,0)==AIR && mining_drop(JOINT_BASALT,1)==AIR &&
        mining_drop(JOINT_BASALT,2)==JOINT_BASALT_ITEM && mining_drop(BASALT_PAVING,1)==BASALT_PAVING_ITEM);
    const TerrainKey fresh=TERRAIN_KEY(seed,GENERATOR_JOINT_COAST,OVERWORLD,WORLD_STANDARD,0);
    const TerrainKey old=TERRAIN_KEY(seed,GENERATOR_RAIN_GARDEN,OVERWORLD,WORLD_STANDARD,0);
    static TerrainResult a,b,other;
    terrain_prepare(fresh,0,0,&a); terrain_prepare(old,0,0,&b);
    CHECK(!memcmp(a.biomes,b.biomes,sizeof a.biomes) && !memcmp(a.cells,b.cells,sizeof a.cells) &&
        a.wide.count==b.wide.count && !memcmp(a.wide.cell,b.wide.cell,a.wide.count*sizeof a.wide.cell[0]));
    for (int cx=28;cx<=30;++cx) for (int cz=-5;cz<=-3;++cz) {
        terrain_prepare(fresh,cx,cz,&a);
        terrain_prepare(fresh,cx+5,cz-4,&other);
        terrain_prepare(fresh,cx,cz,&b);
        CHECK(!memcmp(a.biomes,b.biomes,sizeof a.biomes) && !memcmp(a.tops,b.tops,sizeof a.tops) &&
            a.wide.count==b.wide.count && !memcmp(a.wide.cell,b.wide.cell,a.wide.count*sizeof a.wide.cell[0]));
        CHECK(a.wide.count<=TERRAIN_WIDE_ARCHITECTURE_LIMIT);
        for (unsigned i=0;i<a.wide.count;++i) if (a.wide.cell[i].cell==JOINT_BASALT) {
            const unsigned index=a.wide.cell[i].index,col=index/WY;
            CHECK(a.biomes[col]==BIOME_JOINT_COAST && a.cells[index]==STONE);
            exposed+=index%WY==a.tops[col]; covered+=index%WY+1==a.tops[col];
            if (a.tops[col]>high) high=a.tops[col];
            if (a.tops[col]<low) low=a.tops[col];
        }
    }
    CHECK(exposed>10 && covered>10 && high-low>=3);
    /* Natural tidal edge: warm only the surrounding real chunks, do
       not place a creature or alter its habitat in this fixture. */
    generate();
    px=475.5f; py=66.015f; pz=-55.5f;
    player_reset_rules(CREATIVE);
    player_rules.day_tick=4260;
    for (int cx=28;cx<=31;++cx) for (int cz=-5;cz<=-3;++cz)
        (void)world_cell(cx*CHUNK_SIZE,(int)py,cz*CHUNK_SIZE);
    int natural_crab=0;
    for (int i=0;i<1200 && !natural_crab;++i) {
        actors_step(.1f);
        for (int slot=0;slot<ACTOR_CAPACITY;++slot) {
            const ActorRecord a=actors_in(OVERWORLD)[slot];
            if (a.type!=ACTOR_TIDE_CRAB) continue;
            int ax=(int)floorf(a.feet.x),az=(int)floorf(a.feet.z);
            natural_crab=terrain_biome(ax,az)==BIOME_JOINT_COAST;
        }
    }
    CHECK(natural_crab);
    FlatLayout layout;
    CHECK(!flat_parse("bedrock,8*stone,loam | meadow",&layout) &&
        world_begin_layout(seed,GENERATOR_JOINT_COAST,WORLD_SUPERFLAT,1,&layout));
    for (int z0=2;z0<=13;++z0) CHECK(set_block(5,layout.height-1,z0,BASALT_PAVING));
    const float ordinary=walk_on(3,layout.height-1,0), road=walk_on(5,layout.height-1,0);
    const float flying=walk_on(3,layout.height-1,1), road_flying=walk_on(5,layout.height-1,1);
    CHECK(ordinary>1.f && fabsf(road/ordinary-1.18f)<.02f && fabsf(road_flying-flying)<.01f);
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){JOINT_BASALT_ITEM,1,0,0,0};
    inventory[1]=(ItemStack){GRAVEL,1,0,0,0};
    const int slots[]={0,1,-1,-1};
    ItemIcon icon;
    CHECK(craft_preview(slots,2).type==BASALT_PAVING_ITEM && craft_preview(slots,2).count==2 &&
        craft_take(slots,2) && item_icon(BASALT_PAVING_ITEM,&icon) && icon.path[0] &&
        item_def(BASALT_PAVING_ITEM)->placed_block==BASALT_PAVING);
    printf("JOINT COAST old biome/negative seam/rocks/paving: exposed=%d covered=%d relief=%d crab=%d walk %.3f->%.3f failures=%d\n",
        exposed,covered,high-low,natural_crab,ordinary,road,failures);
    world_reset();
#undef CHECK
    return failures;
}
