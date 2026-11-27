#include "../game/actors.h"
#include "../game/chests.h"
#include "../game/dungeon.h"
#include "../game/experience.h"
#include "../world/dungeons.h"
#include "../world/internal.h"
#include "../world/light.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(x) do { if (!(x)) { printf("DUNGEON TEST line %d: %s\n",__LINE__,#x); ++failures; } } while (0)

static int chunk(int x) { return x/CHUNK_SIZE-(x%CHUNK_SIZE<0); }
static int find_room(DungeonPlan *plan) {
    for (int x=-10;x<=10;++x) for (int z=-10;z<=10;++z) {
        DungeonPlan p={0};
        if (!dungeon_plan_for(x*DUNGEON_REGION,z*DUNGEON_REGION,&p)) continue;
        if (chunk(p.x-p.radius_x)!=chunk(p.x+p.radius_x) || chunk(p.z-p.radius_z)!=chunk(p.z+p.radius_z)) {
            *plan=p;
            return 1;
        }
    }
    return 0;
}
static int spawner_visit(void *ctx,int x,int y,int z,int raw) {
    DungeonPlan *plan=(DungeonPlan *)ctx;
    if (x==plan->x && y==plan->floor+1 && z==plan->z && raw==plan->spawner) ++plan->chest_count;
    return 1;
}
static int mob_count(void) {
    int count=0;
    const ActorRecord *list=actors_in(OVERWORLD);
    for (int i=0;i<ACTOR_CAPACITY;++i) count+=list[i].type!=ACTOR_EMPTY;
    return count;
}
static void advance_cages(int steps) {
    for (int i=0;i<steps;++i) dungeon_step(1,(Vec3){px,py,pz});
}
int dungeon_selftest(void) {
    failures=0;
    enum { SEED=1712 };
    CHECK(world_begin_generated(SEED,GENERATOR_DUNGEONS));
    DungeonPlan p={0};
    if (!find_room(&p)) { CHECK(0 && "deterministic seam fixture missing"); return failures; }
    static TerrainResult first, second, reversed;
    int ax=chunk(p.x-p.radius_x),az=chunk(p.z-p.radius_z);
    int bx=chunk(p.x+p.radius_x),bz=chunk(p.z+p.radius_z);
    TerrainKey key=TERRAIN_KEY(SEED,GENERATOR_DUNGEONS,OVERWORLD,WORLD_STANDARD,0);
    terrain_prepare(key,ax,az,&first);
    terrain_prepare(key,bx,bz,&second);
    terrain_prepare(key,ax,az,&reversed);
    CHECK(!memcmp(first.cells,reversed.cells,sizeof first.cells) &&
        first.wide.count==reversed.wide.count &&
        !memcmp(first.wide.cell,reversed.wide.cell,first.wide.count*sizeof(TerrainWideCell)));
    terrain_prepare(key,bx,bz,&reversed);
    CHECK(!memcmp(second.cells,reversed.cells,sizeof second.cells) &&
        second.wide.count==reversed.wide.count &&
        !memcmp(second.wide.cell,reversed.wide.cell,second.wide.count*sizeof(TerrainWideCell)));
    int cx=chunk(p.x),cz=chunk(p.z),y=p.floor+1;
    for (int a=cx-2;a<=cx+2;++a) for (int b=cz-2;b<=cz+2;++b)
        (void)world_cell(a*CHUNK_SIZE,y,b*CHUNK_SIZE);
    CHECK(world_cell(p.x,y,p.z)==p.spawner);
    CHECK(block(p.x,p.floor+DUNGEON_ROOF,p.z)==COBBLESTONE);
    int openings=0;
    for (int dx=-p.radius_x;dx<=p.radius_x;++dx) for (int dz=-p.radius_z;dz<=p.radius_z;++dz) {
        int floor=block(p.x+dx,p.floor,p.z+dz);
        CHECK(floor==COBBLESTONE || floor==MOSS_STONE);
        if ((dx==-p.radius_x || dx==p.radius_x || dz==-p.radius_z || dz==p.radius_z) &&
            block(p.x+dx,y,p.z+dz)==AIR) {
            ++openings;
            CHECK(block(p.x+dx,y+1,p.z+dz)==AIR);
        }
    }
    CHECK(openings>=1 && openings<=5);
    DungeonPlan seen=p;
    seen.chest_count=0;
    CHECK(world_visit_dungeon_spawners(cx,cz,1,spawner_visit,&seen) && seen.chest_count==1);

    ChestPos pos={p.x+p.radius_x-1,y,p.z+p.radius_z-1};
    ChestView view={OVERWORLD,{{0,0,0},{0,0,0}},0,0};
    CHECK(world_cell(pos.x,pos.y,pos.z)==DUNGEON_CHEST_CELL && chest_open(pos,&view)==CHEST_OK && view.count==1);
    int items=0;
    for (int i=0;i<CHEST_SLOTS;++i) {
        int count=chest_slot(&view,i).count;
        items+=count;
        while (count--) chest_automation_remove(&view,i);
    }
    CHECK(items>=4 && chest_open(pos,&view)==CHEST_OK && world_cell(pos.x,pos.y,pos.z)==CHEST);
    for (int i=0;i<CHEST_SLOTS;++i) CHECK(!chest_slot(&view,i).count);

    player_reset_rules(SURVIVAL);
    actors_reset();
    px=p.x+.5f; py=y+.01f; pz=p.z+2.5f;
    CHECK(game_set_difficulty(PEACEFUL));
    advance_cages(25);
    CHECK(!dungeon_clocks()[0].random && !mob_count());
    CHECK(game_set_difficulty(NORMAL));
    float room_x=px;
    px+=96.f;
    advance_cages(25);
    CHECK(!dungeon_clocks()[0].random && !mob_count());
    px=room_x;
    BlockEdit lamps[4];
    int n=0;
    for (int dx=-2;dx<=2;dx+=4) for (int dz=-2;dz<=2;dz+=4)
        lamps[n++]=(BlockEdit){p.x+dx,y+2,p.z+dz,GLOWSTONE};
    CHECK(world_set_blocks(lamps,n));
    advance_cages(80);
    CHECK(dungeon_clocks()[0].random && !mob_count());
    for (int i=0;i<n;++i) lamps[i].cell=AIR;
    CHECK(world_set_blocks(lamps,n));
    advance_cages(3000);
    int spawned=mob_count();
    CHECK(spawned>0 && spawned<=DUNGEON_MOB_LIMIT);
    const ActorRecord *list=actors_in(OVERWORLD);
    int expected=p.spawner==DUNGEON_ZOMBIE_CELL ? ACTOR_ZOMBIE :
        p.spawner==DUNGEON_SKELETON_CELL ? ACTOR_SKELETON : ACTOR_SPIDER;
    for (int i=0;i<ACTOR_CAPACITY;++i) if (list[i].type) CHECK(list[i].type==expected);
    actors_reset();
    yaw=0; pitch=-.43f; selected_slot=0;
    inventory[0]=(ItemStack){IRON_PICKAXE,1,0,0,0};
    Hit hit=target();
    CHECK(hit.hit && hit.x==p.x && hit.y==y && hit.z==p.z);
    experience_reset();
    CHECK(mine()!=NULL && block(p.x,y,p.z)==AIR && inventory[0].damage==1);
    size_t orb_count=0;
    const XPOrb *orbs=experience_orbs(OVERWORLD,&orb_count);
    unsigned xp=0;
    for (size_t i=0;i<orb_count;++i) xp+=orbs[i].value;
    CHECK(xp>=15 && xp<=43);
    seen.chest_count=0;
    CHECK(world_visit_dungeon_spawners(cx,cz,1,spawner_visit,&seen) && seen.chest_count==0);
    advance_cages(100);
    CHECK(!mob_count());
    CHECK(world_select_dimension(NETHER) && !dungeon_plan_for(p.x,p.z,&seen));
    CHECK(world_begin_options(SEED,GENERATOR_DUNGEONS,WORLD_STANDARD,1) &&
        dungeon_plan_for(p.x,p.z,&seen) && seen.x==p.x && seen.floor==p.floor && seen.spawner==p.spawner);
    CHECK(world_begin_generated(SEED,GENERATOR_MUSHROOM_ISLAND) && !dungeon_plan_for(p.x,p.z,&seen) &&
        !dungeon_spawner_cell(world_cell(p.x,y,p.z)));
    printf("DUNGEON TEST seed=%u room=%d,%d,%d openings=%d loot=%d guarded=%d failures=%d\n",
        SEED,p.x,y,p.z,openings,items,spawned,failures);
    actors_reset();
    world_reset();
    return failures;
}
