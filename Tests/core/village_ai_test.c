#include "world_test.h"
#include "../game/actors.h"
#include "../game/village_routes.h"
#include "../game/villagers.h"
#include "../world/redstone.h"
#include "../world/villages.h"
#include <math.h>
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { printf("VILLAGE AI TEST failed line %d: %s\n",__LINE__,#c); ++bad; } } while (0)
typedef struct { VillagePlan plan; int x,y,z,dir,actor; } Cottage;

static int door_open(Cottage c) {
    int raw=world_cell(c.x,c.y,c.z);
    return door_cell(raw) && (door_bits(raw)&DOOR_OPEN) && world_cell(c.x,c.y+1,c.z)==door_peer(raw);
}
static void observe(Cottage c) {
    int side=(c.dir+1)&3;
    px=c.x+.5f+3*rail_dx(c.dir)+2*rail_dx(side);
    pz=c.z+.5f+3*rail_dz(c.dir)+2*rail_dz(side);
    py=c.y+.01f; vy=0.f;
}
static int cottage(Cottage *c) {
    if (!world_begin_generated(992382978u,GENERATOR_DOORS)) return 0;
    generate();
    if (!village_plan_for(144,-97,&c->plan) || !village_home_door(&c->plan,0,&c->x,&c->z,&c->dir)) return 0;
    c->y=c->plan.floor+1; c->actor=-1;
    for (int x=-12;x<=12;x+=12) for (int z=-12;z<=12;z+=12) (void)world_cell(c->x+x,c->y,c->z+z);
    observe(*c);
    villagers_step(20);
    const ActorRecord *records=actors_in(OVERWORLD);
    for (int i=0;i<ACTOR_CAPACITY;++i) if (records[i].type==ACTOR_VILLAGER && records[i].traits==VILLAGER_FARMER) {
        c->actor=i; break;
    }
    return c->actor>=0 && block(c->x,c->y,c->z)==WOOD_DOOR;
}
static float side_of_door(Cottage c) {
    Vec3 p=actors_in(OVERWORLD)[c.actor].feet;
    return (p.x-c.x-.5f)*rail_dx(c.dir)+(p.z-c.z-.5f)*rail_dz(c.dir);
}
static int tick(Cottage c,int n) {
    int saw=0;
    for (int i=0;i<n;++i) { actors_step(.05f); saw|=door_open(c); }
    return saw;
}
static int wait_open(Cottage c) {
    for (int i=0;i<90;++i) { actors_step(.05f); if (door_open(c)) return 1; }
    return 0;
}
int village_ai_selftest(void) {
    int bad=0;
    /* Test an actual detour and prove the search won't request an unloaded
       adjacent chunk even outside interactive play. */
    world_begin(192);
    VillageWalk w={10,10,160,12,10};
    for (int x=8;x<=14;++x) for (int z=8;z<=12;++z) CHECK(set_block(x,159,z,STONE));
    CHECK(set_block(11,160,10,STONE));
    VillageStep path[VILLAGE_ROUTE_STEPS];
    int n=village_route(&w,10,10,12,10,path);
    CHECK(n==4 && path[n-1].x==2 && path[n-1].z==0);
    CHECK(!chunk_cached_view(1,0).cells);
    CHECK(!village_route(&w,10,10,17,10,path) && !chunk_cached_view(1,0).cells);

    Cottage c;
    if (!cottage(&c)) return bad+1;
    CHECK(side_of_door(c)>.5f && !door_open(c));
    /* Let the full game loop cross dusk while the resident is still a bit
       off-centre from daytime wandering. Open-leaf clearance is tight. */
    player_rules.day_tick=11998;
    int dusk_open=0;
    for (int i=0;i<100;++i) { game_step(.05f,0,0,0,0); dusk_open|=door_open(c); }
    CHECK(dusk_open && side_of_door(c)<-.65f && !door_open(c));
    float in=side_of_door(c);
    tick(c,35);
    CHECK(fabsf(side_of_door(c)-in)<.08f);
    player_rules.day_tick=2000;
    CHECK(tick(c,110) && side_of_door(c)>.7f && !door_open(c));

    if (!cottage(&c)) return bad+1;
    player_rules.day_tick=13000;
    CHECK(set_block(c.x-2*rail_dx(c.dir),c.y,c.z-2*rail_dz(c.dir),CRAFTING_TABLE));
    CHECK(tick(c,140) && side_of_door(c)<-.65f && !door_open(c));

    /* A doorway occupant keeps the lease open, then the villager
       shuts it after everyone can pass. Player toggles take ownership. */
    if (!cottage(&c)) return bad+1;
    player_rules.day_tick=13000;
    CHECK(wait_open(c));
    px=c.x+.5f; py=c.y+.01f; pz=c.z+.5f;
    tick(c,40);
    CHECK(door_open(c));
    observe(c);
    tick(c,100);
    CHECK(side_of_door(c)<-.65f && !door_open(c));

    if (!cottage(&c)) return bad+1;
    player_rules.day_tick=13000;
    CHECK(wait_open(c) && door_toggle(c.x,c.y,c.z) && door_toggle(c.x,c.y,c.z));
    tick(c,120);
    CHECK(side_of_door(c)<-.65f && door_open(c));

    if (!cottage(&c)) return bad+1;
    CHECK(set_block(c.x,c.y,c.z,AIR) && door_place(c.x,c.y,c.z,c.dir,0,1));
    player_rules.day_tick=13000;
    CHECK(!tick(c,100) && side_of_door(c)>.5f);
    int side=(c.dir+1)&3,x=c.x+rail_dx(side),z=c.z+rail_dz(side);
    CHECK(set_block(x,c.y+1,z,REDSTONE_BLOCK));
    for (int i=0;i<8;++i) redstone_step(c.x,c.z);
    CHECK(door_open(c));
    tick(c,125);
    CHECK(side_of_door(c)<-.65f && door_open(c));
    CHECK(set_block(x,c.y+1,z,AIR));
    for (int i=0;i<8;++i) redstone_step(c.x,c.z);
    CHECK(!door_open(c));
    printf("VILLAGE AI TEST detour/no-load/return/stay/leave/occupied/manual/iron/redstone failures=%d\n",bad);
    return bad;
}
