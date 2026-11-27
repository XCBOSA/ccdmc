#include "../game/survey.h"
#include "../world/dungeons.h"
#include "../world/internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(b) do { if (!(b)) { printf("STRATA TEST line %d: %s\n",__LINE__,#b); ++failures; } } while (0)
static int chunk(int cell) { return cell/CHUNK_SIZE-(cell%CHUNK_SIZE<0); }
static int story(const char *s,const char *word) { return s && strstr(s,word); }
int strata_gallery_selftest(void) {
    int failures=0,raw=0;
    const uint64_t seed=1712;
    CHECK(world_begin_generated(seed,GENERATOR_STRATA_GALLERIES));
    DungeonPlan plan={0};
    for (int rx=-10;!plan.entrances && rx<=10;++rx) for (int rz=-10;rz<=10;++rz) {
        DungeonPlan maybe;
        if (!strata_gallery_plan_for(rx*DUNGEON_REGION,rz*DUNGEON_REGION,&maybe)) continue;
        if (chunk(maybe.x-maybe.radius_x)!=chunk(maybe.x+maybe.radius_x) ||
            chunk(maybe.z-maybe.radius_z)!=chunk(maybe.z+maybe.radius_z)) { plan=maybe; break; }
    }
    if (!plan.entrances) return 1;
    DungeonPlan old;
    CHECK(!dungeon_plan_for(plan.x,plan.z,&old));
    TerrainKey key=TERRAIN_KEY(seed,GENERATOR_STRATA_GALLERIES,OVERWORLD,WORLD_STANDARD,0);
    TerrainResult *first=(TerrainResult *)malloc(sizeof *first),*second=(TerrainResult *)malloc(sizeof *second);
    if (!first || !second) { free(first); free(second); return 1; }
    terrain_prepare(key,chunk(plan.x),chunk(plan.z),first);
    terrain_prepare(key,chunk(plan.x)+1,chunk(plan.z)-1,second);
    terrain_prepare(key,chunk(plan.x),chunk(plan.z),second);
    CHECK(!memcmp(first->cells,second->cells,sizeof first->cells) && first->wide.count==second->wide.count &&
        !memcmp(first->wide.cell,second->wide.cell,first->wide.count*sizeof first->wide.cell[0]));
    free(first); free(second);
    int mouths=0,new_samples=0;
    for (int side=0;side<4;++side) for (int along=-3;along<=3;++along) {
        if (!((plan.entrances>>(side*7+along+3))&1u)) continue;
        int edge_x=plan.x+(side==0 ? -plan.radius_x : side==1 ? plan.radius_x : along);
        int edge_z=plan.z+(side==2 ? -plan.radius_z : side==3 ? plan.radius_z : along);
        const int dx=side==0 ? -1 : side==1 ? 1 : 0,dz=side==2 ? -1 : side==3 ? 1 : 0;
        ++mouths;
        for (int k=-1;k<=1;++k) {
            CHECK(world_cell(edge_x+k*dx,plan.floor+1,edge_z+k*dz)==AIR);
            CHECK(world_cell(edge_x+k*dx,plan.floor+2,edge_z+k*dz)==AIR);
        }
    }
    CHECK(mouths>0 && world_cell(plan.x,plan.floor+1,plan.z)==AIR);
    memset(inventory,0,sizeof inventory); selected_slot=0;
    inventory[0]=(ItemStack){SURVEY_TAPPER,1,0,0,0};
    for (int i=0;i<2;++i) {
        int x,y,z;
        strata_gallery_sample(plan,i,&x,&y,&z);
        const int sample=i ? STRATA_SAMPLE_LIME : STRATA_SAMPLE_SHALE;
        const Hit hit={1,x,y,z,x,y,z+1};
        CHECK(world_cell(x,y,z)==sample && solid(world_cell(x,y-1,z)));
        if (!i) {
            for (int k=1;k<SLOT_COUNT;++k) inventory[k]=(ItemStack){STONE,STACK_MAX,0,0,0};
            CHECK(story(strata_sample_tap(hit),"背包已满") && world_cell(x,y,z)==sample);
            memset(inventory+1,0,sizeof inventory-sizeof inventory[0]);
        }
        CHECK(story(strata_sample_tap(hit),i ? "石灰岩交界样片" : "页岩交界样片"));
        CHECK(inventory[i+1].type==(i ? LIME_SECTION : SHALE_SECTION) && inventory[i+1].count==1 &&
            world_cell(x,y,z)==strata_sample_host(sample) && !strata_sample_tap(hit));
        new_samples+=strata_sample_host(sample)==world_recorded_cell(OVERWORLD,x,y,z);
    }
    inventory[3]=(ItemStack){STRIATED_GLASS,1,0,0,0}; inventory[4]=(ItemStack){IRON_INGOT,1,0,0,0};
    const int recipe[]={1,2,3,4};
    CHECK(craft_preview(recipe,2).type==STRATA_GAUGE && craft_take(recipe,2) &&
        inventory[1].type==STRATA_GAUGE);
    const int y=200,z=-15;
    for (int i=-1;i<=1;++i) for (int d=0;d<=SURVEY_DEPTH;++d) CHECK(set_block(i,y,z-d,STONE));
    CHECK(set_block(-1,y,z-2,SLATE_ROCK) && set_block(0,y,z,LIMESTONE_ROCK) &&
        set_block(1,y,z-1,AIR) && set_block(1,y,z-2,JOINT_BASALT));
    SurveyReading lanes[3];
    const Hit wall={1,0,y,z,0,y,z+1};
    CHECK(strata_gauge_read(wall,lanes) && lanes[0].state==SURVEY_SEAM && lanes[0].depth==2 &&
        lanes[0].rock==SLATE_ROCK && lanes[1].state==SURVEY_EXPOSED && lanes[1].rock==LIMESTONE_ROCK &&
        lanes[2].state==SURVEY_UNSUITABLE && lanes[2].rock==0);
    CHECK(story(strata_gauge_tap(wall),"右：岩面中断") &&
        !strata_gauge_read((Hit){1,0,y,z,0,y+1,z},lanes));
    CHECK(set_block(63,y,9,STONE) && !world_resident_cell(64,y,9,&raw));
    CHECK(strata_gauge_read((Hit){1,63,y,9,63,y,10},lanes) && lanes[2].state==SURVEY_INCOMPLETE &&
        !world_resident_cell(64,y,9,&raw));
    CHECK(world_begin_options(seed,GENERATOR_STRATA_GALLERIES,WORLD_STANDARD,1) &&
        !strata_gallery_plan_for(plan.x,plan.z,&old));
    CHECK(world_begin_generated(seed,GENERATOR_JOINT_COAST) && dungeon_plan_for(plan.x,plan.z,&old) &&
        world_cell(old.x,old.floor+1,old.z)==old.spawner && !strata_gallery_plan_for(plan.x,plan.z,&old));
    world_reset();
    printf("STRATA TEST cave mouths=%d samples=%d compare/legacy/chunk residency failures=%d\n",
        mouths,new_samples,failures);
    return failures;
}
