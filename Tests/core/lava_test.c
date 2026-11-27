#include "../world/lava.h"
#include "../world/light.h"
#include "../game/game.h"
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { printf("LAVA TEST failed line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
enum { Y=239 };

static void until(int ticks) { for (int i=0;i<ticks;++i) lava_step(); }
static int platform(int x,int z) {
    for (int a=x-8;a<=x+8;++a) for (int b=z-8;b<=z+8;++b)
        if (!set_block(a,Y-1,b,STONE)) return 0;
    return 1;
}

int lava_selftest(void) {
    int failures=0,raw;
    const int x=-17,z=5;
    world_begin(922);
    CHECK(platform(x,z));
    CHECK(set_block(x,Y,z,LAVA));
    until(LAVA_SURFACE_TICKS-1);
    CHECK(world_cell(x+1,Y,z)==AIR);
    lava_step();
    raw=world_cell(x+1,Y,z);
    CHECK(lava_strength(raw)==6 && lava_flow_cell(raw) && block(x+1,Y,z)==LAVA && light_emission(raw)==15);
    CHECK(lava_surface(raw,AIR)<lava_surface(LAVA,AIR));
    until(180);
    CHECK(lava_strength(world_cell(x+2,Y,z))==4 && lava_strength(world_cell(x+3,Y,z))==2);
    CHECK(world_cell(x+4,Y,z)==AIR);
    CHECK(set_block(x,Y,z,AIR));
    until(450);
    CHECK(world_cell(x+1,Y,z)==AIR && world_cell(x+3,Y,z)==AIR);

    CHECK(world_select_dimension(NETHER) && platform(x,z) && set_block(x,Y,z,LAVA));
    until(LAVA_NETHER_TICKS-1);
    CHECK(world_cell(x+1,Y,z)==AIR);
    lava_step();
    CHECK(lava_strength(world_cell(x+1,Y,z))==7);
    until(200);
    CHECK(lava_strength(world_cell(x+7,Y,z))==1 && world_cell(x+8,Y,z)==AIR);
    CHECK(world_select_dimension(OVERWORLD) && world_cell(x,Y,z)==AIR);

    /* A resident source must not load a missing neighbouring chunk. Once
       terrain actually arrives the queued edge flows without another edit. */
    const int far=-17-1200*CHUNK_SIZE;
    CHECK(!world_resident_cell(far+1,Y,z,&raw));
    CHECK(set_block(far,Y-1,z,STONE) && set_block(far,Y,z,LAVA));
    until(100);
    CHECK(!world_resident_cell(far+1,Y,z,&raw));
    CHECK(set_block(far+1,Y-1,z,STONE));
    CHECK(set_block(far-1,Y,z,STONE) && set_block(far,Y,z-1,STONE) && set_block(far,Y,z+1,STONE));
    until(90);
    CHECK(world_cell(far+1,Y,z)>255 && block(far+1,Y,z)==LAVA);

    world_begin(931);
    CHECK(platform(0,z));
    /* A single eastern hole is a closer outlet than the supported sides. */
    CHECK(set_block(1,Y-1,z,AIR) && set_block(0,Y,z,LAVA));
    until(120);
    CHECK(world_cell(0,Y,z)==LAVA && world_cell(-1,Y,z)==AIR && world_cell(0,Y,z-1)==AIR);
    CHECK(world_cell(1,Y-1,z)==LAVA_FALLING && world_cell(1,Y-2,z)==LAVA_FALLING);

    const int t=8;
    CHECK(platform(t,z+30));
    for (int i=0;i<20;++i) water_step();
    CHECK(set_block(t,Y,z+30,LAVA) && set_block(t+1,Y,z+30,WATER));
    water_step();
    CHECK(world_cell(t,Y,z+30)==OBSIDIAN && world_cell(t+1,Y,z+30)==WATER);
    world_begin(941);
    CHECK(platform(t,z+30));
    CHECK(set_block(t,Y+1,z+30,STONE) && set_block(t+2,Y+1,z+30,STONE) &&
        set_block(t+1,Y+1,z+29,STONE) && set_block(t+1,Y+1,z+31,STONE));
    CHECK(set_block(t,Y,z+30,LAVA));
    until(90);
    for (int i=0;i<20;++i) water_step();
    CHECK(lava_flow_cell(world_cell(t+1,Y,z+30)) && set_block(t+1,Y+1,z+30,WATER));
    water_step();
    CHECK(world_cell(t+1,Y,z+30)==COBBLESTONE && world_cell(t,Y,z+30)==LAVA);
    CHECK(set_block(t+4,Y,z+32,WATER) && set_block(t+4,Y+1,z+32,LAVA));
    until(60);
    CHECK(world_cell(t+4,Y,z+32)==STONE && world_cell(t+4,Y+1,z+32)==LAVA);

    /* The real bucket input ignores moving lava, but can scoop a source. */
    player_reset_rules(SURVIVAL);
    selected_slot=0;
    inventory[0]=(ItemStack){BUCKET,1,0,0,0};
    px=t-.5f; py=Y+2.2f; pz=z+30.5f; yaw=0.f; pitch=-1.5707f;
    CHECK(lava_flow_cell(world_cell(t-1,Y,z+30)));
    (void)player_use_bucket();
    CHECK(inventory[0].type==BUCKET && lava_flow_cell(world_cell(t-1,Y,z+30)));
    px=t+4.5f; pz=z+32.5f;
    (void)player_use_bucket();
    CHECK(inventory[0].type==LAVA_BUCKET && world_cell(t+4,Y+1,z+32)==AIR);

    printf("LAVA TEST cadence/range/ledge/fall/recede/quench/no-load/emission failures=%d\n",failures);
    return failures;
}
