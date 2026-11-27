#include "village_routes.h"
#include "../world/barriers.h"
#include <stdlib.h>
#include <string.h>

enum { WIDTH=2*VILLAGE_WALK_RADIUS+1, NODES=WIDTH*WIDTH, MAX_EXPANDED=384 };
enum { UNKNOWN, BLOCKED, WALKABLE, PASS_NS, PASS_EW };
typedef struct { uint16_t score,parent,queued; uint8_t terrain; } Node;
/* One search runs on the game thread, never on the mesher/terrain worker. */
static Node nodes[NODES];
static uint16_t heap[NODES+1];
static unsigned heap_size;
static int goal_x,goal_z;

static int passable(int raw) {
    return raw==AIR || raw==TALL_GRASS || raw==TORCH || torch_power_cell(raw) ||
        wire_cell(raw) || pressure_cell(raw) || rail_type(cell_type(raw));
}
static int terrain_at(const VillageWalk *area,int x,int z) {
    int floor,body,head;
    if (!world_resident_cell(x,area->y-1,z,&floor) || !door_support(floor) ||
        !world_resident_cell(x,area->y,z,&body) || !world_resident_cell(x,area->y+1,z,&head)) return BLOCKED;
    if (door_cell(body) && !door_upper(body) && head==door_peer(body)) {
        int bits=door_bits(body),home=x==area->door_x && z==area->door_z;
        if ((bits&DOOR_OPEN) || (home && !(bits&(DOOR_IRON|DOOR_POWERED)))) return PASS_NS+(bits&1);
        return BLOCKED;
    }
    return passable(body) && passable(head) ? WALKABLE : BLOCKED;
}
int village_route_cell(const VillageWalk *area,int x,int z) {
    return area && terrain_at(area,x,z)!=BLOCKED;
}
static int inside(int x,int z) { return (unsigned)x<WIDTH && (unsigned)z<WIDTH; }
static unsigned estimate(unsigned i) { return (unsigned)(abs((int)(i/WIDTH)-goal_x)+abs((int)(i%WIDTH)-goal_z)); }
static int better(unsigned a,unsigned b) {
    unsigned ha=estimate(a),hb=estimate(b),fa=nodes[a].score+ha,fb=nodes[b].score+hb;
    return fa!=fb ? fa<fb : ha!=hb ? ha<hb : a<b;
}
static void insert_or_raise(unsigned id) {
    unsigned at=nodes[id].queued;
    if (!at) at=++heap_size;
    while (at>1 && better(id,heap[at/2])) {
        heap[at]=heap[at/2]; nodes[heap[at]].queued=(uint16_t)at;
        at/=2;
    }
    heap[at]=(uint16_t)id; nodes[id].queued=(uint16_t)at;
}
static unsigned extract(void) {
    unsigned first=heap[1],last=heap[heap_size--],at=1;
    nodes[first].queued=0;
    if (!heap_size) return first;
    while (at*2<=heap_size) {
        unsigned child=at*2;
        if (child<heap_size && better(heap[child+1],heap[child])) ++child;
        if (!better(heap[child],last)) break;
        heap[at]=heap[child]; nodes[heap[at]].queued=(uint16_t)at;
        at=child;
    }
    heap[at]=(uint16_t)last; nodes[last].queued=(uint16_t)at;
    return first;
}
static int step_ok(int terrain,int dir) { return terrain==WALKABLE || terrain==PASS_NS+(dir&1); }
int village_route(const VillageWalk *area,int x,int z,int tx,int tz,VillageStep *out) {
    if (!area || !out) return 0;
    int ox=area->center_x-VILLAGE_WALK_RADIUS,oz=area->center_z-VILLAGE_WALK_RADIUS;
    x-=ox; z-=oz; tx-=ox; tz-=oz;
    if (!inside(x,z) || !inside(tx,tz) || (x==tx && z==tz)) return 0;
    int start=x*WIDTH+z,target=tx*WIDTH+tz;
    memset(nodes,0,sizeof nodes);
    goal_x=tx; goal_z=tz; heap_size=0;
    int first=terrain_at(area,x+ox,z+oz);
    nodes[start]=(Node){1,(uint16_t)start,0,(uint8_t)(first==BLOCKED ? WALKABLE : first)};
    insert_or_raise((unsigned)start);
    for (int explored=0;heap_size && explored<MAX_EXPANDED;++explored) {
        int id=(int)extract();
        if (id==target) {
            int n=nodes[id].score-1;
            if (n>VILLAGE_ROUTE_STEPS) return 0;
            for (int i=n-1;i>=0;--i) {
                out[i]=(VillageStep){(int8_t)(id/WIDTH-VILLAGE_WALK_RADIUS),(int8_t)(id%WIDTH-VILLAGE_WALK_RADIUS)};
                id=nodes[id].parent;
            }
            return n;
        }
        for (int d=0;d<4;++d) {
            if (!step_ok(nodes[id].terrain,d)) continue;
            int nx=id/WIDTH+rail_dx(d),nz=id%WIDTH+rail_dz(d);
            if (!inside(nx,nz)) continue;
            unsigned next=(unsigned)(nx*WIDTH+nz),new_score=nodes[id].score+1u;
            Node *n=&nodes[next];
            if (n->score && n->score<=new_score) continue;
            if (!n->terrain) n->terrain=(uint8_t)terrain_at(area,nx+ox,nz+oz);
            if (!step_ok(n->terrain,d)) continue;
            n->score=(uint16_t)new_score; n->parent=(uint16_t)id;
            insert_or_raise(next);
        }
    }
    return 0;
}
