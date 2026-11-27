#include "dungeon.h"
#include "actors.h"
#include "../world/light.h"
#include <math.h>
#include <string.h>

static DungeonClock clocks[DUNGEON_CLOCKS];
static uint32_t use_order[DUNGEON_CLOCKS], sequence;

static uint32_t draw(uint32_t *state) {
    uint32_t r=*state;
    r^=r<<13; r^=r>>17; r^=r<<5;
    return *state=r;
}
void dungeon_reset(void) {
    memset(clocks,0,sizeof clocks);
    memset(use_order,0,sizeof use_order);
    sequence=0;
}
const DungeonClock *dungeon_clocks(void) { return clocks; }
int dungeon_restore_clock(DungeonClock clock) {
    if (!inworld(clock.x,clock.y,clock.z) || !clock.random || clock.delay>DUNGEON_MAX_DELAY) return 0;
    int free_slot=-1;
    for (int i=0;i<DUNGEON_CLOCKS;++i) {
        const DungeonClock *c=&clocks[i];
        if (!c->random) { if (free_slot<0) free_slot=i; }
        else if (c->x==clock.x && c->y==clock.y && c->z==clock.z) return 0;
    }
    if (free_slot<0) return 0;
    clocks[free_slot]=clock;
    use_order[free_slot]=++sequence;
    return 1;
}
static DungeonClock *clock_at(int x,int y,int z) {
    int oldest=0;
    for (int i=0;i<DUNGEON_CLOCKS;++i) {
        DungeonClock *c=&clocks[i];
        if (c->random && c->x==x && c->y==y && c->z==z) {
            use_order[i]=++sequence;
            return c;
        }
        if (use_order[i]<use_order[oldest]) oldest=i;
    }
    unsigned seed=hash2(x,z)^((unsigned)y*UINT32_C(0x9e3779b9));
    clocks[oldest]=(DungeonClock){x,y,z,seed ? seed : 1u,DUNGEON_INITIAL_DELAY};
    use_order[oldest]=++sequence;
    return &clocks[oldest];
}
static int near_cage(Vec3 point,Vec3 cage) {
    return fabsf(point.x-cage.x)<=4.5f && fabsf(point.z-cage.z)<=4.5f && fabsf(point.y-cage.y)<=2.f;
}
typedef struct { unsigned ticks; Vec3 player; } SpawnerStep;
static int tick_cage(void *opaque,int x,int y,int z,int raw) {
    const SpawnerStep *step=(const SpawnerStep *)opaque;
    Vec3 center={x+.5f,y+.5f,z+.5f};
    float dx=center.x-step->player.x,dy=center.y-step->player.y-.9f,dz=center.z-step->player.z;
    if (dx*dx+dy*dy+dz*dz>16.f*16.f) return 1;
    DungeonClock *c=clock_at(x,y,z);
    if (c->delay>step->ticks) { c->delay=(uint16_t)(c->delay-step->ticks); return 1; }
    int type=raw==MINE_CAVE_SPIDER_CELL ? ACTOR_CAVE_SPIDER : raw==DUNGEON_ZOMBIE_CELL ? ACTOR_ZOMBIE :
        raw==DUNGEON_SKELETON_CELL ? ACTOR_SKELETON : ACTOR_SPIDER;
    int nearby=0,added=0;
    const ActorRecord *list=actors_in(OVERWORLD);
    for (int i=0;i<ACTOR_CAPACITY;++i)
        if (list[i].type==type && near_cage(list[i].feet,center)) ++nearby;
    if (nearby<DUNGEON_MOB_LIMIT) for (int attempt=0;attempt<4 && nearby<DUNGEON_MOB_LIMIT;++attempt) {
        uint32_t r=draw(&c->random);
        /* Two independent samples on each axis favour positions near the cage. */
        int tx=x+(int)(r&3u)-(int)((r>>2)&3u), tz=z+(int)((r>>4)&3u)-(int)((r>>6)&3u);
        int ty=y+(int)((r>>8)%3u)-1, ground,space;
        if (!world_resident_cell(tx,ty-1,tz,&ground) || !solid(cell_type(ground)) ||
            !world_resident_cell(tx,ty,tz,&space) ||
            (space!=AIR && !(type==ACTOR_CAVE_SPIDER && space==COBWEB))) continue;
        int light=block_light_at_resident(tx,ty,tz);
        if (light<0 || light>7 || (player_rules.day_tick<12000 && light_has_open_sky(tx,ty,tz))) continue;
        if (actors_spawn_dungeon(type,(Vec3){tx+.5f,ty+.01f,tz+.5f},((int)(r>>16)-32768)*.0000958738f)) {
            ++nearby;
            ++added;
        }
    }
    /* Avoid doing four light/physics queries on every tick in a lit room. */
    c->delay=(uint16_t)(added || nearby>=DUNGEON_MOB_LIMIT ?
        DUNGEON_MIN_DELAY+draw(&c->random)%(DUNGEON_MAX_DELAY-DUNGEON_MIN_DELAY+1u) : DUNGEON_INITIAL_DELAY);
    return 1;
}
void dungeon_step(unsigned elapsed,Vec3 player) {
    if (!elapsed || world_dimension()!=OVERWORLD || world_generation()<GENERATOR_DUNGEONS ||
        game_effective_difficulty()==PEACEFUL || !player_rules.health) return;
    SpawnerStep step={elapsed,player};
    world_visit_dungeon_spawners((int)floorf(player.x/CHUNK_SIZE),(int)floorf(player.z/CHUNK_SIZE),1,tick_cage,&step);
}
