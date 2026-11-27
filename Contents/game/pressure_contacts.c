#include "pressure_contacts.h"
#include "actors.h"
#include "boats.h"
#include "drops.h"
#include "combat.h"
#include "button_arrows.h"
#include "experience.h"
#include "fishing.h"
#include "gravity.h"
#include "horses.h"
#include "minecarts.h"
#include "tripwire.h"
#include "../world/redstone.h"
#include "../world/space.h"
#include <math.h>

typedef struct { int min_x,max_x,min_z,max_z,sensors; } Region;
/* Project each individual body only into the tiny set of overlapped cells.
   In particular a stack counts once, regardless of the item count in it. */
static void body(Region a,BlockBox b,int living) {
    if (!(b.x1>a.min_x && b.x0<a.max_x && b.z1>a.min_z && b.z0<a.max_z &&
        b.y1>0 && b.y0<WY && b.x1>b.x0 && b.y1>b.y0 && b.z1>b.z0)) return;
    int x0=(int)floorf(fmaxf((float)a.min_x,b.x0)), x1=(int)ceilf(fminf((float)a.max_x,b.x1));
    int z0=(int)floorf(fmaxf((float)a.min_z,b.z0)), z1=(int)ceilf(fminf((float)a.max_z,b.z1));
    int y0=(int)floorf(fmaxf(0.f,b.y0)), y1=(int)ceilf(fminf((float)WY,b.y1));
    int trips=a.sensors&SENSOR_TRIPWIRE;
    int plates=(a.sensors&SENSOR_PLATES) && (living || (a.sensors&SENSOR_OBJECT_PLATES));
    for (int y=y0;y<y1;++y) {
        int on_plate=plates && b.y0<y+.25f;
        if (!on_plate && !trips) continue;
        for (int x=x0;x<x1;++x) {
            int plate_x=on_plate && b.x0<x+.875f && b.x1>x+.125f;
            if (!plate_x && !trips) continue;
            for (int z=z0;z<z1;++z) {
                if (plate_x && b.z0<z+.875f && b.z1>z+.125f) redstone_plate_contact(x,y,z,living);
                if (trips) tripwire_body_contact((CircuitPos){x,y,z},b);
            }
        }
    }
}
static void centered(Region a,Vec3 p,float r) { body(a,body_box(p.x,p.y-r,p.z,r,r*2),0); }
static int drop(const Drop *p,void *context) {
    centered(*(const Region *)context,(Vec3){p->x,p->y,p->z},.13f);
    return 1;
}
void pressure_contacts(int cx,int cz,int radius,int sensors) {
    Region a={(cx-radius)*CHUNK_SIZE,(cx+radius+1)*CHUNK_SIZE,
              (cz-radius)*CHUNK_SIZE,(cz+radius+1)*CHUNK_SIZE,sensors};
    if (player_rules.health) body(a,body_box(px,py,pz,.28f,1.72f),1);
    Dimension d=world_dimension();
    BlockBox b;
    for (int i=0;i<ACTOR_CAPACITY;++i) if (actors_body_at(d,i,&b)) body(a,b,1);
    for (int i=0;i<HORSE_CAPACITY;++i) if (horse_body_at(d,i,&b)) body(a,b,1);
    if (!(sensors&(SENSOR_OBJECT_PLATES|SENSOR_TRIPWIRE))) return;
    const BoatRecord *boats=boats_in(d);
    for (int i=0;i<BOAT_CAPACITY;++i) if (boats[i].occupied) {
        Vec3 p=boats[i].position;
        body(a,body_box(p.x,p.y,p.z,BOAT_BODY_RADIUS,BOAT_BODY_HEIGHT),0);
    }
    const CartRecord *carts=carts_in(d);
    for (int i=0;i<CART_CAPACITY;++i) if (carts[i].occupied) {
        Vec3 p=carts[i].position;
        body(a,body_box(p.x,p.y,p.z,CART_BODY_RADIUS,CART_BODY_HEIGHT),0);
    }
    drops_visible(cx,cz,drop,&a);
    size_t count=0;
    const XPOrb *orbs=experience_orbs(d,&count);
    for (size_t i=0;i<count;++i) if (orbs[i].value) centered(a,orbs[i].point,.11f);
    const FlyingArrow *arrows=combat_arrows();
    for (int i=0;i<ARROW_CAPACITY;++i) if (arrows[i].life>0.f) centered(a,arrows[i].position,.06f);
    const ButtonArrow *lodged=button_arrows_in(d,&count);
    for (size_t i=0;i<count;++i) centered(a,lodged[i].point,.06f);
    const ActorFireball *shots=actors_fireballs();
    for (int i=0;i<ACTOR_FIREBALLS;++i) if (shots[i].seconds>0.f)
        centered(a,shots[i].point,shots[i].kind==HOSTILE_ARROW ? .06f : .10f);
    const FallingBlock *falling=gravity_falling(d,&count);
    for (size_t i=0;i<count;++i)
        body(a,body_box(falling[i].x+.5f,falling[i].bottom,falling[i].z+.5f,.49f,.98f),0);
    if (fishing_active()) {
        FishingView hook=fishing_view();
        if (hook.phase!=FISH_IDLE && hook.phase!=FISH_LANDED) centered(a,hook.point,.10f);
    }
}
