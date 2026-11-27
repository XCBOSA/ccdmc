#define _POSIX_C_SOURCE 200809L
#include "../game/game.h"
#include "../world/light.h"
#include "../world/internal.h"
#include "../world/redstone.h"
#include <stdio.h>
#include <time.h>

enum { Y=150 };
#define CHECK_LIGHT(c) do { if (!(c)) { printf("LIGHT TEST failed line %d: %s\n",__LINE__,#c); ++failures; } } while (0)

static void ticks(int n) { while (n--) redstone_step(-16,0); }
static int level(int x,int z) { return block_light_at(x,Y,z); }
static int wait_light(int x,int z) {
    const struct timespec pause={0,2000000};
    for (int i=0;i<4000;++i) {
        world_stream_pump(x,z);
        int light=level(x,z);
        if (light>=0) return light;
        nanosleep(&pause,NULL);
    }
    return -1;
}
static int sky_after_top_removal(void) {
    int failures=0;
    const int x=-17,z=-16,upper=12,lower=7;
    CHECK_LIGHT(world_begin_options(100,GENERATOR_CURRENT,WORLD_SUPERFLAT,1));
    CHECK_LIGHT(set_block(x,lower,z,STONE) && set_block(x,9,z,GLASS) &&
        set_block(x,upper,z,WOOD));
    CHECK_LIGHT(!light_has_open_sky(x,upper,z) && light_has_open_sky(x,upper+1,z));
    CHECK_LIGHT(set_block(x,upper,z,AIR) && light_has_open_sky(x,upper,z) &&
        light_has_open_sky(x,lower+1,z) && !light_has_open_sky(x,lower,z));
    CHECK_LIGHT(block(x,9,z)==GLASS && block(x,lower,z)==STONE);
    CHECK_LIGHT(set_block(x,lower,z,GLASS) && light_has_open_sky(x,lower,z) &&
        !light_has_open_sky(x,SUPERFLAT_SURFACE,z));
    /* Removing a transparent top cell must not change the opacity of water
       below it, nor confuse a column with no opaque cells for bedrock. */
    CHECK_LIGHT(set_block(0,4,-1,WATER) && set_block(0,6,-1,GLASS) &&
        set_block(0,6,-1,AIR) && !light_has_open_sky(0,4,-1));
    CHECK_LIGHT(set_block(0,4,-1,AIR) && light_has_open_sky(0,4,-1));
    for (int y=SUPERFLAT_SURFACE;y>=0;--y) CHECK_LIGHT(set_block(0,y,-1,AIR));
    CHECK_LIGHT(light_has_open_sky(0,0,-1));
    return failures;
}
int block_light_selftest(void) {
    int failures=sky_after_top_removal();
    world_begin(581);
    inventory_reset();
    inventory[0]=(ItemStack){GLOWSTONE_DUST,4,0,0,0};
    inventory[1]=(ItemStack){REDSTONE_DUST,4,0,0,0};
    CHECK_LIGHT(craft_take((int[]){0,0,0,0},2));
    CHECK_LIGHT(inventory[0].type==GLOWSTONE);
    CHECK_LIGHT(craft_take((int[]){-1,1,-1,1,0,1,-1,1,-1},3));
    CHECK_LIGHT(inventory[0].type==REDSTONE_LAMP_ITEM && inventory[0].count==1 &&
        mining_drop(REDSTONE_LAMP,0)==REDSTONE_LAMP_ITEM);
    CHECK_LIGHT(set_block(-17,Y,0,REDSTONE_LAMP) && set_block(-18,Y,0,REDSTONE_BLOCK));
    ticks(1);
    CHECK_LIGHT(block(-17,Y,0)==REDSTONE_LAMP && world_cell(-17,Y,0)==LAMP_ON);
    CHECK_LIGHT(level(-17,0)==15 && level(-16,0)==14 && level(-3,0)==1 && level(-2,0)==0);
    LightView seam=block_light_view(-2,0);
    CHECK_LIGHT(seam.ready && block_light_level(seam,16,Y,0)==14);
    CHECK_LIGHT(set_block(-16,Y,0,GLASS) && level(-16,0)==14);
    CHECK_LIGHT(set_block(-16,Y,0,WATER) && level(-16,0)==12);
    CHECK_LIGHT(set_block(-16,Y,0,AIR) && set_block(-13,Y,0,GLOWSTONE));
    CHECK_LIGHT(level(-16,0)==14 && level(-13,0)==15);
    CHECK_LIGHT(set_block(-18,Y,0,AIR));
    ticks(1); ticks(3);
    CHECK_LIGHT(world_cell(-17,Y,0)==LAMP_ON && level(-16,0)==14);
    CHECK_LIGHT(set_block(-18,Y,0,REDSTONE_BLOCK));
    ticks(1); ticks(4);
    CHECK_LIGHT(world_cell(-17,Y,0)==LAMP_ON);
    CHECK_LIGHT(set_block(-18,Y,0,AIR));
    ticks(5);
    CHECK_LIGHT(world_cell(-17,Y,0)==LAMP_OFF && level(-16,0)==12);
    CHECK_LIGHT(set_block(-13,Y,0,AIR) && level(-16,0)==0);

    /* One block of air is sealed on all six sides. Opening its upper
       neighbour (glass) restores light without changing the lamp. */
    const BlockEdit walls[]={
        {-16,Y-1,5,STONE},{-16,Y+1,5,STONE},{-17,Y,5,STONE},
        {-15,Y,5,STONE},{-16,Y,4,STONE},{-16,Y,6,STONE},{-16,Y+2,5,GLOWSTONE}
    };
    CHECK_LIGHT(world_set_blocks(walls,7) && !light_has_open_sky(-16,Y,5) && level(-16,5)==0);
    CHECK_LIGHT(set_block(-16,Y+1,5,GLASS) && level(-16,5)==13);
    CHECK_LIGHT(set_block(-16,Y+2,5,TORCH) && light_has_open_sky(-16,Y,5) && level(-16,5)==12);
    unsigned long long rev=world_light_revision(-1,0);
    CHECK_LIGHT(set_block(-15,Y,5,DIRT) && world_light_revision(-1,0)==rev);
    CHECK_LIGHT(set_block(-15,Y,5,STONE));
    CHECK_LIGHT(set_block(-16,Y+2,5,AIR) && level(-16,5)==0);

    /* Use the shipping pthread and change both world and dimension while
       requests are running. A stale result cannot be applied to either. */
    CHECK_LIGHT(light_start());
    CHECK_LIGHT(set_block(-17,Y,0,GLOWSTONE) && level(-16,0)==-1);
    CHECK_LIGHT(world_select_dimension(NETHER));
    CHECK_LIGHT(set_block(-17,Y,0,TORCH) && wait_light(-16,0)==13);
    CHECK_LIGHT(world_begin_generated(55185,GENERATOR_BOOKS) && level(-16,0)==0);
    CHECK_LIGHT(world_stream_start());
    CHECK_LIGHT(set_block(-17,Y,0,GLOWSTONE));
    world_set_interactive(1);
    CHECK_LIGHT(wait_light(-16,0)==14);
    CHECK_LIGHT(set_block(-17,Y,0,AIR) && wait_light(-16,0)==0);
    world_stream_stop();
    world_reset();
    printf("LIGHT TEST craft/lamp/hold/decay/seam/overlap/opacity/sky/worker failures=%d\n",failures);
    return failures;
}
