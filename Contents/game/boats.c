#include "boats.h"
#include "drops.h"
#include "../world/space.h"
#include "minecarts.h"
#include "horses.h"
#include <math.h>
#include <string.h>

static BoatRecord boats[DIMENSION_COUNT][BOAT_CAPACITY];
static Vec3 motion[DIMENSION_COUNT][BOAT_CAPACITY];
static unsigned revision;
static Dimension riding_dimension;
static int riding = -1;

static float clamp(float n,float min,float max) { return fmaxf(min,fminf(max,n)); }
static void sync_player(const BoatRecord *a) {
    px=a->position.x; py=a->position.y+.18f; pz=a->position.z;
    vy=0; grounded=0; player_rules.fall_origin=py;
}
unsigned boats_revision(void) { return revision; }
const BoatRecord *boats_in(Dimension dimension) {
    return (unsigned)dimension < DIMENSION_COUNT ? boats[dimension] : NULL;
}
void boats_reset(void) {
    memset(boats,0,sizeof boats);
    memset(motion,0,sizeof motion);
    riding=-1;
    ++revision;
}
void boat_unseat(void) { riding=-1; }
int boat_rider(void) {
    return player_rules.health && riding_dimension == world_dimension() ? riding : -1;
}
int boat_restore_rider(int index) {
    Dimension d=world_dimension();
    if (index < -1 || index >= BOAT_CAPACITY) return 0;
    if (index >= 0) {
        const BoatRecord *a=&boats[d][index];
        if (!a->occupied || !player_rules.health || cart_rider()>=0 || horse_rider()>=0 ||
            fabsf(px-a->position.x)>.25f || fabsf(py-a->position.y-.18f)>.25f ||
            fabsf(pz-a->position.z)>.25f) return 0;
        player_rules.flying=0;
        sync_player(a);
    }
    riding=index; riding_dimension=d;
    return 1;
}
int boat_restore(Dimension d, BoatRecord record) {
    Vec3 p=record.position;
    if ((unsigned)d>=DIMENSION_COUNT || record.occupied!=1 ||
        !(fabsf(p.x)<WORLD_SAVE_LIMIT && p.y>=0 && p.y<WY && fabsf(p.z)<WORLD_SAVE_LIMIT &&
        fabsf(record.heading)<=3.142f)) return 0;
    for (int i=0;i<BOAT_CAPACITY;++i) if (!boats[d][i].occupied) {
        boats[d][i]=record;
        motion[d][i]=(Vec3){0,0,0};
        ++revision;
        return 1;
    }
    return 0;
}

