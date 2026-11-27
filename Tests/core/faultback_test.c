#include "../game/actors.h"
#include "../game/drops.h"
#include "../game/interaction.h"
#include "../game/portals.h"
#include "../game/sound.h"
#include "../game/spawn_eggs.h"
#include "../world/pressure_layout.h"
#include <stdio.h>
#include <string.h>

enum { Y=PRESSURE_FLOOR+1 };
static const Vec3 home={13.5f,Y+.018f,-7.5f};
static int failures;
#define CHECK(test) do { if (!(test)) { printf("FAULTBACK line %d: %s\n",__LINE__,#test); ++failures; } } while (0)

static void advance(int steps) { while (steps--) actors_step(.05f); }
static void fresh(void) {
    CHECK(world_begin_generated(66700,GENERATOR_CURRENT) && world_select_dimension(NETHER));
    CHECK(game_set_difficulty(NORMAL));
    actors_reset(); drops_reset(); sound_clear(); portals_reset();
    player_reset_rules(SURVIVAL);
    px=9.5f; py=Y+.018f; pz=-7.5f; yaw=1.5707963f; pitch=-.24f;
    selected_slot=0;
    memset(inventory,0,sizeof inventory);
}
static int spawn(void) { return actors_restore(NETHER,(ActorRecord){home,-1.57f,ACTOR_FAULTBACK,FAULTBACK_HEALTH,0}); }
static int phase(void) { return actors_faultback_phase(NETHER,0); }
int faultback_selftest(void) {
    failures=0;
    fresh();
    CHECK(!actors_restore(OVERWORLD,(ActorRecord){home,0,ACTOR_FAULTBACK,FAULTBACK_HEALTH,0}));
    CHECK(spawn());
    advance(1);
    CHECK(phase()==FAULTBACK_WINDUP && player_rules.health==MAX_HEALTH && sound_active());
    inventory[0]=(ItemStack){SURVEY_TAPPER,1,0,0,0};
    Interaction report=game_tap();
    CHECK(report.message && strstr(report.message,"侧移") && inventory[0].count==1);
    advance(36);
    CHECK(phase()==FAULTBACK_RECOVER && player_rules.health==MAX_HEALTH-5 &&
        actors_in(NETHER)[0].feet.x>px && actors_in(NETHER)[0].feet.x<home.x-1.f);
    advance(17);
    CHECK(player_rules.health==MAX_HEALTH-5 && !actors_fireballs()[0].seconds);

    fresh(); CHECK(spawn()); advance(1);
    pz=-5.30f; /* The saved lane must not turn toward a moving player. */
    advance(40);
    CHECK(phase()==FAULTBACK_RECOVER && player_rules.health==MAX_HEALTH &&
        actors_in(NETHER)[0].feet.z==home.z && actors_in(NETHER)[0].feet.x<home.x-2.f);

    fresh(); CHECK(spawn());
    CHECK(set_block(11,Y,-8,STONE));
    advance(6);
    CHECK(phase()==FAULTBACK_IDLE);
    CHECK(set_block(11,Y,-8,AIR));
    advance(5); CHECK(phase()==FAULTBACK_WINDUP);
    CHECK(set_block(11,Y,-8,STONE));
    advance(36);
    CHECK(phase()==FAULTBACK_RECOVER && actors_in(NETHER)[0].feet.x>12.7f && player_rules.health==MAX_HEALTH);

    fresh(); CHECK(spawn()); advance(1);
    const Hit west={.hit=1,.x=-PRESSURE_VALVE_X,.y=Y,.z=PRESSURE_VALVE_Z},
        east={.hit=1,.x=PRESSURE_VALVE_X,.y=Y,.z=PRESSURE_VALVE_Z};
    CHECK(block(west.x,west.y,west.z)==PRESSURE_CLOSED);
    CHECK(portals_use_aquifer(west) && (portals_pressure_relief()&1) && phase()==FAULTBACK_WINDUP);
    CHECK(portals_use_aquifer(east) && phase()==FAULTBACK_CALM);
    advance(45);
    CHECK(phase()==FAULTBACK_CALM && actors_in(NETHER)[0].feet.x==home.x && player_rules.health==MAX_HEALTH &&
        !actors_hostile_near(home,.9f,1.f));
    CHECK(actors_hit_arrow((Vec3){home.x,home.y+.6f,home.z},FAULTBACK_HEALTH) &&
        !actors_in(NETHER)[0].type && drops[0].item.type==SINTERED_TILE_ITEM && drops[0].item.count==1);
    CHECK(spawn_egg_def(EGG_FAULTBACK) && spawn_egg_at(EGG_FAULTBACK,home));
    CHECK(game_set_difficulty(PEACEFUL));
    actors_apply_difficulty();
    CHECK(!actors_in(NETHER)[0].type && !spawn_egg_at(EGG_FAULTBACK,home));

    fresh(); px=.5f; pz=-5.5f;
    (void)block(-13,Y,-9); (void)block(13,Y,-9);
    advance(50);
    int wild=0;
    for (int i=0;i<ACTOR_CAPACITY;++i) if (actors_in(NETHER)[i].type==ACTOR_FAULTBACK) ++wild;
    CHECK(wild==2);
    world_reset();
    printf("FAULTBACK TEST habitat/tell/lock/dodge/cover/contact/relief/ceramic/egg failures=%d\n",failures);
    return failures;
}
