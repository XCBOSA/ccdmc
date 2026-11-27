#include "world_test.h"
#include "../game/drops.h"
#include "../game/interaction.h"
#include "../game/tripwire.h"
#include "../world/internal.h"
#include "../world/space.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("TRIPWIRE TEST failed line %d: %s\n",__LINE__,#c); ++bad; } } while (0)
enum { Y=181, LEFT=-18, RIGHT=-14, MID=-16 };
static void ticks(int n) { while (n-- >0) redstone_step(MID,0); }
static int power(int x,int y,int z) { return redstone_tripwire_power(x,y,z); }
static int pair(int y,int z) { return power(LEFT,y,z)==15 && power(RIGHT,y,z)==15; }
static unsigned dropped(int item) {
    unsigned n=0;
    for (int i=0;i<MAXDROP;++i) if (drops[i].item.type==item) n+=drops[i].item.count;
    return n;
}
static int span(int y,int z) {
    if (!set_block(LEFT-1,y,z,STONE) || !set_block(RIGHT+1,y,z,STONE) ||
        !tripwire_place_hook((CircuitPos){LEFT,y,z},(CircuitPos){LEFT-1,y,z}) ||
        !tripwire_place_hook((CircuitPos){RIGHT,y,z},(CircuitPos){RIGHT+1,y,z})) return 0;
    for (int x=LEFT+1;x<RIGHT;++x) if (!tripwire_place_string((CircuitPos){x,y,z})) return 0;
    return 1;
}

