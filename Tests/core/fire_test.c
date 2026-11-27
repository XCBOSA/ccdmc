#include "../game/actors.h"
#include "../game/blast.h"
#include "../game/droppers.h"
#include "../game/drops.h"
#include "../game/fire.h"
#include "../game/interaction.h"
#include "../world/light.h"
#include "../world/redstone.h"
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { printf("FIRE TEST failed line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
enum { Y=150 };
static void ticks(int x,int z,int n) { while (n--) fires_step(x,z,1); }
static int snapshot(const FireCell *s,void *ptr) {
    FireCell *out=(FireCell *)ptr;
    if (s->x==out->x && s->y==out->y && s->z==out->z) *out=*s;
    return 1;
}
static int count_fire(const FireCell *s,void *ptr) { (void)s; ++*(int *)ptr; return 1; }
int fire_selftest(void) {
    int failures=0;
    world_begin(937162); generate(); player_reset_rules(SURVIVAL); inventory_reset();
    CHECK(fire_use_igniter(-19,Y+5,2)==IGNITE_BLOCKED && world_cell(-19,Y+5,2)==AIR);
    CHECK(set_block(-17,Y,-1,STONE) && set_block(-17,Y-1,0,STONE));
    px=-16.5f; py=149.01f; pz=2.6f; yaw=0; pitch=0; selected_slot=0;
    inventory[0]=(ItemStack){FLINT_AND_STEEL,1,0,0,0};
    CHECK(game_tap().message && world_cell(-17,Y,0)==FIRE_CELL && inventory[0].damage==1);
    CHECK(!collides(-16.5f,Y+.01f,.5f) && block_light_at(-17,Y,0)==15);
    CHECK(mine() && world_cell(-17,Y,0)==AIR && block(-17,Y,-1)==STONE);
    CHECK(fire_ignite(-17,Y,0));
    CHECK(set_block(-17,Y-1,0,AIR));
    ticks(-17,0,1);
    CHECK(world_cell(-17,Y,0)==AIR);

    CHECK(set_block(-17,Y-1,5,STONE) && set_block(-16,Y-1,5,STONE) &&
        fire_ignite(-16,Y,5) && set_block(-17,Y,5,WATER));
    for (int i=0;i<8;++i) water_step();
    CHECK(water_strength(world_cell(-16,Y,5)));
    int count=0;
    CHECK(fire_visit(OVERWORLD,count_fire,&count) && count==0);

    CHECK(set_block(-17,Y-1,11,NETHERRACK) && set_block(-16,Y-1,11,STONE) &&
        set_block(-16,Y,11,WOOL) && set_block(-17,Y,12,TNT_BLOCK) && fire_ignite(-17,Y,11));
    FireCell before={-17,Y,11,0,0,0},after=before;
    CHECK(fire_visit(OVERWORLD,snapshot,&before));
    ticks(500,500,60);
    CHECK(fire_visit(OVERWORLD,snapshot,&after) && after.attempts==before.attempts && after.delay==0);
    CHECK(!fire_restore(&before));
    ticks(-17,11,800);
    CHECK(world_cell(-17,Y,11)==FIRE_CELL && world_cell(-16,Y,11)!=WOOL && world_cell(-17,Y,12)==AIR);
    size_t primed=0;
    const PrimedBlast *t=blasts_in(OVERWORLD,&primed);
    CHECK(primed==1 && t[0].kind==BLAST_TNT);
    blasts_reset();

    CHECK(set_block(2,Y-1,10,STONE) && fire_ignite(2,Y,10));
    ticks(2,10,190);
    CHECK(world_cell(2,Y,10)==AIR);
    CHECK(world_select_dimension(NETHER) && world_cell(-17,Y,11)!=FIRE_CELL &&
        world_select_dimension(OVERWORLD) && world_cell(-17,Y,11)==FIRE_CELL);

    CHECK(set_block(8,Y-1,6,NETHERRACK) && ejector_place(7,Y,6,EAST,DISPENSER));
    inventory[0]=(ItemStack){FLINT_AND_STEEL,1,0,0,0};
    CHECK(dropper_move(7,Y,6,DROPPER_SLOTS,0,0) && set_block(6,Y,6,REDSTONE_BLOCK));
    for (int i=0;i<5;++i) redstone_step(7,6);
    CHECK(world_cell(8,Y,6)==FIRE_CELL && dropper_at(7,Y,6)->slots[0].damage==1);

    for (int x=20;x<=24;++x) CHECK(set_block(x,Y-1,4,NETHERRACK));
    CHECK(fire_ignite(20,Y,4) && set_block(23,Y,4,WATER));
    px=20.5f; py=Y+.01f; pz=4.5f; vy=0;
    game_step(.05f,0,0,0,0);
    CHECK(player_rules.health==MAX_HEALTH-1 && player_burn_state().remaining>0);
    px=22.5f; py=Y+.01f;
    for (int j=0;j<21;++j) game_step(.05f,0,0,0,0);
    CHECK(player_burn_state().remaining>0 && player_rules.health<MAX_HEALTH-1);
    px=23.5f; py=Y+.01f;
    game_step(.05f,0,0,0,0);
    CHECK(!player_burn_state().remaining);
    CHECK(player_restore_burn((PlayerBurn){40,12}));
    player_effects.fire=200; px=20.5f; py=Y+.01f;
    int health=player_rules.health;
    game_step(.05f,0,0,0,0);
    CHECK(!player_burn_state().remaining && player_rules.health==health);

    CHECK(set_block(30,Y-1,4,NETHERRACK) && fire_ignite(30,Y,4));
    CHECK(actors_restore(OVERWORLD,(ActorRecord){{30.5f,Y+.01f,4.5f},0,ACTOR_PIG,10,0}));
    actors_step(.05f);
    CHECK(actors_on_fire(OVERWORLD,0));
    Vec3 pig=actors_in(OVERWORLD)[0].feet; pig.y+=.4f;
    CHECK(actors_hit_arrow(pig,20));
    int cooked=0;
    for (int i=0;i<MAXDROP;++i) if (drops[i].item.type==COOKED_PORK) cooked+=drops[i].item.count;
    CHECK(cooked>0);
    world_reset(); drops_reset();
    printf("FIRE TEST flint/picking/light/support/water/seam/burnout/eternal/TNT/dormant/dispenser/player/armor/cooked-drop failures=%d\n",failures);
    return failures;
}
