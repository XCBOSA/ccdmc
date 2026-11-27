#include "pistons.h"
#include "forestry.h"
#include "actors.h"
#include "boats.h"
#include "drops.h"
#include "horses.h"
#include "minecarts.h"
#include "../world/rails.h"
#include "../world/space.h"
#include <math.h>

CircuitPos piston_offset(CircuitPos p,int dir,int distance) {
    if ((unsigned)dir>=6) return p;
    if (dir<4) { p.x+=rail_dx(dir)*distance; p.z+=rail_dz(dir)*distance; }
    else p.y+=dir==EJECTOR_UP ? distance : -distance;
    return p;
}
int piston_peer(CircuitPos p,int raw,CircuitPos *other,int *expected) {
    int dir=piston_facing(raw);
    if (piston_head_cell(raw)) {
        if (other) *other=piston_offset(p,dir,-1);
        if (expected) *expected=raw-PISTON_EXTENDED;
        return 1;
    }
    if (!piston_extended(raw)) return 0;
    if (other) *other=piston_offset(p,dir,1);
    if (expected) *expected=raw+PISTON_EXTENDED;
    return 1;
}
static BlockBox part_along(int x,int y,int z,int dir,float lo,float hi,float outer0,float outer1) {
    if (dir==WEST || dir==NORTH || dir==EJECTOR_DOWN) {
        float next=1.f-hi; hi=1.f-lo; lo=next;
    }
    BlockBox b={outer0,outer0,outer0,outer1,outer1,outer1};
    if (dir==WEST || dir==EAST) { b.x0=lo; b.x1=hi; }
    else if (dir==NORTH || dir==SOUTH) { b.z0=lo; b.z1=hi; }
    else { b.y0=lo; b.y1=hi; }
    return (BlockBox){x+b.x0,y+b.y0,z+b.z0,x+b.x1,y+b.y1,z+b.z1};
}
int piston_boxes(int x,int y,int z,int raw,BlockBox boxes[PISTON_PARTS]) {
    if (!piston_cell(raw)) return 0;
    int dir=piston_facing(raw),head=piston_head_cell(raw),extended=piston_extended(raw);
    boxes[0]=part_along(x,y,z,dir,head ? .75f : 0.f,!head && extended ? .75f : 1.f,0.f,1.f);
    if (!head && !extended) return 1;
    boxes[1]=part_along(x,y,z,dir,head ? 0.f : .75f,head ? .75f : 1.f,.375f,.625f);
    return 2;
}
static int usable(CircuitPos p) {
    return p.x>=-WORLD_SAVE_LIMIT && p.x<=WORLD_SAVE_LIMIT && p.z>=-WORLD_SAVE_LIMIT &&
        p.z<=WORLD_SAVE_LIMIT && inworld(p.x,p.y,p.z);
}
static int at(CircuitPos p) { return world_cell(p.x,p.y,p.z); }
static BlockEdit edit(CircuitPos p,int cell) { return (BlockEdit){p.x,p.y,p.z,cell}; }
int piston_place(int x,int y,int z,int direction,int sticky) {
    CircuitPos p={x,y,z};
    if ((unsigned)direction>=6 || !usable(p) || !world_chunk_ready(x,z)) return 0;
    int raw=at(p);
    if (raw!=AIR && !water_strength(raw)) return 0;
    BlockEdit e=edit(p,PISTON_BASE+(sticky ? PISTON_STICKY : 0)+direction);
    return world_set_blocks(&e,1);
}
static int free_drop(void) {
    for (int i=0;i<MAXDROP;++i) if (!drops[i].item.count) return i;
    return -1;
}
const char *piston_break(Hit h) {
    CircuitPos p={h.x,h.y,h.z},other;
    int raw=at(p),expected;
    if (!piston_cell(raw)) return NULL;
    BlockEdit out[2]={edit(p,AIR)};
    int n=1;
    if (piston_peer(p,raw,&other,&expected) && usable(other)) {
        if (!world_chunk_ready(other.x,other.z)) return "等待相邻区块载入";
        if (at(other)==expected) out[n++]=edit(other,AIR);
    }
    int slot=-1;
    if (player_rules.mode==SURVIVAL && (piston_base_cell(raw) || n==2)) {
        slot=free_drop();
        if (slot<0) return "掉落区已满，未拆除活塞";
    }
    if (!world_set_blocks(out,n)) return "暂时无法拆除活塞";
    if (slot>=0) {
        CircuitPos base=piston_base_cell(raw) ? p : other;
        int item=piston_sticky(raw) ? STICKY_PISTON_ITEM : PISTON_ITEM;
        drops[slot]=make_drop(base.x+.5f,base.y+.4f,base.z+.5f,(ItemStack){(uint16_t)item,1,0,0,0});
        drops_dirty=1;
    }
    return "已拆除活塞";
}

