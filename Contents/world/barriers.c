#include "barriers.h"
#include "internal.h"
#include "redstone.h"
#include "../game/game.h"

static int encode(GateState g) {
    return (g.powered ? POWERED_GATE_BASE : GATE_BASE)+(g.open ? 4 : 0)+g.facing;
}
int gate_decode(int raw,GateState *out) {
    if (!gate_cell(raw)) return 0;
    int powered=raw<POWERED_GATE_END;
    int variant=raw-(powered ? POWERED_GATE_BASE : GATE_BASE);
    if (out) *out=(GateState){variant&3,(variant&4)!=0,powered};
    return 1;
}

static void refresh_signal(int x,int y,int z,int raw) {
    GateState g;
    if (!gate_decode(raw,&g)) return;
    int signal=redstone_receives(x,y,z);
    if (signal==g.powered) return;
    g.powered=g.open=signal;
    /* Neither the open leaves nor the powered bit affects neighbour power. */
    world_write_cell(x,y,z,encode(g));
}
void gate_refresh_redstone(int x,int y,int z) { refresh_signal(x,y,z,world_cell(x,y,z)); }

void barrier_after_edit(int x,int y,int z,int before,int after) {
    if (gate_cell(after) && !gate_cell(before)) refresh_signal(x,y,z,after);
    if ((before==REDSTONE_BLOCK)==(after==REDSTONE_BLOCK)) return;
    for (int d=0;d<4;++d) {
        int nx=x+rail_dx(d),nz=z+rail_dz(d);
        refresh_signal(nx,y,nz,world_cell(nx,y,nz));
    }
    refresh_signal(x,y-1,z,world_cell(x,y-1,z));
    refresh_signal(x,y+1,z,world_cell(x,y+1,z));
}
int gate_place(int x,int y,int z,int facing) {
    if ((unsigned)facing>=4 || !inworld(x,y,z) || block(x,y,z)!=AIR || !solid(block(x,y-1,z))) return 0;
    GateState gate={facing,0,0};
    BlockEdit edit={x,y,z,encode(gate)};
    return world_set_blocks(&edit,1);
}
int gate_toggle(int x,int y,int z,int approach) {
    GateState g;
    if (!gate_decode(world_cell(x,y,z),&g)) return 0;
    if (!g.open && ((g.facing+2)&3)==approach) g.facing=approach;
    g.open=!g.open;
    return world_write_cell(x,y,z,encode(g));
}

int door_support(int raw) {
    int type=cell_type(raw);
    return solid(type) && type!=FARMLAND && type!=ENCHANTING_TABLE && !chest_shape_type(type) &&
        type!=ANVIL && type!=HOPPER && type!=PISTON_HEAD && !piston_extended(raw);
}
int door_item(int raw) { return door_bits(raw)&DOOR_IRON ? IRON_DOOR_ITEM : WOOD_DOOR_ITEM; }