int tripwire_selftest(void) {
    int bad=0;
    world_begin(41721); generate(); player_reset_rules(SURVIVAL);
    px=MID+.5f; py=Y+.01f; pz=4.5f;
    CHECK(!(world_contact_sensors_near(-1,0,4)&SENSOR_TRIPWIRE));
    for (int x=LEFT-1;x<=RIGHT+1;++x) CHECK(set_block(x,Y-1,0,STONE));
    CHECK(!tripwire_place_hook((CircuitPos){MID,Y,0},(CircuitPos){MID,Y-1,0}));
    CHECK(span(Y,0) && (world_contact_sensors_near(-1,0,4)&SENSOR_TRIPWIRE));
    CHECK(world_cell(LEFT,Y,0)==TRIPWIRE_HOOK_BASE+WEST && world_cell(RIGHT,Y,0)==TRIPWIRE_HOOK_BASE+EAST);
    CHECK(set_block(LEFT-1,Y+1,0,REDSTONE_LAMP) && set_block(RIGHT,Y,1,REDSTONE_LAMP));
    ticks(5);
    CHECK(!power(LEFT,Y,0) && !power(RIGHT,Y,0) && world_cell(LEFT-1,Y+1,0)==LAMP_OFF);

    /* Normal movement casts no matching ray; entity bounds drive the line. */
    pz=.50f;
    ticks(1);
    CHECK(pair(Y,0) && world_cell(LEFT-1,Y+1,0)==LAMP_ON && world_cell(RIGHT,Y,1)==LAMP_ON &&
        !world_point_blocked(MID+.5f,Y+.08f,.5f,1));
    ticks(TRIPWIRE_PULSE_TICKS+2);
    CHECK(pair(Y,0));
    pz=4.50f;
    ticks(TRIPWIRE_PULSE_TICKS+5);
    CHECK(!power(LEFT,Y,0) && !power(RIGHT,Y,0) && world_cell(RIGHT,Y,1)==LAMP_OFF);
    drops[0]=make_drop(MID+.50f,Y+.04f,.5f,(ItemStack){APPLE,1,0,0,0});
    ticks(1);
    CHECK(pair(Y,0));
    drops_reset(); ticks(TRIPWIRE_PULSE_TICKS+5);

    /* The silent path does not create a pulse. Loose strings cannot activate. */
    CHECK(tripwire_break((CircuitPos){MID,Y,0},1) && dropped(STRING)==1);
    px=LEFT+1.5f; pz=.5f;
    ticks(1);
    CHECK(!power(LEFT,Y,0) && !power(RIGHT,Y,0));
    px=MID+.5f; pz=4.5f;
    drops_reset();
    CHECK(tripwire_place_string((CircuitPos){MID,Y,0}) && tripwire_break((CircuitPos){MID,Y,0},0) &&
        pair(Y,0) && dropped(STRING)==1);
    drops_reset(); ticks(TRIPWIRE_PULSE_TICKS+5);
    CHECK(tripwire_place_string((CircuitPos){MID,Y,0}) && set_block(MID,Y-1,0,AIR));
    pz=.5f;
    ticks(1);
    CHECK(!power(LEFT,Y,0) && !power(RIGHT,Y,0)); /* Suspension discontinuity. */
    pz=4.5f;
    CHECK(span(Y+3,0));
    py=Y+3.70f; pz=.5f;
    ticks(1);
    CHECK(!power(LEFT,Y+3,0));
    py=Y+3.30f;
    ticks(1);
    CHECK(pair(Y+3,0));
    pz=4.5f;
    CHECK(set_block(LEFT-1,Y+3,0,AIR));
    ticks(TRIPWIRE_PULSE_TICKS+5);
    CHECK(world_cell(LEFT,Y+3,0)==AIR && !power(RIGHT,Y+3,0) && dropped(TRIPWIRE_HOOK_ITEM)==1);

    /* The opened, cross-chunk line must not leak a signal to another dimension. */
    CHECK(set_block(MID,Y-1,0,STONE));
    pz=.5f; py=Y+.01f;
    for (int z=-3;z<=3;++z) CHECK(set_block(MID,Y-1,z,STONE));
    CHECK(set_block(MID,Y,-3,STONE) && set_block(MID,Y,3,STONE) &&
        tripwire_place_hook((CircuitPos){MID,Y,-2},(CircuitPos){MID,Y,-3}) &&
        tripwire_place_hook((CircuitPos){MID,Y,2},(CircuitPos){MID,Y,3}) &&
        tripwire_place_string((CircuitPos){MID,Y,-1}) && tripwire_place_string((CircuitPos){MID,Y,1}));
    ticks(1);
    CHECK(pair(Y,0) && power(MID,Y,-2)==15 && power(MID,Y,2)==15 &&
        world_select_dimension(NETHER) && !power(LEFT,Y,0) &&
        !(world_contact_sensors_near(-1,0,4)&SENSOR_TRIPWIRE) && world_select_dimension(OVERWORLD));
    px=240.5f; pz=10.5f;
    for (int i=0;i<TRIPWIRE_PULSE_TICKS+2;++i) redstone_step(240,10);
    CHECK(!power(LEFT,Y,0) && !power(RIGHT,Y,0) && !power(MID,Y,-2) && !power(MID,Y,2));

    /* Load just the center chunk. The cross-chunk line is not guessed
       armed and looking for its missing end doesn't load that chunk. */
    CHECK(world_select_dimension(END) && world_select_dimension(OVERWORLD));
    int resident_cell=0;
    CHECK(world_cell(MID,Y,0)==TRIPWIRE_CELL && !world_resident_cell(LEFT,Y,0,&resident_cell));
    tripwire_body_contact((CircuitPos){MID,Y,0},(BlockBox){-16.f,Y+.01f,0.f,-15.f,Y+.2f,1.f});
    CHECK(!world_resident_cell(LEFT,Y,0,&resident_cell) && !power(RIGHT,Y,0));

    /* Touch uses the normal aim→tap placement route and long-press mining. */
    const int x0=10,z0=1;
    CHECK(set_block(x0,Y-1,z0,STONE) && set_block(x0,Y,z0,AIR));
    memset(inventory,0,sizeof inventory);
    selected_slot=0; inventory[0]=(ItemStack){STRING,2,0,0,0};
    px=x0+.5f; py=Y+.01f; pz=3.5f; yaw=0.f; pitch=-.65f;
    Hit h=target();
    CHECK(h.hit && h.y==Y-1 && h.z==z0 && h.ay==Y);
    CHECK(game_tap().message && world_cell(x0,Y,z0)==TRIPWIRE_CELL && inventory[0].count==1);
    h=target();
    CHECK(h.hit && h.y==Y && h.z==z0);
    inventory[0]=(ItemStack){SHEARS,1,0,0,0};
    CHECK(mine() && world_cell(x0,Y,z0)==AIR && inventory[0].damage==1);
    /* Hook mounting uses the adjacent cell from the same tap route. */
    CHECK(set_block(x0,Y+1,0,STONE) && set_block(x0,Y+1,1,AIR));
    inventory[0]=(ItemStack){TRIPWIRE_HOOK_ITEM,2,0,0,0};
    pitch=0.f;
    CHECK(game_tap().message && world_cell(x0,Y+1,1)==TRIPWIRE_HOOK_BASE+NORTH && inventory[0].count==1);
    printf("TRIPWIRE TEST mounting/picking/seam/contacts/conduction/cutting/suspension/support/dimension failures=%d\n",bad);
    return bad;
}