static int water_level(float x,float z,float y,float *level) {
    int a=(int)floorf(x), c=(int)floorf(z), by=(int)floorf(y);
    for (int b=by+1;b>=by-2;--b) {
        if (!inworld(a,b,c)) continue;
        int cell=world_cell(a,b,c), above=world_cell(a,b+1,c);
        if (!water_strength(cell) || water_strength(above)) continue;
        float v=b+water_surface(cell,above)+.045f;
        if (fabsf(v-y)>.75f) continue;
        *level=v;
        return 1;
    }
    return 0;
}
static BlockBox hull_box(Vec3 p) {
    return (BlockBox){p.x-.54f,p.y+.035f,p.z-.54f,p.x+.54f,p.y+1.9f,p.z+.54f};
}
static int hull_clear(Vec3 p) {
    return !world_box_blocked(hull_box(p),SPACE_LAVA|SPACE_IGNORE_LILY_PAD);
}
static void part_lily_pads(Vec3 at) {
    BlockBox hull=hull_box(at);
    for (int x=(int)floorf(hull.x0);x<(int)ceilf(hull.x1);++x)
        for (int z=(int)floorf(hull.z0);z<(int)ceilf(hull.z1);++z)
            for (int y=(int)ceilf(at.y-.20f);y<(int)ceilf(at.y+.28f);++y)
                if (world_cell(x,y,z)==LILY_PAD && set_block(x,y,z,AIR))
                    (void)drop_loose_item((Vec3){x+.5f,y+.12f,z+.5f},(ItemStack){LILY_PAD_ITEM,1,0,0,0});
}
static int occupied(Vec3 p,int ignore) {
    const BoatRecord *bank=boats[world_dimension()];
    for (int i=0;i<BOAT_CAPACITY;++i) if (i!=ignore && bank[i].occupied) {
        const Vec3 b=bank[i].position;
        if (fabsf(b.y-p.y)<.8f && hypotf(b.x-p.x,b.z-p.z)<1.35f) return 1;
    }
    return 0;
}
int boat_overlaps_cell(int x,int y,int z) {
    const BoatRecord *b=boats[world_dimension()];
    for (int i=0;i<BOAT_CAPACITY;++i) if (b[i].occupied) {
        Vec3 p=b[i].position;
        if (p.y<y+1 && p.y+BOAT_BODY_HEIGHT>y && p.x+BOAT_BODY_RADIUS>x && p.x-BOAT_BODY_RADIUS<x+1 &&
            p.z+BOAT_BODY_RADIUS>z && p.z-BOAT_BODY_RADIUS<z+1) return 1;
    }
    return 0;
}
static int target_boat(void) {
    const BoatRecord *bank=boats[world_dimension()];
    int indexes[BOAT_CAPACITY], count=0;
    for (int i=0;i<BOAT_CAPACITY;++i) if (bank[i].occupied &&
        fabsf(bank[i].position.y-py)<7.f &&
        hypotf(bank[i].position.x-px,bank[i].position.z-pz)<6.f) indexes[count++]=i;
    if (!count) return -1;
    Vec3 f=forward();
    for (float t=.12f;t<5.2f;t+=.12f) {
        Vec3 p={px+f.x*t,py+1.55f+f.y*t,pz+f.z*t};
        if (world_point_blocked(p.x,p.y,p.z,0)) break;
        for (int i=0;i<count;++i) {
            Vec3 v=bank[indexes[i]].position;
            if (fabsf(p.y-v.y-.22f)<.32f && hypotf(p.x-v.x,p.z-v.z)<.76f) return indexes[i];
        }
    }
    return -1;
}
const char *boat_dismount(void) {
    int rider=boat_rider();
    if (rider<0) return NULL;
    BoatRecord *a=&boats[world_dimension()][rider];
    Vec3 p=a->position;
    const int sides[][2]={{1,0},{-1,0},{0,1},{0,-1},{1,1},{1,-1},{-1,1},{-1,-1}};
    /* Prefer a solid shore, then nearby water at the hull's height. */
    for (int dry=1;dry>=0;--dry) for (int i=0;i<8;++i) {
        float x=p.x+sides[i][0]*1.25f,z=p.z+sides[i][1]*1.25f;
        for (int rise=0;rise<=1;++rise) {
            float y=floorf(p.y)+.01f+(float)rise;
            int below=block((int)floorf(x),(int)floorf(y)-1,(int)floorf(z));
            int cell=block((int)floorf(x),(int)floorf(y),(int)floorf(z));
            if (below==LAVA || cell==LAVA || (dry && (!solid(below) || cell==WATER)) || collides(x,y,z)) continue;
            if (!dry && below!=WATER && cell!=WATER) continue;
            riding=-1;
            px=x; py=y; pz=z; vy=0; grounded=0; player_rules.fall_origin=y;
            return "已下船";
        }
    }
    return "这里没有安全的下船位置";
}