static int set_door(int x,int y,int z,int lower) {
    BlockEdit pair[2]={{x,y,z,lower},{x,y+1,z,door_peer(lower)}};
    return world_set_blocks(pair,2);
}
static int get_door(int x,int *y,int z) {
    int raw=world_cell(x,*y,z);
    if (!door_cell(raw)) return AIR;
    int mate=door_peer(raw);
    if (world_cell(x,*y+(door_upper(raw) ? -1 : 1),z)!=mate) return AIR;
    if (door_upper(raw)) { --*y; raw=mate; }
    return raw;
}
int door_closed_wood(int x,int *y,int z) {
    int raw=get_door(x,y,z);
    return raw && !(door_bits(raw)&(DOOR_IRON|DOOR_OPEN)) ? raw : AIR;
}
int door_break_closed_wood(int x,int y,int z,int expected) {
    if (!expected || door_closed_wood(x,&y,z)!=expected) return 0;
    const BlockEdit edits[]={{x,y,z,AIR},{x,y+1,z,AIR}};
    return world_set_blocks(edits,2);
}
static int matching_door(int x,int y,int z,int material_facing) {
    int same_level=y,raw=get_door(x,&same_level,z);
    return same_level==y && raw && (door_bits(raw)&(DOOR_IRON|3))==material_facing ? raw : AIR;
}
int door_place(int x,int y,int z,int facing,int right,int iron) {
    if ((unsigned)facing>=4 || y<1 || y>=WY-1 || world_cell(x,y,z)!=AIR || world_cell(x,y+1,z)!=AIR ||
        !door_support(world_cell(x,y-1,z))) return 0;
    int edge=(facing+1)&3,dx=rail_dx(edge),dz=rail_dz(edge),base=facing+(iron?DOOR_IRON:0);
    int left=matching_door(x-dx,y,z-dz,base),other=matching_door(x+dx,y,z+dz,base);
    /* A new pair hinges outwards in either placement order. Don't rehinge
       an existing pair when adding a third door, or alter another material. */
    if (!!left!=!!other) {
        int sign=left ? -1 : 1;
        if (!matching_door(x+sign*dx*2,y,z+sign*dz*2,base)) {
            right=sign<0;
            int mine=DOOR_BASE+base+(right?DOOR_HINGE:0);
            int peer=DOOR_BASE+((door_bits(left ? left : other)&~DOOR_HINGE)+(right?0:DOOR_HINGE));
            int nx=x+sign*dx,nz=z+sign*dz;
            const BlockEdit doors[]={{x,y,z,mine},{x,y+1,z,door_peer(mine)},
                {nx,y,nz,peer},{nx,y+1,nz,door_peer(peer)}};
            return world_set_blocks(doors,4);
        }
    }
    return set_door(x,y,z,DOOR_BASE+base+(right?DOOR_HINGE:0));
}
int door_toggle(int x,int y,int z) {
    int raw=get_door(x,&y,z);
    return raw && !(door_bits(raw)&DOOR_IRON) && set_door(x,y,z,DOOR_BASE+(door_bits(raw)^DOOR_OPEN));
}
int door_refresh_redstone(int x,int y,int z) {
    int raw=get_door(x,&y,z);
    if (!raw) return 1;
    int bits=door_bits(raw),signal=redstone_receives(x,y,z) || redstone_receives(x,y+1,z);
    if (signal==!!(bits&DOOR_POWERED)) return 1;
    bits=(bits&~(DOOR_POWERED|DOOR_OPEN))+(signal ? DOOR_POWERED+DOOR_OPEN : 0);
    return set_door(x,y,z,DOOR_BASE+bits);
}

int wall_attachment_support(int raw) {
    const int type=cell_type(raw);
    return door_support(raw) && type!=GLASS && type!=LEAF && type!=ICE &&
        type!=PACKED_ICE && type!=CACTUS;
}
int wall_attachment_place(int x,int y,int z,int side,int ladder,int top) {
    if ((unsigned)side>=4 || !inworld(x,y,z) || world_cell(x,y,z)!=AIR ||
        !world_chunk_ready(x+rail_dx(side),z+rail_dz(side)) ||
        !wall_attachment_support(world_cell(x+rail_dx(side),y,z+rail_dz(side)))) return 0;
    const BlockEdit edit={x,y,z,side+(ladder ? LADDER : WOOD_TRAPDOOR+(top ? TRAPDOOR_TOP : 0))};
    return world_set_blocks(&edit,1);
}
int trapdoor_toggle(int x,int y,int z) {
    int raw=world_cell(x,y,z);
    if (!trapdoor_cell(raw)) return 0;
    int bits=(raw-WOOD_TRAPDOOR)^TRAPDOOR_OPEN;
    return world_write_cell(x,y,z,WOOD_TRAPDOOR+bits) ? (bits&TRAPDOOR_OPEN ? 2 : 1) : 0;
}
int trapdoor_refresh_redstone(int x,int y,int z) {
    int raw=world_cell(x,y,z);
    if (!trapdoor_cell(raw)) return 1;
    int bits=raw-WOOD_TRAPDOOR,signal=redstone_receives(x,y,z);
    if (signal==!!(bits&TRAPDOOR_POWERED)) return 1;
    bits=(bits&~(TRAPDOOR_POWERED|TRAPDOOR_OPEN))|(signal ? TRAPDOOR_POWERED|TRAPDOOR_OPEN : 0);
    return world_write_cell(x,y,z,WOOD_TRAPDOOR+bits);
}

