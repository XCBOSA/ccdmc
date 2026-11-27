#include "space.h"
#include "build_blocks.h"
#include "../game/hoppers.h"
#include "../game/pistons.h"
#include <float.h>
#include <math.h>
#include <stddef.h>

BlockBox body_box(float x,float y,float z,float radius,float height) {
    float x0=x-radius,x1=x+radius,z0=z-radius,z1=z+radius;
    /* Preserve a representable interval near the world limit, where the
       float lattice can be wider than a small body's radius. */
    if (x0==x1) x1=nextafterf(x1,FLT_MAX);
    if (z0==z1) z1=nextafterf(z1,FLT_MAX);
    return (BlockBox){x0,y+.001f,z0,x1,y+height-.001f,z1};
}
static float surface(int type) {
    if (type==ENCHANTING_TABLE) return .75f;
    if (type==BED) return BED_HEIGHT;
    if (type==FARMLAND) return FARMLAND_HEIGHT;
    if (type==LILY_PAD) return LILY_PAD_HEIGHT;
    return solid(type) ? 1.f : 0.f;
}
static void stand_boxes(int x,int y,int z,BlockBox boxes[2]) {
    boxes[0]=(BlockBox){x+.11f,(float)y,z+.17f,x+.89f,y+.44f,z+.83f};
    boxes[1]=(BlockBox){x+.22f,y+.44f,z+.43f,x+.78f,y+.875f,z+.77f};
}
int dye_plant_box(int raw,int x,int y,int z,BlockBox *out) {
    if (!out) return 0;
    if (cactus_cell(raw)) {
        *out=(BlockBox){x+.0625f,(float)y,z+.0625f,x+.9375f,y+1.f,z+.9375f};
        return 1;
    }
    if (!cocoa_cell(raw)) return 0;
    int stage=cocoa_stage(raw),dir=cocoa_support_side(raw);
    float width=.119f+.059f*stage,depth=.255f+.076f*stage,y0=y+.31f-.068f*stage,y1=y+.68f;
    float cx=x+.50f+(float)rail_dx(dir)*(.5f-depth*.5f);
    float cz=z+.50f+(float)rail_dz(dir)*(.5f-depth*.5f);
    float w=rail_dx(dir) ? depth*.5f : width,d=rail_dz(dir) ? depth*.5f : width;
    *out=(BlockBox){cx-w,y0,cz-d,cx+w,y1,cz+d};
    return 1;
}
int world_cactus_contact(BlockBox body) {
    body.x0-=.045f; body.x1+=.045f;
    body.z0-=.045f; body.z1+=.045f;
    body.y0-=.028f;
    for (int x=(int)floorf(body.x0);x<=(int)floorf(body.x1);++x)
        for (int z=(int)floorf(body.z0);z<=(int)floorf(body.z1);++z)
            for (int y=(int)floorf(body.y0);y<=(int)floorf(body.y1);++y) {
                int raw;
                BlockBox cactus;
                if (world_resident_cell(x,y,z,&raw) && cactus_cell(raw) &&
                    dye_plant_box(raw,x,y,z,&cactus) && box_overlap(cactus,body)) return 1;
            }
    return 0;
}
int world_cobweb_contact(BlockBox body) {
    int from=(int)floorf(body.y0),to=(int)ceilf(body.y1);
    for (int x=(int)floorf(body.x0);x<(int)ceilf(body.x1);++x)
        for (int z=(int)floorf(body.z0);z<(int)ceilf(body.z1);++z)
            if (world_resident_web_column(x,z,from,to)) return 1;
    return 0;
}
int anvil_boxes(int x,int y,int z,int raw,BlockBox boxes[ANVIL_PARTS]) {
    if (!anvil_cell(raw)) return 0;
    BlockBox parts[ANVIL_PARTS]={
        {.125f,0,.125f,.875f,.25f,.875f},
        {.25f,.25f,.3125f,.75f,.625f,.6875f},
        {0,.625f,.1875f,1,1,.8125f}
    };
    for (int i=0;i<ANVIL_PARTS;++i) {
        BlockBox b=parts[i];
        if (!((raw-ANVIL_BASE)&1)) b=(BlockBox){b.z0,b.y0,b.x0,b.z1,b.y1,b.x1};
        boxes[i]=(BlockBox){x+b.x0,y+b.y0,z+b.z0,x+b.x1,y+b.y1,z+b.z1};
    }
    return ANVIL_PARTS;
}

