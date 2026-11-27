#include "../game/air_station.h"
#include "../game/blast.h"
#include "../game/interaction.h"
#include "../world/light.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("AIR STATION TEST failed line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
static int said(const char *value,const char *part) { return value && strstr(value,part); }
int air_station_selftest(void) {
    int failures=0;
    CHECK(world_begin_profile(56182,GENERATOR_CURRENT,WORLD_SUPERFLAT));
    generate(); player_reset_rules(SURVIVAL);
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){CLAY_BALL,1,0,0,0}; inventory[1]=(ItemStack){LEATHER,1,0,0,0};
    const int two[]={0,1,-1,-1};
    CHECK(craft_preview(two,2).type==PUMP_GASKET && craft_take(two,2) && inventory[0].count==2);
    inventory[1]=(ItemStack){IRON_INGOT,3,0,0,0};
    CHECK(craft_preview(two,2).type==AIR_OUTLET_ITEM && craft_take(two,2) && inventory[2].type==AIR_OUTLET_ITEM);
    inventory[3]=(ItemStack){SLATE_ITEM,2,0,0,0};
    inventory[4]=(ItemStack){STRIATED_GLASS,1,0,0,0};
    const int four[]={0,1,3,4},pipe[]={1,3,-1,-1};
    CHECK(craft_preview(four,2).type==AIR_STATION_ITEM && craft_take(four,2) && inventory[0].type==AIR_STATION_ITEM);
    CHECK(craft_preview(pipe,2).type==AIR_PIPE_ITEM && craft_take(pipe,2) && inventory[1].count==4);
    const Hit pump={1,0,4,-2,0,0,0},outlet={1,3,4,-2,0,0,0};
    CHECK(set_block(0,4,-2,AIR_STATION));
    CHECK(world_set_blocks(&(BlockEdit){3,4,-2,AIR_OUTLET_BASE},1));
    for (int i=-1;i<=1;++i) for (int z=-1;z<=1;++z) (void)block(i*CHUNK_SIZE,4,z*CHUNK_SIZE);
    px=.5f; py=4.01f; pz=1.5f; yaw=0; pitch=-.34f;
    selected_slot=5;
    CHECK(target().hit && block(target().x,target().y,target().z)==AIR_STATION);
    CHECK(said(game_tap().message,"储压 2/6"));
    CHECK(said(air_station_tap(pump),"储压 4/6"));
    CHECK(said(air_station_tap(pump),"储压 6/6"));
    CHECK(said(air_station_tap(pump),"已满") && light_emission(world_cell(0,4,-2))==6);
    CHECK(set_block(0,5,-2,STONE) && said(air_station_tap(pump),"留空") && set_block(0,5,-2,AIR));
    CHECK(said(air_station_tap(outlet),"潜入水中") && world_cell(0,4,-2)==AIR_STATION_BASE+6);

    CHECK(set_block(0,5,1,WATER));
    player_rules.air=2; player_rules.submerged_time=.49f;
    CHECK(said(air_station_tap(pump),"干处") && said(air_station_tap(outlet),"未接通"));
    CHECK(set_block(1,4,-2,AIR_PIPE) && set_block(2,4,-2,AIR_PIPE));
    CHECK(set_block(2,5,-2,AIR_STATION));
    CHECK(said(air_station_tap(outlet),"只能接一台") && player_rules.air==2 && world_cell(0,4,-2)==AIR_STATION_BASE+6);
    CHECK(set_block(2,5,-2,AIR));
    CHECK(said(air_station_tap(outlet),"储压 4/6") && player_rules.air==MAX_AIR && player_rules.submerged_time==0.f);
    CHECK(said(air_station_tap(outlet),"不消耗储压") && world_cell(0,4,-2)==AIR_STATION_BASE+4);
    player_rules.air=3;
    CHECK(set_block(2,4,-2,AIR) && said(air_station_tap(outlet),"未接通") && player_rules.air==3);
    CHECK(set_block(2,4,-2,AIR_PIPE) && said(air_station_tap(outlet),"储压 2/6"));
    player_rules.air=3;
    CHECK(said(air_station_tap(outlet),"储压 0/6") && !light_emission(world_cell(0,4,-2)));
    player_rules.air=3;
    CHECK(said(air_station_tap(outlet),"储压不足") && player_rules.air==3);
    for (int x=4;x<=4+AIR_PIPE_LIMIT;++x) CHECK(set_block(x,4,-2,AIR_PIPE));
    CHECK(said(air_station_tap(outlet),"最多 24 段") && player_rules.air==3);
    CHECK(!blast_restore(OVERWORLD,(PrimedBlast){{2,4,0},{0},0,BLAST_ARCHIVE_BOSS_FIRST}));
    world_reset();
    printf("AIR STATION TEST craft/taps/pressure/ambient intake/dry/closed/multiple/budget/retired burst failures=%d\n",failures);
    return failures;
}
