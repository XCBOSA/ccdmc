#include "lava.h"
#include "redstone.h"
#include <stdlib.h>
#include <string.h>

enum { LAVA_BUCKETS=4093, LAVA_BUDGET=96, NO_OUTLET=99 };
typedef struct PendingLava {
    int x,y,z;
    uint64_t due;
    struct PendingLava *next,*same;
} PendingLava;
typedef struct {
    PendingLava *head,*tail,*buckets[LAVA_BUCKETS];
    uint64_t tick;
} LavaBank;
static LavaBank lava[DIMENSION_COUNT];
static const int dx[]={1,-1,0,0}, dz[]={0,0,1,-1};

static int period(Dimension dim) { return dim==NETHER ? LAVA_NETHER_TICKS : LAVA_SURFACE_TICKS; }
static unsigned bucket(int x,int y,int z) {
    return (hash2(x,z)^((unsigned)y*2654435761u))%LAVA_BUCKETS;
}
static int bounded(int x,int y,int z) {
    return x>=-WORLD_SAVE_LIMIT && x<=WORLD_SAVE_LIMIT && z>=-WORLD_SAVE_LIMIT &&
        z<=WORLD_SAVE_LIMIT && inworld(x,y,z);
}
static int enqueue(int x,int y,int z,int delay,int restoring) {
    if (!bounded(x,y,z)) return !restoring;
    LavaBank *bank=&lava[world_dimension()];
    unsigned h=bucket(x,y,z);
    for (PendingLava *p=bank->buckets[h];p;p=p->same)
        if (p->x==x && p->y==y && p->z==z) return !restoring;
    uint64_t due=bank->tick+(unsigned)delay;
    /* Same delay for live updates; the saved FIFO is sorted by deadline. */
    if (bank->tail && bank->tail->due>due) return 0;
    PendingLava *p=(PendingLava *)malloc(sizeof *p);
    if (!p) return 0;
    *p=(PendingLava){x,y,z,due,NULL,bank->buckets[h]};
    bank->buckets[h]=p;
    if (bank->tail) bank->tail->next=p;
    else bank->head=p;
    bank->tail=p;
    return 1;
}
int lava_restore(int x,int y,int z,int delay) {
    return delay>=1 && delay<=period(world_dimension()) && enqueue(x,y,z,delay,1);
}
int lava_visit(Dimension dim,WorldRecord visit,void *context) {
    if ((unsigned)dim>=DIMENSION_COUNT || !visit) return 0;
    const LavaBank *bank=&lava[dim];
    for (const PendingLava *p=bank->head;p;p=p->next)
        if (!visit(context,p->x,p->y,p->z,p->due>bank->tick ? (int)(p->due-bank->tick) : 1)) return 0;
    return 1;
}
void lava_reset(void) {
    for (int d=0;d<DIMENSION_COUNT;++d) {
        while (lava[d].head) {
            PendingLava *p=lava[d].head;
            lava[d].head=p->next;
            free(p);
        }
        memset(&lava[d],0,sizeof lava[d]);
    }
}

static int may_fill(int raw) {
    return raw==AIR || raw==FIRE_CELL || raw==SNOW_LAYER || raw==TALL_GRASS ||
        raw==TORCH || raw==SUGAR_CANE || filter_mat_cell(raw) || crop_type(cell_type(raw)) ||
        circuit_cell(raw) || lava_flow_cell(raw);
}
static int hot(int x,int y,int z) {
    int raw;
    return world_resident_cell(x,y,z,&raw) && lava_strength(raw);
}
void lava_notify_edit(int x,int y,int z,int before,int after) {
    if (before==after || !bounded(x,y,z)) return;
    if (!lava_strength(before) && !lava_strength(after)) {
        if (may_fill(before)==may_fill(after) && !!water_strength(before)==!!water_strength(after)) return;
        int active=hot(x,y+1,z);
        for (int i=0;!active && i<4;++i)
            active=hot(x+dx[i],y,z+dz[i]) || hot(x+dx[i],y+1,z+dz[i]);
        if (!active) active=hot(x,y-1,z); /* Water placed above a source. */
        if (!active) return;
    }
    const int delay=period(world_dimension());
    (void)enqueue(x,y,z,delay,0);
    (void)enqueue(x,y+1,z,delay,0);
    (void)enqueue(x,y-1,z,delay,0);
    for (int i=0;i<4;++i) {
        (void)enqueue(x+dx[i],y,z+dz[i],delay,0);
        (void)enqueue(x+dx[i],y+1,z+dz[i],delay,0);
    }
}

