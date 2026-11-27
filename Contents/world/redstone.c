#include "redstone.h"
#include "../game/pistons.h"
#include "internal.h"
#include "../game/drops.h"
#include "../game/container_signal.h"
#include "../game/chests.h"
#include "../game/tripwire.h"
#include "../game/pressure_contacts.h"
#include "../game/button_arrows.h"
#include "../game/droppers.h"
#include "../game/blast.h"
#include "../game/minecarts.h"
#include <stdlib.h>
#include <string.h>

enum { HASH_BUCKETS=257, CIRCUIT_RADIUS=4, SOLVE_BUDGET=224, GATE_BUDGET=96,
       TIMED_BUDGET=224, BURNOUT_HISTORY=60, BURNOUT_WAIT=160 };
typedef struct {
    uint16_t *cells;
    unsigned char *present;
    size_t count,capacity;
} Queue;
typedef struct CircuitChunk {
    int cx,cz;
    Queue logic,gates;
    struct Device *timers;
    struct CircuitChunk *next;
} CircuitChunk;
typedef struct Device {
    RedstoneTiming time;
    struct Device *next;
    struct Device *pulse_next,**pulse_link;
    int parked;
    unsigned contacts;
    unsigned long long contact_frame;
} Device;
static CircuitChunk *banks[DIMENSION_COUNT][HASH_BUCKETS];
static Device *pulse_timers[DIMENSION_COUNT];
static unsigned queued[DIMENSION_COUNT];
static size_t timers[DIMENSION_COUNT];
static size_t pulse_counts[DIMENSION_COUNT];
static size_t parked_count[DIMENSION_COUNT];
static unsigned long long pressure_frame;
static int rebuild[DIMENSION_COUNT];
static const ItemStack empty_stack={0,0,0,0,0};

static int chunk_coordinate(int v) { return v/CHUNK_SIZE-(v%CHUNK_SIZE<0); }
static int usable(CircuitPos p) {
    return p.y>=0 && p.y<WY && p.x>=-WORLD_SAVE_LIMIT && p.x<=WORLD_SAVE_LIMIT &&
        p.z>=-WORLD_SAVE_LIMIT && p.z<=WORLD_SAVE_LIMIT;
}
static CircuitPos offset(CircuitPos p,int d) {
    if (d>=4) { p.y+=d==4 ? -1 : 1; return p; }
    return (CircuitPos){p.x+rail_dx(d),p.y,p.z+rail_dz(d)};
}
static int opposite(int d) { return d<4 ? (d+2)&3 : 9-d; }
static int at(CircuitPos p) { return world_cell(p.x,p.y,p.z); }
static int resident_or_edit(CircuitPos p) {
    int raw;
    if (!usable(p)) return AIR;
    return world_resident_cell(p.x,p.y,p.z,&raw) ? raw :
        world_recorded_cell(world_dimension(),p.x,p.y,p.z);
}
int redstone_cistern_probe_level(int x,int y,int z,int raw) {
    if (!cistern_probe_cell(raw)) return -1;
    const CircuitPos p={x,y,z};
    if (!usable(p)) return -1;
    const int tank=resident_or_edit(offset(p,opposite(cistern_probe_facing(raw))));
    return cistern_cell(tank) ? tank-CISTERN_EMPTY : -1;
}
static int probe_signal(int level,int raw) {
    return level>=0 && (cistern_probe_empty(raw) ? !level : level>=2) ? 15 : 0;
}
static int opaque(int raw) {
    int type=cell_type(raw);
    return solid(type) && type!=GLASS && type!=ICE && type!=LEAF && type!=PACKED_ICE && !chest_shape_type(type) &&
        type!=FARMLAND && type!=ENCHANTING_TABLE && type!=ANVIL && type!=HOPPER && !piston_cell(raw) &&
        !cistern_probe_cell(raw);
}
static int support(CircuitPos p,int raw) {
    int s=at(p);
    return opaque(s) || (wire_cell(raw) && s==GLASS) ||
        (build_is_full_top(s) && (wire_cell(raw) || repeater_cell(raw) || comparator_cell(raw) || pressure_cell(raw)));
}
static int switched(int raw) { return lever_cell(raw) && (raw-LEVER_BASE)>=LEVER_ON; }
static int sink(int raw) {
    return gate_cell(raw) || door_cell(raw) || trapdoor_cell(raw) || lamp_cell(raw) || ejector_cell(raw) || raw==TNT_BLOCK ||
        piston_base_cell(raw) || cell_type(raw)==RAIL;
}
static CircuitPos anchor(CircuitPos p,int raw) {
    int mount=(raw-LEVER_BASE)&7;
    return offset(p,mount<2 ? 4 : mount<4 ? 5 : mount-LEVER_WALL);
}
static int same(CircuitPos a,CircuitPos b) { return a.x==b.x && a.y==b.y && a.z==b.z; }
static int lever_power(CircuitPos p,int raw) { return switched(raw) && support(anchor(p,raw),raw); }
static int torch_mount(int raw) { return (raw-TORCH_POWER_BASE)%TORCH_POWER_ON; }
static int torch_on(int raw) { return raw>=TORCH_POWER_BASE+TORCH_POWER_ON; }
static int repeater_facing(int raw) { return (raw-REPEATER_BASE)&3; }
static int repeater_on(int raw) { return (raw-REPEATER_BASE)&REPEATER_ON; }
static int repeater_delay(int raw) { return 1+(((raw-REPEATER_BASE)>>2)&3); }
static int comparator_facing(int raw) { return (raw-COMPARATOR_BASE)&3; }
static int button_mount(int raw) { return (raw-BUTTON_BASE)&3; }
static int button_interval(int raw) { return raw>=BUTTON_WOOD ? 30 : 20; }
static CircuitPos button_anchor(CircuitPos p,int raw) { return offset(p,button_mount(raw)); }
static int subtracting(int raw) { return (raw-COMPARATOR_BASE)&COMPARATOR_SUBTRACT; }
static int latched_level(CircuitPos p);
static int maximum(int a,int b) { return a>b ? a : b; }
static CircuitPos timed_anchor(CircuitPos p,int raw) { return offset(p,torch_power_cell(raw) ? torch_mount(raw) : 4); }
static int powdered(CircuitPos p) { int r=at(p); return wire_cell(r) ? r-WIRE_BASE : 0; }
int redstone_wire_power(int x,int y,int z) { return powdered((CircuitPos){x,y,z}); }
/* Neighbouring dust connects along level ground, up an opaque riser
   with headroom, or down past a non-opaque lip, not through a wall. */
static int wire_neighbor(CircuitPos p,int d,CircuitPos *out) {
    CircuitPos q=offset(p,d);
    int side=at(q);
    if (wire_cell(side)) { *out=q; return 1; }
    if (opaque(side) || side==GLASS) {
        if (opaque(at(offset(p,5)))) return 0;
        ++q.y;
    } else {
        /* The lower endpoint must actually see a supporting riser. */
        if (!support(offset(p,4),WIRE_BASE) || opaque(side)) return 0;
        --q.y;
    }
    if (!wire_cell(at(q))) return 0;
    *out=q;
    return 1;
}
WireShape redstone_wire_shape(int x,int y,int z) {
    CircuitPos p={x,y,z};
    WireShape shape={0,0};
    for (int d=0;d<4;++d) {
        CircuitPos q;
        if (wire_neighbor(p,d,&q)) {
            shape.mask|=1<<d;
            if (q.y>p.y) shape.raised|=1<<d;
        } else {
            int raw=at(offset(p,d));
            if (raw==REDSTONE_BLOCK || lever_cell(raw) || torch_power_cell(raw) || pressure_cell(raw) || button_cell(raw) ||
                (air_relay_cell(raw) && air_relay_facing(raw)==opposite(d)) ||
                (cistern_probe_cell(raw) && cistern_probe_facing(raw)==opposite(d)) ||
                raw==TRAPPED_CHEST_CELL || tripwire_hook_cell(raw) || detector_track(raw) ||
                (repeater_cell(raw) && ((d^repeater_facing(raw))&1)==0) ||
                (comparator_cell(raw) && ((d^comparator_facing(raw))&1)==0)) shape.mask|=1<<d;
        }
    }
    if (shape.mask && !(shape.mask&(shape.mask-1)))
        shape.mask|=((shape.mask<<2)|(shape.mask>>2))&15;
    return shape;
}
/* A mounted lever, the top of a lit torch, and a repeater's front
   drive the receiving solid block; the block doesn't repeat that signal. */
