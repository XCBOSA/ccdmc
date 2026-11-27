#include "../world/barriers.h"
#include "../world/build_blocks.h"
#include "../world/internal.h"
#include "../world/rails.h"
#include "../world/villages.h"
#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { printf("ARCHITECTURE TEST failed line %d: %s\n",__LINE__,#c); ++failures; } } while (0)

static int chunk(int a) { return a/CHUNK_SIZE-(a%CHUNK_SIZE<0); }
static void village(int gen) {
    const int shaped=gen>=GENERATOR_ARCHITECTURE;
    VillagePlan p;
    if (!world_begin_generated(132974u,gen) || !village_plan_for(-2756,-2136,&p)) { CHECK(0); return; }
    int x,z;
    village_global(&p,-11,8,&x,&z);
    CHECK(world_cell(x,p.floor+7,z)==(shaped ? build_slab_raw(BUILD_OAK,BUILD_SLAB_BOTTOM) : AIR));
    village_global(&p,-11,7,&x,&z);
    int roof=world_cell(x,p.floor+6,z);
    CHECK(roof==(shaped ? build_stair_raw(BUILD_OAK,(SOUTH+p.turn)&3,0) : AIR));
    village_global(&p,1,-18,&x,&z);
    int back=(NORTH+p.turn)&3,y=p.floor+6;
    CHECK(world_cell(x,y,z)==(shaped ? LADDER+back : COBBLESTONE));
    CHECK(village_natural_ladder(x,y,z)==(shaped ? LADDER+back : -1));
    if (shaped) {
        int wx=x+rail_dx(back),wz=z+rail_dz(back);
        CHECK(wall_attachment_support(world_cell(wx,y,wz)));
        CHECK(set_block(wx,y,wz,AIR) && world_cell(x,y,z)==AIR &&
              world_cell(x,y-1,z)==LADDER+back);
    }
}
static void tower_seam(void) {
    if (!world_begin_generated(132974u,GENERATOR_ARCHITECTURE)) { CHECK(0); return; }
    for (int rx=-16;rx<=16;++rx) for (int rz=-16;rz<=16;++rz) {
        VillagePlan p;
        if (!village_plan_for(rx*VILLAGE_REGION_EDGE,rz*VILLAGE_REGION_EDGE,&p)) continue;
        int x,z,y=p.floor+2,raw;
        village_global(&p,1,-18,&x,&z);
        int d=(NORTH+p.turn)&3,wx=x+rail_dx(d),wz=z+rail_dz(d);
        if (chunk(x)==chunk(wx) && chunk(z)==chunk(wz)) continue;
        CHECK(wall_attachment_support(world_cell(wx,y,wz)));
        CHECK(!world_resident_cell(x,y,z,&raw) && village_natural_ladder(x,y,z)==LADDER+d);
        CHECK(set_block(wx,y,wz,AIR) && world_recorded_cell(OVERWORLD,x,y,z)==AIR &&
              world_cell(x,y,z)==AIR && world_cell(x,y-1,z)==LADDER+d);
        return;
    }
    CHECK(0 && "expected a ladder and support in separate chunks");
}
static void temple(int gen) {
    const int shaped=gen>=GENERATOR_ARCHITECTURE;
    TemplePlan p;
    if (!world_begin_generated(295588u,gen) || !temple_plan_for(-4572,-4127,&p)) { CHECK(0); return; }
    for (int a=6;a<=7;++a) for (int b=9;b<=13;++b) {
        BlockEdit point=temple_position(p,a,b-13,b,AIR);
        int val=world_cell(point.x,point.y,point.z);
        if (shaped) CHECK(val==build_stair_raw(BUILD_COBBLE,(SOUTH+p.rotation)&3,0));
        else CHECK(val==COBBLESTONE || val==MOSS_STONE);
    }
}
static void stronghold(int gen) {
    const int shaped=gen>=GENERATOR_ARCHITECTURE;
    int x,z;
    if (!world_begin_generated(1712u,gen) || !nearest_stronghold(0,0,&x,&z)) { CHECK(0); return; }
    for (int a=-16;a<=-8;++a) for (int b=-1;b<=1;++b) {
        int y=STRONGHOLD_FLOOR-a-7;
        CHECK(world_cell(x+a,y,z+b)==(shaped ? build_stair_raw(BUILD_STONE_BRICK,WEST,0) : STONE_BRICKS));
    }
    CHECK(world_cell(x-3,STRONGHOLD_FLOOR+1,z)==(shaped ? build_stair_raw(BUILD_STONE_BRICK,EAST,0) : AIR));
    for (int dx=-2;dx<=2;++dx) for (int dz=-2;dz<=2;++dz)
        CHECK(world_cell(x+dx,STRONGHOLD_FLOOR+1,z+dz)==AIR);
    CHECK(world_cell(x+4,STRONGHOLD_FLOOR+1,z-3)==SLATE_ROCK);
    CHECK(world_cell(x+6,STRONGHOLD_FLOOR+2,z+3)==LIMESTONE_ROCK);
    CHECK(world_cell(x+3,STRONGHOLD_FLOOR+1,z+4)==TORCH);
}

static void fortress(void) {
    enum { SEED=58100 };
    static TerrainResult before,after;
    int risers=0,seams=0;
    for (int cx=0;cx<8;++cx) for (int cz=0;cz<8;++cz) {
        terrain_prepare(TERRAIN_KEY(SEED,GENERATOR_ARCHITECTURE,NETHER,WORLD_STANDARD,0),cx,cz,&after);
        if (!after.wide.count) continue;
        terrain_prepare(TERRAIN_KEY(SEED,GENERATOR_WETLANDS,NETHER,WORLD_STANDARD,0),cx,cz,&before);
        CHECK(!before.wide.count);
        for (unsigned i=0;i<after.wide.count;++i) {
            TerrainWideCell w=after.wide.cell[i];
            int lx=w.index/WY/CHUNK_SIZE,lz=w.index/WY%CHUNK_SIZE;
            CHECK(build_stair(w.cell) && build_material(w.cell)==BUILD_NETHER_BRICK &&
                before.cells[w.index]==NETHER_BRICKS && after.cells[w.index]==NETHER_BRICKS);
            ++risers;
            seams+=lx==0 || lx==CHUNK_SIZE-1 || lz==0 || lz==CHUNK_SIZE-1;
        }
    }
    CHECK(risers>12 && seams);
    printf("ARCHITECTURE TEST fortress risers=%d on chunk seams=%d\n",risers,seams);
}
int architecture_selftest(void) {
    failures=0;
    /* Mix generator versions in one process, including worker snapshots
       while the live world uses a different generator. */
    village(GENERATOR_WETLANDS); village(GENERATOR_ARCHITECTURE); tower_seam();
    temple(GENERATOR_WETLANDS); temple(GENERATOR_ARCHITECTURE);
    stronghold(GENERATOR_WETLANDS); stronghold(GENERATOR_ARCHITECTURE);
    fortress();
    world_reset();
    printf("ARCHITECTURE TEST village stair roofs, ladder support, temple and stronghold stairs failures=%d\n",failures);
    return failures;
}