/* Side/up water hardens lava; a downward contact is handled at the
   receiving water cell instead. A missing chunk leaves the answer pending. */
static int touching_water(int x,int y,int z) {
    int cell,missing=0;
    if (world_resident_cell(x,y+1,z,&cell) && water_strength(cell)) return 1;
    for (int i=0;i<4;++i) {
        if (!world_resident_cell(x+dx[i],y,z+dz[i],&cell)) missing=1;
        else if (water_strength(cell)) return 1;
    }
    return missing ? -1 : 0;
}

static int can_drop(int raw) { return may_fill(raw) || water_strength(raw); }

/* Prefer the closest downhill outlet but don't load terrain to find one.
   The depth is small (two/four steps); avoiding immediate backtracking is
   sufficient because any loop is longer than the shortest path. */
static int drop_distance(int x,int y,int z,int reverse,int remaining) {
    int raw;
    if (!world_resident_cell(x,y,z,&raw) || !may_fill(raw)) return NO_OUTLET;
    if (y>0 && world_resident_cell(x,y-1,z,&raw) && can_drop(raw) && !lava_flow_cell(raw)) return 0;
    if (!remaining) return NO_OUTLET;
    int best=NO_OUTLET;
    for (int i=0;i<4;++i) if (i!=reverse) {
        int cost=drop_distance(x+dx[i],y,z+dz[i],i^1,remaining-1)+1;
        if (cost<best) best=cost;
    }
    return best;
}
static int sender_strength(int x,int y,int z,int raw,int to_side,int slope,int loss) {
    int strength=lava_strength(raw)-loss,below;
    if (strength<=0) return 0;
    if (y>0 && world_resident_cell(x,y-1,z,&below) && can_drop(below)) return 0;
    int target=drop_distance(x+dx[to_side],y,z+dz[to_side],to_side^1,slope-1);
    if (target==0) return strength;
    for (int i=0;i<4;++i) if (i!=to_side) {
        if (drop_distance(x+dx[i],y,z+dz[i],i^1,slope-1)<target) return 0;
    }
    return strength;
}

/* Returns false only for incomplete terrain or a failed transactional edit. */
static int update(int x,int y,int z) {
    int raw,above;
    if (!world_resident_cell(x,y,z,&raw)) return 0;
    if (!lava_strength(raw) && !water_strength(raw) && !may_fill(raw)) return 1;
    if (!world_resident_cell(x,y+1,z,&above)) return 0;
    int product=lava_quenched_block(raw),after=raw;
    if (product!=AIR) {
        int wet=touching_water(x,y,z);
        if (wet<0) return 0;
        if (wet) after=product;
    }
    if (after==raw && raw!=LAVA) {
        int falling=0;
        if (lava_strength(above)) {
            /* A source with side contact must harden before it can flow. */
            int wet=lava_quenched_block(above)!=AIR ? touching_water(x,y+1,z) : 0;
            if (wet<0) return 0;
            falling=!wet;
        }
        if (falling) after=water_strength(raw) ? STONE : LAVA_FALLING;
        else if (!water_strength(raw)) {
            int nether=world_dimension()==NETHER && !inside_pressure_works(), strength=0;
            for (int i=0;i<4;++i) {
                int a=x+dx[i],b=z+dz[i],source;
                if (!world_resident_cell(a,y,b,&source)) return 0;
                int from=sender_strength(a,y,b,source,i^1,nether ? 4 : 2,nether ? 1 : 2);
                if (from>strength) strength=from;
            }
            after=strength ? LAVA_FLOW_BASE+strength-1 : lava_flow_cell(raw) ? AIR : raw;
        }
    }
    if (after==raw) return 1;
    if (circuit_cell(raw)) return redstone_replace(x,y,z,after);
    BlockEdit edit={x,y,z,after};
    return world_set_blocks(&edit,1);
}

void lava_step(void) {
    Dimension dim=world_dimension();
    LavaBank *bank=&lava[dim];
    ++bank->tick;
    for (int n=0;n<LAVA_BUDGET && bank->head && bank->head->due<=bank->tick;++n) {
        PendingLava *p=bank->head;
        bank->head=p->next;
        if (!bank->head) bank->tail=NULL;
        PendingLava **link=&bank->buckets[bucket(p->x,p->y,p->z)];
        while (*link!=p) link=&(*link)->same;
        *link=p->same;
        if (!update(p->x,p->y,p->z)) (void)enqueue(p->x,p->y,p->z,period(dim),0);
        free(p);
    }
}