/* A signal edge can close a gate after a body entered the opening. Permit
   that body to leave by the closest free side, without letting another body
   enter the gate or letting it push farther through a closed passage. */
static int moving_out(BlockBox before,BlockBox next,BlockBox gate) {
    if (!box_overlap(before,gate)) return 0;
    const float a[]={before.x1-gate.x0,gate.x1-before.x0,before.y1-gate.y0,
        gate.y1-before.y0,before.z1-gate.z0,gate.z1-before.z0};
    const float b[]={next.x1-gate.x0,gate.x1-next.x0,next.y1-gate.y0,
        gate.y1-next.y0,next.z1-gate.z0,gate.z1-next.z0};
    float nearest=a[0];
    for (int i=1;i<6;++i) nearest=fminf(nearest,a[i]);
    for (int i=0;i<6;++i) if (a[i]<=nearest+.0001f && b[i]<a[i]) return 1;
    return 0;
}
static int box_blocked(BlockBox b,unsigned hazards,const BlockBox *before) {
    const float lim=WORLD_SAVE_LIMIT+4.f;
    if (!(b.x0<b.x1 && b.x0>=-lim && b.x1<=lim && b.z0<b.z1 && b.z0>=-lim && b.z1<=lim &&
        b.y0<b.y1 && b.y0>=-2.f && b.y1<=WY+20.f)) return 1;
    int x0=(int)floorf(b.x0),x1=(int)ceilf(b.x1)-1,z0=(int)floorf(b.z0),z1=(int)ceilf(b.z1)-1;
    int y0=(int)floorf(b.y0),y1=(int)ceilf(b.y1)-1;
    /* Ground actors usually sit in the first half of a cell. Check the
       cell underneath only for a potential protruding 1.5-high barrier. */
    int extra=b.y0<y0+.5f ? 1 : 0;
    for (int x=x0;x<=x1;++x) for (int z=z0;z<=z1;++z) for (int y=y0-extra;y<=y1;++y) {
        int raw=world_cell(x,y,z),type=cell_type(raw);
        if (barrier_type(type)) {
            BlockBox boxes[BARRIER_PARTS];
            int count=barrier_world_boxes(x,y,z,raw,BARRIER_BODY,boxes);
            for (int i=0;i<count;++i)
                if (box_overlap(boxes[i],b) && !(before && (type==FENCE_GATE || door_type(type) ||
                    type==WOOD_TRAPDOOR) && moving_out(*before,b,boxes[i]))) return 1;
        } else if (build_partial(raw) && y>=y0) {
            BlockBox boxes[BUILD_BOXES];
            int n=build_world_boxes(x,y,z,raw,boxes);
            for (int i=0;i<n;++i) if (box_overlap(boxes[i],b)) return 1;
        } else if ((type==CACTUS || type==COCOA) && y>=y0) {
            BlockBox plant;
            if (dye_plant_box(raw,x,y,z,&plant) && box_overlap(plant,b)) return 1;
        } else if (type==BREWING_STAND && y>=y0) {
            BlockBox parts[2];
            stand_boxes(x,y,z,parts);
            if (box_overlap(parts[0],b) || box_overlap(parts[1],b)) return 1;
        } else if (type==HOPPER && y>=y0) {
            BlockBox parts[HOPPER_PARTS];
            int count=hopper_boxes(x,y,z,raw,parts);
            for (int i=0;i<count;++i) if (box_overlap(parts[i],b)) return 1;
        } else if (piston_cell(raw) && y>=y0) {
            BlockBox parts[PISTON_PARTS];
            int count=piston_boxes(x,y,z,raw,parts);
            for (int i=0;i<count;++i) if (box_overlap(parts[i],b) && !(before && moving_out(*before,b,parts[i]))) return 1;
        } else if (type==ENDER_CHEST && y>=y0) {
            if (box_overlap(ender_chest_box(x,y,z),b)) return 1;
        } else if (type==AIR_OUTLET && y>=y0) {
            BlockBox head=air_outlet_box(x,y,z,raw);
            if (box_overlap(head,b) && !(before && moving_out(*before,b,head))) return 1;
        } else if (type==ANVIL && y>=y0) {
            BlockBox parts[ANVIL_PARTS];
            int count=anvil_boxes(x,y,z,raw,parts);
            if (before && count) {
                /* A falling anvil can land on a body. Use its whole shape
                   for egress, since its base and neck suggest conflicting
                   escape directions when considered independently. */
                int inside=0;
                BlockBox hull=parts[0];
                for (int i=0;i<count;++i) {
                    BlockBox q=parts[i];
                    inside|=box_overlap(*before,q);
                    hull=(BlockBox){fminf(hull.x0,q.x0),fminf(hull.y0,q.y0),fminf(hull.z0,q.z0),
                        fmaxf(hull.x1,q.x1),fmaxf(hull.y1,q.y1),fmaxf(hull.z1,q.z1)};
                }
                if (inside && moving_out(*before,b,hull)) continue;
            }
            for (int i=0;i<count;++i) if (box_overlap(parts[i],b)) return 1;
        } else if (y>=y0) {
            if (type==LILY_PAD && (hazards&SPACE_IGNORE_LILY_PAD)) continue;
            float h=surface(type);
            int covered=h>0.f && b.y0<y+h;
            if (covered && before && (type==SAND || type==RED_SAND || type==GRAVEL)) {
                BlockBox settled={(float)x,(float)y,(float)z,x+1.f,y+h,z+1.f};
                if (moving_out(*before,b,settled)) covered=0;
            }
            if (covered || ((hazards&SPACE_WATER) && type==WATER) ||
                ((hazards&SPACE_LAVA) && type==LAVA)) return 1;
        }
    }
    return 0;
}
int world_box_blocked(BlockBox b,unsigned hazards) { return box_blocked(b,hazards,NULL); }
int world_move_blocked(BlockBox before,BlockBox next,unsigned hazards) { return box_blocked(next,hazards,&before); }

