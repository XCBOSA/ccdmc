#include "drops.h"
#include "armor.h"
#include "stack_transfer.h"
#include "../world/space.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Drop dimension_drops[DIMENSION_COUNT][MAXDROP];
enum { BUCKETS=509, ACTIVE_EDGE=2*DROP_SIM_RADIUS+1, ACTIVE_MAX=ACTIVE_EDGE*ACTIVE_EDGE };
typedef struct DropChunk {
    int x,z;
    size_t count,capacity;
    Drop *items;
    struct DropChunk *next;
} DropChunk;
typedef struct {
    DropChunk *buckets[BUCKETS];
    size_t count;
    int active_x,active_z,active_n,active_valid;
    DropChunk *active[ACTIVE_MAX];
} DeathDropBank;
static DeathDropBank banks[DIMENSION_COUNT];
static int keep_inventory;

Drop make_drop(float x,float y,float z,ItemStack item) {
    return (Drop){x,fmaxf(0.f,fminf(WY,y)),z,item,0,DROP_PICKUP_TICKS};
}
int drop_valid(Drop d) {
    return fabsf(d.x)<=WORLD_SAVE_LIMIT && fabsf(d.z)<=WORLD_SAVE_LIMIT &&
        d.y>=0 && d.y<=WY && item_stack_valid(d.item) && d.item.count &&
        d.age<DROP_EXPIRE_TICKS && d.pickup_delay<=DEATH_DROP_PICKUP_TICKS;
}
int drop_loose_item(Vec3 position,ItemStack item) {
    Drop drop=make_drop(position.x,position.y,position.z,item);
    if (!drop_valid(drop)) return 0;
    for (int i=0;i<MAXDROP;++i) if (!drops[i].item.count) {
        drops[i]=drop; drops_dirty=1;
        return 1;
    }
    return 0;
}
int death_keep_inventory(void) { return keep_inventory; }
int death_set_keep_inventory(int keep) {
    if (keep!=0 && keep!=1) return 0;
    keep_inventory=keep;
    return 1;
}
static int coordinate(float pos) { return (int)floorf(pos/CHUNK_SIZE); }
static unsigned bucket(int x,int z) {
    return (((uint32_t)x*0x9e3779b1u)^((uint32_t)z*0x85ebca77u))%BUCKETS;
}
static DropChunk **find(DeathDropBank *bank,int x,int z) {
    DropChunk **p=&bank->buckets[bucket(x,z)];
    while (*p && ((*p)->x!=x || (*p)->z!=z)) p=&(*p)->next;
    return p;
}
static void free_chunk(DropChunk **p) {
    DropChunk *c=*p;
    *p=c->next;
    free(c->items);
    free(c);
}
static Drop *reserve(Dimension d,int cx,int cz,size_t n) {
    DeathDropBank *bank=&banks[d];
    if (!n || n>DEATH_DROP_FILE_LIMIT-bank->count) return NULL;
    DropChunk **p=find(bank,cx,cz);
    DropChunk *c=*p;
    if (!c) {
        c=(DropChunk *)calloc(1,sizeof *c);
        if (!c) return NULL;
        c->x=cx; c->z=cz;
        *p=c;
        bank->active_valid=0;
    }
    size_t need=c->count+n;
    if (need>c->capacity) {
        size_t cap=c->capacity ? c->capacity : 8;
        while (cap<need) cap*=2u;
        Drop *items=(Drop *)realloc(c->items,cap*sizeof *items);
        if (!items) {
            if (!c->count) free_chunk(p);
            return NULL;
        }
        c->items=items; c->capacity=cap;
    }
    Drop *out=c->items+c->count;
    c->count=need;
    bank->count+=n;
    return out;
}
void drops_reset(void) {
    memset(dimension_drops,0,sizeof dimension_drops);
    for (int i=0;i<DIMENSION_COUNT;++i) {
        DeathDropBank *bank=&banks[i];
        for (int j=0;j<BUCKETS;++j) while (bank->buckets[j]) free_chunk(&bank->buckets[j]);
        bank->count=0;
        bank->active_valid=bank->active_n=0;
    }
    keep_inventory=0;
    drops_dirty=1;
}
size_t death_drop_count(Dimension d) { return (unsigned)d<DIMENSION_COUNT ? banks[d].count : 0; }
int death_drop_restore(Dimension d,Drop drop) {
    if ((unsigned)d>=DIMENSION_COUNT || !drop_valid(drop)) return 0;
    Drop *out=reserve(d,coordinate(drop.x),coordinate(drop.z),1);
    if (!out) return 0;
    *out=drop;
    drops_dirty=1;
    return 1;
}
int death_drop_visit(Dimension d,DropVisitor visit,void *ctx) {
    if ((unsigned)d>=DIMENSION_COUNT || !visit) return 0;
    for (int j=0;j<BUCKETS;++j) for (const DropChunk *c=banks[d].buckets[j];c;c=c->next)
        for (size_t i=0;i<c->count;++i) if (!visit(&c->items[i],ctx)) return 0;
    return 1;
}
static int nearby(float x,float z,int cx,int cz) {
    return abs(coordinate(x)-cx)<=DROP_SIM_RADIUS && abs(coordinate(z)-cz)<=DROP_SIM_RADIUS;
}
static void activate(DeathDropBank *bank,int cx,int cz) {
    if (bank->active_valid && bank->active_x==cx && bank->active_z==cz) return;
    bank->active_n=0;
    bank->active_x=cx; bank->active_z=cz; bank->active_valid=1;
    if (!bank->count) return;
    for (int x=cx-DROP_SIM_RADIUS;x<=cx+DROP_SIM_RADIUS;++x)
        for (int z=cz-DROP_SIM_RADIUS;z<=cz+DROP_SIM_RADIUS;++z) {
            DropChunk *c=*find(bank,x,z);
            if (c) bank->active[bank->active_n++]=c;
        }
}
void drops_visible(int cx,int cz,DropVisitor visit,void *ctx) {
    if (!visit) return;
    Dimension d=world_dimension();
    for (int i=0;i<MAXDROP;++i) {
        const Drop *p=&dimension_drops[d][i];
        if (p->item.count && nearby(p->x,p->z,cx,cz) && !visit(p,ctx)) return;
    }
    DeathDropBank *bank=&banks[d];
    activate(bank,cx,cz);
    for (int j=0;j<bank->active_n;++j) {
        const DropChunk *c=bank->active[j];
        for (size_t i=0;i<c->count;++i) if (!visit(&c->items[i],ctx)) return;
    }
}
void drops_collect_automatic(int cx,int cz,DropIntake intake,void *ctx) {
    if (!intake) return;
    for (int i=0;i<MAXDROP;++i) {
        Drop *p=&drops[i];
        if (!p->item.count || !nearby(p->x,p->z,cx,cz)) continue;
        int count=intake(p,ctx);
        if (count>0 && count<=p->item.count) { stack_deduct(&p->item,count); drops_dirty=1; }
    }
    DeathDropBank *b=&banks[world_dimension()];
    activate(b,cx,cz);
    for (int j=0;j<b->active_n;++j) {
        DropChunk *c=b->active[j];
        for (size_t i=0;i<c->count;) {
            Drop *p=&c->items[i];
            int count=intake(p,ctx);
            if (count<=0 || count>p->item.count) { ++i; continue; }
            stack_deduct(&p->item,count);
            drops_dirty=1;
            if (p->item.count) ++i;
            else { *p=c->items[--c->count]; --b->count; }
        }
        if (!c->count) { free_chunk(find(b,c->x,c->z)); b->active_valid=0; }
    }
}
void drops_anvil_impact(BlockBox strike,int exempt) {
    for (int i=0;i<MAXDROP;++i) if (i!=exempt && drops[i].item.count) {
        Drop *p=&drops[i];
        if (!box_contains(strike,p->x,p->y,p->z)) continue;
        p->item=(ItemStack){0,0,0,0,0};
        drops_dirty=1;
    }
    DeathDropBank *b=&banks[world_dimension()];
    if (!b->count) return;
    /* A strike fits strictly inside one block, hence inside one chunk. */
    DropChunk **link=find(b,coordinate(strike.x0),coordinate(strike.z0)),*c=*link;
    if (!c) return;
    for (size_t i=0;i<c->count;) {
        Drop *p=&c->items[i];
        if (!box_contains(strike,p->x,p->y,p->z)) { ++i; continue; }
        *p=c->items[--c->count];
        --b->count;
        drops_dirty=1;
    }
    if (!c->count) { free_chunk(link); b->active_valid=0; }
}
void death_discard_inventory(void) {
    memset(inventory,0,sizeof inventory);
    armor_reset();
}
static float scatter(float at,int chunk,float displacement) {
    float low=chunk*CHUNK_SIZE;
    /* Keep the all-or-nothing transaction in one chunk, including negative seams. */
    return fmaxf(-WORLD_SAVE_LIMIT,fminf(WORLD_SAVE_LIMIT,
        fmaxf(low,fminf(nextafterf(low+CHUNK_SIZE,low),at+displacement))));
}
int death_drop_inventory(Vec3 at) {
    if (keep_inventory) return 1;
    ItemStack items[SLOT_COUNT+ARMOR_SLOTS];
    int n=0;
    for (int i=0;i<SLOT_COUNT;++i) if (inventory[i].count) items[n++]=inventory[i];
    for (int i=0;i<ARMOR_SLOTS;++i) {
        ItemStack worn=armor_worn(i);
        if (worn.count) items[n++]=worn;
    }
    if (!n) return 1;
    at.x=fmaxf(-WORLD_SAVE_LIMIT,fminf(WORLD_SAVE_LIMIT,at.x));
    at.z=fmaxf(-WORLD_SAVE_LIMIT,fminf(WORLD_SAVE_LIMIT,at.z));
    int cx=coordinate(at.x),cz=coordinate(at.z);
    Drop *out=reserve(world_dimension(),cx,cz,(size_t)n);
    if (!out) return 0;
    for (int i=0;i<n;++i) {
        float theta=i*2.39996323f, radius=i ? .25f+.56f*sqrtf((float)i/n) : 0.f;
        float x=scatter(at.x,cx,cosf(theta)*radius),z=scatter(at.z,cz,sinf(theta)*radius);
        if (world_point_blocked(x,at.y+.30f,z,1)) { x=at.x; z=at.z; }
        out[i]=make_drop(x,at.y+.38f+(i%4)*.08f,z,items[i]);
        out[i].pickup_delay=DEATH_DROP_PICKUP_TICKS;
    }
    death_discard_inventory();
    drops_dirty=1;
    return 1;
}
static int step_drop(Drop *p,float dt,unsigned ticks) {
    unsigned age=p->age+ticks;
    if (age>=DROP_EXPIRE_TICKS) { p->item=(ItemStack){0,0,0,0,0}; drops_dirty=1; return 0; }
    p->age=(uint16_t)age;
    p->pickup_delay=p->pickup_delay>ticks ? (uint16_t)(p->pickup_delay-ticks) : 0;
    float next=p->y-2.f*dt;
    int x=(int)floorf(p->x),z=(int)floorf(p->z),y=(int)floorf(next-.13f);
    if (solid(block(x,y,z))) next=y+1.13f;
    if (next<0.f || block(x,(int)floorf(next),z)==LAVA) {
        p->item=(ItemStack){0,0,0,0,0}; drops_dirty=1;
        return 0;
    }
    if (next<p->y-.001f) { p->y=next; drops_dirty=1; }
    float dx=p->x-px,dz=p->z-pz,dy=p->y-(py+.80f);
    if (!p->pickup_delay && dx*dx+dy*dy+dz*dz<2.25f && inventory_collect(&p->item)) {
        if (!p->item.count) drops_dirty=1;
        return 1;
    }
    return 0;
}
int drops_step(float dt,unsigned ticks) {
    Dimension d=world_dimension();
    int cx=coordinate(px),cz=coordinate(pz),picked=0;
    DeathDropBank *bank=&banks[d];
    for (int i=0;i<MAXDROP;++i) {
        Drop *p=&dimension_drops[d][i];
        if (p->item.count && nearby(p->x,p->z,cx,cz)) picked|=step_drop(p,dt,ticks);
    }
    activate(bank,cx,cz);
    for (int j=0;j<bank->active_n;++j) {
        DropChunk *c=bank->active[j];
        for (size_t i=0;i<c->count;) {
            picked|=step_drop(&c->items[i],dt,ticks);
            if (c->items[i].item.count) ++i;
            else { c->items[i]=c->items[--c->count]; --bank->count; }
        }
        if (!c->count) {
            free_chunk(find(bank,c->x,c->z));
            bank->active_valid=0;
        }
    }
    if (picked) { printf("PICKUP\n"); fflush(stdout); }
    return picked;
}
