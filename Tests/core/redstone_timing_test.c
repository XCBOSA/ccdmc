#include "../world/redstone.h"
#include "../game/game.h"
#include <stdio.h>
#include <string.h>

#define CHECK_TIMED(expr) do { if (!(expr)) { printf("TIMED REDSTONE failed line %d: %s\n",__LINE__,#expr); ++failures; } } while (0)

enum { Y=150 };
static void ticks(int count) { while (count-- >0) redstone_step(0,0); }
static int torch(int x,int z) { return world_cell(x,Y,z)>=TORCH_POWER_BASE+TORCH_POWER_ON &&
    torch_power_cell(world_cell(x,Y,z)); }
static int repeater(int x,int z) {
    int raw=world_cell(x,Y,z);
    return repeater_cell(raw) && ((raw-REPEATER_BASE)&REPEATER_ON)!=0;
}
static int floor_lever(int x,int z) {
    return redstone_place_lever((CircuitPos){x,Y,z},(CircuitPos){x,Y-1,z},0);
}
static int gate_open(int x,int z) {
    GateState gate;
    return gate_decode(world_cell(x,Y,z),&gate) && gate.open && gate.powered;
}
static int capture(const RedstoneTiming *timing,void *ctx) {
    RedstoneTiming *out=(RedstoneTiming *)ctx;
    if (timing->position.x==out->position.x && timing->position.y==out->position.y &&
        timing->position.z==out->position.z) *out=*timing;
    return 1;
}
int redstone_timing_selftest(void) {
    int failures=0;
    world_begin(461796);
    generate();
    memset(inventory,0,sizeof inventory);
    inventory[0]=(ItemStack){REDSTONE_DUST,4,0,0,0};
    inventory[1]=(ItemStack){STICK,2,0,0,0};
    CHECK_TIMED(craft_take((int[]){0,-1,1,-1},2) && craft_take((int[]){0,-1,1,-1},2));
    CHECK_TIMED(inventory[2].type==REDSTONE_TORCH_ITEM && inventory[2].count==2);
    inventory[1]=(ItemStack){STONE,3,0,0,0};
    CHECK_TIMED(craft_take((int[]){2,0,2,1,1,1,-1,-1,-1},3) && inventory[1].type==REPEATER_ITEM);
    for (int x=-6;x<=12;++x) for (int z=-15;z<=9;++z) CHECK_TIMED(set_block(x,Y-1,z,STONE));
    CHECK_TIMED(set_block(0,Y,0,STONE) && redstone_place_torch((CircuitPos){1,Y,0},(CircuitPos){0,Y,0}) &&
        redstone_place_lever((CircuitPos){0,Y+1,0},(CircuitPos){0,Y,0},0) && gate_place(2,Y,0,EAST));
    CHECK_TIMED(!redstone_place_torch((CircuitPos){0,Y-2,0},(CircuitPos){0,Y-1,0}));
    ticks(1);
    CHECK_TIMED(torch(1,0) && gate_open(2,0) && !redstone_receives(0,Y,0));
    CHECK_TIMED(redstone_toggle_lever(0,Y+1,0)==2);
    ticks(2);
    CHECK_TIMED(torch(1,0) && gate_open(2,0));
    ticks(1);
    CHECK_TIMED(!torch(1,0) && !gate_open(2,0));
    CHECK_TIMED(redstone_toggle_lever(0,Y+1,0)==1);
    ticks(3);
    CHECK_TIMED(torch(1,0) && gate_open(2,0));

    /* Remove the only edited circuit component in the lever's chunk.
       The torch, three cells away, must still observe the change. */
    CHECK_TIMED(set_block(-18,Y,5,STONE) && set_block(-17,Y,5,STONE) &&
        redstone_place_lever((CircuitPos){-19,Y,5},(CircuitPos){-18,Y,5},0) &&
        redstone_place_torch((CircuitPos){-16,Y,5},(CircuitPos){-17,Y,5}));
    ticks(1);
    CHECK_TIMED(torch(-16,5) && redstone_toggle_lever(-19,Y,5)==2);
    ticks(3);
    CHECK_TIMED(!torch(-16,5) && set_block(-19,Y,5,AIR));
    ticks(3);
    CHECK_TIMED(torch(-16,5));

    CHECK_TIMED(floor_lever(-1,-8) && redstone_place_repeater(0,Y,-8,EAST) &&
        redstone_place_wire(1,Y,-8) && redstone_place_wire(0,Y,-9) && gate_place(2,Y,-8,EAST));
    CHECK_TIMED(redstone_next_delay(0,Y,-8)==2 && redstone_next_delay(0,Y,-8)==3 &&
        redstone_next_delay(0,Y,-8)==4);
    CHECK_TIMED(redstone_toggle_lever(-1,Y,-8)==2);
    ticks(8);
    CHECK_TIMED(!repeater(0,-8) && !gate_open(2,-8));
    ticks(1);
    CHECK_TIMED(repeater(0,-8) && gate_open(2,-8) && redstone_wire_power(1,Y,-8)==15 &&
        !redstone_wire_power(0,Y,-9));
    CHECK_TIMED(redstone_toggle_lever(-1,Y,-8)==1);
    ticks(9);
    CHECK_TIMED(!repeater(0,-8) && !gate_open(2,-8));
    CHECK_TIMED(redstone_toggle_lever(-1,Y,-8)==2); ticks(1);
    CHECK_TIMED(redstone_toggle_lever(-1,Y,-8)==1); ticks(8);
    CHECK_TIMED(repeater(0,-8)); /* Delayed short input became a full-width pulse. */
    ticks(7);
    CHECK_TIMED(repeater(0,-8));
    ticks(1);
    CHECK_TIMED(!repeater(0,-8) && redstone_next_delay(0,Y,-8)==1);

    CHECK_TIMED(floor_lever(-1,-12) && floor_lever(0,-14) &&
        redstone_place_repeater(0,Y,-12,EAST) && redstone_place_repeater(0,Y,-13,SOUTH));
    CHECK_TIMED(redstone_toggle_lever(-1,Y,-12)==2); ticks(3);
    CHECK_TIMED(repeater(0,-12));
    CHECK_TIMED(redstone_toggle_lever(0,Y,-14)==2); ticks(3);
    CHECK_TIMED(repeater(0,-13) && redstone_repeater_locked(0,Y,-12));
    CHECK_TIMED(redstone_toggle_lever(-1,Y,-12)==1); ticks(11);
    CHECK_TIMED(repeater(0,-12));
    CHECK_TIMED(redstone_toggle_lever(0,Y,-14)==1); ticks(3);
    CHECK_TIMED(!redstone_repeater_locked(0,Y,-12) && repeater(0,-12));
    ticks(2);
    CHECK_TIMED(!repeater(0,-12));

    /* Route the torch's own output around the corner, into its anchor.
       See several authentic edges, then burnout and delayed recovery. */
    CHECK_TIMED(set_block(7,Y,0,STONE) && redstone_place_torch((CircuitPos){8,Y,0},(CircuitPos){7,Y,0}));
    const int loop[][2]={{9,0},{9,1},{9,2},{8,2},{7,2},{7,1}};
    for (unsigned i=0;i<sizeof loop/sizeof *loop;++i) CHECK_TIMED(redstone_place_wire(loop[i][0],Y,loop[i][1]));
    RedstoneTiming burned={{8,Y,0},0,0,0,0,{0},0};
    int crossings=0,last_on=torch(8,0);
    for (int i=0;i<80 && !burned.cooldown;++i) {
        ticks(1);
        int on=torch(8,0);
        crossings+=on!=last_on; last_on=on;
        redstone_visit_timing(OVERWORLD,capture,&burned);
    }
    CHECK_TIMED(crossings>=15 && !torch(8,0) && burned.count==8 && burned.cooldown==160);
    CHECK_TIMED(set_block(7,Y,1,AIR)); /* Break feedback; keep the torch's anchor. */
    ticks(159);
    CHECK_TIMED(!torch(8,0));
    ticks(2);
    CHECK_TIMED(!torch(8,0));
    ticks(1);
    CHECK_TIMED(torch(8,0));
    CHECK_TIMED(set_block(7,Y,0,AIR)); ticks(2);
    CHECK_TIMED(block(8,Y,0)==AIR && drops[0].item.type==REDSTONE_TORCH_ITEM);
    printf("TIMED REDSTONE TEST recipes/inversion/pulse/delay/direction/locking/clock/burnout failures=%d\n",failures);
    return failures;
}