enum { BLOCKED, MOVE, DESTROY, EMPTY };
static int reaction(int raw) {
    int t=cell_type(raw);
    switch (t) {
        case AIR: case WATER: case LAVA: case FIRE_CELL: case SNOW_LAYER: return EMPTY;
        case BEDROCK: case OBSIDIAN: case NETHER_PORTAL_X: case NETHER_PORTAL_Z:
        case AQUIFER_DOCK: case AQUIFER_BRINE: case AQUIFER_VENT:
        case STRATA_SAMPLE_SHALE: case STRATA_SAMPLE_LIME:
        case PRESSURE_HATCH: case PRESSURE_LIFT: case PRESSURE_CLOSED: case PRESSURE_OPEN: case PRESSURE_KILN:
        case CHEST: case SADDLE_CHEST: case SMITH_CHEST: case TRAPPED_CHEST: case TEMPLE_CHEST:
        case DUNGEON_CHEST: case DESERT_PYRAMID_CHEST: case ENDER_CHEST: case MONSTER_SPAWNER:
        case FURNACE: case BREWING_STAND: case ENCHANTING_TABLE:
        case BED: case HOPPER: case DROPPER: case DISPENSER: case PISTON_HEAD: case AIR_STATION: return BLOCKED;
        case TALL_GRASS: case SAPLING: case POPPY: case DANDELION: case COCOA: case CACTUS:
        case PUMPKIN_STEM: case SUGAR_CANE: case FILTER_MAT:
        case CROP_WHEAT: case CROP_POTATO: case CROP_CARROT:
        case TORCH: case REDSTONE_WIRE: case REDSTONE_TORCH: case LEVER: case COMPARATOR: case REPEATER:
        case PRESSURE_PLATE: case BUTTON: case TRIPWIRE: case TRIPWIRE_HOOK:
        case RAIL: case POWERED_RAIL: case DETECTOR_RAIL: case ACTIVATOR_RAIL:
        case WOOD_DOOR: case IRON_DOOR: case WOOD_TRAPDOOR: case LADDER: return DESTROY;
        case PISTON: case STICKY_PISTON: return piston_extended(raw) ? BLOCKED : MOVE;
        default: return MOVE;
    }
}
static int fragile_item(int raw) {
    int type=cell_type(raw);
    if (button_cell(raw)) return raw>=BUTTON_WOOD ? WOOD_BUTTON_ITEM : STONE_BUTTON_ITEM;
    if (pressure_cell(raw)) return redstone_plate_item(raw);
    if (crop_type(type)) return type==CROP_WHEAT ? (raw-WHEAT_BASE==7 ? WHEAT : WHEAT_SEEDS) :
        type==CROP_CARROT ? CARROT : POTATO;
    if (filter_mat_cell(raw)) return FILTER_MOSS;
    if (sapling_cell(raw)) return tree_sapling_drop(raw);
    return mining_drop(type,4);
}
int piston_nudge(BlockBox sweep,int dir,BlockBox body,Vec3 *shift) {
    if (!shift || (unsigned)dir>=6 || !box_overlap(sweep,body)) return 0;
    float x=dir==EAST ? sweep.x1-body.x0+.01f : dir==WEST ? sweep.x0-body.x1-.01f : 0.f;
    float y=dir==EJECTOR_UP ? sweep.y1-body.y0+.01f : dir==EJECTOR_DOWN ? sweep.y0-body.y1-.01f : 0.f;
    float z=dir==SOUTH ? sweep.z1-body.z0+.01f : dir==NORTH ? sweep.z0-body.z1-.01f : 0.f;
    static const float offsets[]={.38f,-.38f,.78f,-.78f};
    for (int i=0;i<9;++i) {
        float ox=x,oy=y,oz=z;
        if (i) {
            float side=offsets[(i-1)%4];
            if (i<=4) { if (x) oz+=side; else ox+=side; }
            else { if (y) oz+=side; else oy+=side; }
        }
        BlockBox next={body.x0+ox,body.y0+oy,body.z0+oz,body.x1+ox,body.y1+oy,body.z1+oz};
        if (world_box_blocked(next,0)) continue;
        *shift=(Vec3){ox,oy,oz};
        return 1;
    }
    return -1;
}
static void push_entities(CircuitPos a,CircuitPos b,int dir) {
    BlockBox sweep={(float)(a.x<b.x ? a.x : b.x),(float)(a.y<b.y ? a.y : b.y),(float)(a.z<b.z ? a.z : b.z),
        (a.x>b.x ? a.x : b.x)+1.f,(a.y>b.y ? a.y : b.y)+1.f,(a.z>b.z ? a.z : b.z)+1.f};
    if (player_rules.health && boat_rider()<0 && cart_rider()<0 && horse_rider()<0) {
        Vec3 shift={0};
        int result=piston_nudge(sweep,dir,body_box(px,py,pz,.28f,1.72f),&shift);
        if (result>0) { px+=shift.x; py+=shift.y; pz+=shift.z; if (shift.y) vy=0; grounded=0; }
        else if (result<0) player_take_damage(DAMAGE_SUFFOCATION,1);
    }
    actors_piston_push(sweep,dir);
    for (int i=0;i<MAXDROP;++i) if (drops[i].item.count) {
        Drop *d=&drops[i];
        Vec3 shift;
        if (piston_nudge(sweep,dir,body_box(d->x,d->y,d->z,.14f,.28f),&shift)>0) {
            d->x+=shift.x; d->y+=shift.y; d->z+=shift.z; drops_dirty=1;
        }
    }
}
int piston_refresh(CircuitPos base,int raw,int powered) {
    if (!piston_base_cell(raw) || !!powered==piston_extended(raw)) return 1;
    int dir=piston_facing(raw),sticky=piston_sticky(raw);
    CircuitPos front=piston_offset(base,dir,1);
    if (!usable(front)) return 1;
    if (!world_chunk_ready(front.x,front.z)) return 0;
    int head=PISTON_HEAD_BASE+(sticky ? PISTON_STICKY : 0)+dir;
    if (!powered) {
        BlockEdit out[3]={edit(base,raw-PISTON_EXTENDED)};
        int n=1,pulled=0;
        if (at(front)==head) {
            out[n++]=edit(front,AIR);
            CircuitPos next=piston_offset(base,dir,2);
            if (sticky && usable(next)) {
                if (!world_chunk_ready(next.x,next.z)) return 0;
                int load=at(next);
                if (reaction(load)==MOVE) {
                    out[1].cell=load;
                    out[n++]=edit(next,AIR); pulled=1;
                }
            }
        }
        if (!world_set_blocks(out,n)) return 0;
        if (pulled) push_entities(front,front,dir<4 ? (dir+2)&3 : 9-dir);
        return 1;
    }
    BlockEdit out[PISTON_PUSH_LIMIT+2]={edit(base,raw+PISTON_EXTENDED),edit(front,head)};
    int n=2,load=head,slot=-1,item=AIR;
    CircuitPos end=front;
    for (int dist=1;dist<=PISTON_PUSH_LIMIT+1;++dist) {
        CircuitPos p=piston_offset(base,dir,dist);
        if (!usable(p)) return 1;
        if (!world_chunk_ready(p.x,p.z)) return 0;
        int hit=at(p),how=reaction(hit);
        if (how==BLOCKED || (how==MOVE && dist>PISTON_PUSH_LIMIT)) return 1;
        if (dist>1) out[n++]=edit(p,load);
        end=p;
        if (how!=MOVE) {
            if (how==DESTROY && (item=fragile_item(hit))!=AIR) {
                slot=free_drop();
                if (slot<0) return 0; /* Don't lose the existing item under load. */
            }
            break;
        }
        load=hit;
    }
    if (!world_set_blocks(out,n)) return 0;
    if (slot>=0) {
        drops[slot]=make_drop(end.x+.5f,end.y+.55f,end.z+.5f,(ItemStack){(uint16_t)item,1,0,0,0});
        drops_dirty=1;
    }
    /* Entity damage can generate other drops, so publish the reserved item first. */
    push_entities(front,end,dir);
    return 1;
}
