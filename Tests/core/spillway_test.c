#include "../world/spillways.h"
#include "../world/mineshafts.h"
#include "../game/fall_sieve.h"
#include "../game/item_icons.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(p) do { if (!(p)) { printf("SPILLWAY TEST line %d: %s\n",__LINE__,#p); ++failures; } } while (0)
static int chunk(int x) { return x/CHUNK_SIZE-(x%CHUNK_SIZE<0); }
static int raw(BlockEdit b) { return world_cell(b.x,b.y,b.z); }
static int has(const char *s,const char *word) { return s && strstr(s,word); }
static int fixture(int rot,SpillwayPlan *p) {
    for (int x=-10;x<=10;++x) for (int z=-10;z<=10;++z) {
        if (!spillway_plan_for(x*SPILLWAY_REGION,z*SPILLWAY_REGION,p) || p->rotated!=rot) continue;
        const BlockEdit start=spillway_cell(*p,0,0,0,0),end=spillway_cell(*p,7,0,0,0);
        if (chunk(start.x)!=chunk(end.x) || chunk(start.z)!=chunk(end.z)) return 1;
    }
    return 0;
}
static void warm(SpillwayPlan p) {
    for (int u=-5;u<=9;u+=2) for (int v=-5;v<=3;v+=2)
        (void)raw(spillway_cell(p,u,v,1,0));
}
static void run_water(void) { for (int i=0;i<26;++i) water_step(); }