unsigned fence_connections(int raw,const int adjacent[4]) {
    if (!fence_type(raw) || !adjacent) return 0;
    unsigned mask=0;
    for (int dir=0;dir<4;++dir) {
        int type=cell_type(adjacent[dir]);
        GateState gate;
        if (gate_decode(adjacent[dir],&gate)) {
            /* A gate joins a fence on the sides of its frame, not the passage ends. */
            if ((dir&1)!=(gate.facing&1)) mask|=1u<<dir;
        } else if (type==raw || (solid(type) && type!=FARMLAND && !chest_shape_type(type) &&
            type!=LEAF && type!=ICE && type!=PACKED_ICE && type!=GLASS)) mask|=1u<<dir;
    }
    return mask;
}
static BlockBox rail_box(int dir,float lo,float hi,float width) {
    switch (dir) {
        case NORTH: return (BlockBox){.5f-width,lo,0,.5f+width,hi,.5f};
        case SOUTH: return (BlockBox){.5f-width,lo,.5f,.5f+width,hi,1};
        case WEST: return (BlockBox){0,lo,.5f-width,.5f,hi,.5f+width};
        default: return (BlockBox){.5f,lo,.5f-width,1,hi,.5f+width};
    }
}
unsigned pane_connections(int raw,const int adjacent[4]) {
    if (!pane_cell(raw) || !adjacent) return 0;
    unsigned mask=0;
    for (int dir=0;dir<4;++dir) {
        const int next=adjacent[dir],type=cell_type(next);
        if (pane_cell(next) || type==GLASS || (door_support(next) && type!=LEAF &&
            type!=ICE && type!=PACKED_ICE && type!=CACTUS && type!=ASH_SAND))
            mask|=1u<<dir;
    }
    return mask;
}
BlockBox pane_part(int dir) {
    const float lo=.5f-PANE_HALF_WIDTH,hi=.5f+PANE_HALF_WIDTH;
    switch (dir) {
    case NORTH: return (BlockBox){lo,0,0,hi,1,lo};
    case EAST: return (BlockBox){hi,0,lo,1,1,hi};
    case SOUTH: return (BlockBox){lo,0,hi,hi,1,1};
    case WEST: return (BlockBox){0,0,lo,lo,1,hi};
    default: return (BlockBox){lo,0,lo,hi,1,hi};
    }
}
/* Author in a single local frame (passage along +Z). Reflect the leaves
   for opening the other way, then swap horizontal axes if needed. */
