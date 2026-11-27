#include "chests.h"
#include "actors.h"
#include "stack_transfer.h"
#include "drops.h"
#include "container_signal.h"
#include "../world/redstone.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { ChestRecord *values; size_t count, capacity; } ChestBank;
static ChestBank banks[DIMENSION_COUNT];
static ChestView active_trap;
static const ItemStack empty={0,0,0,0,0};
static const int offsets[4][2]={{-1,0},{1,0},{0,-1},{0,1}};

static ChestBank *bank(void) { return &banks[world_dimension()]; }
static int compare(ChestPos a,ChestPos b) {
    if (a.x!=b.x) return a.x<b.x ? -1 : 1;
    if (a.z!=b.z) return a.z<b.z ? -1 : 1;
    return a.y<b.y ? -1 : a.y!=b.y;
}
static int cell(ChestPos p) { return block(p.x,p.y,p.z); }
static ChestPos adjacent(ChestPos p,int i) {
    return (ChestPos){p.x+offsets[i][0],p.y,p.z+offsets[i][1]};
}
static size_t index_of(const ChestBank *b,ChestPos p) {
    size_t lo=0,hi=b->count;
    while (lo<hi) {
        size_t mid=lo+(hi-lo)/2;
        if (compare(b->values[mid].at,p)<0) lo=mid+1;
        else hi=mid;
    }
    return lo;
}
static ChestRecord *lookup(ChestBank *b,ChestPos p) {
    size_t i=index_of(b,p);
    return i<b->count && !compare(b->values[i].at,p) ? &b->values[i] : NULL;
}
static int reserve(ChestBank *b,size_t extra) {
    if (extra>SIZE_MAX-b->count || b->count+extra>SIZE_MAX/sizeof *b->values) return 0;
    size_t need=b->count+extra;
    if (need<=b->capacity) return 1;
    size_t grow=b->capacity/2 + b->capacity;
    size_t slots=grow<need || grow>SIZE_MAX/sizeof *b->values ? need : grow;
    if (slots<8) slots=8;
    ChestRecord *v=(ChestRecord *)realloc(b->values,slots*sizeof *v);
    if (!v) return 0;
    b->values=v; b->capacity=slots;
    return 1;
}
/* Caller has already reserved space; no failures after a world mutation. */
static ChestRecord *insert(ChestBank *b,ChestPos p) {
    size_t i=index_of(b,p);
    if (i<b->count && !compare(b->values[i].at,p)) return &b->values[i];
    if (i<b->count) memmove(&b->values[i+1],&b->values[i],(b->count-i)*sizeof *b->values);
    ++b->count;
    memset(&b->values[i],0,sizeof b->values[i]);
    b->values[i].at=p;
    return &b->values[i];
}
static void erase(ChestBank *b,ChestPos p) {
    ChestRecord *r=lookup(b,p);
    if (!r) return;
    size_t i=(size_t)(r-b->values);
    --b->count;
    if (i<b->count) memmove(&b->values[i],&b->values[i+1],(b->count-i)*sizeof *b->values);
}
static int nonempty(const ChestRecord *r) {
    if (!r) return 0;
    for (int i=0;i<CHEST_SLOTS;++i) if (r->items[i].count) return 1;
    return 0;
}

