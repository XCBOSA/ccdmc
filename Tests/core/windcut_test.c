#include "world_test.h"
#include "../world/internal.h"
#include "../game/survey.h"
#include <stdio.h>
#include <string.h>

static int part(int p) { return p/CHUNK_SIZE-(p%CHUNK_SIZE<0); }
static BlockEdit site_cell(WindcutPlan p,int x,int y,int z) {
    for (unsigned i=0;i<p.rotation;++i) { const int old=x; x=-z; z=old; }
    return (BlockEdit){p.x+x,p.y+y,p.z+z,0};
}

int windcut_selftest(void) {
    int failures=0,slate=0,limestone=0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"WINDCUT TEST line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
    const uint64_t seed=1712;
    CHECK(world_begin_generated(seed,GENERATOR_WINDCUT));
    WindcutPlan p={0};
    DesertPyramidPlan old={0};
    CHECK(windcut_plan_for(-4495,3775,&p) && p.x==-4495 && p.z==3775);
    CHECK(!desert_pyramid_plan_for(p.x,p.z,&old));
    if (!p.y) return 1;
    const BlockEdit s=site_cell(p,-1,-7,0),l=site_cell(p,1,-7,0);
    CHECK(windcut_contains(p,s.x,s.z) && windcut_contains(p,l.x,l.z) &&
        !windcut_contains(p,p.x+WINDCUT_RADIUS+1,p.z));
    const Hit slate_face={.hit=1,.x=s.x,.y=s.y,.z=s.z,.ax=s.x,.ay=s.y+1,.az=s.z};
    const Hit lime_face={.hit=1,.x=l.x,.y=l.y,.z=l.z,.ax=l.x,.ay=l.y+1,.az=l.z};
    CHECK(world_cell(s.x,s.y,s.z)==SLATE_ROCK && world_cell(s.x,s.y+1,s.z)==AIR);
    CHECK(world_cell(l.x,l.y,l.z)==LIMESTONE_ROCK && world_cell(l.x,l.y+1,l.z)==AIR);
    CHECK(survey_read(slate_face).rock==SLATE_ROCK && survey_read(lime_face).rock==LIMESTONE_ROCK);

    const TerrainKey key=TERRAIN_KEY(seed,GENERATOR_WINDCUT,OVERWORLD,WORLD_STANDARD,0);
    static TerrainResult a,b;
    for (int x=part(p.x-WINDCUT_RADIUS);x<=part(p.x+WINDCUT_RADIUS);++x)
        for (int z=part(p.z-WINDCUT_RADIUS);z<=part(p.z+WINDCUT_RADIUS);++z) {
            terrain_prepare(key,x,z,&a);
            terrain_prepare(key,x+4,z-3,&b);
            terrain_prepare(key,x,z,&b);
            CHECK(memcmp(a.cells,b.cells,sizeof a.cells)==0 &&
                memcmp(a.tops,b.tops,sizeof a.tops)==0 && a.wide.count==b.wide.count &&
                memcmp(a.wide.cell,b.wide.cell,a.wide.count*sizeof a.wide.cell[0])==0);
            CHECK(a.devices.count==0);
            for (unsigned i=0;i<a.wide.count;++i) {
                const TerrainWideCell entry=a.wide.cell[i];
                CHECK(entry.cell!=DESERT_PYRAMID_CHEST_CELL);
                slate+=entry.cell==SLATE_ROCK;
                limestone+=entry.cell==LIMESTONE_ROCK;
            }
        }
    CHECK(slate>12 && limestone>12);

    CHECK(world_begin_options(seed,GENERATOR_WINDCUT,WORLD_STANDARD,1) && !windcut_plan_for(p.x,p.z,&p));
    CHECK(world_begin_generated(seed,GENERATOR_SEEP_GARDENS) && !windcut_plan_for(p.x,p.z,&p) &&
        desert_pyramid_plan_for(p.x,p.z,&old));
    printf("WINDCUT TEST open exposures and retired traps, shale=%d limestone=%d, failures=%d\n",
        slate,limestone,failures);
#undef CHECK
    return failures;
}
