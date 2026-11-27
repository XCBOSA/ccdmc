#include "../world/internal.h"
#include <stdio.h>

enum { SURFACE=190, FAR_COUNT=1200 };
typedef struct { int x,y,z,count; } PendingAt;
static int count_matching(void *ctx,int x,int y,int z,int raw) {
    (void)raw;
    PendingAt *target=(PendingAt *)ctx;
    target->count+=target->x==x && target->y==y && target->z==z;
    return 1;
}
static int pending(Dimension d,int x,int y,int z) {
    PendingAt query={x,y,z,0};
    return water_visit_dimension_pending(d,count_matching,&query) ? query.count : -1;
}

#define CHECK(p) do { if (!(p)) { printf("WATER WAIT TEST line %d: %s\n",__LINE__,#p); ++failures; } } while (0)
static int resident_parity(void) {
    enum { TOP=54, TICKS=32 };
    uint64_t reference[TICKS]={0};
    int failures=0;
    for (int stream=0;stream<=1;++stream) {
        CHECK(world_begin_profile(94329,GENERATOR_CURRENT,WORLD_SUPERFLAT));
        for (int cx=-1;cx<=0;++cx) for (int cz=-1;cz<=0;++cz)
            (void)world_cell(cx*CHUNK_SIZE,TOP,cz*CHUNK_SIZE);
        for (int x=-8;x<=7;++x) for (int z=-8;z<=7;++z)
            CHECK(world_restore_edit(x,TOP-5,z,STONE));
        CHECK(world_restore_edit(-1,TOP-1,-1,STONE) && world_restore_edit(1,TOP-4,-1,FIRE_CELL));
        if (stream) {
            if (!world_stream_start()) { world_stream_stop(); return failures+1; }
            world_set_interactive(1);
        }
        CHECK(set_block(-1,TOP,-1,WATER));
        for (int tick=0;tick<TICKS;++tick) {
            if (tick==11) CHECK(set_block(-1,TOP-1,-1,AIR));
            if (tick==21) CHECK(set_block(-1,TOP,-1,AIR));
            water_step();
            uint64_t shape=UINT64_C(14695981039346656037);
            for (int x=-8;x<=7;++x) for (int z=-8;z<=7;++z) for (int y=TOP-5;y<=TOP;++y) {
                int cell=0;
                CHECK(world_resident_cell(x,y,z,&cell));
                shape=(shape^(unsigned)cell)*UINT64_C(1099511628211);
            }
            if (stream) CHECK(reference[tick]==shape);
            else reference[tick]=shape;
        }
        if (stream) world_stream_stop();
    }
    world_reset();
    return failures;
}
int water_wait_selftest(void) {
    int failures=0,raw;
    const int x=-1,z=-1,y=SURFACE;
    CHECK(world_begin_profile(94329,GENERATOR_CURRENT,WORLD_SUPERFLAT));
    /* This corner has two cold neighbors. Load the center only before
       starting the worker, as the UI does while opening a save. */
    (void)world_cell(x,y,z);
    CHECK(world_restore_edit(x,y+1,z,WATER));
    CHECK(world_restore_edit(-8,y+1,-8,WATER));
    if (!world_stream_start()) { world_stream_stop(); return 1; }
    for (int i=0;i<FAR_COUNT;++i) CHECK(water_restore_pending(4000+i*CHUNK_SIZE,y,-4000));
    CHECK(water_restore_pending(4000,y,-4000));
    CHECK(water_restore_pending(x,y,z) && water_restore_pending(-8,y,-8));
    world_set_interactive(1);
    water_step();
    CHECK(world_resident_cell(-8,y,-8,&raw) && water_strength(raw)==WATER_LEVELS);
    CHECK(world_resident_cell(-8,y-1,-8,&raw) && raw==AIR); /* At most one fall cell per tick. */
    CHECK(world_resident_cell(x,y,z,&raw) && raw==AIR && pending(OVERWORLD,x,y,z)==1);
    CHECK(pending(OVERWORLD,4000,y,-4000)==1 && !world_resident_cell(4000,y,-4000,&raw));
    /* Load just one dependency, not the other. A pending point may migrate
       between wait buckets without disappearing from the save iterator. */
    world_set_interactive(0);
    (void)world_cell(0,y,z);
    world_set_interactive(1);
    water_step();
    CHECK(world_resident_cell(x,y,z,&raw) && raw==AIR && pending(OVERWORLD,x,y,z)==1);
    CHECK(!world_resident_cell(x,y,0,&raw));
    CHECK(world_select_dimension(NETHER));
    world_set_interactive(0);
    (void)world_cell(x,y,0);
    world_set_interactive(1);
    CHECK(pending(OVERWORLD,x,y,z)==1 && !pending(NETHER,x,y,z));
    water_step();
    CHECK(pending(OVERWORLD,x,y,z)==1);
    /* Restored edits from the same chunk, and the matching bank, must be
       visible to the resumed transfer. Load dependencies out of order. */
    CHECK(world_select_dimension(OVERWORLD));
    world_set_interactive(0);
    (void)world_cell(x,y,0);
    (void)world_cell(0,y,z);
    (void)world_cell(x,y,z);
    world_set_interactive(1);
    water_step();
    CHECK(world_resident_cell(x,y,z,&raw) && water_strength(raw)==WATER_LEVELS);
    CHECK(world_resident_cell(x,y-1,z,&raw) && raw==AIR);
    CHECK(pending(OVERWORLD,x,y,z)==0);
    CHECK(pending(OVERWORLD,4000+(FAR_COUNT-1)*CHUNK_SIZE,y,-4000)==1);
    world_stream_stop();
    world_reset();
    CHECK(!pending(OVERWORLD,4000,y,-4000) && !pending(OVERWORLD,x,y,z));
    failures+=resident_parity();
    printf("WATER WAIT TEST cold=%d negative seam/two dependencies/dimension/FIFO/reset failures=%d\n",FAR_COUNT,failures);
    return failures;
}
