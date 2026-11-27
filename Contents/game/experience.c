#include "experience.h"
#include "drops.h"
#include "../world/space.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { XPOrb *orbs; size_t count,cap; } XPBank;
static XPBank banks[DIMENSION_COUNT];
static uint32_t points,rng;
static unsigned revision,pickup;

/* The Java 1.7 curve is not the post-1.8 curve. The first sixteen
   paid levels cost 17 each, then the slope changes at 15 and 30. */
uint32_t experience_until_next(unsigned level) {
    if (level>=30u) {
        uint64_t price=62ull+7ull*(level-30u);
        return price>XP_MAX_POINTS ? XP_MAX_POINTS : (uint32_t)price;
    }
    return level>15u ? 17u+3u*(level-15u) : 17u;
}
static uint64_t level_start(uint32_t level) {
    /* Queried in the bounded binary search below. */
    if (level<=16u) return 17u*level;
    if (level<=30u) {
        uint64_t n=level-16u;
        return 17u*level+3u*n*(n+1u)/2u;
    }
    uint64_t n=level-30u;
    return 825u+62u*n+7u*n*(n-1u)/2u;
}
XPProgress experience_progress(void) {
    uint32_t lo=0,hi=32768;
    while (hi-lo>1) {
        uint32_t mid=(lo+hi)/2;
        if (level_start(mid)<=points) lo=mid;
        else hi=mid;
    }
    return (XPProgress){points,lo,points-(uint32_t)level_start(lo),experience_until_next(lo)};
}
int experience_spend_levels(unsigned cost) {
    XPProgress current=experience_progress();
    if (!cost || player_rules.mode!=SURVIVAL || cost>current.level) return 0;
    unsigned level=current.level-cost;
    /* 1.7 subtracts levels, not the number drawn as raw experience points. */
    points=(uint32_t)level_start(level)+
        (uint32_t)((uint64_t)current.within*experience_until_next(level)/current.next);
    return 1;
}
uint32_t experience_points(void) { return points; }
uint32_t experience_rng(void) { return rng; }
unsigned experience_revision(void) { return revision; }
void experience_reset(void) {
    for (int d=0;d<DIMENSION_COUNT;++d) free(banks[d].orbs);
    memset(banks,0,sizeof banks);
    points=pickup=0;
    rng=world_seed_mix32()^0x9e3779b9u;
    if (!rng) rng=1;
    ++revision;
}
int experience_restore_player(uint32_t value,uint32_t stream) {
    if (value>XP_MAX_POINTS || !stream) return 0;
    points=value; rng=stream; pickup=0;
    return 1;
}
static void credit(unsigned amount) {
    points=amount>(unsigned)XP_MAX_POINTS-points ? (unsigned)XP_MAX_POINTS : points+amount;
}
unsigned experience_roll(unsigned low,unsigned high) {
    if (high<=low) return low;
    uint32_t n=rng ? rng : 1;
    n^=n<<13; n^=n>>17; n^=n<<5;
    rng=n;
    return low+(unsigned)((uint64_t)n%((uint64_t)high-low+1u));
}
unsigned experience_ore(int type) {
    switch (type) {
    case COAL_ORE: return experience_roll(0,2);
    case DIAMOND_ORE: case EMERALD_ORE: return experience_roll(3,7);
    case REDSTONE_ORE: return experience_roll(1,5);
    case NETHER_QUARTZ_ORE: case LAPIS_ORE: return experience_roll(2,5);
    default: return 0;
    }
}
static XPOrb *reserve(XPBank *b,size_t n) {
    if (!n || n>XP_ORB_LIMIT-b->count) return NULL;
    size_t need=b->count+n;
    if (need>b->cap) {
        size_t cap=b->cap ? b->cap : 16;
        while (cap<need) cap*=2u;
        XPOrb *mem=(XPOrb *)realloc(b->orbs,cap*sizeof *mem);
        if (!mem) return NULL;
        b->orbs=mem; b->cap=cap;
    }
    XPOrb *out=b->orbs+b->count;
    b->count=need;
    return out;
}
static int valid_point(Vec3 v) {
    return fabsf(v.x)<=WORLD_SAVE_LIMIT && fabsf(v.z)<=WORLD_SAVE_LIMIT && v.y>=0.f && v.y<=WY+40.f;
}
const XPOrb *experience_orbs(Dimension d,size_t *count) {
    if (!count) return NULL;
    *count=0;
    if ((unsigned)d>=DIMENSION_COUNT) return NULL;
    *count=banks[d].count;
    return banks[d].orbs;
}
int experience_restore_orb(Dimension d,XPOrb orb) {
    if ((unsigned)d>=DIMENSION_COUNT || !valid_point(orb.point) || !orb.value || orb.value>XP_MAX_POINTS ||
        orb.age>=XP_ORB_LIFE || !(fabsf(orb.velocity.x)<=24.f && fabsf(orb.velocity.y)<=24.f &&
        fabsf(orb.velocity.z)<=24.f)) return 0;
    XPOrb *value=reserve(&banks[d],1);
    if (!value) return 0;
    *value=orb; ++revision;
    return 1;
}
static const unsigned denominations[]={2477,1237,617,307,149,73,37,17,7,3,1};
static int emit(Vec3 at,unsigned amount) {
    if (!amount) return 1;
    at.x=fmaxf(-WORLD_SAVE_LIMIT,fminf(WORLD_SAVE_LIMIT,at.x));
    at.y=fmaxf(0.f,fminf(WY+40.f,at.y));
    at.z=fmaxf(-WORLD_SAVE_LIMIT,fminf(WORLD_SAVE_LIMIT,at.z));
    XPBank *b=&banks[world_dimension()];
    unsigned pieces[64];
    size_t n=0;
    unsigned left=amount;
    for (unsigned i=0;i<sizeof denominations/sizeof *denominations && left;++i) {
        unsigned denom=denominations[i];
        while (left>=denom && n<sizeof pieces/sizeof *pieces) { pieces[n++]=denom; left-=denom; }
    }
    if (left) { pieces[0]=amount; n=1; }
    XPOrb *list=reserve(b,n);
    if (!list && n>1) { list=reserve(b,1); n=1; pieces[0]=amount; }
    if (!list) {
        /* Under capacity pressure, preserve the points at this same source
           if an existing orb is touching it. Never move remote rewards. */
        for (size_t i=0;i<b->count;++i) {
            XPOrb *orb=&b->orbs[i];
            if (orb->value<=(unsigned)XP_MAX_POINTS-amount && fabsf(orb->point.x-at.x)<.40f &&
                fabsf(orb->point.y-at.y)<.40f && fabsf(orb->point.z-at.z)<.40f) {
                orb->value+=amount; ++revision;
                return 1;
            }
        }
        return 0;
    }
    for (size_t i=0;i<n;++i) {
        float theta=experience_roll(0,6283)*.001f;
        list[i]=(XPOrb){at,{cosf(theta)*.85f,.95f+experience_roll(0,50)*.012f,sinf(theta)*.85f},pieces[i],0};
    }
    ++revision;
    return 1;
}
void experience_reward(Vec3 at,unsigned amount) {
    if (player_rules.mode!=SURVIVAL || !amount) return;
    if (amount>XP_MAX_POINTS) amount=XP_MAX_POINTS;
    if (!emit(at,amount)) credit(amount);
}
void experience_death(int void_death) {
    pickup=0;
    if (death_keep_inventory() || !points) return;
    unsigned level=experience_progress().level;
    unsigned loss=level>=15u ? 100u : 7u*level;
    if (void_death || emit((Vec3){px,py+.4f,pz},loss)) points=0;
}
static float bounded(float v,float lo,float hi) { return fmaxf(lo,fminf(hi,v)); }
static int move_orb(XPOrb *o,float dt) {
    Vec3 v=o->velocity,p=o->point;
    Vec3 diff={px-p.x,py+.7f-p.y,pz-p.z};
    float d2=diff.x*diff.x+diff.y*diff.y+diff.z*diff.z;
    if (d2<64.f && d2>.0001f) {
        float dist=sqrtf(d2),weight=1.f-dist*.125f;
        float gain=15.f*weight*weight*dt/dist;
        v.x+=diff.x*gain; v.y+=diff.y*gain; v.z+=diff.z*gain;
    }
    float drag=1.f-2.1f*dt;
    v.x=bounded(v.x*drag,-10.f,10.f);
    v.y=bounded((v.y-6.f*dt)*drag,-10.f,10.f);
    v.z=bounded(v.z*drag,-10.f,10.f);
    float x=p.x+v.x*dt,y=p.y+v.y*dt,z=p.z+v.z*dt;
    if (!world_point_blocked(x,p.y,p.z,1)) p.x=x; else v.x*=-.30f;
    if (!world_point_blocked(p.x,p.y,z,1)) p.z=z; else v.z*=-.30f;
    if (!world_point_blocked(p.x,y,p.z,1)) p.y=y; else v.y=v.y<-.8f ? -.28f*v.y : 0.f;
    int changed=p.x!=o->point.x || p.y!=o->point.y || p.z!=o->point.z;
    o->point=p; o->velocity=v;
    return changed;
}
void experience_step(float dt,unsigned ticks) {
    if (player_rules.mode!=SURVIVAL || !player_rules.health || !(dt>0.f && dt<=.1f)) return;
    pickup=pickup>ticks ? pickup-ticks : 0;
    XPBank *b=&banks[world_dimension()];
    float reach=(DROP_SIM_RADIUS+1)*CHUNK_SIZE;
    int changed=0;
    for (size_t i=0;i<b->count;) {
        XPOrb *o=&b->orbs[i];
        if (fabsf(o->point.x-px)>reach || fabsf(o->point.z-pz)>reach) { ++i; continue; }
        int remove=ticks>=(unsigned)XP_ORB_LIFE-o->age;
        if (!remove) {
            o->age=(uint16_t)(o->age+ticks);
            int substeps=dt>.05f ? 2 : 1;
            for (int part=0;part<substeps;++part) changed|=move_orb(o,dt/substeps);
            Vec3 p=o->point;
            if (!valid_point(p) || block((int)floorf(p.x),(int)floorf(p.y),(int)floorf(p.z))==LAVA) remove=1;
            else {
                float dx=p.x-px,dy=p.y-(py+.70f),dz=p.z-pz;
                if (!pickup && dx*dx+dy*dy+dz*dz<.95f*.95f) {
                    credit(o->value); pickup=XP_PICKUP_INTERVAL; remove=1;
                }
            }
        }
        if (remove) { *o=b->orbs[--b->count]; changed=1; }
        else ++i;
    }
    if (changed) ++revision;
}
