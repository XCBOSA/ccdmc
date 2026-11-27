#include "gravity.h"
#include "actors.h"
#include "anvil.h"
#include "drops.h"
#include "horses.h"
#include "../world/space.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { BUCKETS=257, START_BUDGET=48 };
typedef char GravityCellFits[(CHUNK_SIZE*CHUNK_SIZE*WY<=65536) ? 1 : -1];
typedef struct Checks {
    struct Checks *next;
    int cx,cz;
    size_t count,capacity;
    uint16_t *cells;
    unsigned char *contains;
} Checks;
typedef struct {
    Checks *waiting[BUCKETS];
    size_t pending,count,capacity;
    FallingBlock *flying;
} GravityBank;
static GravityBank gravity[DIMENSION_COUNT];
static unsigned revision;

int gravity_block(int raw) { return raw==SAND || raw==RED_SAND || raw==GRAVEL || anvil_cell(raw); }
static int open_below(int raw) { return raw==AIR || lava_strength(raw) || water_strength(raw); }
static int chunk(int x) { return x/CHUNK_SIZE-(x%CHUNK_SIZE<0); }
static int coordinates(int x,int y,int z) {
    return x>=-WORLD_SAVE_LIMIT && x<=WORLD_SAVE_LIMIT && z>=-WORLD_SAVE_LIMIT && z<=WORLD_SAVE_LIMIT &&
        y>=0 && y<WY;
}
static unsigned bucket(int x,int z) {
    /* Do not use the terrain seed: these keys are independent of generation. */
    return ((unsigned)x*374761393u ^ (unsigned)z*668265263u)%BUCKETS;
}
static Checks **find(GravityBank *b,int cx,int cz) {
    Checks **node=&b->waiting[bucket(cx,cz)];
    while (*node && ((*node)->cx!=cx || (*node)->cz!=cz)) node=&(*node)->next;
    return node;
}
static int queue(GravityBank *b,FallCheck p,int restoring) {
    if (!coordinates(p.x,p.y,p.z)) return 0;
    int cx=chunk(p.x),cz=chunk(p.z);
    Checks **link=find(b,cx,cz),*c=*link;
    uint16_t local=(uint16_t)((((p.x-cx*CHUNK_SIZE)*CHUNK_SIZE)+(p.z-cz*CHUNK_SIZE))*WY+p.y);
    if (c) {
        if (c->contains) {
            if (c->contains[local/8]&(1u<<(local%8))) return !restoring;
        } else {
            for (size_t i=0;i<c->count;++i) if (c->cells[i]==local) return !restoring;
            if (c->count>=128) {
                c->contains=(unsigned char *)calloc((CHUNK_SIZE*CHUNK_SIZE*WY+7)/8,1);
                if (!c->contains) return 0;
                for (size_t i=0;i<c->count;++i)
                    c->contains[c->cells[i]/8]|=1u<<(c->cells[i]%8);
            }
        }
    }
    if (b->pending>=GRAVITY_FILE_LIMIT) return 0;
    if (!c) {
        c=(Checks *)calloc(1,sizeof *c);
        if (!c) return 0;
        c->cx=cx; c->cz=cz;
        *link=c;
    }
    if (c->count==c->capacity) {
        size_t cap=c->capacity ? 2*c->capacity : 4;
        uint16_t *mem=(uint16_t *)realloc(c->cells,cap*sizeof *mem);
        if (!mem) {
            if (!c->count) { *link=c->next; free(c->contains); free(c); }
            return 0;
        }
        c->cells=mem; c->capacity=cap;
    }
    c->cells[c->count++]=local;
    if (c->contains) c->contains[local/8]|=1u<<(local%8);
    ++b->pending;
    return 1;
}
static FallCheck decode(const Checks *c,uint16_t local) {
    unsigned col=(unsigned)local/WY;
    return (FallCheck){c->cx*CHUNK_SIZE+(int)(col/CHUNK_SIZE),(int)(local%WY),
        c->cz*CHUNK_SIZE+(int)(col%CHUNK_SIZE)};
}
int gravity_prepare_edit(int x,int y,int z,int before,int after) {
    if (before==after) return 1;
    GravityBank *b=&gravity[world_dimension()];
    if (gravity_block(after) && !queue(b,(FallCheck){x,y,z},0)) return 0;
    if (y+1<WY && open_below(after) && !open_below(before) && gravity_block(world_cell(x,y+1,z)) &&
        !queue(b,(FallCheck){x,y+1,z},0)) return 0;
    return 1;
}
int gravity_seed_legacy_edit(void *context,int x,int y,int z,int cell) {
    (void)context;
    if (gravity_block(cell) && open_below(world_cell(x,y-1,z)))
        return queue(&gravity[world_dimension()],(FallCheck){x,y,z},0);
    return !open_below(cell) || gravity_prepare_edit(x,y,z,STONE,cell);
}
void gravity_reset(void) {
    for (int d=0;d<DIMENSION_COUNT;++d) {
        for (int i=0;i<BUCKETS;++i) {
            Checks *p=gravity[d].waiting[i];
            while (p) { Checks *next=p->next; free(p->cells); free(p->contains); free(p); p=next; }
        }
        free(gravity[d].flying);
    }
    memset(gravity,0,sizeof gravity);
    ++revision;
}
unsigned gravity_revision(void) { return revision; }
const FallingBlock *gravity_falling(Dimension d,size_t *count) {
    if ((unsigned)d>=DIMENSION_COUNT) { if (count) *count=0; return NULL; }
    if (count) *count=gravity[d].count;
    return gravity[d].flying;
}
size_t gravity_pending_count(Dimension d) { return (unsigned)d<DIMENSION_COUNT ? gravity[d].pending : 0; }
int gravity_visit_pending(Dimension d,GravityVisitor visit,void *context) {
    if ((unsigned)d>=DIMENSION_COUNT || !visit) return 0;
    GravityBank *b=&gravity[d];
    for (int i=0;i<BUCKETS;++i) for (Checks *c=b->waiting[i];c;c=c->next)
        for (size_t j=0;j<c->count;++j) if (!visit(decode(c,c->cells[j]),context)) return 0;
    return 1;
}
int gravity_restore_pending(Dimension d,FallCheck p) {
    return (unsigned)d<DIMENSION_COUNT && queue(&gravity[d],p,1);
}
static int reserve_fall(GravityBank *b) {
    if (b->count< b->capacity) return 1;
    if (b->count>=GRAVITY_FILE_LIMIT) return 0;
    size_t cap=b->capacity ? 2*b->capacity : 8;
    FallingBlock *mem=(FallingBlock *)realloc(b->flying,cap*sizeof *mem);
    if (!mem) return 0;
    b->flying=mem; b->capacity=cap;
    return 1;
}
int gravity_restore_falling(Dimension d,FallingBlock e) {
    if ((unsigned)d>=DIMENSION_COUNT || !coordinates(e.x,e.origin,e.z) || !gravity_block(e.cell) ||
        !(e.bottom>=0.f && e.bottom<=e.origin) || !(e.speed>=0.f && e.speed<=2.f)) return 0;
    GravityBank *b=&gravity[d];
    if (!reserve_fall(b)) return 0;
    b->flying[b->count++]=e;
    ++revision;
    return 1;
}
static void erase_check(Checks **link) {
    Checks *c=*link;
    if (c->count) return;
    *link=c->next;
    free(c->cells); free(c->contains); free(c);
}
static void activate(GravityBank *b,int cx,int cz,unsigned *work) {
    Checks **link=find(b,cx,cz),*c=*link;
    if (!c) return;
    /* Pop first so the edit can queue the block above even at the limit. */
    size_t left=c->count;
    while (*work && left-- && c->count) {
        FallCheck next=decode(c,c->cells[c->count-1]);
        if (!world_chunk_ready(next.x,next.z)) return;
        uint16_t local=c->cells[--c->count];
        if (c->contains) c->contains[local/8]&=~(1u<<(local%8));
        --b->pending; --*work;
        FallCheck p=decode(c,local);
        int raw=world_cell(p.x,p.y,p.z);
        if (!gravity_block(raw) || !open_below(world_cell(p.x,p.y-1,p.z))) continue;
        if (!reserve_fall(b) || !set_block(p.x,p.y,p.z,AIR)) {
            /* We removed exactly one record; capacity for it already exists.
               The failed edit itself may have reserved the next check. */
            if (b->pending==GRAVITY_FILE_LIMIT) {
                /* The failed edit appended a speculative check immediately
                   above this block. Replace it with the original check. */
                uint16_t bumped=c->cells[c->count-1];
                if (c->contains) c->contains[bumped/8]&=~(1u<<(bumped%8));
                --c->count; --b->pending;
            }
            c->cells[c->count++]=local;
            if (c->contains) c->contains[local/8]|=1u<<(local%8);
            ++b->pending;
            break;
        }
        b->flying[b->count++]=(FallingBlock){p.x,p.z,p.y,(float)p.y,0.f,(uint16_t)raw};
        ++revision;
    }
    erase_check(link);
}
static void impact(FallingBlock e,int y,int damage,int exempt) {
    if (damage<=0) return;
    BlockBox strike={e.x+.02f,y+.02f,e.z+.02f,e.x+.98f,y+.98f,e.z+.98f};
    drops_anvil_impact(strike,exempt);
    actors_anvil_impact(strike,damage);
    horses_anvil_impact(strike,damage);
    if (box_overlap(strike,body_box(px,py,pz,.28f,1.72f))) player_take_damage(DAMAGE_ANVIL,damage);
}
static int item_on_ground(FallingBlock e,int y,int raw) {
    int slot=0;
    while (slot<MAXDROP && drops[slot].item.count) ++slot;
    if (slot==MAXDROP) return -1;
    int type=cell_type(raw),stage=anvil_stage(raw);
    ItemStack item={(uint16_t)type,1,(uint16_t)(stage>=0 ? stage : 0),0,0};
    drops[slot]=make_drop(e.x+.5f,y+.35f,e.z+.5f,item);
    drops_dirty=1;
    return slot;
}
static int land(FallingBlock e,int y) {
    int raw=e.cell,distance=e.origin-y,damage=0;
    if (anvil_cell(raw) && distance>1) {
        damage=2*(distance-1);
        if (damage>40) damage=40;
        unsigned roll=hash2(e.x+131*e.origin,e.z+17*y+509*(int)world_dimension())%100u;
        if (roll<(unsigned)(5+5*(distance-1))) {
            if (anvil_stage(raw)==2) { impact(e,y,damage,-1); return 1; }
            raw+=2;
        }
    }
    int below=world_cell(e.x,y-1,e.z);
    int current=world_cell(e.x,y,e.z);
    /* Rest on terrain; fragile non-solid obstacles leave a drop, not a
       replacement. Never erase an unrelated block placed during the fall. */
    int exempt=-1;
    if (open_below(current) && solid(cell_type(below))) {
        BlockEdit edit={e.x,y,e.z,raw};
        if (!world_set_blocks(&edit,1)) return 0;
    } else if ((exempt=item_on_ground(e,y,raw))<0) return 0;
    impact(e,y,damage,exempt);
    return 1;
}
static int advance(FallingBlock *e) {
    const float speed=(e->speed+.04f)*.98f;
    const float next=e->bottom-speed;
    int top=(int)floorf(e->bottom-.00001f),last=(int)floorf(next-.00001f);
    for (int y=top;y>=last;--y) if (!open_below(world_cell(e->x,y,e->z))) {
        int at=y+1;
        if (at>=WY || at>e->origin) at=e->origin;
        if (land(*e,at)) return 1;
        e->bottom=(float)at; e->speed=0;
        return 0;
    }
    if (next<0.f) return 1;  /* A column without a floor leads into the void. */
    e->bottom=next; e->speed=speed;
    return 0;
}
void gravity_step(unsigned ticks) {
    const int cx=(int)floorf(px/CHUNK_SIZE),cz=(int)floorf(pz/CHUNK_SIZE);
    GravityBank *b=&gravity[world_dimension()];
    while (ticks-- && (b->count || b->pending)) {
        for (size_t i=0;i<b->count;) {
            FallingBlock *e=&b->flying[i];
            int dx=abs(chunk(e->x)-cx),dz=abs(chunk(e->z)-cz);
            if (dx>GRAVITY_CHUNK_RADIUS || dz>GRAVITY_CHUNK_RADIUS || !world_chunk_ready(e->x,e->z)) { ++i; continue; }
            float old=e->bottom;
            if (advance(e)) { *e=b->flying[--b->count]; ++revision; }
            else { if (e->bottom!=old) ++revision; ++i; }
        }
        unsigned work=START_BUDGET;
        if (!b->pending) continue;
        for (int radius=0;radius<=GRAVITY_CHUNK_RADIUS && work;++radius) {
            for (int dx=-radius;dx<=radius && work;++dx) for (int dz=-radius;dz<=radius && work;++dz) {
                if (abs(dx)!=radius && abs(dz)!=radius) continue;
                activate(b,cx+dx,cz+dz,&work);
            }
        }
    }
}