void chests_reset(void) {
    memset(&active_trap,0,sizeof active_trap);
    for (int d=0;d<DIMENSION_COUNT;++d) free(banks[d].values);
    memset(banks,0,sizeof banks);
}
size_t chests_count_in(Dimension d) {
    return (unsigned)d<DIMENSION_COUNT ? banks[d].count : 0;
}
const ChestRecord *chest_record_in(Dimension d,size_t ordinal) {
    return ordinal<chests_count_in(d) ? &banks[d].values[ordinal] : NULL;
}
int chest_restore(const ChestRecord *r) {
    if (!r || !nonempty(r) || !chest_inventory_type(cell(r->at)) ||
        !inworld(r->at.x,r->at.y,r->at.z)) return 0;
    ChestBank *b=bank();
    /* Canonical file order keeps restore linear, rejects duplicates. */
    if (b->count && compare(b->values[b->count-1].at,r->at)>=0) return 0;
    for (int i=0;i<CHEST_SLOTS;++i) if (!item_stack_valid(r->items[i])) return 0;
    if (!reserve(b,1)) return 0;
    b->values[b->count++]=*r;
    return 1;
}
static int find_neighbour(ChestPos p,int kind,ChestPos *near) {
    int count=0;
    for (int i=0;i<4;++i) {
        ChestPos q=adjacent(p,i);
        int type=cell(q);
        if (is_chest(type) && chest_pair_type(type)==kind) {
            if (near) *near=q;
            ++count;
        }
    }
    return count;
}
int chest_can_place_type(ChestPos p,int type) {
    if (!chest_inventory_type(type) || !inworld(p.x,p.y,p.z)) return 0;
    ChestPos partner={0,0,0};
    int count=find_neighbour(p,type,&partner);
    return !count || (count==1 && !find_neighbour(partner,type,NULL));
}
int chest_can_place(ChestPos p) { return chest_can_place_type(p,CHEST); }
ChestJoint chest_joint(ChestPos p) {
    int type=cell(p),kind=chest_pair_type(type);
    if (!is_chest(type)) return CHEST_ALONE;
    ChestPos q={0,0,0},back={0,0,0};
    if (find_neighbour(p,kind,&q)!=1 || find_neighbour(q,kind,&back)!=1 || compare(p,back)) return CHEST_ALONE;
    return q.x<p.x ? CHEST_WEST : q.x>p.x ? CHEST_EAST : q.z<p.z ? CHEST_NORTH : CHEST_SOUTH;
}
static ChestResult resolve_pair(ChestPos p,ChestView *v) {
    int type=cell(p);
    if (!is_chest(type)) return CHEST_MISSING;
    v->dimension=world_dimension();
    v->kind=chest_pair_type(type);
    v->halves[0]=p; v->count=1;
    ChestJoint joint=chest_joint(p);
    if (joint!=CHEST_ALONE) {
        ChestPos q=adjacent(p,joint);
        v->count=2;
        if (compare(p,q)>0) { v->halves[0]=q; v->halves[1]=p; }
        else v->halves[1]=q;
    }
    return CHEST_OK;
}
static ChestResult resolve(ChestPos p,ChestView *v) {
    ChestResult found=resolve_pair(p,v);
    if (found!=CHEST_OK) return found;
    for (int i=0;i<v->count;++i) {
        ChestPos t=v->halves[i];
        int above=block(t.x,t.y+1,t.z);
        /* A handling chute exposes the box from the front even with its
           vertical discharge connected above. */
        if (solid(above) && above!=HOPPER && above!=HYDRAULIC_SORTER) return CHEST_BLOCKED;
        if (actors_cat_blocks_chest(t.x,t.y,t.z)) return CHEST_CAT;
    }
    return CHEST_OK;
}
static int treasure_extra(void) {
    if (world_dimension()==NETHER && world_generation()>=GENERATOR_EQUINE && world_generation()<GENERATOR_FILTER_ARCHIVE)
        return FILTER_MOSS;
    if (world_dimension()==OVERWORLD && world_generation()>=GENERATOR_EQUINE && world_generation()<GENERATOR_BOOKS)
        return SUGAR_CANE;
    return AIR;
}
static uint32_t loot_next(uint32_t *state) {
    *state=*state*1664525u+1013904223u;
    uint32_t bits=*state;
    bits=(bits^(bits>>16))*0x7feb352du;
    return bits^(bits>>15);
}
static uint32_t loot_seed(ChestPos p) {
    return hash2(p.x,p.z)^((unsigned)p.y*0x9e3779b9u);
}
typedef struct { uint16_t item; uint8_t least,most,weight; } Loot;
static void roll_loot(ItemStack items[CHEST_SLOTS],const Loot *prizes,unsigned kinds,
    uint32_t rng,int minimum,int extra) {
    unsigned total=0;
    for (unsigned i=0;i<kinds;++i) total+=prizes[i].weight;
    int slots[CHEST_SLOTS];
    for (int i=0;i<CHEST_SLOTS;++i) slots[i]=i;
    int rolls=minimum+(int)(loot_next(&rng)%(unsigned)extra);
    for (int i=0;i<rolls;++i) {
        unsigned pick=loot_next(&rng)%total,j=0;
        while (j+1<kinds && pick>=prizes[j].weight) pick-=prizes[j++].weight;
        unsigned stack=prizes[j].least+loot_next(&rng)%(unsigned)(prizes[j].most-prizes[j].least+1);
        int at=i+(int)(loot_next(&rng)%(unsigned)(CHEST_SLOTS-i));
        items[slots[at]]=(ItemStack){prizes[j].item,(uint8_t)stack,0,0,0};
        slots[at]=slots[i];
    }
}
void mineshaft_loot(ChestPos p,ItemStack items[CHEST_SLOTS]) {
    static const Loot supplies[]={{RAIL,4,8,12},{TORCH,3,8,10},{BREAD,1,3,10},{COAL,3,8,9},
        {IRON_INGOT,1,5,10},{GOLD_INGOT,1,3,5},{REDSTONE_DUST,4,9,5},{LAPIS_LAZULI,3,6,4},
        {PUMPKIN_SEEDS,2,4,7},{IRON_PICKAXE,1,1,2},{DIAMOND,1,2,3}};
    memset(items,0,CHEST_SLOTS*sizeof *items);
    roll_loot(items,supplies,sizeof supplies/sizeof *supplies,loot_seed(p)^UINT32_C(0x43617274),3,3);
}

