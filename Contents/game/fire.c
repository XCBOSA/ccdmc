#include "fire.h"
#include "../world/build_blocks.h"
#include "blast.h"
#include "portals.h"
#include "weather.h"
#include "../world/internal.h"
#include <math.h>
#include <stdlib.h>

enum { BUCKETS=257, CLOCK_RADIUS=4, SPAN=2*CLOCK_RADIUS+1,
    CLOCK_CHUNKS=SPAN*SPAN, FIRE_BUDGET=48, FIRE_PER_CHUNK=4 };
typedef struct Node {
    FireCell state;
    uint64_t due;
    size_t index;
    struct Node *next;
} Node;
typedef struct Chunk {
    int x,z;
    Node *nodes[16],**heap;
    size_t count,capacity;
    struct Chunk *next;
} Chunk;
typedef struct { Chunk *chunks[BUCKETS]; uint64_t tick; unsigned count,cursor; } Bank;
static Bank banks[DIMENSION_COUNT];
static const int offsets[6][3]={{1,0,0},{-1,0,0},{0,0,1},{0,0,-1},{0,-1,0},{0,1,0}};
static Bank *bank(void) { return &banks[world_dimension()]; }
static int chunk_of(int x) { return x/CHUNK_SIZE-(x%CHUNK_SIZE<0); }
static int valid(int x,int y,int z) {
    return x>-WORLD_SAVE_LIMIT && x<WORLD_SAVE_LIMIT && z>-WORLD_SAVE_LIMIT && z<WORLD_SAVE_LIMIT && inworld(x,y,z);
}
static Chunk **chunk_link(Bank *b,int x,int z) {
    Chunk **c=&b->chunks[((unsigned)x*374761393u^(unsigned)z*668265263u)%BUCKETS];
    while (*c && ((*c)->x!=x || (*c)->z!=z)) c=&(*c)->next;
    return c;
}
static Node **node_link(Chunk *c,int x,int y,int z) {
    Node **n=&c->nodes[((unsigned)x*31u+(unsigned)y*17u+(unsigned)z)&15u];
    while (*n && ((*n)->state.x!=x || (*n)->state.y!=y || (*n)->state.z!=z)) n=&(*n)->next;
    return n;
}
static int earlier(const Node *a,const Node *b) {
    if (a->due!=b->due) return a->due<b->due;
    FireCell x=a->state,y=b->state;
    return x.x<y.x || (x.x==y.x && (x.z<y.z || (x.z==y.z && x.y<y.y)));
}
static void exchange(Chunk *c,size_t a,size_t b) {
    Node *x=c->heap[a],*y=c->heap[b];
    x->index=b; y->index=a; c->heap[a]=y; c->heap[b]=x;
}
static void reorder(Chunk *c,size_t i) {
    while (i && earlier(c->heap[i],c->heap[(i-1)/2])) {
        exchange(c,i,(i-1)/2); i=(i-1)/2;
    }
    while (i*2+1<c->count) {
        size_t j=i*2+1;
        if (j+1<c->count && earlier(c->heap[j+1],c->heap[j])) ++j;
        if (!earlier(c->heap[j],c->heap[i])) break;
        exchange(c,i,j); i=j;
    }
}
static void discard_empty_chunk(Chunk **where) {
    Chunk *c=*where;
    if (!c || c->count) return;
    *where=c->next; free(c->heap); free(c);
}
static int add_fire(FireCell s) {
    Bank *b=bank();
    if (b->count>=FIRE_FILE_LIMIT) return 0;
    int cx=chunk_of(s.x),cz=chunk_of(s.z);
    Chunk **where=chunk_link(b,cx,cz),*c=*where;
    if (!c) {
        c=(Chunk *)calloc(1,sizeof *c);
        if (!c) return 0;
        c->x=cx; c->z=cz; *where=c;
    }
    Node **link=node_link(c,s.x,s.y,s.z);
    if (*link) return 0;
    if (c->count==c->capacity) {
        size_t cap=c->capacity ? c->capacity*2 : 8;
        Node **heap=(Node **)realloc(c->heap,cap*sizeof *heap);
        if (!heap) { discard_empty_chunk(where); return 0; }
        c->heap=heap; c->capacity=cap;
    }
    Node *n=(Node *)malloc(sizeof *n);
    if (!n) { discard_empty_chunk(where); return 0; }
    *n=(Node){s,b->tick+s.delay,c->count,NULL};
    *link=n; c->heap[c->count++]=n; ++b->count;
    reorder(c,n->index);
    return 1;
}
static void forget(int x,int y,int z) {
    Bank *b=bank();
    Chunk **where=chunk_link(b,chunk_of(x),chunk_of(z)),*c=*where;
    if (!c) return;
    Node **link=node_link(c,x,y,z),*n=*link;
    if (!n) return;
    size_t i=n->index;
    *link=n->next; free(n); --b->count;
    if (i<--c->count) { c->heap[i]=c->heap[c->count]; c->heap[i]->index=i; reorder(c,i); }
    discard_empty_chunk(where);
}
static void wake(int x,int y,int z) {
    Bank *b=bank();
    Chunk *c=*chunk_link(b,chunk_of(x),chunk_of(z));
    Node *n=c ? *node_link(c,x,y,z) : NULL;
    if (n && n->due>b->tick+1) { n->due=b->tick+1; reorder(c,n->index); }
}
void fires_reset(void) {
    for (int d=0;d<DIMENSION_COUNT;++d) {
        Bank *b=&banks[d];
        for (int i=0;i<BUCKETS;++i) while (b->chunks[i]) {
            Chunk *c=b->chunks[i]; b->chunks[i]=c->next;
            for (size_t j=0;j<c->count;++j) free(c->heap[j]);
            free(c->heap); free(c);
        }
        b->tick=0; b->count=b->cursor=0;
    }
}
int fire_restore(const FireCell *s) {
    return s && valid(s->x,s->y,s->z) && s->age<=FIRE_MAX_AGE && s->delay<=FIRE_MAX_DELAY &&
        world_recorded_cell(world_dimension(),s->x,s->y,s->z)==FIRE_CELL && add_fire(*s);
}
int fire_visit(Dimension d,FireVisitor visitor,void *ctx) {
    if ((unsigned)d>=DIMENSION_COUNT || !visitor) return 0;
    Bank *b=&banks[d];
    for (int i=0;i<BUCKETS;++i) for (Chunk *c=b->chunks[i];c;c=c->next)
        for (size_t j=0;j<c->count;++j) {
            Node *n=c->heap[j];
            FireCell s=n->state;
            s.delay=(uint8_t)(n->due>b->tick ? n->due-b->tick : 0);
            if (!visitor(&s,ctx)) return 0;
        }
    return 1;
}

