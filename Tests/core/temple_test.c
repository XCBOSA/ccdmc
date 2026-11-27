#include "../game/chests.h"
#include "../game/droppers.h"
#include "../game/interaction.h"
#include "../world/internal.h"
#include "../world/rails.h"
#include "../world/redstone.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"TEMPLE TEST line %d: %s\n",__LINE__,#c); ++failures; } } while(0)
static int chunk(int a) { return a/CHUNK_SIZE-(a%CHUNK_SIZE<0); }
static int raw(BlockEdit c) { return world_cell(c.x,c.y,c.z); }
static int arrows(BlockEdit c) {
    const Dropper *d=dropper_at(c.x,c.y,c.z);
    if (!d) return -1;
    int n=0;
    for (int i=0;i<DROPPER_SLOTS;++i) if (d->slots[i].type==ARROW) n+=d->slots[i].count;
    return n;
}
static void ticks(TemplePlan p,int n) { while (n--) redstone_step(p.x,p.z); }
static void walk_on(BlockEdit c) { px=c.x+.5f; py=c.y+.01f; pz=c.z+.5f; }
static int find_crossing(TemplePlan *out) {
    for (int a=-24;a<=24;++a) for (int b=-24;b<=24;++b) {
        TemplePlan p;
        if (!temple_plan_for(a*192,b*192,&p)) continue;
        BlockEdit left=temple_position(p,5,-4,6,0),right=temple_position(p,9,-4,6,0);
        if (chunk(left.x)!=chunk(right.x) || chunk(left.z)!=chunk(right.z)) {
            *out=p; return 1;
        }
    }
    return 0;
}
int temple_selftest(void) {
    failures=0;
    static TerrainResult initial[2],reordered[2];
    const uint32_t seed=295588u;
    CHECK(world_begin_generated(seed,GENERATOR_TEMPLES));
    TemplePlan p;
    if (!find_crossing(&p)) { CHECK(0 && "need deterministic fixture"); return failures; }
    BlockEdit a=temple_position(p,5,-4,6,TRIPWIRE_HOOK_BASE+WEST),
        b=temple_position(p,9,-4,6,TRIPWIRE_HOOK_BASE+EAST);
    TerrainKey key=TERRAIN_KEY(seed,GENERATOR_TEMPLES,OVERWORLD,WORLD_STANDARD,0);
    terrain_prepare(key,chunk(a.x),chunk(a.z),&initial[0]);
    terrain_prepare(key,chunk(b.x),chunk(b.z),&initial[1]);
    terrain_prepare(key,chunk(b.x),chunk(b.z),&reordered[1]);
    terrain_prepare(key,chunk(a.x),chunk(a.z),&reordered[0]);
    for (int i=0;i<2;++i) {
        CHECK(reordered[i].devices.count==initial[i].devices.count && initial[i].devices.count>0 &&
            !memcmp(reordered[i].cells,initial[i].cells,sizeof initial[i].cells) &&
            !memcmp(reordered[i].devices.cell,initial[i].devices.cell,
                initial[i].devices.count*sizeof(TerrainWideCell)));
    }
    terrain_prepare(TERRAIN_KEY(seed,GENERATOR_DYES,OVERWORLD,WORLD_STANDARD,0),chunk(a.x),chunk(a.z),&reordered[0]);
    CHECK(!reordered[0].devices.count && memcmp(reordered[0].cells,initial[0].cells,sizeof initial[0].cells));
    key.structures_disabled=1;
    terrain_prepare(key,chunk(a.x),chunk(a.z),&reordered[0]);
    CHECK(!reordered[0].devices.count && memcmp(reordered[0].cells,initial[0].cells,sizeof initial[0].cells));
    for (int x=0;x<=12;++x) for (int z=0;z<=14;++z) (void)raw(temple_position(p,x,-4,z,AIR));
    walk_on(temple_position(p,7,1,13,AIR)); player_reset_rules(SURVIVAL);
    ticks(p,12);
    CHECK(raw(a)==a.cell && raw(b)==b.cell);
    BlockEdit stages[]={temple_position(p,7,-4,4,DISPENSER_BASE+SOUTH),
        temple_position(p,7,-4,1,DISPENSER_BASE+SOUTH)};
    for (int i=0;i<2;++i) {
        CHECK(raw(stages[i])==stages[i].cell && arrows(stages[i])>=8);
        int before=arrows(stages[i]);
        walk_on(temple_position(p,7,-4,i ? 2 : 6,0));
        ticks(p,DROPPER_DELAY+4);
        printf("TEMPLE TRAP %d arrows %d -> %d\n",i+1,before,arrows(stages[i]));
        CHECK(arrows(stages[i])==before-1);
        walk_on(temple_position(p,7,1,13,AIR)); ticks(p,TRIPWIRE_PULSE_TICKS+5);
    }
    const int lever_z[]={4,6,8};
    BlockEdit levers[3],barriers[3];
    for (int i=0;i<3;++i) {
        levers[i]=temple_position(p,1,i==1 ? -1 : -2,lever_z[i],0);
        barriers[i]=temple_position(p,3,-3,lever_z[i],0);
        CHECK(solid(raw(barriers[i])));
    }
    for (int i=0;i<3;++i) {
        CHECK(redstone_toggle_lever(levers[i].x,levers[i].y,levers[i].z)); ticks(p,10);
        for (int j=0;j<3;++j) CHECK(j<=i ? raw(barriers[j])==AIR : solid(raw(barriers[j])));
    }
    BlockEdit loot=temple_position(p,3,-4,2,0);
    ChestView v={OVERWORLD,{{0,0,0},{0,0,0}},0,0};
    ChestPos pos={loot.x,loot.y,loot.z};
    CHECK(raw(loot)==TEMPLE_CHEST_CELL && chest_open(pos,&v)==CHEST_OK && v.count==1 && raw(loot)==CHEST);
    int occupied=0;
    for (int i=0;i<CHEST_SLOTS;++i) {
        for (int n=chest_slot(&v,i).count;n>0;--n) {
            ++occupied; chest_automation_remove(&v,i);
        }
        CHECK(!chest_slot(&v,i).count);
    }
    CHECK(occupied>1 && chest_open(pos,&v)==CHEST_OK && !chest_slot(&v,0).count && raw(loot)==CHEST);
    int left=arrows(stages[0]);
    CHECK(world_select_dimension(NETHER) && !temple_plan_for(p.x,p.z,&p) && world_select_dimension(OVERWORLD));
    for (int x=0;x<=12;++x) for (int z=0;z<=14;++z) (void)raw(temple_position(p,x,-4,z,AIR));
    ticks(p,10);
    CHECK(arrows(stages[0])==left && raw(loot)==CHEST);
    printf("TEMPLE TEST seed=%u centre=%d,%d,%d rotation=%u layout/seam/traps/puzzle/loot/dimension failures=%d\n",
        seed,p.x,p.y,p.z,p.rotation,failures);
    world_reset();
    return failures;
}