static int bonus_site(int x,int z,int spawn_y,int max_rise,ChestPos *out) {
    int y=terrain_height(x,z)+1;
    ChestPos p={x,y,z};
    if (!inworld(x,y+2,z) || abs(y-spawn_y)>max_rise ||
        y-1<world_sea_level()) return 0;
    int floor=block(x,y-1,z);
    if (!solid(floor) || floor==LEAF || tree_log_species(floor)>=0 ||
        floor==CACTUS || cell(p)!=AIR || block(x,y+1,z)!=AIR ||
        find_neighbour(p,CHEST,NULL)) return 0;
    /* The chest must have at least one dry, clear place from which to open it. */
    for (int i=0;i<4;++i) {
        ChestPos q=adjacent(p,i);
        if (cell(q)==AIR && block(q.x,q.y+1,q.z)==AIR && solid(block(q.x,q.y-1,q.z))) {
            *out=p;
            return 1;
        }
    }
    return 0;
}

static int bonus_find_site(Vec3 spawn,ChestPos *out) {
    int x=(int)floorf(spawn.x),y=(int)floorf(spawn.y),z=(int)floorf(spawn.z);
    /* Prefer a short walk straight ahead of the initial player look direction.
       Search all sides before giving up if the player spawns near a cliff or sea. */
    for (int rise=1;rise<=5;rise+=4) for (int r=3;r<=12;++r) {
        if (bonus_site(x,z-r,y,rise,out)) return 1;
        for (int i=-r;i<=r;++i) {
            if ((i && bonus_site(x+i,z-r,y,rise,out)) || bonus_site(x+i,z+r,y,rise,out) ||
                (i>-r && i<r && (bonus_site(x-r,z+i,y,rise,out) ||
                    bonus_site(x+r,z+i,y,rise,out)))) return 1;
        }
    }
    return 0;
}

int chest_create_bonus(void) {
    ChestPos p;
    ChestBank *b=bank();
    if (world_dimension()!=OVERWORLD || b->count || !bonus_find_site(player_world_spawn(),&p) ||
        !reserve(b,1)) return 0;
    BlockEdit edits[5]={{p.x,p.y,p.z,CHEST}};
    int count=1;
    for (int i=0;i<4;++i) {
        ChestPos q=adjacent(p,i);
        int floor=block(q.x,q.y-1,q.z);
        if (solid(floor) && floor!=FARMLAND && floor!=LEAF && floor!=CACTUS && cell(q)==AIR)
            edits[count++]=(BlockEdit){q.x,q.y,q.z,TORCH};
    }
    if (!world_set_blocks(edits,count)) return 0;
    static const Loot supplies[]={
        {APPLE,1,2,5},{BREAD,1,2,4},{STICK,1,4,5},{PLANKS,1,3,5},{WOOD,1,3,5}
    };
    uint32_t rng=loot_seed(p)^UINT32_C(0x426f6e75);
    ItemStack *items=insert(b,p)->items;
    roll_loot(items,supplies,sizeof supplies/sizeof *supplies,rng,5,3);
    /* Reserve an actual pickaxe in a free slot; nothing is regenerated on open. */
    int tool=(loot_next(&rng)%3u)==0 ? STONE_PICKAXE : WOOD_PICKAXE;
    int index=(int)(loot_next(&rng)%CHEST_SLOTS);
    while (items[index].count) index=(index+1)%CHEST_SLOTS;
    items[index]=(ItemStack){(uint16_t)tool,1,0,0,0};
    tool=loot_next(&rng)%4u==0 ? STONE_AXE : WOOD_AXE;
    index=(int)(loot_next(&rng)%CHEST_SLOTS);
    while (items[index].count) index=(index+1)%CHEST_SLOTS;
    items[index]=(ItemStack){(uint16_t)tool,1,0,0,0};
    return 1;
}