/* Separate encouragement from consumption. */
static int fuel(int raw) {
    if (build_wooden(raw)) return 15;
    switch (cell_type(raw)) {
    case TNT_BLOCK: case LEAF: case WOOL: case TALL_GRASS: case SAPLING: return 60;
    case BOOKSHELF: case SUGAR_CANE: return 30;
    case WOOD: case BIRCH_LOG: case SPRUCE_LOG: case JUNGLE_LOG: case ACACIA_LOG: case DARK_OAK_LOG: return 5;
    case PLANKS: case WOOD_FENCE: case FENCE_GATE: case WOOD_DOOR: case WOOD_TRAPDOOR: case LADDER: return 15;
    default: return 0;
    }
}
static int eternal(int x,int y,int z) {
    int below=block(x,y-1,z);
    return below==NETHERRACK || (below==BEDROCK && world_dimension()==END);
}
static int near_fuel(int x,int y,int z) {
    int best=0;
    for (int i=0;i<6;++i) {
        int value=fuel(world_cell(x+offsets[i][0],y+offsets[i][1],z+offsets[i][2]));
        if (value>best) best=value;
    }
    return best;
}
static int fire_floor(int type) {
    return solid(type) && type!=HOPPER && type!=ENCHANTING_TABLE && type!=ANVIL;
}
static int supported(int x,int y,int z) {
    return fire_floor(block(x,y-1,z)) || near_fuel(x,y,z);
}
static unsigned roll(int x,int y,int z,unsigned stage) {
    unsigned h=hash_for_seed(world_seed()^(stage*2654435761u)^((unsigned)y*2246822519u),x,z);
    h^=h>>16; h*=0x7feb352du; h^=h>>15; return h;
}
static int kindle(int x,int y,int z,int age) {
    if (!valid(x,y,z) || !world_chunk_ready(x,z) || !supported(x,y,z)) return 0;
    FireCell s={x,y,z,0,(uint8_t)age,(uint8_t)(30+roll(x,y,z,(unsigned)age)%11u)};
    if (!add_fire(s)) return 0;
    BlockEdit e={x,y,z,FIRE_CELL};
    if (world_set_blocks(&e,1)) return 1;
    forget(x,y,z);
    return 0;
}
int fire_ignite(int x,int y,int z) {
    return valid(x,y,z) && world_chunk_ready(x,z) && world_cell(x,y,z)==AIR && kindle(x,y,z,0);
}
int fire_use_igniter(int x,int y,int z) {
    if (!valid(x,y,z) || !world_chunk_ready(x,z)) return IGNITE_BLOCKED;
    int raw=world_cell(x,y,z);
    if (raw==TNT_BLOCK) return blast_prime(x,y,z) ? IGNITE_TNT : IGNITE_BLOCKED;
    if (raw!=AIR && raw!=FIRE_CELL) return IGNITE_BLOCKED;
    return fire_ignite(x,y,z) ? IGNITE_FLAME : IGNITE_BLOCKED;
}
void fire_after_edit(int x,int y,int z,int before,int after) {
    if (before==FIRE_CELL && after!=FIRE_CELL) forget(x,y,z);
    if (!bank()->count || before==after || before==FIRE_CELL || after==FIRE_CELL) return;
    int a=cell_type(before),b=cell_type(after);
    if (fire_floor(a)==fire_floor(b) && fuel(before)==fuel(after) &&
        a!=NETHERRACK && b!=NETHERRACK && a!=BEDROCK && b!=BEDROCK) return;
    for (int i=0;i<6;++i) wake(x+offsets[i][0],y+offsets[i][1],z+offsets[i][2]);
}
static int fire_halo_ready(int x,int z) {
    return world_chunk_ready(x-2,z-2) && world_chunk_ready(x-2,z+2) &&
        world_chunk_ready(x+2,z-2) && world_chunk_ready(x+2,z+2);
}
static void simulate(FireCell s) {
    int x=s.x,y=s.y,z=s.z;
    unsigned entropy=roll(x,y,z,s.attempts);
    int forever=eternal(x,y,z),max_age=FIRE_MAX_AGE;
    if (!forever && (weather_wets(x,y,z) || !supported(x,y,z) || (!near_fuel(x,y,z) && s.age>=3) || s.age>=max_age)) {
        set_block(x,y,z,AIR); return;
    }
    for (int i=0;i<6;++i) {
        int a=x+offsets[i][0],b=y+offsets[i][1],c=z+offsets[i][2];
        int raw=world_cell(a,b,c),v=fuel(raw);
        if (raw==TNT_BLOCK) { blast_prime(a,b,c); continue; }
        unsigned r=roll(a,b,c,entropy);
        if (!v || r%100u>=(unsigned)v) continue;
        if ((r>>8)%3u && kindle(a,b,c,s.age<max_age ? s.age+1 : max_age)) continue;
        set_block(a,b,c,AIR);
    }
    for (int a=-1;a<=1;++a) for (int b=-1;b<=3;++b) for (int c=-1;c<=1;++c) {
        if (!valid(x+a,y+b,z+c) || world_cell(x+a,y+b,z+c)!=AIR) continue;
        int strength=near_fuel(x+a,y+b,z+c);
        if (strength && roll(x+a,y+b,z+c,entropy^0xad37e291u)%(unsigned)(100+(b>1 ? 55*b : 0))<
            (unsigned)(strength/2+(15-s.age)/3)) kindle(x+a,y+b,z+c,s.age<max_age ? s.age+1 : max_age);
    }
}
void fires_step(int x,int z,unsigned ticks) {
    Bank *b=bank();
    b->tick+=ticks;
    if (!b->count || !ticks) return;
    int cx=chunk_of(x),cz=chunk_of(z),budget=FIRE_BUDGET;
    unsigned start=b->cursor;
    for (unsigned j=0;j<CLOCK_CHUNKS && budget;++j) {
        unsigned v=(start+j)%CLOCK_CHUNKS;
        int a=cx+(int)(v/SPAN)-CLOCK_RADIUS,c=cz+(int)(v%SPAN)-CLOCK_RADIUS;
        for (int i=0;i<FIRE_PER_CHUNK && budget;++i) {
            Chunk *chunk=*chunk_link(b,a,c);
            if (!chunk || !chunk->count || chunk->heap[0]->due>b->tick) break;
            Node *n=chunk->heap[0];
            FireCell s=n->state;
            int ready=fire_halo_ready(s.x,s.z);
            n->due=b->tick+(ready ? 30+roll(s.x,s.y,s.z,s.attempts)%11u : 1);
            if (ready) {
                ++n->state.attempts;
                if (n->state.age<FIRE_MAX_AGE) ++n->state.age;
            }
            reorder(chunk,0);
            if (ready) {
                if (world_cell(s.x,s.y,s.z)==FIRE_CELL) simulate(s);
                else forget(s.x,s.y,s.z);
            }
            --budget;
        }
        b->cursor=(v+1)%CLOCK_CHUNKS;
    }
    if (budget) b->cursor=(start+1)%CLOCK_CHUNKS;
}
int fire_contact(BlockBox b) {
    int x0=(int)floorf(b.x0),x1=(int)floorf(b.x1),y0=(int)floorf(b.y0),y1=(int)floorf(b.y1);
    int z0=(int)floorf(b.z0),z1=(int)floorf(b.z1);
    if (!bank()->count) return 0;
    for (int x=x0;x<=x1;++x) for (int y=y0;y<=y1;++y) for (int z=z0;z<=z1;++z)
        if (world_cell(x,y,z)==FIRE_CELL) return 1;
    return 0;
}