int boat_launch_on_water(int x,int y,int z,float offset_x,float offset_z,float heading) {
    if (!inworld(x,y,z) || !world_chunk_ready(x,z)) return 0;
    int cell=world_cell(x,y,z),above=world_cell(x,y+1,z);
    if (!water_strength(cell) || water_strength(above)) return 0;
    Vec3 p={x+.5f+offset_x,y+water_surface(cell,above)+.045f,z+.5f+offset_z};
    return hull_clear(p) && !occupied(p,-1) &&
        boat_restore(world_dimension(),(BoatRecord){p,remainderf(heading,6.2831853f),1});
}
const char *boat_use(void) {
    if (!player_rules.health || cart_rider()>=0 || horse_rider()>=0) return NULL;
    if (boat_rider()>=0) return boat_dismount();
    Dimension d=world_dimension();
    int index=target_boat();
    if (index>=0) {
        BoatRecord *a=&boats[d][index];
        if (collides(a->position.x,a->position.y+.18f,a->position.z)) return "船上方空间不足";
        riding_dimension=d; riding=index;
        player_rules.flying=0;
        sync_player(a);
        return "已上船；点「下船」离开";
    }
    ItemStack *held=&inventory[selected_slot];
    if (held->type!=BOAT || !held->count) return NULL;
    Vec3 f=forward();
    for (float t=.2f;t<=5.f;t+=.12f) {
        Vec3 p={px+f.x*t,py+1.55f+f.y*t,pz+f.z*t};
        int x=(int)floorf(p.x), y=(int)floorf(p.y), z=(int)floorf(p.z);
        if (world_point_blocked(p.x,p.y,p.z,0) || block(x,y,z)==LAVA) break;
        int cell=world_cell(x,y,z), above=world_cell(x,y+1,z);
        if (!water_strength(cell) || water_strength(above)) continue;
        p.y=y+water_surface(cell,above)+.045f;
        if (hypotf(p.x-px,p.z-pz)<.85f || !hull_clear(p) || occupied(p,-1)) return "水面太窄，放不下船";
        if (!boat_restore(d,(BoatRecord){p,remainderf(yaw,6.2831853f),1})) return "这个维度的船位已满";
        if (player_rules.mode==SURVIVAL && !--held->count) *held=(ItemStack){0,0,0,0,0};
        return "船已下水，对准船轻点即可上船";
    }
    return "对准有空间的水面才能放船";
}
const char *boat_take_down(void) {
    int i=target_boat();
    if (i<0 || i==boat_rider()) return NULL;
    BoatRecord *a=&boats[world_dimension()][i];
    if (player_rules.mode==SURVIVAL) {
        int slot=0;
        while (slot<MAXDROP && drops[slot].item.count) ++slot;
        if (slot==MAXDROP) return "掉落物已满，请先拾取";
        drops[slot]=make_drop(a->position.x,a->position.y+.2f,a->position.z,(ItemStack){BOAT,1,0,0,0});
        drops_dirty=1;
    }
    a->occupied=0;
    ++revision;
    return "已收起船";
}
int boats_step(float dt,int keys_side,int keys_front) {
    if (!(dt>0 && dt<=.1f)) return 0;
    Dimension d=world_dimension();
    if (!player_rules.health) boat_unseat();
    int rider=boat_rider();
    for (int i=0;i<BOAT_CAPACITY;++i) if (boats[d][i].occupied) {
        BoatRecord *a=&boats[d][i];
        Vec3 *v=&motion[d][i], p=a->position;
        if (i!=rider && (fabsf(p.x-px)>48.f || fabsf(p.z-pz)>48.f)) {
            v->x=v->z=0;
            continue;
        }
        float side=clamp(mx+keys_side,-1.f,1.f), front=clamp(-my+keys_front,-1.f,1.f);
        float strength=hypotf(side,front);
        if (strength>1) { side/=strength; front/=strength; }
        float change=fminf(1.f,dt*(i==rider?4.f:2.2f));
        float vx=i==rider ? (cosf(yaw)*side+sinf(yaw)*front)*5.8f : 0.f;
        float vz=i==rider ? (sinf(yaw)*side-cosf(yaw)*front)*5.8f : 0.f;
        v->x+=(vx-v->x)*change; v->z+=(vz-v->z)*change;
        Vec3 next={p.x+v->x*dt,p.y,p.z+v->z*dt};
        float surface;
        int valid=water_level(next.x,next.z,p.y,&surface);
        if (valid) {
            next.y=p.y+clamp(surface-p.y,-dt*3.f,dt*3.f);
            if (world_move_blocked(hull_box(p),hull_box(next),SPACE_LAVA|SPACE_IGNORE_LILY_PAD) || occupied(next,i)) valid=0;
        }
        if (!valid) { v->x=v->z=0; next=p; }
        if (next.x!=p.x || next.y!=p.y || next.z!=p.z) {
            if (next.x!=p.x || next.z!=p.z) part_lily_pads(next);
            if (fabsf(v->x)+fabsf(v->z)>.2f) a->heading=atan2f(v->x,-v->z);
            a->position=next;
            ++revision;
        }
        if (i==rider) sync_player(a);
    }
    return rider>=0;
}