static void generated_chest(ChestPos p,int type,ItemStack items[CHEST_SLOTS]) {
    memset(items,0,CHEST_SLOTS*sizeof *items);
    if (type==SADDLE_CHEST) {
        items[13]=(ItemStack){SADDLE,1,0,0,0};
        int extra=treasure_extra();
        if (extra!=AIR) items[14]=(ItemStack){(uint16_t)extra,3,0,0,0};
        if (world_generation()>=GENERATOR_HORSE_ARMOR) {
            uint32_t rng=loot_seed(p)^0x686f7273u;
            if (loot_next(&rng)%4u==0) {
                unsigned material=loot_next(&rng)%10u;
                uint16_t armor=(uint16_t)(material<5 ? IRON_HORSE_ARMOR :
                    material<9 ? GOLD_HORSE_ARMOR : DIAMOND_HORSE_ARMOR);
                items[15]=(ItemStack){armor,1,0,0,0};
            }
        }
        return;
    }
    if (type==TEMPLE_CHEST) {
        static const Loot jungle[]={
            {BONE,4,6,20},{ROTTEN_FLESH,3,7,16},{IRON_INGOT,1,5,10},{GOLD_INGOT,2,7,10},
            {DIAMOND,1,3,3},{EMERALD,1,3,2},{SADDLE,1,1,3},
            {IRON_HORSE_ARMOR,1,1,1},{GOLD_HORSE_ARMOR,1,1,1},{DIAMOND_HORSE_ARMOR,1,1,1}
        };
        roll_loot(items,jungle,sizeof jungle/sizeof *jungle,loot_seed(p)^0x4a756e67u,2,5);
        return;
    }
    if (type==DUNGEON_CHEST) {
        static const Loot underground[]={
            {SADDLE,1,1,10},{IRON_INGOT,1,4,10},{BREAD,1,1,10},{WHEAT,1,4,10},
            {GUNPOWDER,1,4,10},{STRING,1,4,10},{BUCKET,1,1,10},{REDSTONE_DUST,1,4,10},
            {FRUIT_RATION,1,1,1},{IRON_HORSE_ARMOR,1,1,5},{GOLD_HORSE_ARMOR,1,1,2},
            {DIAMOND_HORSE_ARMOR,1,1,1},{NAME_TAG,1,1,10}
        };
        roll_loot(items,underground,sizeof underground/sizeof *underground,loot_seed(p)^0x44756e67u,4,5);
        return;
    }
    if (type==DESERT_PYRAMID_CHEST) {
        static const Loot desert[]={{BONE,4,6,20},{ROTTEN_FLESH,3,7,16},{IRON_INGOT,1,5,10},
            {GOLD_INGOT,2,7,10},{DIAMOND,1,3,3},{EMERALD,1,3,2},{SADDLE,1,1,3},
            {IRON_HORSE_ARMOR,1,1,2},{GOLD_HORSE_ARMOR,1,1,2},{DIAMOND_HORSE_ARMOR,1,1,1}};
        roll_loot(items,desert,sizeof desert/sizeof *desert,loot_seed(p)^0x44657365u,2,5);
        return;
    }
    if (type!=SMITH_CHEST) return;
    static const Loot prizes[]={
        {BREAD,1,3,16},{APPLE,1,3,14},{IRON_INGOT,1,5,12},{GOLD_INGOT,1,3,6},
        {IRON_PICKAXE,1,1,5},{IRON_SWORD,1,1,5},{IRON_HELMET,1,1,4},{IRON_CHESTPLATE,1,1,4},
        {IRON_LEGGINGS,1,1,4},{IRON_BOOTS,1,1,4},{OBSIDIAN,3,7,5},{DIAMOND,1,3,3},{SADDLE,1,1,3},
        {IRON_HORSE_ARMOR,1,1,1},{GOLD_HORSE_ARMOR,1,1,1},{DIAMOND_HORSE_ARMOR,1,1,1},
        {OAK_SAPLING,3,7,5}
    };
    int generation=world_generation();
    const unsigned kinds=sizeof prizes/sizeof *prizes-
        (generation<GENERATOR_HORSE_ARMOR ? 4 : generation<GENERATOR_FORESTRY ? 1 : 0);
    roll_loot(items,prizes,kinds,loot_seed(p)^0x536d6974u,3,6);
}
int chest_blast_contents(ChestPos p,ItemStack out[CHEST_SLOTS+2]) {
    int n=0;
    int type=cell(p);
    if (!is_chest(type) || !out) return 0;
    ItemStack loot[CHEST_SLOTS];
    const ItemStack *items=NULL;
    if (!chest_inventory_type(type)) { generated_chest(p,type,loot); items=loot; }
    else {
        const ChestRecord *record=lookup(bank(),p);
        if (record) items=record->items;
    }
    if (items) for (int i=0;i<CHEST_SLOTS;++i) if (items[i].count) out[n++]=items[i];
    return n;
}
void chest_blast_forget(ChestPos p) { erase(bank(),p); }
int chest_signal(ChestPos p) {
    ChestView v;
    if (resolve_pair(p,&v)!=CHEST_OK) return 0;
    unsigned weight=0;
    for (int i=0;i<v.count;++i) {
        ChestPos half=v.halves[i];
        int type=cell(half);
        if (chest_inventory_type(type)) {
            const ChestRecord *r=lookup(bank(),half);
            if (r) weight+=container_weight(r->items,CHEST_SLOTS);
        } else {
            ItemStack loot[CHEST_SLOTS];
            generated_chest(half,type,loot);
            weight+=container_weight(loot,CHEST_SLOTS);
        }
    }
    return container_fill_signal(weight,v.count*CHEST_SLOTS);
}
static ChestResult open_chest_view(ChestPos p,ChestView *view,int check_lid) {
    if (!view) return CHEST_MISSING;
    ChestView found;
    memset(&found,0,sizeof found);
    ChestResult ok=check_lid ? resolve(p,&found) : resolve_pair(p,&found);
    if (ok!=CHEST_OK) return ok;
    BlockEdit edits[2];
    int type[2];
    int count=0;
    for (int i=0;i<found.count;++i) {
        ChestPos t=found.halves[i];
        int raw=cell(t);
        if (!chest_inventory_type(raw)) {
            type[count]=raw;
            edits[count++]=(BlockEdit){t.x,t.y,t.z,CHEST};
        }
    }
    ChestBank *b=bank();
    if (count && (!reserve(b,(size_t)count) || !world_set_blocks(edits,count))) return CHEST_NO_MEMORY;
    for (int i=0;i<count;++i) {
        BlockEdit e=edits[i];
        ChestRecord *contents=insert(b,(ChestPos){e.x,e.y,e.z});
        generated_chest(contents->at,type[i],contents->items);
    }
    if (count) for (int h=0;h<found.count;++h) {
        ChestPos half=found.halves[h];
        redstone_container_changed(half.x,half.y,half.z);
    }
    *view=found;
    return CHEST_OK;
}
ChestResult chest_open(ChestPos p,ChestView *view) { return open_chest_view(p,view,1); }
int chest_automation_open(ChestPos p,ChestView *view) { return open_chest_view(p,view,0)==CHEST_OK; }
static int resident_normal_neighbours(ChestPos p,ChestPos *neighbour) {
    int count=0;
    for (int i=0;i<4;++i) {
        const ChestPos q=adjacent(p,i);
        int raw;
        if (!world_resident_cell(q.x,q.y,q.z,&raw)) return -1;
        const int type=cell_type(raw);
        if (!is_chest(type) || chest_pair_type(type)!=CHEST) continue;
        if (raw!=CHEST) return -1; /* Never open or split an unclaimed loot box. */
        *neighbour=q;
        ++count;
    }
    return count;
}
int chest_automation_open_resident(ChestPos p,ChestView *view) {
    int raw;
    if (!view || !world_resident_cell(p.x,p.y,p.z,&raw) || raw!=CHEST) return 0;
    ChestPos partner={0},back={0};
    const int count=resident_normal_neighbours(p,&partner);
    if (count<0) return 0;
    ChestView found={world_dimension(),{p},1,CHEST};
    if (count==1) {
        const int adjacent_count=resident_normal_neighbours(partner,&back);
        if (adjacent_count<0) return 0;
        if (adjacent_count==1 && !compare(p,back)) {
            found.count=2;
            const int first=compare(p,partner)<0;
            found.halves[0]=first ? p : partner;
            found.halves[1]=first ? partner : p;
        }
    }
    *view=found;
    return 1;
}
int chest_view_valid(const ChestView *v) {
    if (!v || v->dimension!=world_dimension() || (v->count!=1 && v->count!=2)) return 0;
    ChestView resolved;
    if (resolve(v->halves[0],&resolved)!=CHEST_OK || v->count!=resolved.count || v->kind!=resolved.kind) return 0;
    for (int i=0;i<v->count;++i) {
        if (compare(v->halves[i],resolved.halves[i]) || cell(resolved.halves[i])!=v->kind) return 0;
    }
    return 1;
}
int chest_trap_power(ChestPos p) {
    if (active_trap.dimension!=world_dimension()) return 0;
    for (int i=0;i<active_trap.count;++i)
        if (!compare(active_trap.halves[i],p)) return 1;
    return 0;
}
static void notify_trap(const ChestView *view) {
    for (int i=0;i<view->count;++i) {
        ChestPos p=view->halves[i];
        redstone_trap_changed(p.x,p.y,p.z);
    }
}
void chest_access_end(void) {
    ChestView old=active_trap;
    active_trap.count=0;
    if (old.dimension==world_dimension()) notify_trap(&old);
}
void chest_access_begin(const ChestView *view) {
    chest_access_end();
    if (!view || view->kind!=TRAPPED_CHEST || !chest_view_valid(view)) return;
    active_trap=*view;
    notify_trap(&active_trap);
}
void chest_trap_removed(ChestPos p) {
    if (chest_trap_power(p)) chest_access_end();
}
static int valid_index(const ChestView *v,int index) {
    return index>=0 && (index<v->count*CHEST_SLOTS ||
        (index>=DOUBLE_CHEST_SLOTS && index<DOUBLE_CHEST_SLOTS+SLOT_COUNT));
}
ItemStack chest_slot(const ChestView *v,int i) {
    if (!v || v->dimension!=world_dimension() || (v->count!=1 && v->count!=2) || !valid_index(v,i))
        return empty;
    if (i>=DOUBLE_CHEST_SLOTS) return inventory[i-DOUBLE_CHEST_SLOTS];
    ChestRecord *r=lookup(bank(),v->halves[i/CHEST_SLOTS]);
    return r ? r->items[i%CHEST_SLOTS] : empty;
}
static void write_slot(ChestBank *b,const ChestView *v,int i,ItemStack stack) {
    if (i>=DOUBLE_CHEST_SLOTS) { inventory[i-DOUBLE_CHEST_SLOTS]=stack; return; }
    ChestPos p=v->halves[i/CHEST_SLOTS];
    ChestRecord *r=stack.count ? insert(b,p) : lookup(b,p);
    if (r) r->items[i%CHEST_SLOTS]=stack;
}
static void notify_contents(const ChestView *v) {
    for (int i=0;i<v->count;++i) {
        ChestPos p=v->halves[i];
        if (!nonempty(lookup(bank(),p))) erase(bank(),p);
        redstone_container_changed(p.x,p.y,p.z);
    }
}
/* Automation uses a freshly opened container within one game tick;
   unlike hand interaction, a solid lid does not block its inventory. */
