#include "../game/drops.h"
#include "../game/farming.h"
#include "../game/game.h"
#include "../world/internal.h"
#include "../world/space.h"
#include "../world/villages.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("VILLAGE TEST failed line %d: %s\n",__LINE__,#c); ++failures; } } while (0)

static int style_slot(Biome biome) { return biome==BIOME_PLAINS ? 0 : biome==BIOME_DESERT ? 1 : biome==BIOME_SAVANNA ? 2 : -1; }
static int test_chunk(int p) { return p<0 ? (p+1)/CHUNK_SIZE-1 : p/CHUNK_SIZE; }
static int center_cell(VillagePlan p,int a,int b,int dy) {
    int x,z; village_global(&p,a,b,&x,&z);
    return world_cell(x,p.floor+dy,z);
}
static int compare_snapshot(TerrainKey first,TerrainKey second,int cx,int cz,int expected_equal) {
    TerrainResult *before=(TerrainResult *)malloc(sizeof *before),*after=(TerrainResult *)malloc(sizeof *after);
    if (!before || !after) { free(before);free(after);return 0; }
    terrain_generate(first,cx,cz,before->cells,before->tops,before->biomes);
    terrain_generate(second,cx,cz,after->cells,after->tops,after->biomes);
    int equal=memcmp(before->cells,after->cells,sizeof before->cells)==0 &&
        memcmp(before->tops,after->tops,sizeof before->tops)==0 &&
        memcmp(before->biomes,after->biomes,sizeof before->biomes)==0;
    free(before);free(after);
    return equal==expected_equal;
}

int village_selftest(void) {
    const uint32_t seed=132974u;
    int failures=0,found=0,orientations=0;
    VillagePlan samples[3]={{0}};
    CHECK(world_begin_generated(seed,GENERATOR_VILLAGES));
    for (int rx=-11;rx<=11 && found!=7;++rx) for (int rz=-11;rz<=11 && found!=7;++rz) {
        VillagePlan plan;
        if (!village_plan_for(rx*VILLAGE_REGION_EDGE,rz*VILLAGE_REGION_EDGE,&plan)) continue;
        int style=style_slot(plan.style);
        if (style<0) { CHECK(style>=0); continue; }
        if (!(found&(1<<style))) { samples[style]=plan; found|=1<<style; }
    }
    CHECK(found==7);
    for (int i=0;i<3;++i) if (found&(1<<i)) {
        VillagePlan p=samples[i],again;
        CHECK(village_plan_for(p.x,p.z,&again) && again.x==p.x && again.z==p.z &&
            again.floor==p.floor && again.style==p.style && again.turn==p.turn);
        orientations|=1<<p.turn;
        int road=i==1 ? SANDSTONE : GRAVEL;
        int x,z,first_cx=0,first_cz=0,crosses=0;
        /* Walk the outside edge of the transverse street, passing the well.
           It must cross a real chunk boundary without a hole or a cliff. */
        for (int a=-16;a<=16;++a) {
            village_global(&p,a,2,&x,&z);
            int cx=test_chunk(x),cz=test_chunk(z);
            if (a==-16) { first_cx=cx;first_cz=cz; }
            crosses|=cx!=first_cx || cz!=first_cz;
            CHECK(world_cell(x,p.floor,z)==road &&
                !world_box_blocked(body_box(x+.5f,p.floor+1.01f,z+.5f,.28f,1.72f),0));
        }
        CHECK(crosses);
        CHECK(center_cell(p,5,0,0)==WATER && center_cell(p,5,0,-3)==(i==1 ? SANDSTONE : COBBLESTONE));
        CHECK(center_cell(p,0,12,0)==WATER && center_cell(p,1,12,0)==FARM_BASE+7 &&
            center_cell(p,1,12,1)==WHEAT_BASE+7 && center_cell(p,-3,12,1)==POTATO_BASE+7);
        for (int home=0;home<VILLAGE_HOMES;++home) {
            static const int a[]={-11,10,0,-11,11},b[]={6,-5,-13,-5,6}, inward[]={1,-1,-1,-1,1};
            CHECK(village_home_entry(&p,home,&x,&z) && world_cell(x,p.floor,z)==road);
            for (int march=0;march<=2;++march) {
                village_global(&p,a[home],b[home]+(march-1)*inward[home],&x,&z);
                CHECK(solid(block(x,p.floor,z)) &&
                    !world_box_blocked(body_box(x+.5f,p.floor+1.01f,z+.5f,.28f,1.72f),0));
            }
        }
        CHECK(center_cell(p,-11,-10,1)==FURNACE && center_cell(p,-10,-10,1)==CHEST);
        printf("VILLAGE TEST generated style=%d center=(%d,%d,%d) turn=%u\n",p.style,p.x,p.floor,p.z,p.turn);
    }

    if (found&1) {
        VillagePlan p=samples[0];
        int x,z; village_global(&p,1,12,&x,&z);
        player_reset_rules(SURVIVAL);
        memset(inventory,0,sizeof inventory); selected_slot=0; drops_reset();
        Hit crop={1,x,p.floor+1,z,x,p.floor+2,z};
        CHECK(farm_use(crop) && world_cell(x,p.floor+1,z)==AIR);
        int wheat=0;
        for (int i=0;i<MAXDROP;++i) wheat+=drops[i].item.type==WHEAT ? drops[i].item.count : 0;
        CHECK(wheat>0);
        inventory[0]=(ItemStack){WHEAT_SEEDS,2,0,0,0};
        CHECK(farm_use((Hit){1,x,p.floor,z,x,p.floor+1,z}) && inventory[0].count==1 &&
            world_cell(x,p.floor+1,z)==WHEAT_BASE);

        int cx=test_chunk(p.x),cz=test_chunk(p.z);
        TerrainKey old=TERRAIN_KEY(seed,GENERATOR_BOOKS,OVERWORLD,WORLD_STANDARD,0);
        TerrainKey now=TERRAIN_KEY(seed,GENERATOR_VILLAGES,OVERWORLD,WORLD_STANDARD,0);
        CHECK(compare_snapshot(old,now,cx,cz,0));
        CHECK(compare_snapshot(old,now,cx+3,cz,1));
        old.dimension=now.dimension=NETHER;
        CHECK(compare_snapshot(old,now,cx,cz,1));
        old.dimension=now.dimension=END;
        CHECK(compare_snapshot(old,now,0,0,1));

        /* A dimension round trip retires chunks; the edit still wins on reload. */
        CHECK(world_select_dimension(NETHER) && world_select_dimension(OVERWORLD));
        CHECK(world_cell(x,p.floor+1,z)==WHEAT_BASE);
    }
    VillagePlan ignored;
    CHECK(world_select_dimension(NETHER) && !village_plan_for(0,0,&ignored));
    CHECK(world_begin_generated(seed,GENERATOR_BOOKS) && !village_plan_for(0,0,&ignored));
    printf("VILLAGE TEST terrain/materials/doors/walking/farm/edits/old-generator masks=%d,%d failures=%d\n",
        found,orientations,failures);
    return failures;
}
