#define _POSIX_C_SOURCE 200809L
#include "../game/game.h"
#include "../game/portals.h"
#include "../world/internal.h"
#include "../world/aquifer_layout.h"
#include "../world/temples.h"
#include "../world/villages.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum { ROWS=17,COLS=257 };
static int heights[ROWS][COLS];
static Biome biomes[ROWS][COLS];
static TerrainResult raw_default,raw_variant;

static int stream_column(int x,int z) {
    struct timespec pause={0,1000000};
    for (int i=0;i<3000;++i) {
        world_stream_pump(x,z);
        if (world_chunk_ready(x,z)) return 1;
        nanosleep(&pause,NULL);
    }
    return 0;
}

int world_profiles_selftest(void) {
    const uint64_t seed=98213;
    int failures=0,default_changes=0,large_changes=0,default_high=0,amplified_high=0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"world profiles:%d: %s\n",__LINE__,#c); ++failures; } } while (0)
    CHECK(world_begin_generated(seed,GENERATOR_CURRENT) && world_profile()==WORLD_STANDARD);
    for (int i=0;i<ROWS;++i) for (int j=0;j<COLS;++j) {
        int x=(j-128)*16,z=(i-8)*256;
        int height=terrain_height(x,z);
        heights[i][j]=height;
        if (height>default_high) default_high=height;
        biomes[i][j]=terrain_biome(x,z);
        default_changes+=j>0 && biomes[i][j]!=biomes[i][j-1];
    }
    CHECK(world_begin_profile(seed,GENERATOR_CURRENT,WORLD_LARGE_BIOMES));
    for (int i=0;i<ROWS;++i) {
        Biome previous=BIOME_LEGACY;
        for (int j=0;j<COLS;++j) {
            Biome b=terrain_biome((j-128)*16,(i-8)*256);
            large_changes+=j>0 && b!=previous;
            previous=b;
        }
    }
    CHECK(default_changes>50 && large_changes*4<default_changes*3);
    CHECK(world_begin_profile(seed,GENERATOR_CURRENT,WORLD_AMPLIFIED));
    for (int i=0;i<ROWS;++i) for (int j=0;j<COLS;++j) {
        int x=(j-128)*16,z=(i-8)*256,h=terrain_height(x,z);
        CHECK(h>=heights[i][j] && h<=WY-26 && terrain_biome(x,z)==biomes[i][j]);
        if (h>amplified_high) amplified_high=h;
    }
    CHECK(amplified_high>=default_high+45);
    int slate=0,limestone=0;
    for (int x=-2;x<=2;++x) for (int z=-2;z<=2;++z) {
        terrain_prepare(TERRAIN_KEY(seed,GENERATOR_PRE_GEOLOGY,OVERWORLD,WORLD_STANDARD,0),x,z,&raw_default);
        terrain_prepare(TERRAIN_KEY(seed,GENERATOR_GEOLOGY,OVERWORLD,WORLD_STANDARD,0),x,z,&raw_variant);
        CHECK(!memcmp(raw_default.cells,raw_variant.cells,sizeof raw_default.cells));
        for (unsigned i=0;i<raw_default.wide.count;++i) CHECK(!geology_block(raw_default.wide.cell[i].cell));
        for (unsigned i=0;i<raw_variant.wide.count;++i) {
            TerrainWideCell entry=raw_variant.wide.cell[i];
            if (!geology_block(entry.cell)) continue;
            CHECK(raw_variant.cells[entry.index]==STONE);
            if (entry.cell==SLATE_ROCK) { ++slate; CHECK(entry.index%WY>=13 && entry.index%WY<=32); }
            else if (entry.cell==LIMESTONE_ROCK) { ++limestone; CHECK(entry.index%WY>=6); }
            else CHECK(0);
        }
    }
    CHECK(slate>0 && limestone>0);
    for (int d=NETHER;d<=END;++d) {
        terrain_prepare(TERRAIN_KEY(seed,GENERATOR_CURRENT,(Dimension)d,WORLD_STANDARD,0),1,-1,&raw_default);
        for (int p=WORLD_SUPERFLAT;p<WORLD_PROFILE_COUNT;++p) {
            terrain_prepare(TERRAIN_KEY(seed,GENERATOR_CURRENT,(Dimension)d,(WorldProfile)p,0),1,-1,&raw_variant);
            CHECK(!memcmp(raw_default.cells,raw_variant.cells,sizeof raw_default.cells));
        }
    }

    CHECK(!world_begin_profile(seed,GENERATOR_LEGACY,WORLD_SUPERFLAT));
    CHECK(world_begin_profile(seed,GENERATOR_CURRENT,WORLD_SUPERFLAT));
    generate();
    CHECK(world_profile()==WORLD_SUPERFLAT && world_sea_level()==SUPERFLAT_SURFACE &&
        px==.5f && pz==.5f && py>4.f && py<4.1f);
    for (int x=-18;x<=18;++x) for (int z=-18;z<=18;++z) {
        CHECK(terrain_biome(x,z)==BIOME_PLAINS && terrain_height(x,z)==3 &&
            block(x,0,z)==BEDROCK && block(x,1,z)==DIRT && block(x,2,z)==DIRT &&
            block(x,3,z)==GRASS && block(x,4,z)==AIR);
    }
    VillagePlan village={0};
    CHECK(village_plan_for(0,0,&village) && village.floor==3);
    int wx,wz;
    village_global(&village,5,0,&wx,&wz);
    CHECK(block(wx,0,wz)==BEDROCK && block(wx,1,wz)==WATER);
    TemplePlan temple={0};
    CHECK(!temple_plan_for(village.x,village.z,&temple));
    CHECK(!item_def(ARCHIVE_BEARING_ITEM) && !item_def(ARCHIVE_FRAME));

    int stream=world_stream_start();
    CHECK(stream);
    if (stream) {
        world_set_interactive(1);
        const int far_x=VILLAGE_REGION_EDGE*8;
        CHECK(!chunk_cached_view(far_x/CHUNK_SIZE,0).cells);
        CHECK(stream_column(far_x,0) && block(far_x,3,0)==GRASS && block(far_x,4,0)==AIR);
        CHECK(world_begin_options(seed,GENERATOR_CURRENT,WORLD_SUPERFLAT,1));
        world_set_interactive(1);
        CHECK(!village_plan_for(wx,wz,&village) && stream_column(wx,wz) &&
            block(wx,0,wz)==BEDROCK && block(wx,1,wz)==DIRT && block(wx,3,wz)==GRASS);
    }
    world_stream_stop();
    static const int flat_heights[]={9,136,52,51,48,14,49,32};
    static const int flat_tops[]={GRASS,GRASS,WATER,GRASS,SNOW,COBBLESTONE,SAND,TERRACOTTA};
    static const Biome flat_biomes[]={BIOME_PLAINS,BIOME_MOUNTAINS,BIOME_BEACH,BIOME_PLAINS,
        BIOME_ICE_PLAINS,BIOME_PLAINS,BIOME_DESERT,BIOME_DESERT};
    FlatLayout layout={0};
    for (int i=0;i<FLAT_PRESETS;++i) {
        CHECK(!flat_parse(flat_presets[i].code,&layout) && flat_valid(&layout) &&
            layout.height==flat_heights[i] && layout.blocks[0]==BEDROCK &&
            layout.blocks[layout.height-1]==flat_tops[i] && layout.biome==flat_biomes[i]);
        CHECK(!strcmp(flat_name(&layout),flat_presets[i].name));
    }
    CHECK(!flat_parse(" bedrock, 4 * sandstone, oxide-sand | dryland | settlements ",&layout) && layout.height==6 &&
        layout.blocks[5]==RED_SAND && layout.features==FLAT_VILLAGE);
    FlatLayout damaged=layout;
    damaged.blocks[1]=255;
    CHECK(!flat_valid(&damaged) && !world_begin_layout(seed,GENERATOR_CURRENT,WORLD_SUPERFLAT,0,&damaged));
    CHECK(flat_parse("bedrock,0*turf",&layout) && flat_parse("bedrock,253*stone",&layout) &&
        flat_parse("bedrock,marbleunknown",&layout) && flat_parse("bedrock | nowhere",&layout) &&
        flat_parse("bedrock | meadow | settlements,",&layout) &&
        flat_parse("bedrock | meadow | settlements,settlements",&layout) && flat_parse("2*air",&layout) &&
        flat_parse("2;7",&layout) && flat_parse("bedrock | meadow |",&layout));

    CHECK(!flat_parse(flat_presets[2].code,&layout));
    CHECK(world_begin_layout(seed,GENERATOR_CURRENT,WORLD_SUPERFLAT,0,&layout));
    generate();
    CHECK(py>52.f && py<53.1f && block(0,52,0)==COBBLESTONE && block(5,52,0)==AIR &&
        block(5,51,0)==WATER && block(5,20,0)==WATER && block(5,19,0)==SAND);
    CHECK(!village_plan_for(0,0,&village));
    stream=world_stream_start();
    CHECK(stream);
    if (stream) {
        world_set_interactive(1);
        CHECK(stream_column(-172,185) && block(-172,51,185)==WATER && block(-172,19,185)==SAND);
        CHECK(!flat_parse(flat_presets[6].code,&layout));
        CHECK(world_begin_layout(seed,GENERATOR_CURRENT,WORLD_SUPERFLAT,0,&layout));
        world_set_interactive(1);
        CHECK(stream_column(-172,185) && block(-172,51,185)==AIR && block(-172,15,185)==SANDSTONE &&
            terrain_biome(-172,185)==BIOME_DESERT && village_plan_for(0,0,&village) && village.floor==48);
    }
    world_stream_stop();
    CHECK(!flat_parse(flat_presets[1].code,&layout));
    CHECK(world_begin_layout(seed,GENERATOR_GEOLOGY,WORLD_SUPERFLAT,0,&layout));
    int strong_x=0,strong_z=0;
    CHECK(nearest_stronghold(0,0,&strong_x,&strong_z) &&
        block(strong_x+5,STRONGHOLD_FLOOR+1,strong_z-4)==SADDLE_CHEST);
    CHECK(world_begin_layout(seed,GENERATOR_GEOLOGY,WORLD_SUPERFLAT,1,&layout) &&
        !nearest_stronghold(0,0,&strong_x,&strong_z));
    CHECK(world_begin_profile(seed,GENERATOR_GEOLOGY,WORLD_STANDARD));
    int sx=0,sz=0;
    CHECK(nearest_stronghold(0,0,&sx,&sz));
    CHECK(block(sx+5,STRONGHOLD_FLOOR+1,sz-4)==SADDLE_CHEST);
    CHECK(world_select_dimension(NETHER));
    int fx=0,fy=-1,fz=0;
    for (int x=-96;x<96 && fy<0;++x) for (int z=-96;z<96;++z) {
        int h=nether_fortress_floor(x,z);
        if (h>=0 && block(x,h,z)==NETHER_BRICKS) { fx=x; fy=h; fz=z; break; }
    }
    CHECK(fy>31);
    CHECK(world_begin_options(seed,GENERATOR_GEOLOGY,WORLD_STANDARD,1));
    int rx=0,rz=0;
    CHECK(!nearest_stronghold(0,0,&rx,&rz) && block(sx+5,STRONGHOLD_FLOOR+1,sz-4)!=SADDLE_CHEST &&
        world_structures_disabled());
    CHECK(!valid_cell(ARCHIVE_FRAME) && !valid_cell(ARCHIVE_GATE));
    CHECK(world_select_dimension(NETHER) && nether_fortress_floor(fx,fz)==-1 &&
        block(fx,fy,fz)!=NETHER_BRICKS && block(fx,0,fz)==BEDROCK);
    terrain_prepare(TERRAIN_KEY(seed,GENERATOR_CURRENT,END,WORLD_STANDARD,0),1,-1,&raw_default);
    terrain_prepare(TERRAIN_KEY(seed,GENERATOR_CURRENT,END,WORLD_STANDARD,1),1,-1,&raw_variant);
    CHECK(!memcmp(raw_default.cells,raw_variant.cells,sizeof raw_default.cells));
    /* The live world here is an older generator: the captured request still
       creates the new chamber. Reverse the live world and replay old keys too. */
    terrain_prepare(TERRAIN_KEY(seed,GENERATOR_AQUIFER,END,WORLD_STANDARD,0),0,0,&raw_default);
    const int dock=AQUIFER_DOCK_Z*WY+AQUIFER_FLOOR+1;
    int aquifer_dock=0;
    for (unsigned i=0;i<raw_default.wide.count;++i)
        aquifer_dock|=raw_default.wide.cell[i].index==dock && raw_default.wide.cell[i].cell==AQUIFER_DOCK;
    CHECK(aquifer_dock && world_begin_generated(seed,GENERATOR_AQUIFER));
    terrain_prepare(TERRAIN_KEY(seed,GENERATOR_GEOLOGY,END,WORLD_STANDARD,0),0,0,&raw_variant);
    CHECK(!raw_variant.wide.count && memcmp(raw_default.cells,raw_variant.cells,sizeof raw_default.cells));
    world_begin(0);
    printf("WORLD PROFILES biome transitions %d/%d heights %d/%d failures=%d\n",
        default_changes,large_changes,default_high,amplified_high,failures);
#undef CHECK
    return failures!=0;
}
