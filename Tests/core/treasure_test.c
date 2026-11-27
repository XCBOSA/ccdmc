#include "world_test.h"
#include "../game/chests.h"
#include "../game/drops.h"
#include "../game/enchanting.h"
#include "../game/experience.h"
#include "../game/hoppers.h"
#include "../game/interaction.h"
#include "../world/internal.h"
#include "../world/redstone.h"
#include "../world/villages.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("TREASURE TEST failed line %d: %s\n",__LINE__,#c); ++bad; } } while (0)
static const uint32_t seed=992382978u;

static int smith(ChestPos *p,int version) {
    VillagePlan v;
    return world_begin_generated(seed,version) && village_plan_for(144,-97,&v) && village_smith_chest(&v,&p->x,&p->y,&p->z);
}
static void look(float x,float y,float z) {
    float dx=x-px,dz=z-pz;
    yaw=atan2f(dx,-dz); pitch=atan2f(y-py-1.55f,hypotf(dx,dz));
}
static int face_chest(ChestPos p) {
    for (int d=0;d<4;++d) {
        int x=p.x+rail_dx(d),z=p.z+rail_dz(d);
        if (!solid(block(x,p.y-1,z)) || block(x,p.y,z)!=AIR || block(x,p.y+1,z)!=AIR) continue;
        px=x+.5f; py=p.y+.01f; pz=z+.5f; vy=0.f; look(p.x+.5f,p.y+.45f,p.z+.5f);
        return 1;
    }
    return 0;
}
static int count_drops(int type) {
    int n=0;
    for (int i=0;i<MAXDROP;++i) if (drops[i].item.type==type) n+=drops[i].item.count;
    return n;
}
static unsigned xp(void) {
    size_t count;
    unsigned total=0;
    const XPOrb *orbs=experience_orbs(OVERWORLD,&count);
    for (size_t i=0;i<count;++i) total+=orbs[i].value;
    return total;
}
static int ore_generation(void) {
    int bad=0,mountains=0,flat=0,ore=0;
    static TerrainResult prior,current;
    for (int cx=-40;cx<=40 && (mountains<8 || flat<4);cx+=2)
        for (int cz=-40;cz<=40 && (mountains<8 || flat<4);cz+=2) {
            int high=terrain_biome(cx*CHUNK_SIZE+8,cz*CHUNK_SIZE+8)==BIOME_MOUNTAINS;
            if (high ? mountains>=8 : flat>=4) continue;
            if (high) ++mountains; else ++flat;
            terrain_prepare(TERRAIN_KEY(seed,GENERATOR_DOORS,OVERWORLD,WORLD_STANDARD,0),cx,cz,&prior);
            terrain_prepare(TERRAIN_KEY(seed,GENERATOR_TREASURE,OVERWORLD,WORLD_STANDARD,0),cx,cz,&current);
            CHECK(!memcmp(prior.biomes,current.biomes,sizeof prior.biomes));
            int n=0;
            for (int i=0;i<CHUNK_CELLS;++i) if (current.cells[i]!=prior.cells[i]) {
                if (current.cells[i]==SMITH_CHEST_CELL) { CHECK(prior.cells[i]==CHEST); continue; }
                CHECK(current.cells[i]==EMERALD_ORE_CELL && prior.cells[i]==STONE &&
                    current.biomes[i/WY]==BIOME_MOUNTAINS && i%WY>=4 && i%WY<=31);
                ++n;
            }
            CHECK(n<=8);
            ore+=n;
        }
    CHECK(mountains==8 && flat==4 && ore>=8);
    for (int d=NETHER;d<=END;++d) {
        terrain_prepare(TERRAIN_KEY(seed,GENERATOR_DOORS,(Dimension)d,WORLD_STANDARD,0),-3,5,&prior);
        terrain_prepare(TERRAIN_KEY(seed,GENERATOR_TREASURE,(Dimension)d,WORLD_STANDARD,0),-3,5,&current);
        CHECK(!memcmp(prior.cells,current.cells,sizeof prior.cells));
    }
    printf("TREASURE GENERATION mountains=%d other=%d natural_ore=%d\n",mountains,flat,ore);
    return bad;
}
int treasure_selftest(void) {
    int bad=0;
    ChestPos p;
    if (!smith(&p,GENERATOR_DOORS)) return 1;
    CHECK(block(p.x,p.y,p.z)==CHEST && chest_signal(p)==0);
    if (!smith(&p,GENERATOR_TREASURE)) return bad+1;
    generate();
    CHECK(world_cell(p.x,p.y,p.z)==SMITH_CHEST_CELL && !world_has_edit(p.x,p.y,p.z));
    ItemStack natural[CHEST_SLOTS+2],again[CHEST_SLOTS+2];
    int count=chest_blast_contents(p,natural),signal=chest_signal(p);
    CHECK(count>=3 && count<=8 && signal>=1 && chest_blast_contents(p,again)==count && chests_count_in(OVERWORLD)==0);
    for (int i=0;i<count;++i) CHECK(item_stack_valid(natural[i]) && item_same_kind(natural[i],again[i]) && natural[i].count==again[i].count);
    CHECK(!world_has_edit(p.x,p.y,p.z));
    ChestView view;
    CHECK(set_block(p.x,p.y+1,p.z,STONE) && chest_open(p,&view)==CHEST_BLOCKED && block(p.x,p.y,p.z)==SMITH_CHEST);
    CHECK(set_block(p.x,p.y+1,p.z,AIR) && face_chest(p));
    memset(inventory,0,sizeof inventory); selected_slot=0;
    Interaction tap=game_tap();
    CHECK(tap.panel==INTERACTION_CHEST && tap.hit.x==p.x && tap.hit.y==p.y && tap.hit.z==p.z);
    if (chest_open(p,&view)!=CHEST_OK) return bad+1;
    CHECK(block(p.x,p.y,p.z)==CHEST && chest_signal(p)==signal);
    int taken=0;
    for (int i=0;i<CHEST_SLOTS;++i) {
        ItemStack item=chest_slot(&view,i);
        if (!item.count) continue;
        CHECK(taken<count && item_same_kind(item,natural[taken]) && item.count==natural[taken].count);
        CHECK(chest_move(&view,i,DOUBLE_CHEST_SLOTS+taken,0)==CHEST_OK);
        ++taken;
    }
    CHECK(taken==count && chest_signal(p)==0 && chest_blast_contents(p,again)==0 && chests_count_in(OVERWORLD)==0);
    CHECK(world_select_dimension(NETHER) && world_select_dimension(OVERWORLD) && chest_open(p,&view)==CHEST_OK &&
        chest_signal(p)==0 && block(p.x,p.y,p.z)==CHEST);

    if (!smith(&p,GENERATOR_TREASURE)) return bad+1;
    generate();
    CHECK(set_block(p.x,p.y-1,p.z,AIR) && hopper_place((Hit){1,p.x,p.y-2,p.z,p.x,p.y-1,p.z}));
    redstone_step(p.x,p.z); hoppers_step(p.x,p.z);
    const Hopper *h=hopper_at(p.x,p.y-1,p.z);
    CHECK(h && h->slots[0].count==1 && item_same_kind(h->slots[0],natural[0]) && block(p.x,p.y,p.z)==CHEST);

    if (!smith(&p,GENERATOR_TREASURE)) return bad+1;
    generate();
    for (int i=0;i<MAXDROP;++i) drops[i].item=(ItemStack){STONE,1,0,0,0};
    CHECK(chest_break(p) && block(p.x,p.y,p.z)==SMITH_CHEST);
    drops_reset();
    CHECK(chest_break(p) && block(p.x,p.y,p.z)==AIR && count_drops(CHEST)==1);
    for (int i=0;i<count;++i) CHECK(i+1<MAXDROP && item_same_kind(drops[i+1].item,natural[i]) && drops[i+1].item.count==natural[i].count);

    bad+=ore_generation();
    world_begin(33); generate();
    px=.5f; py=149.01f; pz=2.5f; yaw=0.f; pitch=0.f; selected_slot=0;
    CHECK(set_block(0,150,0,EMERALD_ORE) && world_cell(0,150,0)==EMERALD_ORE_CELL);
    inventory[0]=(ItemStack){STONE_PICKAXE,1,0,0,0};
    CHECK(mine() && !count_drops(EMERALD) && !xp());
    CHECK(set_block(0,150,0,EMERALD_ORE));
    inventory[0]=(ItemStack){IRON_PICKAXE,1,0,0,0};
    CHECK(mine() && count_drops(EMERALD)==1 && xp()>=3 && xp()<=7);
    uint64_t silk=enchant_code(ENCH_SILK_TOUCH,1),fortune=enchant_code(ENCH_FORTUNE,3);
    CHECK(set_block(0,150,0,EMERALD_ORE));
    inventory[0]=(ItemStack){IRON_PICKAXE,1,0,silk,0};
    unsigned old_xp=xp();
    CHECK(mine() && count_drops(EMERALD_ORE_ITEM)==1 && xp()==old_xp);
    inventory[0]=(ItemStack){IRON_PICKAXE,1,0,fortune,0};
    int multi=0;
    for (int i=0;i<12;++i) {
        drops_reset();
        CHECK(set_block(0,150,0,EMERALD_ORE) && mine());
        int n=count_drops(EMERALD);
        CHECK(n>=1 && n<=4);
        multi|=n>1;
    }
    CHECK(multi);
    memset(inventory,0,sizeof inventory); inventory[0]=(ItemStack){EMERALD,9,0,0,0};
    const int grid[]={0,0,0,0,0,0,0,0,0},one[]={0,-1,-1,-1};
    CHECK(craft_take(grid,3) && inventory[0].type==EMERALD_BLOCK_ITEM && inventory[0].count==1);
    CHECK(craft_take(one,2) && inventory[0].type==EMERALD && inventory[0].count==9);
    printf("TREASURE TEST hand/comparator/hopper/break/no-refill/mining/silk/fortune/storage failures=%d\n",bad);
    return bad;
}
