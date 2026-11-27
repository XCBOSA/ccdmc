#define _POSIX_C_SOURCE 200809L
#include "../world/internal.h"
#include "../world/terrain_cover.h"
#include "../game/game.h"
#include "../game/item_icons.h"
#include "../game/pistons.h"
#include "../world/rails.h"
#include "../world/villages.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

#define REQUIRE_STREAM(expr) do { if (!(expr)) { puts("STREAM TEST failed: " #expr); ++failures; } } while (0)

static ChunkView wait_for(int cx,int cz) {
    const struct timespec delay={0,2000000};
    for (int i=0;i<2600;++i) {
        world_stream_pump(cx*CHUNK_SIZE,cz*CHUNK_SIZE);
        ChunkView v=chunk_view(cx,cz);
        if (v.cells) return v;
        nanosleep(&delay,NULL);
    }
    return (ChunkView){NULL,NULL,NULL,NULL,0,NULL,NULL};
}
static int paths_valid(const char *p) {
    if (!p || !*p) return 0;
    int length=0;
    for (;*p;++p) {
        if (*p==';') {
            if (length<6 || length%2) return 0;
            length=0; continue;
        }
        if (!(*p>='0' && *p<='9') && !(*p>='A' && *p<='F')) return 0;
        ++length;
    }
    return length>=6 && length%2==0;
}
static int cover_matches(ChunkView view) {
    if (!view.cells || !view.cover) return 0;
    for (int column=0;column<CHUNK_COLUMNS;++column) for (int y=0;y<WY;++y) {
        int raw=chunk_snapshot_cell(view.cells,view.high,column*WY+y);
        int full=(unsigned)raw<256 && terrain_full_cover[raw];
        if (raw==BROWN_MUSHROOM_BLOCK && view.cells[column*WY+y]==BROWN_MUSHROOM_TERRAIN_BYTE) full=1;
        int wet=water_strength(raw)!=0;
        if ((int)((view.cover[column].seal[y>>6]>>(y&63))&1u)!=(full || wet) ||
            (int)((view.cover[column].dry[y>>6]>>(y&63))&1u)!=(raw!=AIR && !wet)) return 0;
    }
    return 1;
}
static int wait_ahead(int cx,int cz,int hx,int hz) {
    const struct timespec delay={0,2000000};
    int x=cx*CHUNK_SIZE,z=cz*CHUNK_SIZE,top;
    for (int step=0;step<1500;++step) {
        world_stream_pump_directional(x,z,hx,hz);
        if (world_resident_top(x+5*CHUNK_SIZE*hx,z+5*CHUNK_SIZE*hz,&top))
            return top==SUPERFLAT_SURFACE && world_resident_top(x,z,&top);
        nanosleep(&delay,NULL);
    }
    return 0;
}
static int terrain_result_matches(const TerrainResult *a,const TerrainResult *b) {
    return a && b && a->x==b->x && a->z==b->z &&
        !memcmp(a->cells,b->cells,sizeof a->cells) &&
        !memcmp(a->tops,b->tops,sizeof a->tops) &&
        !memcmp(a->biomes,b->biomes,sizeof a->biomes) &&
        !memcmp(a->sky_tops,b->sky_tops,sizeof a->sky_tops) &&
        !memcmp(a->cover,b->cover,sizeof a->cover) &&
        !memcmp(a->emitters,b->emitters,sizeof a->emitters) &&
        a->emitter_mask==b->emitter_mask && a->wide.count==b->wide.count &&
        !memcmp(a->wide.cell,b->wide.cell,b->wide.count*sizeof b->wide.cell[0]) &&
        a->devices.count==b->devices.count &&
        !memcmp(a->devices.cell,b->devices.cell,b->devices.count*sizeof b->devices.cell[0]);
}
static const TerrainResult *receive_chunk(int cx,int cz) {
    const struct timespec delay={0,2000000};
    for (int i=0;i<1500;++i) {
        terrain_worker_request(cx,cz,1);
        const TerrainResult *r=terrain_worker_take();
        if (r) return r;
        nanosleep(&delay,NULL);
    }
    return NULL;
}
static int terrain_history_selftest(void) {
    int failures=0;
    static TerrainResult expected,actual;
    const TerrainKey reference=TERRAIN_KEY(UINT64_C(0x123456789ac31),GENERATOR_CURRENT,OVERWORLD,WORLD_AMPLIFIED,0);
    const TerrainKey below=TERRAIN_KEY(29342,GENERATOR_EQUINE,NETHER,WORLD_STANDARD,0);
    static const int places[][2]={{-19,25},{-19,24},{-1,0},{0,-1},{9,-7},{37,41},{-65,-41}};
    terrain_history_clear();
    for (unsigned i=0;i<sizeof places/sizeof places[0];++i) {
        const int x=places[i][0],z=places[i][1];
        terrain_prepare(reference,x,z,&expected);
        terrain_history_prepare(reference,1,x,z,&actual);
        REQUIRE_STREAM(!actual.reused && terrain_result_matches(&expected,&actual));
        memset(&actual,0,sizeof actual);
        terrain_history_prepare(reference,1,x,z,&actual);
        REQUIRE_STREAM(actual.reused && terrain_result_matches(&expected,&actual));
    }
    /* A reset at the same coordinates must not restore the old world's
       output even while the worker's previous request is allowed to finish. */
    terrain_prepare(below,0,-1,&expected);
    terrain_history_prepare(below,2,0,-1,&actual);
    REQUIRE_STREAM(!actual.reused && terrain_result_matches(&expected,&actual));
    terrain_history_prepare(below,2,0,-1,&actual);
    REQUIRE_STREAM(actual.reused && terrain_result_matches(&expected,&actual));
    terrain_history_clear();
    printf("TERRAIN HISTORY TEST cells/height/cover/wide/epoch failures=%d\n",failures);
    return failures;
}
static int terrain_handoff_selftest(void) {
    int failures=0;
    static TerrainResult expected;
    const TerrainKey surface=TERRAIN_KEY(992382978u,GENERATOR_CURRENT,OVERWORLD,WORLD_STANDARD,0);
    const TerrainKey below=TERRAIN_KEY(73292,GENERATOR_EQUINE,NETHER,WORLD_STANDARD,0);
    terrain_worker_reset(surface);
    if (!terrain_worker_start()) return 1;
    /* Wrap the ready queue multiple times while alternating game-thread
       scratch generation with worker results at the same coordinates. */
    for (int i=0;i<18;++i) {
        const int x=-19+i,z=25-i;
        terrain_prepare(below,x,z,&expected);
        const TerrainResult *immediate=terrain_prepare_immediate(below,x,z);
        terrain_worker_request(x,z,1);
        REQUIRE_STREAM(terrain_result_matches(immediate,&expected));
        const TerrainResult *result=receive_chunk(x,z);
        terrain_prepare(surface,x,z,&expected);
        REQUIRE_STREAM(result!=immediate && terrain_result_matches(result,&expected));
    }
    terrain_worker_request(300,300,1);
    terrain_worker_reset(below);
    const TerrainResult *after_reset=receive_chunk(300,300);
    terrain_prepare(below,300,300,&expected);
    REQUIRE_STREAM(terrain_result_matches(after_reset,&expected));
    terrain_worker_stop();
    terrain_prepare(surface,300,300,&expected);
    REQUIRE_STREAM(terrain_result_matches(terrain_prepare_immediate(surface,300,300),&expected));
    printf("TERRAIN HANDOFF TEST queue wrap/immediate/epoch failures=%d\n",failures);
    return failures;
}
static int terrain_history_edits_selftest(void) {
    int failures=0;
    const int cx=-19,cz=25,x=cx*CHUNK_SIZE,z=cz*CHUNK_SIZE,y=248;
    if (!world_begin_generated(29342,GENERATOR_CURRENT) || !world_stream_start()) {
        world_stream_stop();
        return 1;
    }
    world_set_interactive(1);
    REQUIRE_STREAM(wait_for(cx,cz).cells!=NULL);
    REQUIRE_STREAM(world_restore_edit(x,y,z,IRON_BLOCK) &&
        world_restore_edit(x,y+1,z,BUTTON_BASE+STONE));
    /* Evict from the main-thread cache without creating pressure in the
       worker's history; synchronous reads do not populate that history. */
    world_set_interactive(0);
    for (int i=0;i<CHUNK_CACHE;++i) (void)chunk_view(401+i,310);
    REQUIRE_STREAM(!chunk_peek_view(cx,cz).cells);
    world_set_interactive(1);
    world_begin_cache_sample();
    ChunkView view=wait_for(cx,cz);
    ChunkCacheSample sample=world_finish_cache_sample();
    REQUIRE_STREAM(view.cells && view.high && view.tops[0]==y+1 && sample.generated_reused>0 &&
        world_cell(x,y,z)==IRON_BLOCK && world_cell(x,y+1,z)==BUTTON_BASE+STONE &&
        cover_matches(view));
    world_stream_stop();
    printf("TERRAIN HISTORY TEST current player edits after eviction failures=%d\n",failures);
    return failures;
}
int streaming_selftest(void) {
    int failures=terrain_history_selftest()+terrain_handoff_selftest()+terrain_history_edits_selftest();
    REQUIRE_STREAM(world_begin_generated(29342,GENERATOR_BOOKS));
    static TerrainResult expected;
    terrain_generate(TERRAIN_KEY(29342,GENERATOR_BOOKS,OVERWORLD,WORLD_STANDARD,0),-19,25,
        expected.cells,expected.tops,expected.biomes);
    if (!world_stream_start()) {
        puts("STREAM TEST worker creation failed");
        return 1;
    }
    world_set_interactive(1);
    REQUIRE_STREAM(!chunk_view(-19,25).cells && world_cell(-19*CHUNK_SIZE,199,25*CHUNK_SIZE)==BEDROCK);
    REQUIRE_STREAM(!set_block(-19*CHUNK_SIZE,198,25*CHUNK_SIZE,WOOD));
    REQUIRE_STREAM(world_restore_edit(-19*CHUNK_SIZE,199,25*CHUNK_SIZE,IRON_BLOCK));
    ChunkView v=wait_for(-19,25);
    expected.cells[199]=IRON_BLOCK;
    expected.tops[0]=199;
    REQUIRE_STREAM(v.cells && !memcmp(expected.cells,v.cells,CHUNK_CELLS) &&
        !memcmp(expected.tops,v.tops,CHUNK_COLUMNS) && !memcmp(expected.biomes,v.biomes,CHUNK_COLUMNS));
    REQUIRE_STREAM(cover_matches(v));
    const int ax=-19*CHUNK_SIZE,az=25*CHUNK_SIZE;
    REQUIRE_STREAM(set_block(ax,127,az,STONE) && set_block(ax,128,az,STONE) &&
        world_restore_edit(ax,128,az,BUTTON_BASE+STONE) && set_block(ax,255,az,STONE));
    REQUIRE_STREAM(cover_matches(chunk_cached_view(-19,25)));
    REQUIRE_STREAM(set_block(ax,127,az,WATER) && world_restore_edit(ax,128,az,FLOW_BASE+2) &&
        cover_matches(chunk_cached_view(-19,25)));
    REQUIRE_STREAM(set_block(ax,127,az,LAVA) && set_block(ax,255,az,AIR) &&
        cover_matches(chunk_cached_view(-19,25)));
    REQUIRE_STREAM(world_select_dimension(NETHER) && world_select_dimension(OVERWORLD) &&
        cover_matches(wait_for(-19,25)));
    if (v.cells) REQUIRE_STREAM(set_block(-19*CHUNK_SIZE,199,25*CHUNK_SIZE,AIR) &&
        world_cell(-19*CHUNK_SIZE,199,25*CHUNK_SIZE)==AIR);
    REQUIRE_STREAM(!chunk_view(121,-120).cells);
    REQUIRE_STREAM(world_begin_generated(73292,GENERATOR_EQUINE) && world_select_dimension(NETHER));
    terrain_generate(TERRAIN_KEY(73292,GENERATOR_EQUINE,NETHER,WORLD_STANDARD,0),121,-120,
        expected.cells,expected.tops,expected.biomes);
    REQUIRE_STREAM(!chunk_view(121,-120).cells);
    v=wait_for(121,-120);
    REQUIRE_STREAM(v.cells && !memcmp(expected.cells,v.cells,CHUNK_CELLS) &&
        !memcmp(expected.tops,v.tops,CHUNK_COLUMNS) && !memcmp(expected.biomes,v.biomes,CHUNK_COLUMNS));
    REQUIRE_STREAM(cover_matches(v));
    world_stream_stop();
    world_begin(7132);
    enum { TRANSACTION_Y=194 };
    (void)block(3,TRANSACTION_Y,8); /* Load only the root chunk. */
    REQUIRE_STREAM(world_restore_edit(2,TRANSACTION_Y,8,REDSTONE_BLOCK));
    for (int x=4;x<=15;++x) REQUIRE_STREAM(world_restore_edit(x,TRANSACTION_Y,8,x==4 ? GLASS : STONE));
    REQUIRE_STREAM(world_restore_edit(3,TRANSACTION_Y,8,PISTON_BASE+EAST) && redstone_restore(3,TRANSACTION_Y,8,PISTON_BASE+EAST));
    if (world_stream_start()) {
        world_set_interactive(1);
        redstone_step(3,8);
        REQUIRE_STREAM(!chunk_cached_view(1,0).cells &&
            world_cell(3,TRANSACTION_Y,8)==PISTON_BASE+EAST && world_cell(4,TRANSACTION_Y,8)==GLASS);
        REQUIRE_STREAM(wait_for(1,0).cells!=NULL);
        redstone_step(3,8);
        REQUIRE_STREAM(world_cell(3,TRANSACTION_Y,8)==PISTON_BASE+PISTON_EXTENDED+EAST &&
            world_cell(4,TRANSACTION_Y,8)==PISTON_HEAD_BASE+EAST &&
            world_cell(5,TRANSACTION_Y,8)==GLASS && world_cell(16,TRANSACTION_Y,8)==STONE);
    } else REQUIRE_STREAM(0 && "piston test terrain worker");
    world_stream_stop();
    REQUIRE_STREAM(world_begin_generated(992382978u,GENERATOR_DOORS));
    VillagePlan village;
    int dx,dz,facing;
    if (village_plan_for(144,-97,&village) && village_home_door(&village,0,&dx,&dz,&facing) && world_stream_start()) {
        int cx=dx/CHUNK_SIZE-(dx%CHUNK_SIZE<0),cz=dz/CHUNK_SIZE-(dz%CHUNK_SIZE<0),y=village.floor+1;
        world_set_interactive(1);
        REQUIRE_STREAM(!chunk_cached_view(cx,cz).cells);
        v=wait_for(cx,cz);
        REQUIRE_STREAM(v.cells && v.high && world_cell(dx,y,dz)==DOOR_BASE+facing &&
            world_cell(dx,y+1,dz)==door_peer(DOOR_BASE+facing));
        REQUIRE_STREAM(set_block(dx,y+1,dz,AIR));
        REQUIRE_STREAM(world_select_dimension(NETHER) && world_select_dimension(OVERWORLD) && !chunk_cached_view(cx,cz).cells);
        v=wait_for(cx,cz);
        REQUIRE_STREAM(v.cells && world_cell(dx,y,dz)==AIR && world_cell(dx,y+1,dz)==AIR);
    } else REQUIRE_STREAM(0 && "natural door test terrain worker");
    world_stream_stop();
    REQUIRE_STREAM(world_begin_options(4321,GENERATOR_CURRENT,WORLD_SUPERFLAT,1));
    if (world_stream_start()) {
        world_set_interactive(1);
        int top;
        REQUIRE_STREAM(!world_resident_top(-53*CHUNK_SIZE,(-37-5)*CHUNK_SIZE,&top));
        /* No renderer requests the destination: the pump prefetches it.
           Turning must redirect subsequent prefetches without a world reset. */
        REQUIRE_STREAM(wait_ahead(-53,-37,0,-1));
        REQUIRE_STREAM(wait_ahead(-53,-37,1,0));
    } else REQUIRE_STREAM(0 && "directional prefetch terrain worker");
    world_stream_stop();
    REQUIRE_STREAM(world_begin_options(4321,GENERATOR_CURRENT,WORLD_SUPERFLAT,1));
    const int tx=-53,tz=-37;
    /* Make one forward chunk the oldest, then saturate the resident cache
       with chunks at unrelated coordinates; there is no renderer to touch it. */
    REQUIRE_STREAM(chunk_view(tx,tz-5).cells!=NULL);
    for (int i=0;i<CHUNK_CACHE-1;++i) (void)chunk_view(200+i,310);
    if (world_stream_start()) {
        int top;
        world_set_interactive(1);
        world_begin_cache_sample();
        REQUIRE_STREAM(wait_ahead(tx,tz-1,0,-1));
        REQUIRE_STREAM(world_resident_top(tx*CHUNK_SIZE,(tz-5)*CHUNK_SIZE,&top));
        ChunkCacheSample same_heading=world_finish_cache_sample();
        REQUIRE_STREAM(same_heading.evictions>0 && !same_heading.near_evictions && !same_heading.ahead_evictions);
        REQUIRE_STREAM(wait_ahead(tx,tz-1,1,0));
        REQUIRE_STREAM(!world_resident_top(tx*CHUNK_SIZE,(tz-5)*CHUNK_SIZE,&top));
    } else REQUIRE_STREAM(0 && "cache retention worker");
    world_stream_stop();
    world_reset();
    int count=0;
    for (int i=1;i<ITEM_LIMIT;++i) if (item_def(i)) {
        ItemIcon icon;
        ++count;
        if (!item_icon(i,&icon)) { printf("Missing item icon: %d %s\n",i,item_name(i)); ++failures; continue; }
        for (int n=0;n<ITEM_ICON_LAYERS;++n) if (!paths_valid(icon.path[n])) {
            printf("Invalid icon polygon: %d %s layer %d\n",i,item_name(i),n); ++failures;
        }
    }
    const int cube_items[]={GRASS,DIRT,STONE,WOOD,COAL_ORE,GLASS,CRAFTING_TABLE,FURNACE,REDSTONE_LAMP_ITEM};
    for (unsigned i=0;i<sizeof cube_items/sizeof *cube_items;++i) {
        ItemIcon icon;
        REQUIRE_STREAM(item_icon(cube_items[i],&icon) && !strcmp(icon.path[0],"2481E4EC8F2C"));
    }
    printf("STREAM TEST worker/closed boundary/replayed edits/epoch/dimension/icons=%d failures=%d\n",count,failures);
    return failures;
}