int spillway_selftest(void) {
    const TerrainKey key=TERRAIN_KEY(1712,GENERATOR_SPILLWAYS,OVERWORLD,WORLD_STANDARD,0);
    int failures=0;
    static TerrainResult a,b;
    SpillwayPlan p={0};
    for (int rot=0;rot<2;++rot) {
        CHECK(world_begin_generated(key.seed,key.generation));
        if (!fixture(rot,&p)) { CHECK(0 && "missing cross-chunk plan"); continue; }
        const BlockEdit sieve=spillway_cell(p,1,0,1,FALL_SIEVE);
        const BlockEdit plug=spillway_cell(p,0,1,3,CLAY);
        const BlockEdit head=spillway_cell(p,-1,1,3,WATER);
        const BlockEdit falling=spillway_cell(p,1,1,1,AIR);
        const BlockEdit exit=spillway_cell(p,SPILLWAY_END+1,p.mouth,1,AIR);
        const Hit hit={1,sieve.x,sieve.y,sieve.z,sieve.x,sieve.y,sieve.z};
        terrain_prepare(key,chunk(sieve.x),chunk(sieve.z),&a);
        terrain_prepare(key,chunk(exit.x),chunk(exit.z),&b);
        terrain_prepare(key,chunk(sieve.x),chunk(sieve.z),&b);
        CHECK(!memcmp(a.cells,b.cells,sizeof a.cells) && a.wide.count==b.wide.count &&
              !memcmp(a.wide.cell,b.wide.cell,a.wide.count*sizeof a.wide.cell[0]));
        warm(p);
        for (int n=SPILLWAY_END-1;n<=SPILLWAY_END+1;++n)
            for (int v=p.mouth;v<=p.mouth+1;++v)
                for (int dy=1;dy<=2;++dy) CHECK(raw(spillway_cell(p,n,v,dy,AIR))==AIR);
        CHECK(raw(sieve)==FALL_SIEVE && raw(plug)==CLAY && raw(head)==WATER && raw(falling)==AIR);
        ItemIcon icon;
        CHECK(item_icon(FALL_SIEVE_ITEM,&icon) && mining_drop(FALL_SIEVE,0)==FALL_SIEVE_ITEM);
        MineshaftPlan retired;
        CHECK(!mineshaft_plan(key,sieve.x,sieve.z,&retired));
        player_reset_rules(SURVIVAL);
        memset(inventory,0,sizeof inventory); selected_slot=0;
        inventory[0]=(ItemStack){GRAVEL,6,0,0,0};
        CHECK(has(fall_sieve_use(hit),"先引水") && inventory[0].count==6);
        CHECK(set_block(plug.x,plug.y,plug.z,AIR));
        run_water();
        const int flow=water_strength(raw(falling));
        CHECK(flow>=3 && water_strength(raw(spillway_cell(p,1,1,0,0)))==flow);
        CHECK(water_strength(raw(spillway_cell(p,5,1,-1,0)))>0);
        CHECK(raw(spillway_cell(p,1,0,2,0))==AIR && raw(spillway_cell(p,3,0,1,0))==AIR &&
            raw(spillway_cell(p,7,p.mouth,1,0))==AIR);
        for (int i=1;i<SLOT_COUNT;++i) inventory[i]=(ItemStack){STONE,64,0,0,0};
        CHECK(has(fall_sieve_use(hit),"装不下") && inventory[0].count==6);
        memset(inventory+1,0,sizeof inventory-sizeof inventory[0]);
        CHECK(has(fall_sieve_use(hit),"已淘洗") && inventory[0].count==3 &&
            inventory[1].type==TIDE_GRIT && inventory[1].count==2);
        CHECK(set_block(plug.x,plug.y,plug.z,CLAY));
        run_water();
        CHECK(raw(falling)==AIR && has(fall_sieve_use(hit),"先引水") && inventory[0].count==3);
        CHECK(set_block(falling.x,falling.y,falling.z,WATER) &&
            has(fall_sieve_use(hit),"静水或平流") && inventory[0].count==3);
        const BlockEdit cover=spillway_cell(p,1,0,2,STONE);
        CHECK(set_block(cover.x,cover.y,cover.z,STONE) && has(fall_sieve_use(hit),"顶部留空"));
        printf("SPILLWAY TEST rotation=%d at=%d,%d,%d waterfall=%d\n",rot,sieve.x,sieve.y,sieve.z,flow);
    }
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){SINTERED_TILE_ITEM,1,0,0,0};
    inventory[1]=(ItemStack){STRIATED_GLASS,1,0,0,0};
    inventory[2]=(ItemStack){GRAVEL,1,0,0,0};
    inventory[3]=(ItemStack){PUMP_GASKET,1,0,0,0};
    const int recipe[]={0,1,2,3};
    CHECK(craft_preview(recipe,2).type==FALL_SIEVE_ITEM && craft_take(recipe,2) &&
          inventory[0].type==FALL_SIEVE_ITEM);
    CHECK(world_begin_options(key.seed,key.generation,WORLD_STANDARD,1) && !spillway_plan_for(p.x,p.z,&p));
    CHECK(world_begin_generated(key.seed,GENERATOR_STRATA_GALLERIES) && !spillway_plan_for(p.x,p.z,&p));
    CHECK(world_begin_generated(key.seed,key.generation));
    /* Place with restoration rather than fire/redstone/terrain notifications
       so the fixture itself cannot load the neighbouring chunk. */
    (void)world_cell(63,200,10);
    CHECK(world_restore_edit(63,200,10,FALL_SIEVE));
    const Hit edge={1,63,200,10,63,200,10};
    inventory[0]=(ItemStack){0,0,0,0,0};
    int cold;
    CHECK(!world_resident_cell(64,200,10,&cold) &&
          has(fall_sieve_use(edge),"等相邻区块载入") && !world_resident_cell(64,200,10,&cold));
    CHECK(world_begin_generated(1716,key.generation));
    /* This region previously accepted a mouth with its supply end sticking
       out of a coastal cliff. A replacement site must be fully covered. */
    if (spillway_plan_for(-688,815,&p)) {
        for (int u=SPILLWAY_BEGIN;u<=SPILLWAY_END;++u) for (int v=SPILLWAY_NEAR;v<=SPILLWAY_FAR;++v) {
            const BlockEdit q=spillway_cell(p,u,v,0,0);
            CHECK(terrain_height(q.x,q.z)>=p.floor+SPILLWAY_ROOF+3);
        }
    }
    world_reset();
    printf("SPILLWAY TEST failures=%d\n",failures);
    return failures;
}
