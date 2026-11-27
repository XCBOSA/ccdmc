#include "world_test.h"
#include "../game/actors.h"
#include "../game/furnace.h"
#include "../game/heat_bench.h"
#include "../game/interaction.h"
#include "../game/item_icons.h"
#include "../game/portals.h"
#include "../world/pressure_layout.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int sounds_like(const char *note,const char *needle) { return note && strstr(note,needle); }
static int count_moles(Dimension d) {
    int n=0;
    for (int i=0;i<ACTOR_CAPACITY;++i) n+=actors_in(d)[i].type==ACTOR_KILN_MOLE;
    return n;
}

int kiln_mole_selftest(void) {
    int failures=0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"KILN MOLE line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
    enum { Y=165 };
    const Hit source={1,4,Y,4,5,Y,4},sink={1,4,Y,12,5,Y,12};
    CHECK(world_begin_profile(731,GENERATOR_CURRENT,WORLD_SUPERFLAT));
    generate();
    player_reset_rules(SURVIVAL); selected_slot=0; memset(inventory,0,sizeof inventory);
    px=5.5f; py=Y+.015f; pz=6.5f; yaw=0; pitch=atan2f(-1.28f,2.f);
    for (int z=3;z<=12;++z) for (int x=3;x<=6;++x) CHECK(set_block(x,Y-1,z,STONE));
    CHECK(set_block(source.x,Y,source.z,HEAT_BENCH_EMPTY) &&
        set_block(sink.x,Y,sink.z,HEAT_BENCH_EMPTY) && set_block(3,Y,4,FURNACE));
    Furnace fire={.x=3,.y=Y,.z=4,.burn=365,.fuel_duration=1600};
    CHECK(furnace_restore(&fire) && sounds_like(heat_bench_use(source),"灰窑鼹来访"));
    CHECK(furnace_at(3,Y,4) && furnace_at(3,Y,4)->burn==5 && count_moles(OVERWORLD)==1 &&
        !actors_visit_heat_bench(source) && !actors_hostile_near((Vec3){px,py,pz},5,3));
    CHECK(world_cell(source.x,Y,source.z)==HEAT_BENCH_EMPTY+3);
    CHECK(sounds_like(game_tap().message,"拿黏土球"));
    for (int i=0;i<SLOT_COUNT;++i) inventory[i]=(ItemStack){STONE,64,0,0,0};
    inventory[0]=(ItemStack){CLAY_BALL,2,0,0,0};
    CHECK(sounds_like(game_tap().message,"背包没有") && inventory[0].count==2 &&
        world_cell(source.x,Y,source.z)==HEAT_BENCH_EMPTY+3);
    CHECK(set_block(6,Y,4,HEAT_BENCH_EMPTY) &&
        world_set_heat_bench_level(6,Y,4,HEAT_BENCH_EMPTY,HEAT_BENCH_EMPTY+1));
    CHECK(sounds_like(game_tap().message,"只能有一台") &&
        world_cell(source.x,Y,source.z)==HEAT_BENCH_EMPTY+3 && inventory[0].count==2);
    CHECK(set_block(6,Y,4,AIR));
    inventory[0].count=1;
    CHECK(sounds_like(game_tap().message,"压制了暖陶芯") && inventory[0].type==WARM_CORE &&
        world_cell(source.x,Y,source.z)==HEAT_BENCH_EMPTY+2);
    CHECK(world_set_heat_bench_level(sink.x,Y,sink.z,HEAT_BENCH_EMPTY,HEAT_BENCH_FULL));
    CHECK(sounds_like(heat_bench_use(sink),"回温台已满") && inventory[0].type==WARM_CORE);
    CHECK(world_set_heat_bench_level(sink.x,Y,sink.z,HEAT_BENCH_FULL,HEAT_BENCH_EMPTY));
    CHECK(sounds_like(heat_bench_use(sink),"放热") && inventory[0].type==CERAMIC_CORE &&
        world_cell(sink.x,Y,sink.z)==HEAT_BENCH_EMPTY+1);
    CHECK(sounds_like(heat_bench_use(source),"陶芯已充热") && inventory[0].type==WARM_CORE &&
        world_cell(source.x,Y,source.z)==HEAT_BENCH_EMPTY+1);
    CHECK(sounds_like(heat_bench_use(sink),"放热") && world_cell(sink.x,Y,sink.z)==HEAT_BENCH_EMPTY+2);
    inventory[0]=(ItemStack){IRON_PICKAXE,1,17,0,0};
    CHECK(sounds_like(heat_bench_use(sink),"修复 12 点") && inventory[0].damage==5 &&
        world_cell(sink.x,Y,sink.z)==HEAT_BENCH_EMPTY+1);
    ItemIcon icon;
    CHECK(item_icon(WARM_CORE,&icon) && icon.path[3] && icon.rgb[3]!=0x604f54);
    actors_step(.05f);
    CHECK(actors_mole_warm(OVERWORLD,0));
    CHECK(world_set_heat_bench_level(source.x,Y,source.z,HEAT_BENCH_EMPTY+1,HEAT_BENCH_EMPTY));
    for (int i=0;i<9;++i) actors_step(.05f);
    CHECK(!actors_mole_warm(OVERWORLD,0));

    CHECK(world_begin_generated(731,GENERATOR_CURRENT));
    generate();
    CHECK(world_select_dimension(NETHER));
    px=-4.5f; py=PRESSURE_FLOOR+1.015f; pz=-8.5f; player_reset_rules(SURVIVAL);
    (void)world_cell(-5,PRESSURE_FLOOR,PRESSURE_KILN_Z);
    for (int i=0;i<24;++i) actors_step(.1f);
    CHECK(count_moles(NETHER)==0);
    CHECK(world_restore_edit(-PRESSURE_VALVE_X,PRESSURE_FLOOR+1,PRESSURE_VALVE_Z,PRESSURE_OPEN) &&
        world_restore_edit(PRESSURE_VALVE_X,PRESSURE_FLOOR+1,PRESSURE_VALVE_Z,PRESSURE_OPEN));
    portals_reload_pressure_relief();
    for (int i=0;i<44;++i) actors_step(.1f);
    CHECK(count_moles(NETHER)==1 && portals_pressure_relief()==3u);
    printf("KILN MOLE bench visitor/migration path/conserved heat/full bag/repair/geothermal habitat failures=%d\n",failures);
    actors_reset(); world_reset();
#undef CHECK
    return failures;
}
