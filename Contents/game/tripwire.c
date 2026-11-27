#include "tripwire.h"

static int quiet_active;
static CircuitPos quiet_at;

static int same(CircuitPos a,CircuitPos b) { return a.x==b.x && a.y==b.y && a.z==b.z; }
static CircuitPos along(CircuitPos p,int dir) {
    return (CircuitPos){p.x+rail_dx(dir),p.y,p.z+rail_dz(dir)};
}
static int resident(CircuitPos p,int *raw) { return world_resident_cell(p.x,p.y,p.z,raw); }
int tripwire_floor(int raw) {
    int type=cell_type(raw);
    return solid(type) && !chest_shape_type(type) && type!=FARMLAND && type!=HOPPER &&
        type!=ANVIL && type!=ENCHANTING_TABLE && type!=CACTUS && !piston_cell(raw);
}
int tripwire_wall(int raw) {
    int type=cell_type(raw);
    return tripwire_floor(raw) && type!=GLASS && type!=LEAF && type!=ICE && type!=PACKED_ICE;
}
static int ground_at(CircuitPos p,int *value) {
    if (p.y==0) { *value=0; return 1; }
    int floor;
    --p.y;
    if (!resident(p,&floor)) return 0;
    *value=tripwire_floor(floor);
    return 1;
}
int tripwire_place_hook(CircuitPos put,CircuitPos against) {
    if (!inworld(put.x,put.y,put.z) || world_cell(put.x,put.y,put.z)!=AIR) return 0;
    for (int d=0;d<4;++d) if (same(along(put,d),against) && tripwire_wall(world_cell(against.x,against.y,against.z))) {
        BlockEdit edit={put.x,put.y,put.z,TRIPWIRE_HOOK_BASE+d};
        return world_set_blocks(&edit,1);
    }
    return 0;
}
int tripwire_place_string(CircuitPos at) {
    if (!inworld(at.x,at.y,at.z) || world_cell(at.x,at.y,at.z)!=AIR) return 0;
    BlockEdit edit={at.x,at.y,at.z,TRIPWIRE_CELL};
    return world_set_blocks(&edit,1);
}
static int find_hook(CircuitPos from,int dir,int grounded,CircuitPos *found) {
    for (int distance=1;distance<TRIPWIRE_MAX_SPAN;++distance) {
        int raw,floor;
        from=along(from,dir);
        if (!resident(from,&raw) || !ground_at(from,&floor) || floor!=grounded) break;
        if (raw==TRIPWIRE_CELL) continue;
        int support;
        if (!tripwire_hook_cell(raw) || tripwire_mount(raw)!=dir ||
            !resident(along(from,dir),&support) || !tripwire_wall(support)) break;
        *found=from;
        return distance;
    }
    return 0;
}
static void pulse_through(CircuitPos at,int grounded) {
    /* The origin can be the just-broken string. All other spans must
       still be continuous, independently on the two intersecting axes. */
    for (int d=0;d<2;++d) {
        CircuitPos one,two;
        int a=find_hook(at,d,grounded,&one);
        if (!a) continue;
        int b=find_hook(at,d+2,grounded,&two);
        if (b && a+b<=TRIPWIRE_MAX_SPAN) redstone_tripwire_pulse(one,two);
    }
}
void tripwire_wire_removed(CircuitPos at) {
    if (quiet_active && same(quiet_at,at)) return;
    int grounded;
    if (ground_at(at,&grounded)) pulse_through(at,grounded);
}
int tripwire_break(CircuitPos at,int with_shears) {
    int raw=world_cell(at.x,at.y,at.z);
    if (raw!=TRIPWIRE_CELL && !tripwire_hook_cell(raw)) return 0;
    /* Block writes and their notifications are synchronous on the game thread. */
    quiet_active=raw==TRIPWIRE_CELL && with_shears;
    quiet_at=at;
    int ok=redstone_replace(at.x,at.y,at.z,AIR);
    quiet_active=0;
    return ok;
}
void tripwire_body_contact(CircuitPos at,BlockBox body) {
    int raw,grounded;
    if (!resident(at,&raw) || raw!=TRIPWIRE_CELL || !ground_at(at,&grounded)) return;
    float low=grounded ? .0f : .0625f, high=grounded ? .09375f : .5f;
    if (body.y1>at.y+low && body.y0<at.y+high && body.x0<at.x+1 && body.x1>at.x &&
        body.z0<at.z+1 && body.z1>at.z) pulse_through(at,grounded);
}
unsigned tripwire_links(const int neighbours[4]) {
    unsigned result=0;
    for (int d=0;d<4;++d) {
        int raw=neighbours[d];
        if (raw==TRIPWIRE_CELL || (tripwire_hook_cell(raw) && tripwire_mount(raw)==d)) result|=1u<<d;
    }
    return result;
}
static BlockBox toward(BlockBox box,int d) {
    if (rail_dx(d)<0 || rail_dz(d)<0) {
        float lo=1.f-box.x1;
        box.x1=1.f-box.x0; box.x0=lo;
    }
    if (!rail_dx(d)) return (BlockBox){box.z0,box.y0,box.x0,box.z1,box.y1,box.x1};
    return box;
}
int tripwire_boxes(int raw,int grounded,unsigned links,int powered,BlockBox out[TRIPWIRE_PARTS]) {
    if (!out) return 0;
    float y=grounded ? .069f : .25f;
    if (tripwire_hook_cell(raw)) {
        int d=tripwire_mount(raw);
        out[0]=toward((BlockBox){.875f,y,.32f,1.f,y+.32f,.68f},d);
        out[1]=toward((BlockBox){.59f,y+(powered ? .018f : .06f),.453f,.91f,y+.118f,.547f},d);
        out[2]=toward((BlockBox){.57f,y+.007f,.425f,.676f,y+.084f,.575f},d);
        if (!(links&(1u<<((d+2)&3)))) return 3;
        out[3]=toward((BlockBox){0.f,y,.488f,.612f,y+.018f,.512f},d);
        return 4;
    }
    if (raw!=TRIPWIRE_CELL) return 0;
    out[0]=(BlockBox){.474f,y,.474f,.526f,y+.018f,.526f};
    int n=1;
    for (int d=0;d<4;++d) if (links&(1u<<d))
        out[n++]=toward((BlockBox){.526f,y,.488f,1.f,y+.018f,.512f},d);
    return n;
}
int tripwire_pick_boxes(CircuitPos at,int raw,BlockBox out[TRIPWIRE_PARTS]) {
    int grounded=0, neighbours[4]={0,0,0,0};
    (void)ground_at(at,&grounded);
    for (int d=0;d<4;++d) (void)resident(along(at,d),&neighbours[d]);
    int n=tripwire_boxes(raw,grounded,tripwire_links(neighbours),
        tripwire_hook_cell(raw) && redstone_tripwire_power(at.x,at.y,at.z),out);
    for (int i=0;i<n;++i) {
        BlockBox b=out[i];
        float touch=raw==TRIPWIRE_CELL ? .0675f : 0.f;
        out[i]=(BlockBox){at.x+b.x0-touch,at.y+b.y0-touch,at.z+b.z0-touch,
            at.x+b.x1+touch,at.y+b.y1+touch,at.z+b.z1+touch};
    }
    return n;
}