int chest_automation_insert(const ChestView *v,ItemStack item) {
    if (!v || v->dimension!=world_dimension() || v->count<1 || v->count>2) return 0;
    for (int i=0;i<v->count*CHEST_SLOTS;++i) {
        ItemStack dst=chest_slot(v,i);
        if (!stack_receive(&dst,item,1)) continue;
        ChestBank *b=bank();
        if (!lookup(b,v->halves[i/CHEST_SLOTS]) && !reserve(b,1)) return 0;
        write_slot(b,v,i,dst);
        notify_contents(v);
        return 1;
    }
    return 0;
}
void chest_automation_remove(const ChestView *v,int i) {
    if (!v || (unsigned)i>=DOUBLE_CHEST_SLOTS || i>=v->count*CHEST_SLOTS ||
        v->dimension!=world_dimension()) return;
    ItemStack item=chest_slot(v,i);
    if (!item.count) return;
    stack_deduct(&item,1);
    write_slot(bank(),v,i,item);
    notify_contents(v);
}
ChestResult chest_move(const ChestView *v,int source,int dest,int single) {
    if (!chest_view_valid(v)) return CHEST_MISSING;
    if (source==dest || !valid_index(v,source) || !valid_index(v,dest)) return CHEST_NO_MOVE;
    ItemStack a=chest_slot(v,source),b=chest_slot(v,dest);
    if (!a.count || !item_stack_valid(a) || !item_stack_valid(b)) return CHEST_NO_MOVE;
    if (!b.count || (item_same_kind(a,b) && item_def(a.type)->max_stack>1)) {
        int room=item_def(a.type)->max_stack-b.count;
        if (room<=0) return CHEST_NO_MOVE;
        int n=single ? 1 : a.count<room ? a.count : room;
        b=(ItemStack){a.type,(uint8_t)(b.count+n),a.damage,a.enchants,a.custom};
        if (!(a.count-=n)) a=empty;
    } else {
        if (single) return CHEST_NO_MOVE;
        ItemStack swap=b; b=a; a=swap;
    }
    ChestBank *cb=bank();
    /* Only the destination can be new; the source already had an item. */
    if (dest<DOUBLE_CHEST_SLOTS && b.count && !lookup(cb,v->halves[dest/CHEST_SLOTS]) &&
        !reserve(cb,1)) return CHEST_NO_MEMORY;
    write_slot(cb,v,source,a);
    write_slot(cb,v,dest,b);
    for (int i=0;i<v->count;++i) {
        ChestPos p=v->halves[i];
        if (!nonempty(lookup(cb,p))) erase(cb,p);
        if (source<DOUBLE_CHEST_SLOTS || dest<DOUBLE_CHEST_SLOTS) redstone_container_changed(p.x,p.y,p.z);
    }
    return CHEST_OK;
}
const char *chest_break(ChestPos p) {
    int type=cell(p);
    if (!is_chest(type)) return NULL;
    ChestBank *b=bank();
    ItemStack release[CHEST_SLOTS+4];
    int n=0;
    if (player_rules.mode==SURVIVAL)
        release[n++]=(ItemStack){(uint16_t)(type==TRAPPED_CHEST ? TRAPPED_CHEST_ITEM : CHEST),1,0,0,0};
    int above=block(p.x,p.y+1,p.z);
    if (player_rules.mode==SURVIVAL && rail_type(above))
        release[n++]=(ItemStack){(uint16_t)mining_drop(above,0),1,0,0,0};
    n+=chest_blast_contents(p,release+n);
    int free_drops[CHEST_SLOTS+4],available=0;
    for (int i=0;i<MAXDROP && available<n;++i) if (!drops[i].item.count) free_drops[available++]=i;
    if (available<n) return "掉落区已满；箱子和物品均未移除";
    if (!set_block(p.x,p.y,p.z,AIR)) return "无法移除箱子";
    erase(b,p);
    for (int i=0;i<n;++i) {
        float x=p.x+.23f+(i%4)*.16f,z=p.z+.23f+(i/4%4)*.16f;
        drops[free_drops[i]]=make_drop(x,p.y+.48f,z,release[i]);
    }
    if (n) drops_dirty=1;
    return "箱子已拆下，内容可在附近拾取";
}
