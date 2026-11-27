#include "world_test.h"
#include "../world/internal.h"
#include "../game/seep.h"
#include <stdio.h>
#include <string.h>

static int chunk(int a) { return a/CHUNK_SIZE-(a%CHUNK_SIZE<0); }
static int raw(BlockEdit p) { return world_cell(p.x,p.y,p.z); }

int rain_garden_selftest(void) {
    int failures=0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"RAIN GARDEN TEST line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
    const uint64_t seed=295588u;
    CHECK(world_begin_generated(seed,GENERATOR_RAIN_GARDEN));
    RainGardenPlan garden={0};
    int found=0;
    for (int x=-26;x<=26 && !found;++x) for (int z=-26;z<=26 && !found;++z) {
        if (!rain_garden_plan_for(x*192,z*192,&garden)) continue;
        const BlockEdit q=rain_garden_position(garden,2,RAIN_GARDEN_BED_LEVEL+1,0);
        found=chunk(q.x)!=chunk(garden.x) || chunk(q.z)!=chunk(garden.z);
    }
    if (!found) { CHECK(found); return failures; }
    const BlockEdit bed=rain_garden_position(garden,0,RAIN_GARDEN_BED_LEVEL,0);
    const BlockEdit west=rain_garden_position(garden,-1,RAIN_GARDEN_BED_LEVEL,0);
    const BlockEdit plug=rain_garden_position(garden,1,RAIN_GARDEN_BED_LEVEL+1,0);
    const BlockEdit east=rain_garden_position(garden,2,RAIN_GARDEN_BED_LEVEL+1,0);
    const BlockEdit spill=rain_garden_position(garden,1,RAIN_GARDEN_BED_LEVEL,0);
    const Hit tap={.hit=1,.x=bed.x,.y=bed.y,.z=bed.z,.ax=bed.x,.ay=bed.y+1,.az=bed.z};
    static TerrainResult a,b;
    const TerrainKey key=TERRAIN_KEY(seed,GENERATOR_RAIN_GARDEN,OVERWORLD,WORLD_STANDARD,0);
    for (int x=chunk(garden.x-7);x<=chunk(garden.x+7);++x)
        for (int z=chunk(garden.z-7);z<=chunk(garden.z+7);++z) {
            terrain_prepare(key,x,z,&a);
            terrain_prepare(key,x+4,z-1,&b);
            terrain_prepare(key,x,z,&b);
            CHECK(!a.devices.count && !b.devices.count &&
                !memcmp(a.cells,b.cells,sizeof a.cells) && a.wide.count==b.wide.count &&
                !memcmp(a.wide.cell,b.wide.cell,a.wide.count*sizeof a.wide.cell[0]));
            for (unsigned i=0;i<a.wide.count;++i) CHECK(a.wide.cell[i].cell!=TEMPLE_CHEST_CELL);
            (void)world_cell(x*CHUNK_SIZE,bed.y,z*CHUNK_SIZE);
        }
    CHECK(rain_garden_contains(garden,bed.x,bed.z) &&
        !rain_garden_contains(garden,garden.x+RAIN_GARDEN_RADIUS+1,garden.z));
    CHECK(raw(bed)==SEEP_BED && raw(west)==WATER && raw(east)==WATER && raw(plug)==CLAY && raw(spill)==AIR);
    CHECK(seep_wet_faces(bed.x,bed.y,bed.z)==1);
    player_reset_rules(SURVIVAL);
    memset(inventory,0,sizeof inventory);
    selected_slot=0;
    inventory[0]=(ItemStack){CLAY_BALL,1,0,0,0};
    CHECK(seep_use(tap) && inventory[0].type==SEEP_TILE_ITEM && inventory[0].count==2);
    CHECK(set_block(plug.x,plug.y,plug.z,AIR));
    for (int i=0;i<8;++i) water_step();
    CHECK(water_strength(raw(spill)) && seep_wet_faces(bed.x,bed.y,bed.z)==2 &&
        world_cell(bed.x,bed.y+1,bed.z)==AIR);
    inventory[0]=(ItemStack){CLAY_BALL,1,0,0,0};
    CHECK(seep_use(tap) && inventory[0].type==SEEP_BRIGHT_ITEM && inventory[0].count==2);
    CHECK(set_block(plug.x,plug.y,plug.z,CLAY));
    for (int i=0;i<9;++i) water_step();
    CHECK(raw(spill)==AIR && seep_wet_faces(bed.x,bed.y,bed.z)==1);
    TemplePlan old={0};
    CHECK(!temple_plan_for(garden.x,garden.z,&old));
    CHECK(world_begin_options(seed,GENERATOR_RAIN_GARDEN,WORLD_STANDARD,1) &&
        !rain_garden_plan_for(garden.x,garden.z,&garden));
    CHECK(world_begin_generated(seed,GENERATOR_WINDCUT) &&
        !rain_garden_plan_for(garden.x,garden.z,&garden) && temple_plan_for(garden.x,garden.z,&old));
    printf("RAIN GARDEN TEST clay lip, dual supply, re-seal and cross-chunk terrain, centre=(%d,%d,%d) R%u failures=%d\n",
        garden.x,garden.y,garden.z,garden.rotation,failures);
    world_reset();
#undef CHECK
    return failures;
}