static int diode(CircuitPos p,int raw,int toward) {
    if (repeater_cell(raw) && repeater_on(raw) && repeater_facing(raw)==toward &&
        support(timed_anchor(p,raw),raw)) return 15;
    if (comparator_cell(raw) && comparator_facing(raw)==toward &&
        support(timed_anchor(p,raw),raw)) return latched_level(p);
    return 0;
}
static int strongly_driven(CircuitPos solid_at,int raw) {
    if (!opaque(raw)) return 0;
    int signal=0;
    for (int d=0;d<6;++d) {
        CircuitPos q=offset(solid_at,d);
        int raw=at(q);
        if (switched(raw) && same(anchor(q,raw),solid_at)) return 15;
        if (torch_power_cell(raw) && torch_on(raw) && d==4 && support(timed_anchor(q,raw),raw)) return 15;
        if ((pressure_cell(raw) || detector_track(raw)) && d==5) signal=maximum(signal,latched_level(q));
        if (raw==TRAPPED_CHEST_CELL && d==5)
            signal=maximum(signal,chest_trap_power((ChestPos){q.x,q.y,q.z}));
        if (button_cell(raw) && same(button_anchor(q,raw),solid_at) && latched_level(q)) return 15;
        if (tripwire_hook_cell(raw) && opposite(d)==tripwire_mount(raw) && tripwire_wall(at(solid_at)) &&
            latched_level(q)) return 15;
        signal=maximum(signal,diode(q,raw,opposite(d)));
        if (signal==15) break;
    }
    return signal;
}
static int emits(CircuitPos q,int raw,int toward,int include_wire) {
    if (air_relay_cell(raw)) return toward==air_relay_facing(raw) ? latched_level(q) : 0;
    if (cistern_probe_cell(raw)) return toward==cistern_probe_facing(raw) ?
        probe_signal(redstone_cistern_probe_level(q.x,q.y,q.z,raw),raw) : 0;
    if (raw==REDSTONE_BLOCK || lever_power(q,raw)) return 15;
    if (raw==TRAPPED_CHEST_CELL) return chest_trap_power((ChestPos){q.x,q.y,q.z});
    if (detector_track(raw))
        return full_top_surface(at(offset(q,4))) ? latched_level(q) : 0;
    if (pressure_cell(raw))
        return support(offset(q,4),raw) ? latched_level(q) : 0;
    if (button_cell(raw))
        return support(button_anchor(q,raw),raw) ? latched_level(q) : 0;
    if (tripwire_hook_cell(raw))
        return latched_level(q) && tripwire_wall(at(offset(q,tripwire_mount(raw)))) ? 15 : 0;
    if (torch_power_cell(raw)) return torch_on(raw) && toward!=torch_mount(raw) &&
        support(timed_anchor(q,raw),raw) ? 15 : 0;
    if (repeater_cell(raw) || comparator_cell(raw)) return diode(q,raw,toward);
    if (!include_wire || !wire_cell(raw) || raw==WIRE_BASE || !support(offset(q,4),raw)) return 0;
    if (toward==4) return raw-WIRE_BASE;
    if (toward>=4) return 0;
    WireShape shape=redstone_wire_shape(q.x,q.y,q.z);
    return (!shape.mask || (shape.mask&(1<<toward))) ? raw-WIRE_BASE : 0;
}
static int input_from(CircuitPos p,int direction,int wires) {
    CircuitPos q=offset(p,direction);
    int raw=at(q);
    int direct=emits(q,raw,opposite(direction),wires);
    return direct==15 ? 15 : maximum(direct,strongly_driven(q,raw));
}
static int source_at(CircuitPos p,int include_wire) {
    int signal=0;
    for (int d=0;d<6;++d) {
        signal=maximum(signal,input_from(p,d,include_wire));
        if (signal==15) break;
    }
    return signal;
}
static int locked(CircuitPos p,int raw) {
    int rear=repeater_facing(raw);
    for (int side=1;side<=3;side+=2) {
        int d=(rear+side)&3;
        CircuitPos q=offset(p,d);
        int other=at(q);
        if (diode(q,other,opposite(d))) return 1;
    }
    return 0;
}
int redstone_repeater_locked(int x,int y,int z) {
    CircuitPos p={x,y,z}; int raw=at(p);
    return repeater_cell(raw) && locked(p,raw);
}
int redstone_receives(int x,int y,int z) {
    CircuitPos p={x,y,z};
    if (world_circuit_near(x,z)) return source_at(p,1)!=0;
    for (int d=0;d<6;++d) if (at(offset(p,d))==REDSTONE_BLOCK) return 1;
    return 0;
}

