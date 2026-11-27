#include "button_arrows.h"
#include "drops.h"
#include "../world/internal.h"
#include "../world/redstone.h"
#include <math.h>
#include <string.h>

static ButtonArrow arrows[DIMENSION_COUNT][BUTTON_ARROW_LIMIT];
static size_t counts[DIMENSION_COUNT];
static unsigned revision;

const ButtonArrow *button_arrows_in(Dimension d,size_t *count) {
    if (count) *count=(unsigned)d<DIMENSION_COUNT ? counts[d] : 0;
    return (unsigned)d<DIMENSION_COUNT ? arrows[d] : NULL;
}
unsigned button_arrows_revision(void) { return revision; }
void button_arrows_reset(void) { memset(counts,0,sizeof counts); ++revision; }
int button_arrow_present(int x,int y,int z) {
    Dimension d=world_dimension();
    for (size_t i=0;i<counts[d];++i) {
        const ButtonArrow *a=&arrows[d][i];
        if (a->x==x && a->y==y && a->z==z && a->raw==world_recorded_cell(d,x,y,z)) return 1;
    }
    return 0;
}
static int coordinate_ok(int x,int y,int z) {
    return x>=-WORLD_SAVE_LIMIT && x<=WORLD_SAVE_LIMIT && z>=-WORLD_SAVE_LIMIT && z<=WORLD_SAVE_LIMIT &&
        (unsigned)y<WY;
}
int button_arrow_restore(Dimension d,ButtonArrow a) {
    if ((unsigned)d>=DIMENSION_COUNT || counts[d]>=BUTTON_ARROW_LIMIT || !coordinate_ok(a.x,a.y,a.z) ||
        a.raw<BUTTON_WOOD || a.raw>=BUTTON_END || !a.ticks || a.ticks>BUTTON_ARROW_LIFE || a.recoverable>1 ||
        !(a.point.x>=a.x && a.point.x<=a.x+1 && a.point.y>=a.y && a.point.y<=a.y+1 &&
          a.point.z>=a.z && a.point.z<=a.z+1)) return 0;
    float length=a.direction.x*a.direction.x+a.direction.y*a.direction.y+a.direction.z*a.direction.z;
    if (!(length>=.9f && length<=1.1f)) return 0;
    arrows[d][counts[d]++]=a;
    ++revision;
    return 1;
}
static float intersect(Vec3 from,Vec3 delta,BlockBox box) {
    float origin[]={from.x,from.y,from.z},dir[]={delta.x,delta.y,delta.z};
    float lo[]={box.x0,box.y0,box.z0},hi[]={box.x1,box.y1,box.z1};
    float near=0.f,far=1.f;
    for (int axis=0;axis<3;++axis) {
        if (fabsf(dir[axis])<.00001f) {
            if (origin[axis]<lo[axis] || origin[axis]>hi[axis]) return 2.f;
        } else {
            float a=(lo[axis]-origin[axis])/dir[axis],b=(hi[axis]-origin[axis])/dir[axis];
            near=fmaxf(near,fminf(a,b));
            far=fminf(far,fmaxf(a,b));
            if (near>far) return 2.f;
        }
    }
    return near;
}
int button_arrow_impact(Vec3 from,Vec3 to,int recoverable) {
    if (!world_has_wooden_buttons()) return 0;
    Dimension d=world_dimension();
    if (counts[d]>=BUTTON_ARROW_LIMIT ||
        !(fabsf(from.x)<WORLD_SAVE_LIMIT && fabsf(to.x)<WORLD_SAVE_LIMIT &&
          fabsf(from.z)<WORLD_SAVE_LIMIT && fabsf(to.z)<WORLD_SAVE_LIMIT &&
          from.y>=-1.f && from.y<=WY+1.f && to.y>=-1.f && to.y<=WY+1.f)) return 0;
    Vec3 delta={to.x-from.x,to.y-from.y,to.z-from.z};
    float length=sqrtf(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
    if (!(length>.00001f && length<=1.f)) return 0; /* A single physics substep. */
    float best=2.f,obstacle=2.f; int xbest=0,ybest=0,zbest=0,rawbest=0;
    for (int x=(int)floorf(fminf(from.x,to.x));x<=(int)floorf(fmaxf(from.x,to.x));++x)
        for (int y=(int)floorf(fminf(from.y,to.y));y<=(int)floorf(fmaxf(from.y,to.y));++y)
            for (int z=(int)floorf(fminf(from.z,to.z));z<=(int)floorf(fmaxf(from.z,to.z));++z) {
                if (!coordinate_ok(x,y,z)) continue;
                int raw=world_cell(x,y,z);
                BlockBox box;
                if (raw>=BUTTON_WOOD && button_cell(raw) &&
                    redstone_button_box(raw,redstone_button_power(x,y,z)!=0,&box)) {
                    float t=intersect(from,delta,(BlockBox){x+box.x0,y+box.y0,z+box.z0,x+box.x1,y+box.y1,z+box.z1});
                    if (t<best) { best=t; xbest=x;ybest=y;zbest=z;rawbest=raw; }
                } else if (solid(cell_type(raw))) {
                    obstacle=fminf(obstacle,intersect(from,delta,(BlockBox){(float)x,(float)y,(float)z,x+1.f,y+1.f,z+1.f}));
                }
            }
    if (best>1.f || best>=obstacle || !redstone_press_button(xbest,ybest,zbest)) return 0;
    Vec3 at={fminf(xbest+1.f,fmaxf((float)xbest,from.x+best*delta.x)),
        fminf(ybest+1.f,fmaxf((float)ybest,from.y+best*delta.y)),
        fminf(zbest+1.f,fmaxf((float)zbest,from.z+best*delta.z))};
    arrows[d][counts[d]++]=(ButtonArrow){at,{delta.x/length,delta.y/length,delta.z/length},xbest,ybest,zbest,
        (uint16_t)rawbest,BUTTON_ARROW_LIFE,(uint8_t)(recoverable!=0)};
    ++revision;
    return 1;
}
void button_arrows_tick(void) {
    Dimension d=world_dimension();
    for (size_t i=0;i<counts[d];) {
        ButtonArrow *a=&arrows[d][i];
        int remove=!--a->ticks;
        if (!remove && a->raw!=world_recorded_cell(d,a->x,a->y,a->z)) {
            if (!a->recoverable) remove=1;
            else for (int j=0;j<MAXDROP;++j) if (!drops[j].item.count) {
                drops[j]=make_drop(a->point.x,a->point.y,a->point.z,(ItemStack){ARROW,1,0,0,0});
                drops_dirty=1; remove=1; break;
            }
        } else if (!remove && a->recoverable && a->ticks<BUTTON_ARROW_LIFE-10 &&
            player_rules.mode==SURVIVAL && player_rules.health &&
            fabsf(px-a->point.x)<.62f && fabsf(pz-a->point.z)<.62f && a->point.y>=py-.2f && a->point.y<py+1.72f) {
            remove=inventory_add_stack((ItemStack){ARROW,1,0,0,0});
        }
        if (remove) { *a=arrows[d][--counts[d]]; ++revision; }
        else ++i;
    }
}
