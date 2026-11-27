#include "rails.h"
#include "internal.h"
#include "redstone.h"
#include "../game/minecarts.h"

static const RailEnd shapes[][2]={
    {{NORTH,0},{SOUTH,0}}, {{WEST,0},{EAST,0}},
    {{WEST,0},{EAST,1}}, {{WEST,1},{EAST,0}},
    {{NORTH,1},{SOUTH,0}}, {{NORTH,0},{SOUTH,1}},
    {{NORTH,0},{EAST,0}}, {{EAST,0},{SOUTH,0}},
    {{SOUTH,0},{WEST,0}}, {{WEST,0},{NORTH,0}}
};

int rail_decode(int cell,RailPiece *piece) {
    int type=cell_type(cell);
    if (!piece || !rail_type(type)) return 0;
    RailKind kind=type==ACTIVATOR_RAIL ? RAIL_ACTIVATOR : type==DETECTOR_RAIL ? RAIL_DETECTOR :
        type==POWERED_RAIL ? RAIL_BOOSTER : RAIL_NORMAL;
    int shape=mine_cart_track(cell) ? cell-MINE_CART_TRACK_BASE :
        kind==RAIL_ACTIVATOR ? cell-ACTIVATOR_TRACK_BASE : kind==RAIL_DETECTOR ? cell-DETECTOR_TRACK_BASE :
        cell>=TRACK_BASE ? cell-(kind==RAIL_BOOSTER ? TRACK_POWERED : TRACK_BASE) : STRAIGHT_NS;
    if (shape<0 || shape>=(kind==RAIL_NORMAL ? 10 : 6)) return 0;
    *piece=(RailPiece){{shapes[shape][0],shapes[shape][1]},kind,shape};
    return 1;
}
int rail_at(RailCell p,RailPiece *piece) { return rail_decode(world_cell(p.x,p.y,p.z),piece); }

RailSample rail_sample(RailCell p,RailPiece rail,float t) {
    int a=rail.ends[0].dir,b=rail.ends[1].dir;
    float ax=rail_dx(a)*.5f,az=rail_dz(a)*.5f,bx=rail_dx(b)*.5f,bz=rail_dz(b)*.5f;
    float u=1.f-t;
    float dy=(float)(rail.ends[1].up-rail.ends[0].up);
    float y=p.y+.045f+rail.ends[0].up+dy*t;
    if ((a+2)%4==b) return (RailSample){p.x+.5f+ax*u+bx*t,y,p.z+.5f+az*u+bz*t,bx-ax,dy,bz-az};
    return (RailSample){p.x+.5f+ax*u*u+bx*t*t,y,p.z+.5f+az*u*u+bz*t*t,
        2.f*(bx*t-ax*u),dy,2.f*(bz*t-az*u)};
}

int rail_link(RailCell p,int end,RailCell *next,int *entry) {
    RailPiece rail;
    if (!next || !entry || (unsigned)end>=2 || !rail_at(p,&rail)) return 0;
    RailEnd edge=rail.ends[end];
    int x=p.x+rail_dx(edge.dir), z=p.z+rail_dz(edge.dir), y=p.y+edge.up;
    for (int offset=0;offset>=-1;--offset) {
        RailCell neighbor={x,y+offset,z};
        RailPiece piece;
        if (!rail_at(neighbor,&piece)) continue;
        for (int i=0;i<2;++i) if (piece.ends[i].dir==(edge.dir+2)%4 &&
             piece.ends[i].up==-offset) { *next=neighbor; *entry=i; return 1; }
    }
    return 0;
}

static int adjacent_source(RailCell p) {
    return redstone_receives(p.x,p.y,p.z);
}
int rail_is_energized(RailCell p) {
    RailPiece rail;
    if (!rail_at(p,&rail) || (rail.kind!=RAIL_BOOSTER && rail.kind!=RAIL_ACTIVATOR)) return 0;
    RailKind group=rail.kind;
    if (adjacent_source(p)) return 1;
    for (int end=0;end<2;++end) {
        RailCell cur=p;
        int exit=end;
        for (int distance=0;distance<8;++distance) {
            RailCell q;
            int entry;
            if (!rail_link(cur,exit,&q,&entry) || !rail_at(q,&rail) || rail.kind!=group) break;
            if (adjacent_source(q)) return 1;
            cur=q; exit=1-entry;
        }
    }
    return 0;
}

/* Driving requires reciprocal edges and equal boundary heights;
   corners that only touch diagonally never join. */
