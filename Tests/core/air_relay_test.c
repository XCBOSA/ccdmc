#include "../game/air_station.h"
#include "../game/interaction.h"
#include "../game/item_icons.h"
#include "../world/redstone.h"
#include "../world/rails.h"
#include "../world/flat.h"
#include <stdio.h>
#include <string.h>

static int said(const char *actual,const char *part) { return actual && strstr(actual,part); }
#define CHECK(c) do { if (!(c)) { printf("AIR RELAY TEST failed line %d: %s\n",__LINE__,#c); ++failures; } } while (0)

int air_relay_selftest(void) {
    int failures=0,charge=-1,raw;
    enum { Y=4,Z=-2 };
    CHECK(world_begin_profile(56182,GENERATOR_CURRENT,WORLD_SUPERFLAT));
    generate(); player_reset_rules(SURVIVAL);
    for (int x=-1;x<=1;++x) for (int z=-1;z<=1;++z) (void)block(x*CHUNK_SIZE,Y,z*CHUNK_SIZE);
    memset(inventory,0,sizeof inventory); selected_slot=0;
    inventory[0]=(ItemStack){LAMINATE_ITEM,1,0,0,0};
    inventory[1]=(ItemStack){STRIATED_GLASS,1,0,0,0};
    inventory[2]=(ItemStack){IRON_INGOT,1,0,0,0};
    inventory[3]=(ItemStack){PUMP_GASKET,1,0,0,0};
    const int kit[]={0,1,2,3};
    CHECK(craft_preview(kit,2).type==AIR_RELAY_ITEM && craft_take(kit,2) && inventory[0].type==AIR_RELAY_ITEM);

    const Hit pump={1,0,Y,Z,0,0,0},relay={1,0,Y,Z+1,0,0,0};
    CHECK(set_block(0,Y,Z,AIR_STATION));
    px=.5f; py=Y+.01f; pz=1.5f; yaw=0; pitch=-.34f;
    CHECK(said(game_tap().message,"方块已放置") &&
        world_cell(0,Y,Z+1)==AIR_RELAY_BASE+SOUTH && !inventory[0].count);
    CHECK(said(air_station_tap(relay),"请为供气站加压") && !redstone_timing_count(OVERWORLD));
    CHECK(said(air_station_tap(pump),"储压 2/6") && said(air_station_tap(pump),"储压 4/6"));
    CHECK(redstone_place_wire(0,Y,Z+2) && set_block(0,Y,Z+3,REDSTONE_LAMP));
    CHECK(set_block(1,Y,Z+1,REDSTONE_LAMP) && set_block(0,Y+1,Z+1,REDSTONE_LAMP));
    CHECK(said(air_station_tap(relay),"储压 2/6") && redstone_timing_count(OVERWORLD)==1);
    CHECK(redstone_press_air_relay(0,Y,Z+1,&charge)==AIR_RELAY_BUSY && charge==-1 &&
        world_cell(0,Y,Z)==AIR_STATION_BASE+2);
    redstone_step(0,0);
    CHECK(redstone_wire_power(0,Y,Z+2)==AIR_RELAY_POWER &&
        world_cell(0,Y,Z+3)==LAMP_ON && world_cell(1,Y,Z+1)==LAMP_OFF &&
        world_cell(0,Y+1,Z+1)==LAMP_OFF && !redstone_receives(0,Y,Z));
    for (int i=1;i<AIR_RELAY_TICKS+5;++i) redstone_step(0,0);
    CHECK(redstone_wire_power(0,Y,Z+2)==0 && world_cell(0,Y,Z+3)==LAMP_OFF &&
        !redstone_timing_count(OVERWORLD) && world_cell(0,Y,Z)==AIR_STATION_BASE+2);
    CHECK(redstone_press_air_relay(0,Y,Z+1,&charge)==AIR_RELAY_STARTED && charge==0);
    CHECK(set_block(0,Y,Z+1,AIR) && !redstone_timing_count(OVERWORLD));
    CHECK(world_set_blocks(&(BlockEdit){0,Y,Z+1,AIR_RELAY_BASE+EAST},1) &&
        redstone_press_air_relay(0,Y,Z+1,&charge)==AIR_RELAY_DISCONNECTED);
    CHECK(!world_resident_cell(32,Y,Z,&raw));
    CHECK(world_restore_edit(31,Y,Z,AIR_RELAY_BASE+WEST) &&
        redstone_press_air_relay(31,Y,Z,&charge)==AIR_RELAY_UNLOADED &&
        !world_resident_cell(32,Y,Z,&raw));
    ItemIcon icon;
    CHECK(item_icon(AIR_RELAY_ITEM,&icon) && icon.path[3] &&
        cell_type(AIR_RELAY_BASE+WEST)==AIR_RELAY_BASE &&
        mining_drop(AIR_RELAY_BASE+WEST,0)==AIR_RELAY_ITEM);
    for (int i=0;i<9;++i) inventory[i]=(ItemStack){REDSTONE_DUST,1,0,0,0};
    const int old[]={0,1,2,3,4,5,6,7,8};
    CHECK(craft_preview(old,3).type!=REDSTONE_BLOCK);
    inventory[0]=(ItemStack){REDSTONE_BLOCK,1,0,0,0};
    const int salvage[]={0,-1,-1,-1};
    CHECK(craft_preview(salvage,2).type==REDSTONE_DUST && craft_preview(salvage,2).count==9);
    FlatLayout archived={2,BIOME_PLAINS,0,{BEDROCK,REDSTONE_BLOCK}}, new_layout;
    CHECK(flat_valid(&archived) && flat_parse("bedrock,conductive-stone",&new_layout));
    world_reset();
    printf("AIR RELAY TEST placement/reserve/direction/expiry/unloaded/recipe failures=%d\n",failures);
    return failures;
}
