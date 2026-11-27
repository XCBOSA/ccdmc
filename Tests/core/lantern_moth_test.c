#include "../game/actors.h"
#include "../game/blast.h"
#include "../game/spawn_eggs.h"
#include "../game/drops.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { ++failures; printf("NURSERY TEST failed line %d: %s\n",__LINE__,#c); } } while (0)
enum { Y=185 };
static const Hit mark={1,0,Y,0,0,Y+1,0};

static void step(unsigned ticks) {
    for (unsigned i=0;i<ticks;++i) actors_step(.05f);
}
static unsigned moths(void) {
    unsigned n=0;
    for (int i=0;i<ACTOR_CAPACITY;++i) n+=actors_in(OVERWORLD)[i].type==ACTOR_LANTERN_MOTH;
    return n;
}
static int contains(const char *s,const char *part) { return s && strstr(s,part)!=NULL; }
static int beds(int near,int far,int wet) {
    const BlockEdit e[]={{1,Y-1,0,FARM_BASE+(wet ? 7 : 0)},{1,Y,0,near},
        {3,Y-1,0,FARM_BASE+(wet ? 7 : 0)},{3,Y,0,far}};
    return world_set_blocks(e,sizeof e/sizeof *e);
}
int lantern_moth_selftest(void) {
    failures=0;
    GameDifficulty was=game_selected_difficulty();
    world_begin(491923); generate(); player_reset_rules(SURVIVAL);
    CHECK(game_set_difficulty(PEACEFUL));
    px=.5f; py=Y+.015f; pz=3.5f; player_rules.fall_origin=py;
    player_rules.day_tick=16000;
    CHECK(set_block(0,Y-1,0,STONE) && set_block(-1,Y-1,0,DIRT) && set_block(0,Y,0,RESONANCE_MARKER));
    CHECK(contains(actors_moth_station(mark),"旁边种花") && !moths());
    CHECK(set_block(-1,Y,0,DANDELION));
    CHECK(beds(WHEAT_BASE+2,CARROT_BASE,1));
    player_rules.day_tick=400;
    CHECK(contains(actors_moth_station(mark),"入夜后") && !moths());
    player_rules.day_tick=16000;
    CHECK(contains(actors_moth_station(mark),"两格") && moths()==1);
    CHECK(contains(actors_moth_station(mark),"已就位") && moths()==1);
    step(52);
    CHECK(world_cell(1,Y,0)==WHEAT_BASE+3 && world_cell(3,Y,0)==CARROT_BASE &&
        actors_moth_pollinating(OVERWORLD,0));
    CHECK(beds(WHEAT_BASE+7,CARROT_BASE,1));
    CHECK(set_block(-1,Y,0,POPPY) && set_block(2,Y,0,GLASS) && set_block(2,Y+1,0,GLASS));
    step(108);
    CHECK(world_cell(3,Y,0)==CARROT_BASE);
    CHECK(set_block(2,Y,0,AIR) && set_block(2,Y+1,0,AIR));
    step(108);
    CHECK(world_cell(3,Y,0)==CARROT_BASE+1);
    CHECK(beds(WHEAT_BASE+7,CARROT_BASE,0));
    step(102);
    CHECK(world_cell(3,Y,0)==CARROT_BASE);
    CHECK(beds(WHEAT_BASE+7,CARROT_BASE,1));
    player_rules.day_tick=500;
    step(125);
    CHECK(world_cell(3,Y,0)==CARROT_BASE && actors_moth_wing(OVERWORLD,0)==0.f &&
        contains(actors_moth_station(mark),"歇息"));
    player_rules.day_tick=16000;
    CHECK(set_block(-1,Y,0,AIR));
    step(125);
    CHECK(world_cell(3,Y,0)==CARROT_BASE && moths()==1);
    CHECK(set_block(-1,Y,0,POPPY));
    step(78);
    CHECK(world_cell(3,Y,0)==CARROT_BASE+1);
    CHECK(actors_in(OVERWORLD)[0].health==LANTERN_MOTH_HEALTH &&
        player_rules.health==20);
    const ActorFireball *p=actors_fireballs();
    for (int i=0;i<ACTOR_FIREBALLS;++i) CHECK(!(p[i].seconds>0.f));
    drops_reset();
    CHECK(actors_hit_arrow((Vec3){.5f,Y+1.36f,.5f},LANTERN_MOTH_HEALTH) && !moths());
    for (int i=0;i<MAXDROP;++i) CHECK(!drops[i].item.count);
    CHECK(spawn_egg_at(EGG_LANTERN_MOTH,(Vec3){.5f,Y+.05f,1.5f}) && moths()==1);
    CHECK(world_select_dimension(NETHER) && !actors_spawn_egg(ACTOR_LANTERN_MOTH,(Vec3){0,Y,0},0.f));
    CHECK(world_select_dimension(OVERWORLD) && game_set_difficulty(was));
    printf("NURSERY TEST flowers, range, wall, hydration, daylight, no duplication and passive eggs failures=%d\n",failures);
    return failures;
}