static int elevation(RailCell p,int dir) {
    int x=p.x+rail_dx(dir), z=p.z+rail_dz(dir);
    if (rail_type(block(x,p.y,z))) return 0;
    if (full_top_surface(world_cell(x,p.y,z)) && rail_type(block(x,p.y+1,z))) return 1;
    if (rail_type(block(x,p.y-1,z)) && !solid(block(x,p.y,z))) return -1;
    return 2;
}
static int rail_base(RailKind kind) {
    return kind==RAIL_ACTIVATOR ? ACTIVATOR_TRACK_BASE : kind==RAIL_DETECTOR ? DETECTOR_TRACK_BASE :
        kind==RAIL_BOOSTER ? TRACK_POWERED : TRACK_BASE;
}
static int junction_shape(const int h[4],int powered) {
    int exits=0,count=0;
    for (int d=0;d<4;++d) {
        if (h[d]==1) return -1; /* An ascending approach has different endpoint heights. */
        if (h[d]!=2) { exits|=1<<d; ++count; }
    }
    if (count!=3) return -1;
    /* Three approaches leave two viable corners. A signal selects the
       opposite priority without changing the shared approach. */
    const int preferred[]={BEND_ES,BEND_SW,BEND_NE,BEND_WN};
    for (int i=0;i<4;++i) {
        int bend=preferred[powered ? 3-i : i];
        int needed=(1<<shapes[bend][0].dir)|(1<<shapes[bend][1].dir);
        if ((exits&needed)==needed) return bend;
    }
    return -1;
}
int rail_refresh_junction(RailCell p) {
    RailPiece rail;
    int raw=world_cell(p.x,p.y,p.z);
    if (!rail_decode(raw,&rail) || rail.kind!=RAIL_NORMAL) return 1;
    int h[4];
    for (int d=0;d<4;++d) h[d]=elevation(p,d);
    int shape=junction_shape(h,0);
    if (shape<0) return 1;
    if (adjacent_source(p)) shape=junction_shape(h,1);
    if (shape==rail.shape) return 1;
    if (cart_on_track(p)) return 0;
    return world_write_cell(p.x,p.y,p.z,(mine_cart_track(raw) ? MINE_CART_TRACK_BASE : TRACK_BASE)+shape);
}
static void reflow(RailCell p) {
    RailPiece rail;
    int raw=world_cell(p.x,p.y,p.z);
    if (!rail_decode(raw,&rail)) return;
    if (!full_top_surface(world_cell(p.x,p.y-1,p.z))) {
        int before=world_cell(p.x,p.y,p.z);
        if (world_write_cell(p.x,p.y,p.z,AIR) && detector_track(before))
            redstone_after_edit(p.x,p.y,p.z,before,AIR);
        return;
    }
    int h[4], count=0;
    for (int i=0;i<4;++i) { h[i]=elevation(p,i); count+=h[i]!=2; }
    int old_axis=(rail.shape==STRAIGHT_EW || rail.shape==RISE_E || rail.shape==RISE_W);
    int ns=(h[NORTH]!=2)+(h[SOUTH]!=2), ew=(h[EAST]!=2)+(h[WEST]!=2);
    int axis=ns==ew ? old_axis : ew>ns;
    int shape=axis ? STRAIGHT_EW : STRAIGHT_NS;
    if (axis) {
        if (h[EAST]==1) shape=RISE_E;
        else if (h[WEST]==1) shape=RISE_W;
    } else {
        if (h[NORTH]==1) shape=RISE_N;
        else if (h[SOUTH]==1) shape=RISE_S;
    }
    if (rail.kind==RAIL_NORMAL && count==2 && ns==1 && ew==1 &&
        h[NORTH]!=1 && h[EAST]!=1 && h[SOUTH]!=1 && h[WEST]!=1) {
        if (h[NORTH]!=2) shape=h[EAST]!=2 ? BEND_NE : BEND_WN;
        else shape=h[EAST]!=2 ? BEND_ES : BEND_SW;
    }
    if (rail.kind==RAIL_NORMAL && count==3) {
        int junction=junction_shape(h,0);
        if (junction>=0) shape=adjacent_source(p) ? junction_shape(h,1) : junction;
    }
    world_write_cell(p.x,p.y,p.z,(mine_cart_track(raw) ? MINE_CART_TRACK_BASE : rail_base(rail.kind))+shape);
}
int rail_place(RailCell p,RailKind kind,int east_west) {
    if ((unsigned)kind>RAIL_ACTIVATOR || !inworld(p.x,p.y,p.z) ||
        !full_top_surface(world_cell(p.x,p.y-1,p.z)) || block(p.x,p.y,p.z)!=AIR) return 0;
    int cell=rail_base(kind)+(east_west?STRAIGHT_EW:STRAIGHT_NS);
    if (!world_write_cell(p.x,p.y,p.z,cell)) return 0;
    rail_after_edit(p.x,p.y,p.z,AIR,cell);
    if (kind==RAIL_DETECTOR) redstone_after_edit(p.x,p.y,p.z,AIR,cell);
    return 1;
}
void rail_after_edit(int x,int y,int z,int before,int after) {
    if (!rail_type(cell_type(before)) && !rail_type(cell_type(after))) {
        if (!full_top_surface(before) || full_top_surface(after) || !rail_type(block(x,y+1,z))) return;
        ++y;
    }
    reflow((RailCell){x,y,z});
    for (int dir=0;dir<4;++dir) for (int dy=-1;dy<=1;++dy)
        reflow((RailCell){x+rail_dx(dir),y+dy,z+rail_dz(dir)});
}
