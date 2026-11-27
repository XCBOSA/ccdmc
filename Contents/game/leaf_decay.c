#include "leaf_decay.h"
#include "drops.h"
#include "forestry.h"
#include <stdlib.h>
#include <string.h>

enum { HASH_SIZE=1021, ACTIVE_RADIUS=4, WINDOW=2*ACTIVE_RADIUS+1,
       CELL_BUDGET=192, TRACE_BUDGET=16, REGION_SLICE=24, REGION_TRIES=4, RETRY=10,
       SUPPORT_DISTANCE=4, SUPPORT_EDGE=2*SUPPORT_DISTANCE+1, SUPPORT_NODES=63 };
typedef struct ChunkWork ChunkWork;
typedef struct Region {
    LeafWork work;
    uint64_t due;
    ChunkWork *owner;
    struct Region *next,*prev,*prepared;
} Region;
struct ChunkWork {
    int x,z;
    Region *head,*tail;
    struct ChunkWork *next;
};
typedef struct {
    ChunkWork *buckets[HASH_SIZE];
    uint64_t tick;
    unsigned cursor;
    size_t count;
    Region *prepared;
} DecayBank;
static DecayBank banks[DIMENSION_COUNT];
static const int directions[6][3]={{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};
static int chunk_coord(int x) { return x/CHUNK_SIZE-(x%CHUNK_SIZE<0); }
static int tile_origin(int x) { return (x/LEAF_TILE-(x%LEAF_TILE<0))*LEAF_TILE; }
static int empty(const LeafWork *w) {
    for (int i=0;i<LEAF_TILE;++i) if (w->layers[i]) return 0;
    return 1;
}
static int valid(int x,int y,int z) {
    return x>=-WORLD_SAVE_LIMIT && x<=WORLD_SAVE_LIMIT && z>=-WORLD_SAVE_LIMIT &&
        z<=WORLD_SAVE_LIMIT && (unsigned)y<WY;
}
static ChunkWork **chunk_link(DecayBank *bank,int cx,int cz) {
    unsigned hash=((unsigned)cx*374761393u^(unsigned)cz*668265263u)%HASH_SIZE;
    ChunkWork **link=&bank->buckets[hash];
    while (*link && ((*link)->x!=cx || (*link)->z!=cz)) link=&(*link)->next;
    return link;
}
static Region *lookup(ChunkWork *chunk,int x,int y,int z) {
    if (!chunk) return NULL;
    for (Region *r=chunk->head;r;r=r->next)
        if (r->work.x==x && r->work.y==y && r->work.z==z) return r;
    return NULL;
}
static void append(ChunkWork *c,Region *r) {
    r->prev=c->tail;
    r->next=NULL;
    if (c->tail) c->tail->next=r; else c->head=r;
    c->tail=r;
}
static void detach(ChunkWork *c,Region *r) {
    if (r->prev) r->prev->next=r->next; else c->head=r->next;
    if (r->next) r->next->prev=r->prev; else c->tail=r->prev;
}
static void erase(DecayBank *bank,Region *r) {
    ChunkWork *owner=r->owner;
    detach(owner,r);
    free(r);
    --bank->count;
    if (!owner->head) {
        ChunkWork **link=chunk_link(bank,owner->x,owner->z);
        *link=owner->next;
        free(owner);
    }
}
static Region *allocate(DecayBank *bank,ChunkWork **link,int x,int y,int z,int staging) {
    if (bank->count>=LEAF_WORK_LIMIT) return NULL;
    ChunkWork *owner=*link;
    Region *r=(Region *)calloc(1,sizeof *r);
    if (!r) return NULL;
    if (!owner) {
        owner=(ChunkWork *)calloc(1,sizeof *owner);
        if (!owner) { free(r); return NULL; }
        owner->x=chunk_coord(x); owner->z=chunk_coord(z);
        *link=owner;
    }
    r->owner=owner;
    r->work.x=x; r->work.y=y; r->work.z=z;
    if (staging) {
        r->prepared=bank->prepared;
        bank->prepared=r;
        r->due=bank->tick+8+(hash2(x+31*y,z)%19u);
    }
    append(owner,r);
    ++bank->count;
    return r;
}
static int dirty_radius(int before,int after) {
    if (before==after) return 0;
    if (tree_log_species(before)>=0) return tree_log_species(after)<0 ? SUPPORT_DISTANCE : 0;
    int leaf=before==LEAF || (before>=TREE_LEAF_BASE && before<TREE_LEAF_END) || placed_leaf_cell(before);
    return leaf && cell_type(after)!=LEAF ? 1 : 0;
}
static int max(int x,int y) { return x>y ? x : y; }
static int min(int x,int y) { return x<y ? x : y; }
static int mark(int x,int y,int z,int radius,int staging) {
    if (!radius || !valid(x,y,z)) return 1;
    DecayBank *bank=&banks[world_dimension()];
    int x0=max(-WORLD_SAVE_LIMIT,x-radius),x1=min(WORLD_SAVE_LIMIT,x+radius);
    int y0=max(0,y-radius),y1=min(WY-1,y+radius);
    int z0=max(-WORLD_SAVE_LIMIT,z-radius),z1=min(WORLD_SAVE_LIMIT,z+radius);
    for (int a=tile_origin(x0);a<=x1;a+=LEAF_TILE) for (int b=tile_origin(z0);b<=z1;b+=LEAF_TILE) {
        ChunkWork **link=chunk_link(bank,chunk_coord(a),chunk_coord(b));
        int begin=max(z0,b)-b,end=min(z1,b+LEAF_TILE-1)-b;
        unsigned row=((1u<<(end-begin+1))-1u)<<begin;
        uint64_t mask=0;
        for (int i=max(x0,a)-a;i<=min(x1,a+LEAF_TILE-1)-a;++i) mask|=(uint64_t)row<<(LEAF_TILE*i);
        for (int h=tile_origin(y0);h<=y1;h+=LEAF_TILE) {
            Region *r=lookup(*link,a,h,b);
            if (staging) {
                if (!r && !allocate(bank,link,a,h,b,1)) return 0;
            } else if (r) {
                for (int j=max(h,y0)-h;j<=min(y1,h+LEAF_TILE-1)-h;++j) r->work.layers[j]|=mask;
            }
        }
    }
    return 1;
}
int leaves_prepare_edit(int x,int y,int z,int before,int after) { return mark(x,y,z,dirty_radius(before,after),1); }
void leaves_commit_edit(int x,int y,int z,int before,int after) { (void)mark(x,y,z,dirty_radius(before,after),0); }
void leaves_finish_edits(void) {
    DecayBank *bank=&banks[world_dimension()];
    while (bank->prepared) {
        Region *r=bank->prepared;
        bank->prepared=r->prepared; r->prepared=NULL;
        if (empty(&r->work)) erase(bank,r);
    }
}
void leaves_reset(void) {
    for (int d=0;d<DIMENSION_COUNT;++d) for (int i=0;i<HASH_SIZE;++i) {
        ChunkWork *c=banks[d].buckets[i];
        while (c) {
            Region *r=c->head;
            while (r) { Region *next=r->next; free(r); r=next; }
            ChunkWork *next=c->next; free(c); c=next;
        }
    }
    memset(banks,0,sizeof banks);
}
size_t leaves_pending(Dimension d) { return (unsigned)d<DIMENSION_COUNT ? banks[d].count : 0; }
int leaves_visit(Dimension d,LeafWorkVisitor visitor,void *context) {
    if ((unsigned)d>=DIMENSION_COUNT || !visitor) return 0;
    DecayBank *bank=&banks[d];
    for (int i=0;i<HASH_SIZE;++i) for (ChunkWork *c=bank->buckets[i];c;c=c->next)
        for (Region *r=c->head;r;r=r->next) {
            LeafWork snapshot=r->work;
            snapshot.delay=r->due>bank->tick ? (unsigned)(r->due-bank->tick) : 0;
            if (!visitor(&snapshot,context)) return 0;
        }
    return 1;
}
int leaves_restore(Dimension d,const LeafWork *work) {
    if ((unsigned)d>=DIMENSION_COUNT || !work || !valid(work->x,work->y,work->z) ||
        work->x%LEAF_TILE || work->y%LEAF_TILE || work->z%LEAF_TILE ||
        work->cursor>LEAF_TILE*64 || work->delay>LEAF_DELAY_LIMIT || empty(work)) return 0;
    uint64_t allowed=UINT64_MAX;
    if (work->x==WORLD_SAVE_LIMIT) allowed&=255u;
    if (work->z==WORLD_SAVE_LIMIT) allowed&=UINT64_C(0x0101010101010101);
    for (int i=0;i<LEAF_TILE;++i) if (work->layers[i]&~allowed) return 0;
    DecayBank *bank=&banks[d];
    ChunkWork **link=chunk_link(bank,chunk_coord(work->x),chunk_coord(work->z));
    if (lookup(*link,work->x,work->y,work->z)) return 0;
    Region *r=allocate(bank,link,work->x,work->y,work->z,0);
    if (!r) return 0;
    r->work=*work; r->due=bank->tick+work->delay;
    return 1;
}

enum { NO_LOG, HAS_LOG, UNLOADED };
typedef struct { int8_t x,y,z,distance; } Offset;
/* Breadth-first, only through leaves. A hole is not proof of decay;
   unknown space is retried when the camera eventually loads it. */
static int support(int x,int y,int z) {
    uint8_t seen[SUPPORT_EDGE][SUPPORT_EDGE][SUPPORT_EDGE]={0};
    Offset queue[SUPPORT_NODES]={{0,0,0,0}};
    int count=1,unknown=0;
    seen[SUPPORT_DISTANCE][SUPPORT_DISTANCE][SUPPORT_DISTANCE]=1;
    for (int i=0;i<count;++i) {
        Offset node=queue[i];
        int distance=node.distance+1;
        for (int k=0;k<6;++k) {
            int a=node.x+directions[k][0],h=node.y+directions[k][1],b=node.z+directions[k][2],raw;
            uint8_t *visited=&seen[a+SUPPORT_DISTANCE][h+SUPPORT_DISTANCE][b+SUPPORT_DISTANCE];
            if (*visited) continue;
            *visited=1;
            if (!valid(x+a,y+h,z+b)) continue;
            if (!world_resident_cell(x+a,y+h,z+b,&raw)) { unknown=1; continue; }
            int type=cell_type(raw);
            if (tree_log_species(type)>=0) return HAS_LOG;
            if (type==LEAF && distance<SUPPORT_DISTANCE && count<SUPPORT_NODES)
                queue[count++]=(Offset){(int8_t)a,(int8_t)h,(int8_t)b,(int8_t)distance};
        }
    }
    return unknown ? UNLOADED : NO_LOG;
}
static int decay(int x,int y,int z,int raw) {
    int item=tree_leaf_drop(raw,x,y,z,(ItemStack){0,0,0,0,0});
    int free_slot=0;
    if (item) {
        while (free_slot<MAXDROP && drops[free_slot].item.count) ++free_slot;
        if (free_slot==MAXDROP) return 0;
    }
    BlockEdit remove={x,y,z,AIR};
    if (!world_set_blocks(&remove,1)) return 0;
    if (item) {
        drops[free_slot]=make_drop(x+.5f,y+.38f,z+.5f,(ItemStack){(uint16_t)item,1,0,0,0});
        drops_dirty=1;
    }
    return 1;
}
static int next_bit(const LeafWork *w) {
    for (unsigned i=w->cursor/64;i<LEAF_TILE;++i) {
        uint64_t bits=w->layers[i];
        if (i==w->cursor/64) bits&=UINT64_MAX<<(w->cursor%64);
        if (bits) return (int)(i*64+(unsigned)__builtin_ctzll(bits));
    }
    return -1;
}
static void service(DecayBank *bank,ChunkWork *chunk,int *cells,int *traces) {
    Region *r=chunk->head;
    for (int tried=0;tried<REGION_TRIES && r->due>bank->tick && r->next;++tried) r=r->next;
    if (r->due>bank->tick) return;
    LeafWork *w=&r->work;
    int probe;
    if (!world_resident_cell(w->x,w->y,w->z,&probe)) {
        r->due=bank->tick+RETRY;
    } else for (int f=0;f<REGION_SLICE && *cells>0 && *traces>0;++f) {
        int i=next_bit(w);
        if (i<0) { w->cursor=0; r->due=bank->tick+RETRY; break; }
        --*cells;
        w->cursor=(unsigned)i+1u;
        int x=w->x+(i%64)/LEAF_TILE,y=w->y+i/64,z=w->z+i%LEAF_TILE,raw;
        if (!world_resident_cell(x,y,z,&raw)) continue;
        if (cell_type(raw)==LEAF && !placed_leaf_cell(raw)) {
            --*traces;
            int state=support(x,y,z);
            if (state==UNLOADED || (state==NO_LOG && !decay(x,y,z,raw))) continue;
        }
        w->layers[i/64]&=~(UINT64_C(1)<<(i%64));
    }
    if (empty(w)) erase(bank,r);
    else { detach(chunk,r); append(chunk,r); }
}
void leaves_step(int player_x,int player_z) {
    DecayBank *bank=&banks[world_dimension()];
    ++bank->tick;
    if (!bank->count) return;
    int cx=chunk_coord(player_x),cz=chunk_coord(player_z),cells=CELL_BUDGET,traces=TRACE_BUDGET;
    for (int i=0;i<WINDOW*WINDOW && bank->count && cells>0 && traces>0;++i) {
        unsigned at=bank->cursor++%(WINDOW*WINDOW);
        ChunkWork *c=*chunk_link(bank,cx+(int)(at%WINDOW)-ACTIVE_RADIUS,
            cz+(int)(at/WINDOW)-ACTIVE_RADIUS);
        if (c) service(bank,c,&cells,&traces);
    }
}