static unsigned bucket(int cx,int cz) { return ((unsigned)cx*374761393u^(unsigned)cz*668265263u)%HASH_BUCKETS; }
static CircuitChunk **find(Dimension d,int cx,int cz) {
    CircuitChunk **link=&banks[d][bucket(cx,cz)];
    while (*link && ((*link)->cx!=cx || (*link)->cz!=cz)) link=&(*link)->next;
    return link;
}
static CircuitChunk **position_chunk(Dimension d,CircuitPos p) {
    return find(d,chunk_coordinate(p.x),chunk_coordinate(p.z));
}
static Device **device_link(CircuitChunk *c,CircuitPos p) {
    Device **link=&c->timers;
    while (*link && !same((*link)->time.position,p)) link=&(*link)->next;
    return link;
}
static Device *device_at(CircuitPos p) {
    CircuitChunk *c=*position_chunk(world_dimension(),p);
    return c ? *device_link(c,p) : NULL;
}
static int latched_level(CircuitPos p) {
    Device *node=device_at(p);
    return node ? node->time.output : 0;
}
int redstone_comparator_power(int x,int y,int z) {
    CircuitPos p={x,y,z};
    return comparator_cell(at(p)) ? latched_level(p) : 0;
}
int redstone_plate_power(int x,int y,int z) {
    CircuitPos p={x,y,z};
    return pressure_cell(world_pressure_cell(x,y,z)) ? latched_level(p) : 0;
}
int redstone_button_power(int x,int y,int z) {
    CircuitPos p={x,y,z};
    return button_cell(at(p)) ? latched_level(p) : 0;
}
int redstone_tripwire_power(int x,int y,int z) {
    CircuitPos p={x,y,z};
    int out=latched_level(p);
    if (!out) return 0;
    int raw=at(p);
    return tripwire_hook_cell(raw) && tripwire_wall(at(offset(p,tripwire_mount(raw)))) ? out : 0;
}
int redstone_detector_power(int x,int y,int z) {
    return latched_level((CircuitPos){x,y,z});
}
int redstone_plate_item(int raw) { return pressure_cell(raw) ? STONE_PLATE_ITEM+raw-PRESSURE_BASE : AIR; }
float redstone_plate_height(int x,int y,int z) { return redstone_plate_power(x,y,z) ? .03125f : .0625f; }
static void park(Dimension d,Device *node,int parked) {
    if (node->parked==parked) return;
    node->parked=parked;
    if (parked) ++parked_count[d]; else --parked_count[d];
}
static Device *add_device(Dimension d,CircuitPos p) {
    CircuitChunk **chunk=position_chunk(d,p),*c=*chunk;
    if (timers[d]>=REDSTONE_TIMING_FILE_LIMIT) return NULL;
    if (!c) {
        c=(CircuitChunk *)calloc(1,sizeof *c);
        if (!c) return NULL;
        c->cx=chunk_coordinate(p.x);c->cz=chunk_coordinate(p.z);*chunk=c;
    }
    Device *node=(Device *)calloc(1,sizeof *node);
    if (!node) return NULL;
    node->time.position=p;
    *device_link(c,p)=node;
    ++timers[d];
    return node;
}
static void link_pulse_timer(Dimension d,Device *node) {
    if (node->pulse_link) return;
    node->pulse_link=&pulse_timers[d];
    node->pulse_next=pulse_timers[d];
    if (node->pulse_next) node->pulse_next->pulse_link=&node->pulse_next;
    pulse_timers[d]=node;
    ++pulse_counts[d];
}
void redstone_plate_contact(int x,int y,int z,int living) {
    CircuitPos p={x,y,z};
    if (!usable(p)) return;
    int raw=world_pressure_cell(x,y,z);
    if (!pressure_cell(raw) || (raw==PRESSURE_STONE && !living)) return;
    Dimension d=world_dimension();
    Device *node=device_at(p);
    if (!node) {
        node=add_device(d,p);
        if (!node) return;
        node->time.remaining=1; /* First contact is applied in this tick. */
    }
    if (node->contact_frame!=pressure_frame) {
        node->contact_frame=pressure_frame;
        node->contacts=0;
    }
    if (node->contacts<150) ++node->contacts;
}
static void remove_device(Dimension d,Device **link) {
    Device *dead=*link;
    *link=dead->next;
    if (dead->pulse_link) {
        *dead->pulse_link=dead->pulse_next;
        if (dead->pulse_next) dead->pulse_next->pulse_link=dead->pulse_link;
        --pulse_counts[d];
    }
    if (dead->parked) --parked_count[d];
    free(dead); --timers[d];
}
static void forget_device(CircuitPos p) {
    Dimension d=world_dimension();
    CircuitChunk *c=*position_chunk(d,p);
    if (!c) return;
    Device **link=device_link(c,p);
    if (*link) remove_device(d,link);
}
static void release(CircuitChunk *c) {
    while (c->timers) {
        Device *next=c->timers->next;
        free(c->timers);c->timers=next;
    }
    free(c->logic.cells); free(c->logic.present); free(c->gates.cells); free(c->gates.present); free(c);
}
void redstone_reset(void) {
    button_arrows_reset();
    for (int d=0;d<DIMENSION_COUNT;++d) for (int b=0;b<HASH_BUCKETS;++b) {
        CircuitChunk *c=banks[d][b];
        while (c) { CircuitChunk *next=c->next; release(c); c=next; }
        banks[d][b]=NULL;
    }
    memset(queued,0,sizeof queued);
    memset(rebuild,0,sizeof rebuild);
    pressure_frame=0;
    memset(timers,0,sizeof timers);
    memset(pulse_timers,0,sizeof pulse_timers);
    memset(pulse_counts,0,sizeof pulse_counts);
    memset(parked_count,0,sizeof parked_count);
}
size_t redstone_timing_count(Dimension d) { return (unsigned)d<DIMENSION_COUNT ? timers[d] : 0; }
int redstone_visit_timing(Dimension d,RedstoneTimingVisitor visit,void *ctx) {
    if ((unsigned)d>=DIMENSION_COUNT || !visit) return 0;
    for (int b=0;b<HASH_BUCKETS;++b) for (CircuitChunk *c=banks[d][b];c;c=c->next)
        for (Device *node=c->timers;node;node=node->next) if (!visit(&node->time,ctx)) return 0;
    return 1;
}
int redstone_restore_timing(Dimension d,RedstoneTiming t) {
    CircuitPos p=t.position;
    if ((unsigned)d>=DIMENSION_COUNT || !usable(p) || t.remaining>30 || t.target>15 || t.output>15 ||
        t.cooldown>BURNOUT_WAIT || t.count>8) return 0;
    int raw=world_recorded_cell(d,p.x,p.y,p.z);
    if (ejector_cell(raw)) {
        if ((!t.remaining && !t.target) || t.remaining>DROPPER_DELAY || t.target>1 || t.output || t.cooldown ||
            t.count) return 0;
    } else if (button_cell(raw)) {
        if (!t.remaining || t.remaining>button_interval(raw) || t.target || t.cooldown || t.count || t.output!=15) return 0;
    } else if (air_relay_cell(raw)) {
        if (!t.remaining || t.remaining>AIR_RELAY_TICKS || t.target || t.cooldown || t.count ||
            t.output!=AIR_RELAY_POWER) return 0;
    } else if (tripwire_hook_cell(raw) || detector_track(raw)) {
        int interval=detector_track(raw) ? DETECTOR_RAIL_TICKS : TRIPWIRE_PULSE_TICKS;
        if (!t.remaining || t.remaining>interval || t.target || t.cooldown || t.count || t.output!=15)
            return 0;
    } else if (pressure_cell(raw)) {
        int digital=raw==PRESSURE_STONE || raw==PRESSURE_WOOD;
        if (!t.remaining || t.remaining>pressure_interval(raw) || t.target || t.cooldown || t.count ||
            (!t.output && t.remaining!=1) || (digital && t.output && t.output!=15)) return 0;
    } else if (comparator_cell(raw)) {
        if ((!t.output && !t.remaining) || t.remaining>2 || t.cooldown || t.count ||
            (t.remaining && t.target==t.output) || (!t.remaining && t.target)) return 0;
    } else if (t.output || t.target>1) return 0;
    else if (lamp_cell(raw)) {
        if (raw!=LAMP_ON || !t.remaining || t.remaining>4 || t.target || t.cooldown || t.count) return 0;
    } else if (!timed_circuit_cell(raw) || (t.remaining && t.target==(repeater_cell(raw) ?
        repeater_on(raw)!=0 : torch_on(raw)))) return 0;
    if (repeater_cell(raw) && (!t.remaining || t.remaining>8 || t.cooldown || t.count)) return 0;
    if (torch_power_cell(raw) && (t.remaining>2 || (t.cooldown && (torch_on(raw) || t.remaining)) ||
        (!t.remaining && !t.count && !t.cooldown))) return 0;
    for (unsigned i=0;i<t.count;++i) if (t.ages[i]>BURNOUT_HISTORY ||
        (i && t.ages[i]>t.ages[i-1])) return 0;
    CircuitChunk *c=*position_chunk(d,p);
    if (c && *device_link(c,p)) return 0;
    Device *node=add_device(d,p);
    if (!node) return 0;
    node->time=t;
    if (tripwire_hook_cell(raw) || detector_track(raw) || air_relay_cell(raw)) link_pulse_timer(d,node);
    if ((comparator_cell(raw)||ejector_cell(raw)) && !t.remaining) park(d,node,1);
    return 1;
}
static int enqueue(CircuitPos p,int gate) {
    if (!usable(p)) return 1;
    Dimension d=world_dimension();
    int cx=chunk_coordinate(p.x),cz=chunk_coordinate(p.z);
    CircuitChunk **link=find(d,cx,cz),*c=*link;
    int created=c==NULL;
    if (!c) {
        c=(CircuitChunk *)calloc(1,sizeof *c);
        if (!c) return 0;
        c->cx=cx;c->cz=cz;*link=c;
    }
    Queue *q=gate ? &c->gates : &c->logic;
    uint16_t cell=(uint16_t)((((p.x-cx*CHUNK_SIZE)*CHUNK_SIZE)+(p.z-cz*CHUNK_SIZE))*WY+p.y);
    if (q->present) {
        if (q->present[cell/8]&(1u<<(cell&7))) return 1;
    } else {
        for (size_t i=0;i<q->count;++i) if (q->cells[i]==cell) return 1;
        if (q->count>=96) {
            q->present=(unsigned char *)calloc(CHUNK_CELLS/8,1);
            if (!q->present) return 0;
            for (size_t i=0;i<q->count;++i) q->present[q->cells[i]/8]|=1u<<(q->cells[i]&7);
        }
    }
    if (q->count==q->capacity) {
        size_t cap=q->capacity ? 2*q->capacity : 4;
        uint16_t *cells=(uint16_t *)realloc(q->cells,cap*sizeof *cells);
        if (!cells) {
            if (created) { *link=c->next; release(c); }
            return 0;
        }
        q->cells=cells;q->capacity=cap;
    }
    q->cells[q->count++]=cell;
    if (q->present) q->present[cell/8]|=1u<<(cell&7);
    ++queued[d];
    return 1;
}
static void mark(CircuitPos p,int gate) {
    if (!enqueue(p,gate)) rebuild[world_dimension()]=1;
}
static CircuitPos decode(const CircuitChunk *c,uint16_t local) {
    int col=local/WY;
    return (CircuitPos){c->cx*CHUNK_SIZE+col/CHUNK_SIZE,local%WY,c->cz*CHUNK_SIZE+col%CHUNK_SIZE};
}
static int ready(CircuitPos p) {
    if (!world_chunk_ready(p.x,p.z)) return 0;
    int raw=at(p),halo=comparator_cell(raw) ? 4 : torch_power_cell(raw) ? 3 : 2;
    return world_chunk_ready(p.x-halo,p.z-halo) && world_chunk_ready(p.x-halo,p.z+halo) &&
        world_chunk_ready(p.x+halo,p.z-halo) && world_chunk_ready(p.x+halo,p.z+halo);
}
/* Changing the signal of a comparator doesn't modify its terrain byte. */
static void notify_region(int x,int y,int z) {
    /* Near an unready seam the rounded region is still recorded; processing
       re-reads the actual tile before any simulation or drop. */
    for (int a=-3;a<=3;++a) {
        int row=3-abs(a);
        for (int b=-row;b<=row;++b) {
            int span=row-abs(b);
            for (int c=-span;c<=span;++c) {
                int near=abs(c)<span;
                CircuitPos p={x+a,y+b,z+c};
                if (!usable(p)) continue;
                if (!world_chunk_ready(p.x,p.z)) {
                    mark(p,0);
                    int authored=world_recorded_cell(world_dimension(),p.x,p.y,p.z);
                    if (near || ejector_cell(authored) || piston_base_cell(authored)) mark(p,1);
                    continue;
                }
                int raw=at(p);
                if (near ? circuit_cell(raw) : torch_power_cell(raw)||comparator_cell(raw)) mark(p,0);
                if ((near && sink(raw)) || ejector_cell(raw) || piston_base_cell(raw)) mark(p,1);
            }
        }
    }
}
void redstone_cistern_changed(int x,int y,int z,int before,int after) {
    if (!world_circuit_near(x,z)) return;
    int prev=cistern_cell(before) ? before-CISTERN_EMPTY : -1;
    int next=cistern_cell(after) ? after-CISTERN_EMPTY : -1;
    const CircuitPos tank={x,y,z};
    for (int d=0;d<4;++d) {
        CircuitPos p=offset(tank,d);
        int raw=resident_or_edit(p);
        if (cistern_probe_cell(raw) && cistern_probe_facing(raw)==d && probe_signal(prev,raw)!=probe_signal(next,raw))
            notify_region(p.x,p.y,p.z);
    }
}
int redstone_toggle_cistern_probe(int x,int y,int z,int *next) {
    const CircuitPos p={x,y,z};
    int raw;
    if (!usable(p) || !world_resident_cell(x,y,z,&raw) || !cistern_probe_cell(raw)) return 0;
    const int changed=CISTERN_PROBE_BASE+((raw-CISTERN_PROBE_BASE)^CISTERN_PROBE_EMPTY);
    const BlockEdit edit={x,y,z,(uint16_t)changed};
    if (!world_set_blocks(&edit,1)) return 0;
    if (next) *next=changed;
    return 1;
}
void redstone_tripwire_pulse(CircuitPos one,CircuitPos other) {
    Device *a=device_at(one),*b=device_at(other);
    int fresh_a=a==NULL;
    if (!a) a=add_device(world_dimension(),one);
    if (!a) return;
    if (!b) b=add_device(world_dimension(),other);
    if (!b) {
        if (fresh_a) forget_device(one);
        return;
    }
    Device *pair[]={a,b};
    for (int i=0;i<2;++i) {
        Device *node=pair[i];
        link_pulse_timer(world_dimension(),node);
        node->time.remaining=TRIPWIRE_PULSE_TICKS;
        if (node->time.output) continue;
        node->time.output=15;
        CircuitPos p=node->time.position;
        world_touch_circuit_mesh(p.x,p.z);
        notify_region(p.x,p.y,p.z);
    }
}
void redstone_detector_contact(CircuitPos p) {
    if (!usable(p) || device_at(p)) return;
    int raw=at(p);
    if (!detector_track(raw) || !full_top_surface(world_cell(p.x,p.y-1,p.z))) return;
    Dimension d=world_dimension();
    Device *node=add_device(d,p);
    if (!node) return;
    link_pulse_timer(d,node);
    node->time.remaining=DETECTOR_RAIL_TICKS;
    node->time.output=15;
    world_touch_circuit_mesh(p.x,p.z);
    notify_region(p.x,p.y,p.z);
}
AirRelayResult redstone_press_air_relay(int x,int y,int z,int *remaining) {
    const CircuitPos p={x,y,z};
    int raw;
    if (!usable(p) || !world_resident_cell(x,y,z,&raw) || !air_relay_cell(raw)) return AIR_RELAY_ERROR;
    if (device_at(p)) return AIR_RELAY_BUSY;
    const CircuitPos intake=offset(p,opposite(air_relay_facing(raw)));
    if (!usable(intake)) return AIR_RELAY_DISCONNECTED;
    if (!world_resident_cell(intake.x,intake.y,intake.z,&raw)) return AIR_RELAY_UNLOADED;
    if (!air_station_cell(raw)) return AIR_RELAY_DISCONNECTED;
    const int charge=raw-AIR_STATION_BASE;
    if (charge<AIR_RELAY_COST) return AIR_RELAY_UNCHARGED;
    Dimension d=world_dimension();
    Device *node=add_device(d,p);
    if (!node) return AIR_RELAY_ERROR;
    const BlockEdit discharge={intake.x,intake.y,intake.z,AIR_STATION_BASE+charge-AIR_RELAY_COST};
    if (!world_set_blocks(&discharge,1)) {
        forget_device(p);
        return AIR_RELAY_ERROR;
    }
    node->time.remaining=AIR_RELAY_TICKS;
    node->time.output=AIR_RELAY_POWER;
    link_pulse_timer(d,node);
    if (remaining) *remaining=charge-AIR_RELAY_COST;
    notify_region(x,y,z);
    return AIR_RELAY_STARTED;
}
static void advance_pulse_timers(Dimension d) {
    /* These short pulses tick in the current dimension, not just at the player's feet. */
    for (Device *node=pulse_timers[d];node;) {
        Device *next=node->pulse_next;
        if (node->time.remaining>1) --node->time.remaining;
        else {
            CircuitPos p=node->time.position;
            int raw=world_recorded_cell(d,p.x,p.y,p.z);
            if (detector_track(raw) && cart_on_track((RailCell){p.x,p.y,p.z}))
                node->time.remaining=DETECTOR_RAIL_TICKS;
            else {
                forget_device(p);
                if (!air_relay_cell(raw)) world_touch_circuit_mesh(p.x,p.z);
                notify_region(p.x,p.y,p.z);
            }
        }
        node=next;
    }
}
static int container_type(int raw) {
    return is_chest(cell_type(raw)) || raw==FURNACE || raw==BREWING_STAND || hopper_cell(raw) || ejector_cell(raw);
}
static int wake_piston_line(void *ctx,int x,int y,int z,int raw) {
    CircuitPos p={x,y,z},q=*(const CircuitPos *)ctx;
    int dir=piston_facing(raw),distance=abs(q.x-x)+abs(q.y-y)+abs(q.z-z);
    if (distance>0 && distance<=PISTON_PUSH_LIMIT+1 && same(q,piston_offset(p,dir,distance))) mark(p,1);
    return 1;
}
void redstone_after_edit(int x,int y,int z,int before,int after) {
    CircuitPos p={x,y,z};
    if (before==TRAPPED_CHEST_CELL && after!=before) chest_trap_removed((ChestPos){x,y,z});
    if (before==TRIPWIRE_CELL && after!=before) tripwire_wire_removed(p);
    if ((lamp_cell(before) && !lamp_cell(after)) || (ejector_cell(before) && before!=after) ||
        (air_relay_cell(before) && before!=after) ||
        (tripwire_hook_cell(before) && before!=after) ||
        (detector_track(before) && !detector_track(after)) ||
        ((pressure_cell(before)||button_cell(before)) && before!=after) ||
        (comparator_cell(before) && (!comparator_cell(after) || comparator_facing(before)!=comparator_facing(after))) ||
        (repeater_cell(before) && (!repeater_cell(after) || repeater_facing(before)!=repeater_facing(after))) ||
        (torch_power_cell(before) && (!torch_power_cell(after) || torch_mount(before)!=torch_mount(after)))) forget_device(p);
    if (before==after || (wire_cell(before) && wire_cell(after)) ||
        (lamp_cell(before) && lamp_cell(after)) || (door_cell(before) && door_cell(after)) ||
        (trapdoor_cell(before) && trapdoor_cell(after))) return;
    world_visit_piston_near(x,z,wake_piston_line,&p);
    if (cistern_cell(before) || cistern_cell(after)) redstone_cistern_changed(x,y,z,before,after);
    if (detector_track(before)!=detector_track(after) || air_relay_cell(before) || air_relay_cell(after) ||
        cistern_probe_cell(before) || cistern_probe_cell(after)) {
        notify_region(x,y,z); return;
    }
    if (!(circuit_cell(before)||circuit_cell(after)||sink(before)||sink(after)||
        container_type(before)||container_type(after)) &&
        (opaque(before)==opaque(after) && before!=REDSTONE_BLOCK && after!=REDSTONE_BLOCK &&
            cell_type(before)!=GLASS && cell_type(after)!=GLASS && !lava_strength(before) && !lava_strength(after))) return;
    if (world_circuit_near(x,z) || circuit_cell(before) || circuit_cell(after) || sink(after) ||
        before==REDSTONE_BLOCK || after==REDSTONE_BLOCK)
        notify_region(x,y,z);
}
void redstone_trap_changed(int x,int y,int z) { notify_region(x,y,z); }
void redstone_container_changed(int x,int y,int z) {
    CircuitPos from={x,y,z};
    if (!usable(from) || !world_circuit_near(x,z)) return;
    Dimension d=world_dimension();
    for (int dir=0;dir<4;++dir) {
        CircuitPos p=from;
        for (int distance=0;distance<2;++distance) {
            p=offset(p,dir);
            if (!usable(p)) break;
            int raw=world_recorded_cell(d,p.x,p.y,p.z);
            if (comparator_cell(raw) && comparator_facing(raw)==dir) mark(p,0);
        }
    }
}
int redstone_restore(int x,int y,int z,int raw) {
    return !(circuit_cell(raw)||sink(raw)) || enqueue((CircuitPos){x,y,z},sink(raw));
}
static int restore_task(void *ctx,int x,int y,int z,int raw) {
    (void)ctx;
    return redstone_restore(x,y,z,raw);
}
void redstone_activate_terrain_device(int x,int y,int z,int raw) {
    mark((CircuitPos){x,y,z},sink(raw));
}
static void gates_touching(CircuitPos p) {
    CircuitPos below=offset(p,4);
    int has_upper_sinks=droppers_present() || world_has_pistons();
    for (int d=0;d<6;++d) {
        CircuitPos near=offset(p,d);
        if (sink(at(near))) mark(near,1);
        /* The block above a dropper can also be powered. */
        CircuitPos under=offset(below,d);
        if (has_upper_sinks && usable(under)) {
            int raw=at(under);
            if (ejector_cell(raw) || piston_base_cell(raw)) mark(under,1);
        }
    }
}
static void timers_touching_wire(CircuitPos p) {
    for (int d=0;d<6;++d) {
        CircuitPos q=offset(p,d);
        int raw=at(q);
        if (timed_circuit_cell(raw)) mark(q,0);
        if (!opaque(raw)) continue;
        for (int side=0;side<6;++side) {
            CircuitPos t=offset(q,side);
            int torch=at(t);
            if (torch_power_cell(torch) && same(timed_anchor(t,torch),q)) mark(t,0);
        }
    }
}
int redstone_replace(int x,int y,int z,int replacement) {
    int original=world_cell(x,y,z);
    if (!circuit_cell(original)) return 0;
    int slot=-1;
    if (player_rules.mode==SURVIVAL) {
        for (int i=0;i<MAXDROP;++i) if (!drops[i].item.count) { slot=i; break; }
        if (slot<0) return 0;
    }
    BlockEdit edit={x,y,z,replacement};
    if (!world_set_blocks(&edit,1)) return 0;
    if (slot>=0) {
        ItemStack item=empty_stack;
        item.type=wire_cell(original) ? REDSTONE_DUST : torch_power_cell(original) ? REDSTONE_TORCH_ITEM :
            repeater_cell(original) ? REPEATER_ITEM : comparator_cell(original) ? COMPARATOR_ITEM :
            pressure_cell(original) ? (uint16_t)redstone_plate_item(original) :
            button_cell(original) ? (original>=BUTTON_WOOD ? WOOD_BUTTON_ITEM : STONE_BUTTON_ITEM) :
            original==TRIPWIRE_CELL ? STRING : tripwire_hook_cell(original) ? TRIPWIRE_HOOK_ITEM : LEVER_ITEM;
        item.count=1;
        drops[slot]=make_drop(x+.5f,y+.18f,z+.5f,item);
        drops_dirty=1;
    }
    return 1;
}
static int timer_empty(const RedstoneTiming *t) { return !t->remaining && !t->count && !t->cooldown && !t->output; }
static int device_output(int raw) { return repeater_cell(raw) ? repeater_on(raw)!=0 : torch_on(raw); }
static int requested_output(CircuitPos p,int raw) {
    if (repeater_cell(raw)) return input_from(p,opposite(repeater_facing(raw)),1)!=0;
    return !source_at(timed_anchor(p,raw),1);
}
static int consider_device(CircuitPos p,int raw) {
    Device *node=device_at(p);
    RedstoneTiming *t=node ? &node->time : NULL;
    int repeater=repeater_cell(raw);
    if ((repeater && locked(p,raw)) || (t && t->cooldown)) {
        if (t) { t->remaining=0; t->target=0; if (timer_empty(t)) forget_device(p); }
        return 1;
    }
    int on=device_output(raw),wanted=requested_output(p,raw);
    /* Preserve a scheduled rising repeater edge if its input pulse
       ended early. This stretches short pulses to the selected delay. */
    if (repeater && t && t->remaining && t->target && !on) return 1;
    if (on==wanted) {
        if (t) { t->remaining=0; t->target=0; if (timer_empty(t)) forget_device(p); }
        return 1;
    }
    if (!t) {
        node=add_device(world_dimension(),p);
        if (!node) return 0;
        t=&node->time;
    }
    if (!t->remaining || t->target!=wanted) {
        t->target=(uint8_t)wanted;
        t->remaining=(uint8_t)(repeater ? repeater_delay(raw)*2 : 2);
    }
    return 1;
}
static int compare_signal(CircuitPos p,int raw) {
    int facing=comparator_facing(raw),rear_dir=opposite(facing);
    CircuitPos q=offset(p,rear_dir);
    int q_cell=at(q),rear=input_from(p,rear_dir,1),side=0;
    /* Occupancy drives the digital output; cargo drives only the comparator. */
    int measured=detector_track(q_cell) ? cart_detector_signal((RailCell){q.x,q.y,q.z}) :
        container_signal(q.x,q.y,q.z,cell_type(q_cell));
    if (measured>=0) rear=measured;
    else if (rear<15 && opaque(q_cell)) {
        CircuitPos far=offset(q,rear_dir);
        measured=container_signal(far.x,far.y,far.z,cell_type(at(far)));
        if (measured>=0) rear=measured;
    }
    for (int step=1;step<=3;step+=2) {
        int d=(facing+step)&3;
        CircuitPos s=offset(p,d);
        int cell=at(s);
        /* Side ports are diode/dust/solid redstone block inputs. A powered
           lever or an ordinary strongly powered cube isn't a side input. */
        int power=cell==REDSTONE_BLOCK ? 15 : wire_cell(cell) ? cell-WIRE_BASE :
            air_relay_cell(cell) ? emits(s,cell,opposite(d),0) : diode(s,cell,opposite(d));
        side=maximum(side,power);
    }
    return rear<side ? 0 : subtracting(raw) ? rear-side : rear;
}
static int consider_comparator(CircuitPos p,int raw) {
    Dimension d=world_dimension();
    Device *node=device_at(p);
    int old=node ? node->time.output : 0, wanted=compare_signal(p,raw);
    if (old==wanted) {
        if (!node) return 1;
        if (!old) forget_device(p);
        else {
            node->time.remaining=node->time.target=0;
            park(d,node,1);
        }
        return 1;
    }
    if (!node && !(node=add_device(d,p))) return 0;
    if (!node->time.remaining) node->time.remaining=2;
    node->time.target=(uint8_t)wanted;
    park(d,node,0);
    return 1;
}
static int refresh_sink(CircuitPos p,int raw) {
    if (cell_type(raw)==RAIL) return rail_refresh_junction((RailCell){p.x,p.y,p.z});
    if (door_cell(raw)) return door_refresh_redstone(p.x,p.y,p.z);
    if (trapdoor_cell(raw)) return trapdoor_refresh_redstone(p.x,p.y,p.z);
    if (piston_base_cell(raw)) {
        int facing=piston_facing(raw),powered=0;
        for (int d=0;d<6 && !powered;++d) if (d!=facing) powered=input_from(p,d,1)!=0;
        CircuitPos above=offset(p,5);
        if (!powered && usable(above)) for (int d=0;d<6 && !powered;++d)
            if (d!=4) powered=input_from(above,d,1)!=0; /* Classic quasi-connectivity. */
        return piston_refresh(p,raw,powered);
    }
    if (raw==TNT_BLOCK) {
        for (int d=0;d<6;++d) if (lava_strength(at(offset(p,d)))) return blast_prime(p.x,p.y,p.z);
        return !source_at(p,1) || blast_prime(p.x,p.y,p.z);
    }
    if (ejector_cell(raw)) {
        Dimension d=world_dimension();
        Device *node=device_at(p);
        int powered=source_at(p,1) || source_at(offset(p,5),1);
        if (powered) {
            if (!node && !(node=add_device(d,p))) return 0;
            if (!node->time.target && !node->time.remaining) node->time.remaining=DROPPER_DELAY;
            node->time.target=1;
            park(d,node,!node->time.remaining);
        } else if (node) {
            node->time.target=0;
            /* Like an already queued block tick, a short pulse still fires. */
            if (!node->time.remaining) forget_device(p);
        }
        return 1;
    }
    if (!lamp_cell(raw)) { gate_refresh_redstone(p.x,p.y,p.z); return 1; }
    if (source_at(p,1)) {
        if (raw==LAMP_ON) { forget_device(p); return 1; }
        BlockEdit edit={p.x,p.y,p.z,LAMP_ON};
        return world_set_blocks(&edit,1);
    }
    if (raw==LAMP_OFF || device_at(p)) return 1;
    Device *node=add_device(world_dimension(),p);
    if (!node) return 0;
    node->time.remaining=4;
    return 1;
}
static int weight_output(int raw,unsigned count) {
    if (!count) return 0;
    if (raw==PRESSURE_GOLD) return count<15 ? (int)count : 15;
    if (raw==PRESSURE_IRON) return (int)((count+9)/10);
    return 15;
}
static void advance_timing(Dimension d,int cx,int cz,int *budget) {
    CircuitChunk **chunk=find(d,cx,cz),*c=*chunk;
    if (!c) return;
    for (Device **link=&c->timers;*link;) {
        if ((*link)->parked || (*link)->pulse_link) { link=&(*link)->next; continue; }
        RedstoneTiming *t=&(*link)->time;
        CircuitPos p=t->position;
        if (!ready(p)) { link=&(*link)->next; continue; }
        int raw=at(p);
        if (ejector_cell(raw)) {
            if (t->remaining>1) --t->remaining;
            else if (*budget && dropper_fire(p.x,p.y,p.z,raw)) {
                --*budget;
                t->remaining=0;
                if (!t->target) { remove_device(d,link); continue; }
                park(d,*link,1);
            }
            link=&(*link)->next;
            continue;
        }
        if (button_cell(raw)) {
            if (t->remaining>1) --t->remaining;
            else if (*budget) {
                --*budget;
                if (raw>=BUTTON_WOOD && button_arrow_present(p.x,p.y,p.z) && support(button_anchor(p,raw),raw)) {
                    t->remaining=(uint8_t)button_interval(raw);
                } else {
                    remove_device(d,link);
                    world_touch_circuit_mesh(p.x,p.z);
                    notify_region(p.x,p.y,p.z);
                    continue;
                }
            }
            link=&(*link)->next;
            continue;
        }
        if (pressure_cell(raw)) {
            if (t->remaining>1) --t->remaining;
            else if (*budget) {
                --*budget;
                Device *node=*link;
                int signal=node->contact_frame==pressure_frame && support(offset(p,4),raw) ?
                    weight_output(raw,node->contacts) : 0;
                if (signal!=t->output) {
                    t->output=(uint8_t)signal;
                    world_touch_circuit_mesh(p.x,p.z);
                    notify_region(p.x,p.y,p.z);
                }
                if (!signal) { remove_device(d,link); continue; }
                t->remaining=(uint8_t)pressure_interval(raw);
            }
            link=&(*link)->next;
            continue;
        }
        if (comparator_cell(raw)) {
            if (t->remaining>1) --t->remaining;
            else if (*budget) {
                --*budget;
                int signal=support(timed_anchor(p,raw),raw) ? compare_signal(p,raw) : 0;
                if (signal!=t->output) {
                    t->output=(uint8_t)signal;
                    world_touch_circuit_mesh(p.x,p.z);
                    notify_region(p.x,p.y,p.z);
                }
                t->remaining=t->target=0;
                if (!t->output) { remove_device(d,link); continue; }
                park(d,*link,1);
            }
            link=&(*link)->next;
            continue;
        }
        if (raw==LAMP_ON) {
            if (source_at(p,1)) { remove_device(d,link); continue; }
            if (t->remaining>1) --t->remaining;
            else if (*budget) {
                --*budget;
                BlockEdit edit={p.x,p.y,p.z,LAMP_OFF};
                if (world_set_blocks(&edit,1)) { remove_device(d,link); continue; }
            }
            link=&(*link)->next;
            continue;
        }
        if (!timed_circuit_cell(raw)) { remove_device(d,link); continue; }
        int n=0;
        for (unsigned i=0;i<t->count;++i) if (t->ages[i]<BURNOUT_HISTORY)
            t->ages[n++]=(uint8_t)(t->ages[i]+1);
        t->count=(uint8_t)n;
        if (t->cooldown && !--t->cooldown) mark(p,0);
        if (t->remaining>1) --t->remaining;
        else if (t->remaining && *budget) {
            --*budget;
            int repeater=repeater_cell(raw),to=t->target;
            int same_kind_support=support(timed_anchor(p,raw),raw);
            int fire=same_kind_support && device_output(raw)!=to &&
                !(repeater && locked(p,raw)) &&
                ((repeater && to) || requested_output(p,raw)==to);
            if (fire) {
                int next=repeater ? REPEATER_BASE+((raw-REPEATER_BASE)^REPEATER_ON) :
                    TORCH_POWER_BASE+torch_mount(raw)+(to ? TORCH_POWER_ON : 0);
                BlockEdit edit={p.x,p.y,p.z,next};
                if (!world_set_blocks(&edit,1)) { link=&(*link)->next; continue; }
                if (!repeater && !to) {
                    if (t->count==8) { memmove(t->ages,t->ages+1,7); --t->count; }
                    t->ages[t->count++]=0;
                    if (t->count>=8) t->cooldown=BURNOUT_WAIT;
                }
            }
            t->remaining=0; t->target=0;
            /* A stretched repeater pulse schedules its falling edge. Burned
               torches won't turn on again until the cooldown marks them. */
            mark(p,0);
        }
        if (timer_empty(t)) remove_device(d,link);
        else link=&(*link)->next;
    }
    if (!c->timers && !c->logic.count && !c->gates.count) { *chunk=c->next; release(c); }
}
static int solve(CircuitPos p) {
    int raw=at(p);
    if (!circuit_cell(raw) || raw==TRIPWIRE_CELL) return 1;
    if (tripwire_hook_cell(raw))
        return tripwire_wall(at(offset(p,tripwire_mount(raw)))) || redstone_replace(p.x,p.y,p.z,AIR);
    CircuitPos mount=lever_cell(raw) ? anchor(p,raw) : button_cell(raw) ? button_anchor(p,raw) :
        timed_circuit_cell(raw) ? timed_anchor(p,raw) : offset(p,4);
    if (!support(mount,raw)) {
        if (redstone_replace(p.x,p.y,p.z,AIR)) return 1;
        /* A full drop pool can't leave an unsupported wire powering its
           neighbours. Keep its item and retry the actual drop later. */
        if (wire_cell(raw) && raw!=WIRE_BASE && world_write_cell(p.x,p.y,p.z,WIRE_BASE))
            redstone_after_edit(p.x,p.y,p.z,raw,AIR);
        if ((comparator_cell(raw)||pressure_cell(raw)||button_cell(raw)) && device_at(p)) {
            int signal=latched_level(p);
            forget_device(p);
            if (signal) {
                world_touch_circuit_mesh(p.x,p.z);
                notify_region(p.x,p.y,p.z);
            }
        }
        return 0;
    }
    if (lever_cell(raw) || pressure_cell(raw) || button_cell(raw)) return 1;
    if (comparator_cell(raw)) return consider_comparator(p,raw);
    if (timed_circuit_cell(raw)) return consider_device(p,raw);
    int best=source_at(p,0);
    CircuitPos linked[4];
    int n=0;
    for (int d=0;d<4;++d) if (wire_neighbor(p,d,&linked[n])) {
        int signal=powdered(linked[n++])-1;
        if (signal>best) best=signal;
    }
    gates_touching(p); /* Connection directions can change with the same strength. */
    if (raw!=WIRE_BASE || best) timers_touching_wire(p);
    if (WIRE_BASE+best==raw) return 1;
    if (!world_write_cell(p.x,p.y,p.z,WIRE_BASE+best)) return 0;
    for (int i=0;i<n;++i) mark(linked[i],0);
    return 1;
}
static int process(Dimension d,int cx,int cz,int gate,int *budget) {
    CircuitChunk **link=find(d,cx,cz),*c=*link;
    if (!c) return 0;
    Queue *q=gate ? &c->gates : &c->logic;
    int did=0;
    size_t initial=q->count;
    CircuitPos deferred[SOLVE_BUDGET];
    unsigned postponed=0;
    while (*budget && q->count && initial--) {
        uint16_t cell=q->cells[q->count-1];
        CircuitPos p=decode(c,cell);
        --q->count; --queued[d]; --*budget;
        if (q->present) q->present[cell/8]&=~(1u<<(cell&7));
        if (!ready(p)) { deferred[postponed++]=p; continue; }
        if (!(gate ? refresh_sink(p,at(p)) : solve(p))) { deferred[postponed++]=p; continue; }
        did=1;
    }
    for (unsigned i=0;i<postponed;++i) mark(deferred[i],gate);
    if (!c->logic.count && !c->gates.count && !c->timers) { *link=c->next; release(c); }
    return did;
}
void redstone_step(int x,int z) {
    Dimension d=world_dimension();
    if (rebuild[d]) rebuild[d]=!world_visit_edits(restore_task,NULL);
    int cx=chunk_coordinate(x),cz=chunk_coordinate(z),worked,remaining=SOLVE_BUDGET;
    world_activate_terrain_devices(cx,cz,CIRCUIT_RADIUS);
    button_arrows_tick();
    ++pressure_frame;
    int sensors=world_contact_sensors_near(cx,cz,CIRCUIT_RADIUS);
    if (sensors) pressure_contacts(cx,cz,CIRCUIT_RADIUS,sensors);
    advance_pulse_timers(d);
    if (timers[d]>parked_count[d]+pulse_counts[d]) {
        int actions=TIMED_BUDGET;
        for (int r=0;r<=CIRCUIT_RADIUS;++r)
            for (int a=-r;a<=r;++a) for (int b=-r;b<=r;++b)
                if (abs(a)==r || abs(b)==r) advance_timing(d,cx+a,cz+b,&actions);
    }
    if (!queued[d]) return;
    do {
        worked=0;
        for (int r=0;r<=CIRCUIT_RADIUS && remaining;++r)
            for (int a=-r;a<=r && remaining;++a) for (int b=-r;b<=r && remaining;++b)
                if (abs(a)==r || abs(b)==r) worked|=process(d,cx+a,cz+b,0,&remaining);
    } while (worked && remaining);
    if (!remaining) return; /* Don't flicker gates halfway through a convergence wave. */
    remaining=GATE_BUDGET;
    for (int a=-CIRCUIT_RADIUS;a<=CIRCUIT_RADIUS && remaining;++a)
        for (int b=-CIRCUIT_RADIUS;b<=CIRCUIT_RADIUS && remaining;++b)
            process(d,cx+a,cz+b,1,&remaining);
}
int redstone_place_wire(int x,int y,int z) {
    if (!inworld(x,y,z) || world_cell(x,y,z)!=AIR || !support((CircuitPos){x,y-1,z},WIRE_BASE)) return 0;
    BlockEdit edit={x,y,z,WIRE_BASE};
    return world_set_blocks(&edit,1);
}
int redstone_place_lever(CircuitPos put,CircuitPos against,int east_west) {
    if (!usable(put) || at(put)!=AIR) return 0;
    int mount=-1;
    for (int d=0;d<6;++d) if (same(offset(put,d),against)) {
        mount=d<4 ? LEVER_WALL+d : (d==4 ? LEVER_FLOOR_NS : LEVER_CEILING_NS)+(east_west!=0);
        break;
    }
    if (mount<0 || !opaque(at(against))) return 0;
    BlockEdit edit={put.x,put.y,put.z,LEVER_BASE+mount};
    return world_set_blocks(&edit,1);
}
int redstone_toggle_lever(int x,int y,int z) {
    int raw=world_cell(x,y,z);
    if (!lever_cell(raw) || !support(anchor((CircuitPos){x,y,z},raw),raw)) return 0;
    BlockEdit edit={x,y,z,LEVER_BASE+((raw-LEVER_BASE)^LEVER_ON)};
    return world_set_blocks(&edit,1) ? switched(edit.cell) ? 2 : 1 : 0;
}
int redstone_place_torch(CircuitPos put,CircuitPos against) {
    if (!usable(put) || at(put)!=AIR) return 0;
    for (int d=0;d<5;++d) if (same(offset(put,d),against) && opaque(at(against))) {
        BlockEdit edit={put.x,put.y,put.z,TORCH_POWER_BASE+TORCH_POWER_ON+d};
        return world_set_blocks(&edit,1);
    }
    return 0;
}
int redstone_place_repeater(int x,int y,int z,int facing) {
    CircuitPos p={x,y,z};
    if ((unsigned)facing>=4 || !usable(p) || at(p)!=AIR || !opaque(at(offset(p,4)))) return 0;
    BlockEdit edit={x,y,z,REPEATER_BASE+facing};
    return world_set_blocks(&edit,1);
}
int redstone_next_delay(int x,int y,int z) {
    int raw=world_cell(x,y,z);
    if (!repeater_cell(raw)) return 0;
    int delay=repeater_delay(raw)%4;
    BlockEdit edit={x,y,z,REPEATER_BASE+((raw-REPEATER_BASE)&~12)+(delay<<2)};
    return world_set_blocks(&edit,1) ? delay+1 : 0;
}
int redstone_place_comparator(int x,int y,int z,int facing) {
    CircuitPos p={x,y,z};
    if ((unsigned)facing>=4 || !usable(p) || at(p)!=AIR || !opaque(at(offset(p,4)))) return 0;
    BlockEdit edit={x,y,z,COMPARATOR_BASE+facing};
    return world_set_blocks(&edit,1);
}
int redstone_toggle_comparator(int x,int y,int z) {
    int raw=world_cell(x,y,z);
    if (!comparator_cell(raw)) return 0;
    BlockEdit edit={x,y,z,COMPARATOR_BASE+((raw-COMPARATOR_BASE)^COMPARATOR_SUBTRACT)};
    return world_set_blocks(&edit,1) ? (subtracting(edit.cell) ? 2 : 1) : 0;
}
int redstone_place_plate(int x,int y,int z,int material) {
    CircuitPos p={x,y,z};
    if (!usable(p) || !pressure_cell(material) || at(p)!=AIR || !support(offset(p,4),material)) return 0;
    BlockEdit edit={x,y,z,material};
    return world_set_blocks(&edit,1);
}
int redstone_place_button(CircuitPos put,CircuitPos against,int wooden) {
    if (!usable(put) || !usable(against) || !ready(put) || !ready(against) ||
        at(put)!=AIR || !opaque(at(against))) return 0;
    for (int d=0;d<4;++d) if (same(offset(put,d),against)) {
        BlockEdit edit={put.x,put.y,put.z,(wooden ? BUTTON_WOOD : BUTTON_BASE)+d};
        return world_set_blocks(&edit,1);
    }
    return 0;
}
int redstone_press_button(int x,int y,int z) {
    CircuitPos p={x,y,z};
    if (!usable(p)) return 0;
    int raw=at(p);
    if (!button_cell(raw)) return 0;
    CircuitPos anchor=button_anchor(p,raw);
    if (!ready(anchor) || !support(anchor,raw)) return 0;
    if (device_at(p)) return 2; /* Pressing again does not reset the pulse. */
    Device *node=add_device(world_dimension(),p);
    if (!node) return 0;
    node->time.remaining=(uint8_t)button_interval(raw);
    node->time.output=15;
    world_touch_circuit_mesh(x,z);
    notify_region(x,y,z);
    return 1;
}
static BlockBox rotate(BlockBox p,int mount) {
    if (mount==LEVER_FLOOR_EW || mount==LEVER_CEILING_EW)
        p=(BlockBox){p.z0,p.y0,p.x0,p.z1,p.y1,p.x1};
    if (mount>=LEVER_CEILING_NS && mount<LEVER_WALL)
        return (BlockBox){p.x0,1-p.y1,p.z0,p.x1,1-p.y0,p.z1};
    switch (mount) {
    case LEVER_WALL+NORTH:return (BlockBox){p.x0,p.z0,p.y0,p.x1,p.z1,p.y1};
    case LEVER_WALL+SOUTH:return (BlockBox){p.x0,p.z0,1-p.y1,p.x1,p.z1,1-p.y0};
    case LEVER_WALL+EAST:return (BlockBox){1-p.y1,p.z0,p.x0,1-p.y0,p.z1,p.x1};
    case LEVER_WALL+WEST:return (BlockBox){p.y0,p.z0,p.x0,p.y1,p.z1,p.x1};
    default: return p;
    }
}
int redstone_button_box(int raw,int pressed,BlockBox *out) {
    if (!out || !button_cell(raw)) return 0;
    *out=rotate((BlockBox){.3125f,0,.375f,.6875f,pressed ? .0625f : .125f,.625f},
        LEVER_WALL+button_mount(raw));
    return 1;
}
int redstone_lever_boxes(int raw,BlockBox out[4]) {
    if (!out || !lever_cell(raw)) return 0;
    int mount=(raw-LEVER_BASE)&7,sign=switched(raw) ? 1 : -1;
    out[0]=rotate((BlockBox){.29f,0,.26f,.71f,.13f,.74f},mount);
    for (int i=0;i<3;++i) {
        float z=.5f+.09f*sign*(i+1);
        out[i+1]=rotate((BlockBox){.435f,.11f+i*.103f,z-.085f,.565f,.245f+i*.103f,z+.085f},mount);
    }
    return 4;
}
int redstone_torch_boxes(int raw,BlockBox out[4]) {
    if (!out || !torch_power_cell(raw)) return 0;
    int mount=torch_mount(raw);
    if (mount==4) {
        out[0]=(BlockBox){.43f,0,.43f,.57f,.56f,.57f};
        out[1]=(BlockBox){.40f,.51f,.40f,.60f,.68f,.60f};
        return 2;
    }
    float floor=.24f;
    for (int i=0;i<4;++i) {
        float z=.29f+i*.119f,dist=i*.083f,rad=i==3 ? .089f : .059f;
        out[i]=rotate((BlockBox){.5f-rad,dist,z-rad,.5f+rad,dist+.13f,z+rad},LEVER_WALL+mount);
        out[i].y0+=floor*.5f; out[i].y1+=floor*.5f;
    }
    return 4;
}
