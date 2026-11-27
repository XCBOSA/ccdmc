#include "hoppers.h"
#include "container_automation.h"
#include "drops.h"
#include "stack_transfer.h"
#include "../world/redstone.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { BUCKETS=257 };
typedef struct Node { Hopper h; unsigned long long catch_tick; struct Node *next; } Node;
typedef struct Chunk {
    int x,z;
    Node *head;
    struct Chunk *next;
} Chunk;
typedef struct { size_t count; Chunk *buckets[BUCKETS]; unsigned long long tick; } Bank;
static Bank banks[DIMENSION_COUNT];
static Bank *bank(void) { return &banks[world_dimension()]; }
static int chunk_of(int x) { return x/CHUNK_SIZE-(x%CHUNK_SIZE<0); }
static Chunk **chunk_link(Bank *b,int x,int z) {
    Chunk **p=&b->buckets[((uint32_t)x*374761393u^(uint32_t)z*668265263u)%BUCKETS];
    while (*p && ((*p)->x!=x || (*p)->z!=z)) p=&(*p)->next;
    return p;
}
static Node **node_link(Chunk *c,int x,int y,int z) {
    Node **n=&c->head;
    while (*n && ((*n)->h.x!=x || (*n)->h.y!=y || (*n)->h.z!=z)) n=&(*n)->next;
    return n;
}
static Node *find(int x,int y,int z) {
    Chunk *c=*chunk_link(bank(),chunk_of(x),chunk_of(z));
    return c ? *node_link(c,x,y,z) : NULL;
}
const Hopper *hopper_at(int x,int y,int z) {
    Node *n=find(x,y,z);
    return n ? &n->h : NULL;
}
static Node *create(int x,int y,int z) {
    Bank *b=bank();
    if (b->count>=HOPPER_LIMIT) return NULL;
    Chunk **p=chunk_link(b,chunk_of(x),chunk_of(z));
    Node *n=(Node *)calloc(1,sizeof *n);
    if (!n) return NULL;
    if (!*p) {
        *p=(Chunk *)calloc(1,sizeof **p);
        if (!*p) { free(n); return NULL; }
        (*p)->x=chunk_of(x); (*p)->z=chunk_of(z);
    }
    n->h.x=x; n->h.y=y; n->h.z=z;
    Node **link=&(*p)->head;
    /* A save/restore must not reverse the simulation order of a chain. */
    while (*link) {
        const Hopper *other=&(*link)->h;
        if (other->x>x || (other->x==x && (other->z>z || (other->z==z && other->y>y)))) break;
        link=&(*link)->next;
    }
    n->next=*link; *link=n;
    ++b->count;
    return n;
}
void hopper_forget(int x,int y,int z) {
    Bank *b=bank();
    Chunk **p=chunk_link(b,chunk_of(x),chunk_of(z)),*c=*p;
    if (!c) return;
    Node **n=node_link(c,x,y,z);
    if (!*n) return;
    Node *dead=*n; *n=dead->next; free(dead);
    --b->count;
    if (!c->head) { *p=c->next; free(c); }
}
void hoppers_reset(void) {
    for (int d=0;d<DIMENSION_COUNT;++d) {
        Bank *b=&banks[d];
        for (int i=0;i<BUCKETS;++i) while (b->buckets[i]) {
            Chunk *c=b->buckets[i];
            b->buckets[i]=c->next;
            while (c->head) { Node *n=c->head; c->head=n->next; free(n); }
            free(c);
        }
        b->count=0;
        b->tick=0;
    }
}
int hoppers_visit(Dimension d,HopperVisitor visit,void *context) {
    if ((unsigned)d>=DIMENSION_COUNT || !visit) return 0;
    for (int i=0;i<BUCKETS;++i) for (Chunk *c=banks[d].buckets[i];c;c=c->next)
        for (Node *n=c->head;n;n=n->next) if (!visit(&n->h,context)) return 0;
    return 1;
}
static int valid_position(int x,int y,int z) {
    return (unsigned)y<WY && x>=-WORLD_SAVE_LIMIT && x<=WORLD_SAVE_LIMIT &&
        z>=-WORLD_SAVE_LIMIT && z<=WORLD_SAVE_LIMIT;
}
int hopper_restore(const Hopper *h) {
    if (!h || !valid_position(h->x,h->y,h->z) || h->cooldown>HOPPER_TICKS ||
        !hopper_cell(world_recorded_cell(world_dimension(),h->x,h->y,h->z)) ||
        find(h->x,h->y,h->z)) return 0;
    for (int i=0;i<HOPPER_SLOTS;++i) if (!item_stack_valid(h->slots[i])) return 0;
    Node *n=create(h->x,h->y,h->z);
    if (!n) return 0;
    n->h=*h;
    return 1;
}
int hopper_place(Hit h) {
    if (!h.hit || !valid_position(h.ax,h.ay,h.az) || !world_chunk_ready(h.ax,h.az) ||
        world_cell(h.ax,h.ay,h.az)!=AIR || find(h.ax,h.ay,h.az)) return 0;
    int raw=HOPPER_DOWN;
    if (h.y==h.ay) for (int dir=0;dir<4;++dir)
        if (h.x==h.ax+rail_dx(dir) && h.z==h.az+rail_dz(dir)) { raw=HOPPER_BASE+dir; break; }
    Node *n=create(h.ax,h.ay,h.az);
    if (!n) return 0;
    const BlockEdit edit={h.ax,h.ay,h.az,raw};
    if (world_set_blocks(&edit,1)) return 1;
    hopper_forget(h.ax,h.ay,h.az);
    return 0;
}
const char *hopper_break(int x,int y,int z) {
    Node *n=find(x,y,z);
    if (!n || block(x,y,z)!=HOPPER) return "漏斗已不在这里";
    ItemStack out[HOPPER_SLOTS+1];
    int count=0,free_drop[HOPPER_SLOTS+1],available=0;
    if (player_rules.mode==SURVIVAL) out[count++]=(ItemStack){HOPPER_ITEM,1,0,0,0};
    for (int i=0;i<HOPPER_SLOTS;++i) if (n->h.slots[i].count) out[count++]=n->h.slots[i];
    for (int i=0;i<MAXDROP&&available<count;++i) if (!drops[i].item.count) free_drop[available++]=i;
    if (available!=count) return "掉落物已满，未破坏漏斗";
    if (!set_block(x,y,z,AIR)) return "无法记录漏斗拆除";
    for (int i=0;i<count;++i) drops[free_drop[i]]=make_drop(x+.5f,y+.28f,z+.5f,out[i]);
    drops_dirty|=count!=0;
    return "已拆除漏斗";
}
static void changed(const Hopper *h) { redstone_container_changed(h->x,h->y,h->z); }
int hopper_move(int x,int y,int z,int from,int to,int single) {
    Node *n=find(x,y,z);
    if (!n || !storage_stack_move(n->h.slots,HOPPER_SLOTS,from,to,single)) return 0;
    if (from<HOPPER_SLOTS || to<HOPPER_SLOTS) changed(&n->h);
    return 1;
}
static int receive(Hopper *h,ItemStack item,int limit) {
    int accepted=0,empty=1;
    for (int i=0;i<HOPPER_SLOTS;++i) empty&=!h->slots[i].count;
    for (int i=0;i<HOPPER_SLOTS && accepted<limit && item.count;++i) {
        int count=stack_receive(&h->slots[i],item,limit-accepted);
        accepted+=count;
        stack_deduct(&item,count);
    }
    if (accepted) {
        if (empty) h->cooldown=HOPPER_TICKS;
        changed(h);
    }
    return accepted;
}
int hopper_insert(int x,int y,int z,ItemStack item) {
    Node *n=find(x,y,z);
    return n ? receive(&n->h,item,1) : 0;
}
int hopper_extract(int x,int y,int z,int slot) {
    Node *n=find(x,y,z);
    if (!n || (unsigned)slot>=HOPPER_SLOTS || !n->h.slots[slot].count) return 0;
    stack_deduct(&n->h.slots[slot],1);
    changed(&n->h);
    return 1;
}
static void push(Hopper *h,int raw,int *changed_own) {
    int dir=raw-HOPPER_BASE,down=raw==HOPPER_DOWN;
    int x=h->x+(down ? 0 : rail_dx(dir)), y=h->y-down, z=h->z+(down ? 0 : rail_dz(dir));
    if (!valid_position(x,y,z) || !world_chunk_ready(x,z)) return;
    for (int i=0;i<HOPPER_SLOTS;++i) {
        ItemStack item=h->slots[i];
        if (!item.count) continue;
        int placed=container_insert_one(x,y,z,-down,item);
        if (placed==NO_INVENTORY) return;
        if (placed==INVENTORY_INSERTED) {
            stack_deduct(&h->slots[i],1);
            *changed_own=1;
            break;
        }
    }
}
static int receive_one(ItemStack item,void *ctx) { return receive((Hopper *)ctx,item,1); }
static int absorb(const Drop *drop,void *context) {
    Bank *b=(Bank *)context;
    int x=(int)floorf(drop->x),y=(int)floorf(drop->y),z=(int)floorf(drop->z);
    for (int down=1;down>=0;--down) {
        Node *n=find(x,y-down,z);
        if (!n || n->catch_tick!=b->tick || drop->y<n->h.y+.63f ||
            drop->y>n->h.y+1.5f) continue;
        int count=receive(&n->h,drop->item,drop->item.count);
        if (count) { n->h.cooldown=HOPPER_TICKS; n->catch_tick=0; return count; }
    }
    return 0;
}
void hoppers_step(int player_x,int player_z) {
    Bank *b=bank();
    if (!b->count) return;
    if (!++b->tick) ++b->tick;
    int cx=chunk_of(player_x),cz=chunk_of(player_z),catching=0;
    /* Decrement once before all transfers; a newly filled receiver cannot
       relay a stack the same tick based on chunk traversal order. */
    for (int pass=0;pass<2;++pass)
        for (int x=cx-HOPPER_RADIUS;x<=cx+HOPPER_RADIUS;++x)
            for (int z=cz-HOPPER_RADIUS;z<=cz+HOPPER_RADIUS;++z) {
                Chunk *c=*chunk_link(b,x,z);
                for (Node *n=c ? c->head : NULL;n;n=n->next) {
                    Hopper *h=&n->h;
                    if (!world_chunk_ready(h->x,h->z)) continue;
                    if (!pass) { if (h->cooldown) --h->cooldown; continue; }
                    if (h->cooldown || redstone_receives(h->x,h->y,h->z)) continue;
                    int raw=world_cell(h->x,h->y,h->z);
                    if (!hopper_cell(raw)) continue;
                    int sent=0;
                    push(h,raw,&sent);
                    int source=container_extract_one(h->x,h->y+1,h->z,receive_one,h);
                    if (sent) changed(h);
                    if (sent || source==INVENTORY_INSERTED) h->cooldown=HOPPER_TICKS;
                    if (source==NO_INVENTORY) { n->catch_tick=b->tick; catching=1; }
                }
            }
    if (catching) drops_collect_automatic(cx,cz,absorb,b);
}
int hopper_boxes(int x,int y,int z,int raw,BlockBox out[HOPPER_PARTS]) {
    if (!hopper_cell(raw) || !out) return 0;
    out[0]=(BlockBox){x+.0f,y+.375f,z+.0f,x+1.f,y+.625f,z+1.f};
    out[1]=(BlockBox){x+.0f,y+.625f,z+.0f,x+.125f,y+1.f,z+1.f};
    out[2]=(BlockBox){x+.875f,y+.625f,z+.0f,x+1.f,y+1.f,z+1.f};
    out[3]=(BlockBox){x+.125f,y+.625f,z+.0f,x+.875f,y+1.f,z+.125f};
    out[4]=(BlockBox){x+.125f,y+.625f,z+.875f,x+.875f,y+1.f,z+1.f};
    int dir=raw-HOPPER_BASE,dx=raw==HOPPER_DOWN ? 0 : rail_dx(dir),dz=raw==HOPPER_DOWN ? 0 : rail_dz(dir);
    out[5]=(BlockBox){x+(dx<0 ? 0.f : .375f),y+(raw==HOPPER_DOWN ? 0.f : .25f),
        z+(dz<0 ? 0.f : .375f),x+(dx>0 ? 1.f : .625f),y+.375f,z+(dz>0 ? 1.f : .625f)};
    return HOPPER_PARTS;
}