int world_point_blocked(float x,float y,float z,int physical) {
    const float lim=WORLD_SAVE_LIMIT+4.f;
    if (!(x>=-lim && x<=lim && y>=-2.f && y<=WY+20.f && z>=-lim && z<=lim)) return 1;
    int bx=(int)floorf(x),by=(int)floorf(y),bz=(int)floorf(z);
    int raw=world_cell(bx,by,bz),type=cell_type(raw);
    if (build_partial(raw)) {
        unsigned oct=(x-bx>=.5f)+2u*(z-bz>=.5f)+4u*(y-by>=.5f);
        return !!(build_world_mask(bx,by,bz,raw)&(1u<<oct));
    }
    if (type==CACTUS || type==COCOA) {
        BlockBox plant;
        return dye_plant_box(raw,bx,by,bz,&plant) && box_contains(plant,x,y,z);
    }
    if (type==BREWING_STAND) {
        BlockBox parts[2];
        stand_boxes(bx,by,bz,parts);
        return box_contains(parts[0],x,y,z) || box_contains(parts[1],x,y,z);
    }
    if (type==ENDER_CHEST) return box_contains(ender_chest_box(bx,by,bz),x,y,z);
    if (type==AIR_OUTLET) return box_contains(air_outlet_box(bx,by,bz,raw),x,y,z);
    if (type==ANVIL) {
        BlockBox parts[ANVIL_PARTS];
        int count=anvil_boxes(bx,by,bz,raw,parts);
        for (int i=0;i<count;++i) if (box_contains(parts[i],x,y,z)) return 1;
        return 0;
    }
    if (type==HOPPER) {
        BlockBox parts[HOPPER_PARTS];
        int count=hopper_boxes(bx,by,bz,raw,parts);
        for (int i=0;i<count;++i) if (box_contains(parts[i],x,y,z)) return 1;
        return 0;
    }
    if (piston_cell(raw)) {
        BlockBox parts[PISTON_PARTS];
        int count=piston_boxes(bx,by,bz,raw,parts);
        for (int i=0;i<count;++i) if (box_contains(parts[i],x,y,z)) return 1;
        return 0;
    }
    if (!barrier_type(type) && y<by+surface(type)) return 1;
    int extra=physical && y<by+.5f;
    for (int level=by;level>=by-extra;--level) {
        if (level!=by) raw=world_cell(bx,level,bz);
        if (!barrier_type(cell_type(raw))) continue;
        BlockBox boxes[BARRIER_PARTS];
        int count=barrier_world_boxes(bx,level,bz,raw,physical ? BARRIER_BODY : BARRIER_VISUAL,boxes);
        for (int i=0;i<count;++i) if (box_contains(boxes[i],x,y,z)) return 1;
    }
    return 0;
}
