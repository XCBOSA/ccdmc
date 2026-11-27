#include "../game/interaction.h"
#include "../game/portals.h"
#include "../world/aquifer_layout.h"
#include "../world/pressure_layout.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("AQUIFER TEST failed line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
int aquifer_selftest(void) {
    int failures=0;
    CHECK(world_begin_profile(1234,GENERATOR_CURRENT,WORLD_SUPERFLAT));
    generate();
    player_reset_rules(SURVIVAL);
    CHECK(set_block(0,4,0,AQUIFER_WINCH));
    px=.5f; py=4.01f; pz=3.5f; yaw=0; pitch=-.4f;
    CHECK(target().hit && world_cell(target().x,target().y,target().z)==AQUIFER_WINCH);
    CHECK(game_tap().message);
    player_rules.health=0;
    CHECK(!portals_step(.05f));
    player_rules.health=MAX_HEALTH;
    CHECK(!portals_step(.05f) && world_dimension()==OVERWORLD);
    CHECK(game_tap().message && portals_step(.05f) && inside_aquifer());
    CHECK(block(0,AQUIFER_FLOOR+1,AQUIFER_DOCK_Z)==AQUIFER_DOCK);
    CHECK(block(0,69,1)!=ARCHIVE_EXIT);
    CHECK(!portals_aquifer_samples() && game_tap().message && portals_step(.05f) && world_dimension()==OVERWORLD);
    CHECK(fabsf(pz-3.5f)<.02f);
    pitch=-.4f;
    CHECK(game_tap().message && portals_step(.05f) && inside_aquifer());
    const Hit hatch={1,0,AQUIFER_FLOOR+1,PRESSURE_AQUIFER_HATCH_Z,0,0,0};
    CHECK(block(hatch.x,hatch.y,hatch.z)==PRESSURE_HATCH && portals_use_aquifer(hatch) &&
        !portals_step(.05f) && inside_aquifer());

    const Hit wet={1,AQUIFER_BRINE_X,AQUIFER_BRINE_Y,0,AQUIFER_BRINE_X+1,AQUIFER_BRINE_Y,0};
    const Hit hot={1,AQUIFER_VENT_X,AQUIFER_VENT_Y,0,AQUIFER_VENT_X-1,AQUIFER_VENT_Y,0};
    CHECK(block(wet.x,wet.y,wet.z)==AQUIFER_BRINE && block(hot.x,hot.y,hot.z)==AQUIFER_VENT);
    memset(inventory,0,sizeof inventory);
    selected_slot=0; inventory[0]=(ItemStack){SURVEY_TAPPER,1,0,0,0};
    CHECK(portals_use_aquifer(wet) && !portals_aquifer_samples());
    px=-12.4f; py=51.01f; pz=.5f; yaw=-1.57079633f; pitch=0;
    CHECK(target().x==wet.x && target().y==wet.y);
    for (int i=1;i<SLOT_COUNT;++i) inventory[i]=(ItemStack){DIRT,64,0,0,0};
    const char *full=game_tap().message;
    CHECK(full && strstr(full,"背包已满") && !portals_aquifer_samples());
    inventory[1]=(ItemStack){0};
    CHECK(game_tap().message && portals_aquifer_samples()==AQUIFER_SAMPLE_BRINE && inventory[1].type==BRINE_CORE);
    CHECK(game_tap().message && inventory[1].count==1);
    memset(inventory+2,0,(SLOT_COUNT-2)*sizeof inventory[0]);
    px=12.4f; py=55.01f; pz=.5f; yaw=1.57079633f; pitch=-.32f;
    CHECK(target().x==hot.x && target().y==hot.y);
    CHECK(game_tap().message && portals_aquifer_samples()==AQUIFER_SAMPLE_BRINE);
    px=13.3f;
    int hp=player_rules.health;
    for (int i=0;i<42;++i) CHECK(!portals_step(.05f));
    CHECK(player_rules.health<hp && portals_aquifer_vent_danger());
    px=12.4f;
    selected_slot=2; inventory[2]=(ItemStack){LAMINATE_ITEM,2,0,0,0};
    CHECK(game_tap().message && block(AQUIFER_SEAL_X,AQUIFER_SEAL_Y,0)==LAMINATED_MASONRY && inventory[2].count==1);
    CHECK(!portals_aquifer_vent_danger());
    selected_slot=0;
    CHECK(game_tap().message && portals_aquifer_samples()==AQUIFER_SAMPLES && inventory[3].type==THERMAL_CORE);
    const int recipe[]={1,2,3,-1};
    CHECK(craft_preview(recipe,2).type==AQUIFER_LIGHT_ITEM && craft_take(recipe,2) &&
        inventory[1].type==AQUIFER_LIGHT_ITEM && inventory[1].count==6);
    CHECK(!portals_restore_aquifer(4) && portals_aquifer_samples()==AQUIFER_SAMPLES);
    const Hit lift={1,0,PRESSURE_FLOOR+1,PRESSURE_LIFT_Z,0,0,0};
    const Hit left={1,-PRESSURE_VALVE_X,PRESSURE_FLOOR+1,PRESSURE_VALVE_Z,0,0,0};
    const Hit right={1,PRESSURE_VALVE_X,PRESSURE_FLOOR+1,PRESSURE_VALVE_Z,0,0,0};
    const Hit kiln={1,0,PRESSURE_FLOOR+1,PRESSURE_KILN_Z,0,0,0};
    px=.5f; py=AQUIFER_FLOOR+1.01f; pz=-.5f;
    CHECK(portals_use_aquifer(hatch) && portals_step(.05f) && inside_pressure_works());
    CHECK(portals_use_aquifer(lift) && portals_step(.05f) && inside_aquifer());
    CHECK(portals_use_aquifer(hatch) && portals_step(.05f) && inside_pressure_works());
    CHECK(!collides(px,py,pz) && !portals_pressure_relief());
    CHECK(block(kiln.x,kiln.y,kiln.z)==PRESSURE_KILN && block(left.x,left.y,left.z)==PRESSURE_CLOSED);
    selected_slot=0; inventory[0]=(ItemStack){COBBLESTONE,2,0,0,0};
    CHECK(portals_use_aquifer(kiln) && inventory[0].count==2);
    px=-9.5f; py=PRESSURE_FLOOR+1.01f; pz=PRESSURE_VALVE_Z+.5f;
    hp=player_rules.health;
    for (int i=0;i<32;++i) CHECK(!portals_step(.05f));
    CHECK(player_rules.health<hp && portals_aquifer_vent_danger());
    yaw=-1.57079633f; pitch=-.59f;
    CHECK(target().hit && target().x==left.x && game_tap().message && portals_pressure_relief()==1);
    CHECK(!portals_step(.05f) && !portals_aquifer_vent_danger());
    CHECK(portals_use_aquifer(kiln) && inventory[0].count==2);
    CHECK(block(right.x,right.y,right.z)==PRESSURE_CLOSED);
    px=9.5f; yaw=1.57079633f;
    CHECK(target().hit && target().x==right.x && game_tap().message && portals_pressure_relief()==3);
    for (int i=1;i<SLOT_COUNT;++i) inventory[i]=(ItemStack){DIRT,64,0,0,0};
    const char *no_room=portals_use_aquifer(kiln);
    CHECK(no_room && strstr(no_room,"背包没有空间") && inventory[0].count==2);
    inventory[1]=(ItemStack){0};
    CHECK(portals_use_aquifer(kiln) && inventory[0].count==1 &&
        inventory[1].type==SINTERED_TILE_ITEM && inventory[1].count==2);
    CHECK(portals_use_aquifer(lift) && portals_step(.05f) && inside_aquifer());
    CHECK(portals_use_aquifer(hatch) && portals_step(.05f) && portals_pressure_relief()==3);
    world_reset();
    printf("AQUIFER TEST lift/sampling/thermal interlock/relief/hazard/kiln/output capacity/return failures=%d\n",failures);
    return failures;
}