static BlockBox gate_axis(BlockBox box,int facing) {
    if (facing==NORTH || facing==WEST) {
        float lo=1.f-box.z1;
        box.z1=1.f-box.z0; box.z0=lo;
    }
    if (facing==EAST || facing==WEST)
        return (BlockBox){box.z0,box.y0,box.x0,box.z1,box.y1,box.x1};
    return box;
}
static BlockBox door_axis(BlockBox b,int bits) {
    int dir=bits&3;
    if (bits&DOOR_OPEN) dir=(dir+((bits&DOOR_HINGE)?1:3))&3;
    switch (dir) {
    case EAST: return (BlockBox){1-b.z1,b.y0,b.x0,1-b.z0,b.y1,b.x1};
    case SOUTH: return (BlockBox){1-b.x1,b.y0,1-b.z1,1-b.x0,b.y1,1-b.z0};
    case WEST: return (BlockBox){b.z0,b.y0,1-b.x1,b.z1,b.y1,1-b.x0};
    default: return b;
    }
}
static int door_boxes(int raw,int purpose,BlockBox *out) {
    const float edge=.1875f;
    int bits=door_bits(raw),n=0;
    if (purpose!=BARRIER_VISUAL) out[n++]=(BlockBox){0,0,0,1,1,edge};
    else if (door_upper(raw)) {
        out[n++]=(BlockBox){0,0,0,1,.32f,edge};
        out[n++]=(BlockBox){0,.85f,0,1,1,edge};
        out[n++]=(BlockBox){0,.32f,0,.15f,.85f,edge};
        out[n++]=(BlockBox){.85f,.32f,0,1,.85f,edge};
        out[n++]=(BlockBox){.455f,.32f,0,.545f,.85f,edge};
        out[n++]=(BlockBox){.15f,.555f,0,.85f,.635f,edge};
    } else {
        out[n++]=(BlockBox){0,0,0,1,1,edge};
        for (int i=1;i<4;++i) {
            float x=.25f*i;
            out[n++]=(BlockBox){x-.018f,.03f,edge,x+.018f,.95f,.203f};
        }
        float latch=bits&DOOR_HINGE ? .16f : .84f;
        out[n++]=(BlockBox){latch-.055f,.82f,edge,latch+.055f,.92f,.252f};
    }
    for (int i=0;i<n;++i) out[i]=door_axis(out[i],bits);
    return n;
}
static int attachment_boxes(int raw,int purpose,BlockBox *out) {
    const int side=wall_attachment_dir(raw);
    if (ladder_cell(raw)) {
        if (purpose!=BARRIER_VISUAL) { out[0]=door_axis((BlockBox){0,0,0,1,1,.125f},side); return 1; }
        out[0]=(BlockBox){.10f,0,0,.22f,1,.103f};
        out[1]=(BlockBox){.78f,0,0,.90f,1,.103f};
        for (int i=0;i<4;++i) {
            float y=.095f+i*.2375f;
            out[2+i]=(BlockBox){.16f,y,.024f,.84f,y+.098f,.127f};
        }
        for (int i=0;i<6;++i) out[i]=door_axis(out[i],side);
        return 6;
    }
    const float thick=.1875f;
    int bits=raw-WOOD_TRAPDOOR,n=1;
    out[0]=(BlockBox){0,0,0,1,thick,1};
    if (purpose==BARRIER_VISUAL) {
        out[0]=(BlockBox){0,0,0,1,thick,.20f};
        out[1]=(BlockBox){0,0,.80f,1,thick,1};
        out[2]=(BlockBox){0,0,.20f,.19f,thick,.80f};
        out[3]=(BlockBox){.81f,0,.20f,1,thick,.80f};
        out[4]=(BlockBox){.43f,0,.20f,.57f,thick,.80f};
        out[5]=(BlockBox){.19f,0,.435f,.81f,thick,.565f};
        n=6;
    }
    for (int i=0;i<n;++i) {
        BlockBox b=out[i];
        if (bits&TRAPDOOR_OPEN) {
            const float a=bits&TRAPDOOR_TOP ? 1.f-b.z1 : b.z0;
            b=(BlockBox){b.x0,a,b.y0,b.x1,a+(b.z1-b.z0),b.y1};
        } else if (bits&TRAPDOOR_TOP) {
            b.y0+=1.f-thick; b.y1+=1.f-thick;
        }
        out[i]=door_axis(b,side);
    }
    return n;
}
int barrier_boxes(int raw,unsigned links,int purpose,BlockBox out[BARRIER_PARTS]) {
    if (!out) return 0;
    if (pane_cell(raw)) {
        int n=0;
        out[n++]=pane_part(PANE_CENTER);
        for (int dir=0;dir<4;++dir) if (pane_parts(links)&(1u<<dir)) out[n++]=pane_part(dir);
        return n;
    }
    if (door_cell(raw)) return door_boxes(raw,purpose,out);
    if (wall_attachment_dir(raw)>=0) return attachment_boxes(raw,purpose,out);
    int collision=purpose==BARRIER_BODY;
    GateState gate;
    if (gate_decode(raw,&gate)) {
        if (collision) {
            if (gate.open) return 0;
            out[0]=gate_axis((BlockBox){0,0,.375f,1,BARRIER_HEIGHT,.625f},gate.facing);
            return 1;
        }
        if (purpose==BARRIER_PICK && !gate.open) {
            /* Slightly forgiving for touch: the visible frame is selectable
               through gaps between its rails, without blocking arrows there. */
            out[0]=gate_axis((BlockBox){0,.31f,.375f,1,.975f,.625f},gate.facing);
            return 1;
        }
        out[0]=gate_axis((BlockBox){0,.31f,.375f,.125f,.975f,.625f},gate.facing);
        out[1]=gate_axis((BlockBox){.875f,.31f,.375f,1,.975f,.625f},gate.facing);
        int n=2;
        for (int rail=0;rail<2;++rail) {
            float y=rail ? .744f : .387f;
            if (gate.open) {
                out[n++]=gate_axis((BlockBox){.061f,y,.5f,.188f,y+.132f,.93f},gate.facing);
                out[n++]=gate_axis((BlockBox){.812f,y,.5f,.939f,y+.132f,.93f},gate.facing);
            } else out[n++]=gate_axis((BlockBox){.106f,y,.435f,.894f,y+.132f,.565f},gate.facing);
        }
        if (gate.open) {
            out[n++]=gate_axis((BlockBox){.061f,.393f,.844f,.188f,.878f,.934f},gate.facing);
            out[n++]=gate_axis((BlockBox){.812f,.393f,.844f,.939f,.878f,.934f},gate.facing);
        } else out[n++]=gate_axis((BlockBox){.439f,.387f,.435f,.561f,.88f,.565f},gate.facing);
        return n;
    }
    if (!fence_type(raw)) return 0;
    int n=1;
    out[0]=(BlockBox){.375f,0,.375f,.625f,collision ? BARRIER_HEIGHT : 1.f,.625f};
    for (int d=0;d<4;++d) if (links&(1u<<d)) {
        if (collision) out[n++]=rail_box(d,0,BARRIER_HEIGHT,.125f);
        else {
            out[n++]=rail_box(d,.38f,.55f,.085f);
            out[n++]=rail_box(d,.73f,.90f,.085f);
        }
    }
    return n;
}
int barrier_world_boxes(int x,int y,int z,int raw,int purpose,BlockBox out[BARRIER_PARTS]) {
    unsigned links=0;
    if (pane_cell(raw)) {
        int adjacent[4]={0};
        for (int d=0;d<4;++d) world_resident_cell(x+rail_dx(d),y,z+rail_dz(d),&adjacent[d]);
        links=pane_connections(raw,adjacent);
    } else if (fence_type(raw)) {
        int adjacent[4];
        for (int d=0;d<4;++d) adjacent[d]=world_cell(x+rail_dx(d),y,z+rail_dz(d));
        links=fence_connections(raw,adjacent);
    }
    int n=barrier_boxes(raw,links,purpose,out);
    for (int i=0;i<n;++i) {
        out[i].x0+=x; out[i].x1+=x; out[i].y0+=y; out[i].y1+=y; out[i].z0+=z; out[i].z1+=z;
    }
    return n;
}
int box_overlap(BlockBox a,BlockBox b) {
    return a.x0<b.x1 && a.x1>b.x0 && a.y0<b.y1 && a.y1>b.y0 && a.z0<b.z1 && a.z1>b.z0;
}
int box_contains(BlockBox b,float x,float y,float z) {
    return x>=b.x0 && x<=b.x1 && y>=b.y0 && y<=b.y1 && z>=b.z0 && z<=b.z1;
}
