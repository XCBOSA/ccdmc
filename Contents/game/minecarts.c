#include "minecarts.h"
#include "drops.h"
#include "blast.h"
#include "container_automation.h"
#include "container_signal.h"
#include "stack_transfer.h"
#include "../world/space.h"
#include "../world/redstone.h"
#include "boats.h"
#include "horses.h"
#include "chests.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static CartRecord carts[DIMENSION_COUNT][CART_CAPACITY];
typedef struct {
    ItemStack slots[CART_STORAGE_SLOTS];
    uint64_t serial;
    uint8_t signal,locked,cooldown;
} Cargo;
static Cargo cargo[DIMENSION_COUNT][CART_CAPACITY];
static unsigned cargo_count[DIMENSION_COUNT];
static unsigned hopper_count[DIMENSION_COUNT];
static CartFurnaceDrive furnace_drives[DIMENSION_COUNT][CART_CAPACITY];
static unsigned furnace_count[DIMENSION_COUNT];
static uint64_t next_serial;
static int seated=-1;
static Dimension seat_dim;
static unsigned revision;

unsigned carts_revision(void) { return revision; }
const CartRecord *carts_in(Dimension d) { return (unsigned)d<DIMENSION_COUNT ? carts[d] : NULL; }
void carts_reset(void) {
    memset(carts,0,sizeof carts);
    memset(cargo,0,sizeof cargo);
    memset(cargo_count,0,sizeof cargo_count);
    memset(hopper_count,0,sizeof hopper_count);
    memset(furnace_drives,0,sizeof furnace_drives);
    memset(furnace_count,0,sizeof furnace_count);
    seated=-1;
    ++revision;
}
int cart_rider(void) { return player_rules.health && seat_dim==world_dimension() ? seated : -1; }
void cart_unseat(void) { seated=-1; }

static void sync_rider(const CartRecord *c) {
    px=c->position.x; py=c->position.y+.25f; pz=c->position.z;
    vy=0; grounded=0; player_rules.fall_origin=py;
}
static float clamp(float v,float low,float high) { return fmaxf(low,fminf(high,v)); }
static Vec3 position(RailSample r) { return (Vec3){r.x,r.y,r.z}; }

static int capacity(int kind) {
    return kind==CART_CHEST ? CART_STORAGE_SLOTS : kind==CART_HOPPER ? HOPPER_CART_SLOTS : 0;
}
static int valid_velocity(float v,float cap) {
    uint32_t bits,limit;
    memcpy(&bits,&v,sizeof bits);
    memcpy(&limit,&cap,sizeof limit);
    /* IEEE positive magnitudes sort as integers. A single integer
       comparison also rejects infinities/NaNs under -ffast-math. */
    return (bits&0x7fffffffu)<=limit;
}
static int detonation_pending(const CartRecord *c) { return c->fuse>CART_TNT_FUSE; }
static float approach_speed(const CartRecord *c) {
    return c->off_track && !detonation_pending(c) ? hypotf(c->free_velocity.x,c->free_velocity.z) : fabsf(c->speed);
}

static void install_cart(Dimension d,int slot,CartRecord a) {
    carts[d][slot]=a;
    if (capacity(a.kind)) {
        Cargo *c=&cargo[d][slot];
        memset(c,0,sizeof *c);
        if (!++next_serial) ++next_serial;
        c->serial=next_serial;
        ++cargo_count[d];
        if (a.kind==CART_HOPPER) ++hopper_count[d];
    }
    if (a.kind==CART_FURNACE) {
        furnace_drives[d][slot]=(CartFurnaceDrive){0,0,0};
        ++furnace_count[d];
    }
    ++revision;
}
int cart_restore(Dimension d,CartRecord a) {
    Vec3 p=a.position;
    if ((unsigned)d>=DIMENSION_COUNT || a.occupied!=1 || a.kind>CART_FURNACE || a.off_track>1 ||
        (a.kind!=CART_TNT ? a.fuse!=0 : a.fuse>CART_TNT_PROTECTED_PENDING) ||
        !(fabsf(p.x)<WORLD_SAVE_LIMIT && fabsf(p.z)<WORLD_SAVE_LIMIT && p.y>=0 && p.y<WY &&
          fabsf(a.heading)<=3.142f && a.along>=0 && a.along<=1) ||
        !valid_velocity(a.speed,detonation_pending(&a) ? BLAST_CART_SPEED_LIMIT : 8.1f) ||
        !valid_velocity(a.fall_distance,WY) || a.fall_distance<0.f ||
        (a.fall_distance!=0.f && (!a.off_track || a.kind!=CART_TNT || detonation_pending(&a)))) return 0;
    if (a.off_track) {
        if (a.along!=0.f || (!detonation_pending(&a) && a.speed!=0.f) ||
            !valid_velocity(a.free_velocity.x,CART_FREE_HORIZONTAL_LIMIT) ||
            !valid_velocity(a.free_velocity.y,CART_FREE_VERTICAL_LIMIT) ||
            !valid_velocity(a.free_velocity.z,CART_FREE_HORIZONTAL_LIMIT)) return 0;
        a.track=(RailCell){0,0,0};
    } else if (
        a.track.x<=-WORLD_SAVE_LIMIT || a.track.x>=WORLD_SAVE_LIMIT ||
        a.track.z<=-WORLD_SAVE_LIMIT || a.track.z>=WORLD_SAVE_LIMIT ||
        a.track.y<0 || a.track.y>=WY ||
        fabsf(p.x-a.track.x-.5f)>.65f || fabsf(p.z-a.track.z-.5f)>.65f ||
        fabsf(p.y-a.track.y)>1.2f) return 0;
    else a.free_velocity=(Vec3){0,0,0};
    for (int i=0;i<CART_CAPACITY;++i) if (!carts[d][i].occupied) {
        install_cart(d,i,a);
        return 1;
    }
    return 0;
}
int cart_furnace_drive(Dimension d,int slot,CartFurnaceDrive *out) {
    if ((unsigned)d>=DIMENSION_COUNT || (unsigned)slot>=CART_CAPACITY || !out ||
        !carts[d][slot].occupied || carts[d][slot].kind!=CART_FURNACE) return 0;
    *out=furnace_drives[d][slot];
    return 1;
}
int cart_restore_furnace_drive(Dimension d,int slot,CartFurnaceDrive drive) {
    CartFurnaceDrive old;
    if (!cart_furnace_drive(d,slot,&old) || drive.fuel>CART_FUEL_MAX) return 0;
    /* Validate IEEE finite values even under the guest's fast-math mode. */
    uint32_t x,z;
    memcpy(&x,&drive.x,sizeof x); memcpy(&z,&drive.z,sizeof z);
    if ((x&0x7f800000u)==0x7f800000u || (z&0x7f800000u)==0x7f800000u ||
        fabsf(drive.x)>1.f || fabsf(drive.z)>1.f) return 0;
    float len=hypotf(drive.x,drive.z);
    if ((!drive.fuel && len!=0.f) || (len!=0.f && (len<.99f || len>1.01f))) return 0;
    furnace_drives[d][slot]=drive;
    ++revision;
    return 1;
}
static Cargo *cargo_for(Dimension d,int slot) {
    if ((unsigned)d>=DIMENSION_COUNT || (unsigned)slot>=CART_CAPACITY) return NULL;
    const CartRecord *c=&carts[d][slot];
    return c->occupied && capacity(c->kind) ? &cargo[d][slot] : NULL;
}
const ItemStack *cart_cargo_in(Dimension d,int slot) {
    const Cargo *c=cargo_for(d,slot);
    return c ? c->slots : NULL;
}
int cart_cargo_count(Dimension d,int slot) {
    return cargo_for(d,slot) ? capacity(carts[d][slot].kind) : 0;
}
int cart_hopper_cooldown(Dimension d,int slot) {
    const Cargo *c=cargo_for(d,slot);
    return c && carts[d][slot].kind==CART_HOPPER ? c->cooldown : -1;
}
int cart_restore_hopper_cooldown(Dimension d,int slot,int ticks) {
    Cargo *c=cargo_for(d,slot);
    if (!c || carts[d][slot].kind!=CART_HOPPER || (unsigned)ticks>HOPPER_CART_TICKS) return 0;
    c->cooldown=(uint8_t)ticks;
    return 1;
}
static int same_track(RailCell a,RailCell b) { return a.x==b.x && a.y==b.y && a.z==b.z; }
static void detector_changed(RailCell p) {
    if (detector_track(world_recorded_cell(world_dimension(),p.x,p.y,p.z)))
        redstone_container_changed(p.x,p.y,p.z);
}
static void cargo_weight(Cargo *c,int count) {
    c->signal=(uint8_t)container_fill_signal(container_weight(c->slots,count),count);
}
static void cargo_changed(int slot,Cargo *c) {
    int previous=c->signal;
    cargo_weight(c,cart_cargo_count(world_dimension(),slot));
    const CartRecord *cart=&carts[world_dimension()][slot];
    if (previous!=c->signal && !cart->off_track) detector_changed(cart->track);
}
int cart_restore_cargo(Dimension d,int slot,const ItemStack *items,int count) {
    Cargo *c=cargo_for(d,slot);
    if (!c || !items || count!=cart_cargo_count(d,slot)) return 0;
    for (int i=0;i<count;++i) if (!item_stack_valid(items[i])) return 0;
    memcpy(c->slots,items,(size_t)count*sizeof *items);
    cargo_weight(c,count);
    return 1;
}
int cart_open_storage(int slot,CartStorageView *view) {
    Dimension d=world_dimension();
    const Cargo *c=cargo_for(d,slot);
    if (!view || !c) return 0;
    *view=(CartStorageView){d,slot,c->serial};
    return 1;
}
int cart_storage_valid(const CartStorageView *view) {
    if (!view || view->dimension!=world_dimension()) return 0;
    const Cargo *c=cargo_for(view->dimension,view->slot);
    return c && c->serial==view->serial;
}
ItemStack cart_storage_item(const CartStorageView *view,int index) {
    if (!cart_storage_valid(view) || (unsigned)index>=(unsigned)cart_cargo_count(view->dimension,view->slot))
        return (ItemStack){0,0,0,0,0};
    return cargo[view->dimension][view->slot].slots[index];
}
int cart_storage_move(const CartStorageView *view,int from,int to,int single) {
    if (!cart_storage_valid(view)) return 0;
    Cargo *c=&cargo[view->dimension][view->slot];
    int count=cart_cargo_count(view->dimension,view->slot);
    if (!storage_stack_move(c->slots,count,from,to,single)) return 0;
    if (from<count || to<count) cargo_changed(view->slot,c);
    return 1;
}
int cart_container_at(int x,int y,int z) {
    Dimension d=world_dimension();
    if (!cargo_count[d]) return -1;
    for (int i=0;i<CART_CAPACITY;++i) if (cargo_for(d,i)) {
        Vec3 p=carts[d][i].position;
        if (p.x>=x && p.x<(float)x+1 && p.y>=y && p.y<(float)y+1 && p.z>=z && p.z<(float)z+1) return i;
    }
    return -1;
}
static int cargo_insert(Dimension d,int slot,ItemStack item,int limit) {
    Cargo *c=cargo_for(d,slot);
    if (!c) return 0;
    int count=cart_cargo_count(d,slot),total=0;
    for (int i=0;i<count && total<limit && item.count;++i) {
        int n=stack_receive(&c->slots[i],item,limit-total);
        if (n) { total+=n; stack_deduct(&item,n); }
    }
    if (total) cargo_changed(slot,c);
    return total;
}
int cart_container_insert(int slot,ItemStack item) {
    return cargo_insert(world_dimension(),slot,item,1);
}
int cart_container_extract(int slot,int index) {
    Dimension d=world_dimension();
    Cargo *c=cargo_for(d,slot);
    if (!c || (unsigned)index>=(unsigned)cart_cargo_count(d,slot) || !c->slots[index].count) return 0;
    stack_deduct(&c->slots[index],1);
    cargo_changed(slot,c);
    return 1;
}
int cart_detector_signal(RailCell spot) {
    Dimension d=world_dimension();
    if (!cargo_count[d]) return 0;
    for (int i=0;i<CART_CAPACITY;++i) if (cargo_for(d,i) && !carts[d][i].off_track && same_track(carts[d][i].track,spot))
        return cargo[d][i].signal;
    return 0;
}
int cart_restore_rider(int index) {
    Dimension d=world_dimension();
    if (index < -1 || index>=CART_CAPACITY) return 0;
    if (index>=0) {
        const CartRecord *a=&carts[d][index];
        if (!a->occupied || a->kind!=CART_RIDEABLE || !player_rules.health || boat_rider()>=0 || horse_rider()>=0 ||
            fabsf(px-a->position.x)>.25f || fabsf(py-a->position.y-.25f)>.25f ||
            fabsf(pz-a->position.z)>.25f) return 0;
        player_rules.flying=0;
        sync_rider(a);
    }
    seated=index; seat_dim=d;
    return 1;
}

static int target_cart(void) {
    const CartRecord *bank=carts[world_dimension()];
    int indexes[CART_CAPACITY],n=0;
    for (int i=0;i<CART_CAPACITY;++i) if (bank[i].occupied &&
        fabsf(py-bank[i].position.y)<7.f &&
        hypotf(px-bank[i].position.x,pz-bank[i].position.z)<6.f) indexes[n++]=i;
    if (!n) return -1;
    Vec3 direction=forward();
    for (float k=.14f;k<5.2f;k+=.10f) {
        Vec3 p={px+direction.x*k,py+1.55f+direction.y*k,pz+direction.z*k};
        if (world_point_blocked(p.x,p.y,p.z,0)) break;
        for (int j=0;j<n;++j) {
            Vec3 v=bank[indexes[j]].position;
            const CartRecord *cart=&bank[indexes[j]];
            float height=cart->kind==CART_RIDEABLE ? .59f : .85f;
            if (p.y>v.y-.09f && p.y<v.y+height && hypotf(p.x-v.x,p.z-v.z)<.64f) return indexes[j];
        }
    }
    return -1;
}
static BlockBox cart_room(Vec3 p,CartKind kind) {
    return body_box(p.x,p.y+.059f,p.z,.003f,kind==CART_RIDEABLE ? 1.912f : .80f);
}
static int cart_in_bounds(Vec3 p) {
    return fabsf(p.x)<WORLD_SAVE_LIMIT && fabsf(p.z)<WORLD_SAVE_LIMIT && p.y>=0 && p.y<WY-1;
}
static int room(Vec3 p,CartKind kind) {
    return cart_in_bounds(p) && !world_box_blocked(cart_room(p,kind),SPACE_WATER|SPACE_LAVA);
}
static int occupied(Vec3 p,int ignore) {
    const CartRecord *bank=carts[world_dimension()];
    for (int i=0;i<CART_CAPACITY;++i) if (bank[i].occupied && i!=ignore) {
        Vec3 v=bank[i].position;
        float dx=v.x-p.x,dz=v.z-p.z;
        if (fabsf(v.y-p.y)<.65f && dx*dx+dz*dz<.87f*.87f) return i;
    }
    return -1;
}
int cart_overlaps_cell(int x,int y,int z) {
    const CartRecord *bank=carts[world_dimension()];
    for (int i=0;i<CART_CAPACITY;++i) if (bank[i].occupied) {
        Vec3 p=bank[i].position;
        if (p.x+CART_BODY_RADIUS>x && p.x-CART_BODY_RADIUS<x+1 &&
            p.z+CART_BODY_RADIUS>z && p.z-CART_BODY_RADIUS<z+1 &&
            p.y<y+1 && p.y+CART_BODY_HEIGHT>y) return 1;
    }
    return 0;
}
int cart_on_track(RailCell spot) {
    const CartRecord *bank=carts[world_dimension()];
    for (int i=0;i<CART_CAPACITY;++i) {
        RailCell rail=bank[i].track;
        if (bank[i].occupied && !bank[i].off_track && same_track(rail,spot)) return 1;
    }
    return 0;
}
static void sense_cart(CartRecord *cart,int slot,RailPiece rail) {
    RailCell spot=cart->track;
    if (rail.kind==RAIL_DETECTOR) redstone_detector_contact((CircuitPos){spot.x,spot.y,spot.z});
    if (rail.kind==RAIL_ACTIVATOR && cart->kind==CART_TNT && !cart->fuse && rail_is_energized(spot)) {
        cart->fuse=CART_TNT_FUSE;
        ++revision;
    }
    if (rail.kind==RAIL_ACTIVATOR && cart->kind==CART_HOPPER)
        cargo[world_dimension()][slot].locked=(uint8_t)(rail_is_energized(spot)!=0);
}

static int detonate(CartRecord *cart,float speed) {
    int protect=cart->fuse>0 && cart->fuse!=CART_TNT_IMPACT_PENDING;
    if (!blast_queue_cart(cart->position,speed,protect)) {
        cart->fuse=protect ? CART_TNT_PROTECTED_PENDING : CART_TNT_IMPACT_PENDING;
        cart->speed=speed;
        cart->free_velocity=(Vec3){0,0,0};
        cart->fall_distance=0.f;
        ++revision;
        return 0;
    }
    cart->occupied=0;
    ++revision;
    return 1;
}
void carts_tick_fuses(void) {
    CartRecord *bank=carts[world_dimension()];
    for (int i=0;i<CART_CAPACITY;++i) if (bank[i].occupied && bank[i].kind==CART_TNT) {
        CartRecord *cart=&bank[i];
        if (!cart->fuse) continue;
        if (detonation_pending(cart) || cart->fuse==1)
            detonate(cart,approach_speed(cart));
        else { --cart->fuse; ++revision; }
    }
}
void carts_tick_furnaces(void) {
    Dimension d=world_dimension();
    if (!furnace_count[d]) return;
    for (int i=0;i<CART_CAPACITY;++i) {
        CartRecord *c=&carts[d][i];
        if (!c->occupied || c->kind!=CART_FURNACE ||
            fabsf(c->position.x-px)>48.f || fabsf(c->position.z-pz)>48.f) continue;
        CartFurnaceDrive *drive=&furnace_drives[d][i];
        if (drive->fuel && !--drive->fuel) {
            drive->x=drive->z=0.f;
            ++revision;
        }
    }
}
typedef struct {
    Vec3 position;
    int slot,upper,side;
} CartIntake;
typedef struct {
    CartIntake carts[CART_CAPACITY];
    Dimension dim;
    int count,flank;
} CartIntakes;

static int cart_receive_one(ItemStack item,void *ctx) {
    return cart_container_insert(*(const int *)ctx,item);
}
static int absorb_drop(const Drop *drop,void *ctx) {
    CartIntakes *batch=(CartIntakes *)ctx;
    for (int i=0;i<batch->count;++i) {
        CartIntake *in=&batch->carts[i];
        float dx=drop->x-in->position.x,dy=drop->y-in->position.y,dz=drop->z-in->position.z;
        if (batch->flank) {
            if (!in->side || fabsf(dx)>.89f || fabsf(dz)>.89f || dy<-.22f || dy>CART_BODY_HEIGHT) continue;
            in->side=0;
        } else {
            /* The original upper scan starts at the cart position plus
               one vertically, and tests intersection with the item box. */
            if (!in->upper || dx<-.125f || dx>1.125f || dz<-.125f || dz>1.125f ||
                dy<.75f || dy>2.125f) continue;
            in->upper=0;
        }
        int n=cargo_insert(batch->dim,in->slot,drop->item,drop->item.count);
        /* Only a complete upper intake starts the cooldown. The low
           sweep is just a fallback around the edge of the cart. */
        if (!batch->flank && n==drop->item.count) {
            cargo[batch->dim][in->slot].cooldown=HOPPER_CART_TICKS;
            in->side=0;
        }
        if (n) return n;
    }
    return 0;
}
void carts_tick_hoppers(int player_x,int player_z) {
    Dimension d=world_dimension();
    if (!hopper_count[d]) return;
    CartIntakes batch;
    batch.dim=d; batch.count=0; batch.flank=0;
    int upper=0;
    for (int i=0;i<CART_CAPACITY;++i) {
        CartRecord *cart=&carts[d][i];
        if (!cart->occupied || cart->kind!=CART_HOPPER) continue;
        Vec3 p=cart->position;
        int x=(int)floorf(p.x),y=(int)floorf(p.y+1.f),z=(int)floorf(p.z);
        if (abs(x-player_x)>48 || abs(z-player_z)>48 || !world_chunk_ready(x,z)) continue;
        RailPiece rail;
        if (!cart->off_track && rail_at(cart->track,&rail) && rail.kind==RAIL_ACTIVATOR) sense_cart(cart,i,rail);
        Cargo *c=&cargo[d][i];
        if (c->locked || (c->cooldown && --c->cooldown)) continue;
        int result=container_extract_one(x,y,z,cart_receive_one,&i);
        if (result==INVENTORY_INSERTED) { c->cooldown=HOPPER_CART_TICKS; continue; }
        if (c->signal==15) continue; /* full (even with non-stackable items) */
        int overhead=result==NO_INVENTORY;
        batch.carts[batch.count++]=(CartIntake){p,i,overhead,1};
        upper|=overhead;
    }
    int cx=player_x/CHUNK_SIZE-(player_x%CHUNK_SIZE<0);
    int cz=player_z/CHUNK_SIZE-(player_z%CHUNK_SIZE<0);
    if (upper) drops_collect_automatic(cx,cz,absorb_drop,&batch);
    int flank=0;
    for (int i=0;i<batch.count;++i) flank|=batch.carts[i].side;
    if (flank) {
        batch.flank=1;
        drops_collect_automatic(cx,cz,absorb_drop,&batch);
    }
}
static int break_cart(int index,int drop_shell) {
    Dimension d=world_dimension();
    CartRecord *cart=&carts[d][index];
    Cargo *c=cargo_for(d,index);
    ItemStack items[CART_STORAGE_SLOTS+2];
    int count=0,available=0,free_slots[CART_STORAGE_SLOTS+2];
    if (drop_shell) {
        items[count++]=(ItemStack){MINECART,1,0,0,0};
        if (cart->kind==CART_TNT) items[count++]=(ItemStack){TNT_ITEM,1,0,0,0};
        if (cart->kind==CART_CHEST) items[count++]=(ItemStack){CHEST,1,0,0,0};
        if (cart->kind==CART_HOPPER) items[count++]=(ItemStack){HOPPER_ITEM,1,0,0,0};
        if (cart->kind==CART_FURNACE) items[count++]=(ItemStack){FURNACE,1,0,0,0};
    }
    if (c) for (int i=0,n=cart_cargo_count(d,index);i<n;++i) if (c->slots[i].count) items[count++]=c->slots[i];
    for (int j=0;j<MAXDROP && available<count;++j) if (!drops[j].item.count) free_slots[available++]=j;
    if (available<count) return 0;
    for (int i=0;i<count;++i) {
        Vec3 p=cart->position;
        float dx=.115f*(i%5-2),dz=.115f*(i/5-2);
        drops[free_slots[i]]=make_drop(p.x+dx,p.y+.24f,p.z+dz,items[i]);
    }
    drops_dirty|=count!=0;
    cart->occupied=0;
    if (cart->kind==CART_FURNACE) {
        --furnace_count[d];
        furnace_drives[d][index]=(CartFurnaceDrive){0,0,0};
    }
    if (c) {
        --cargo_count[d];
        if (cart->kind==CART_HOPPER) --hopper_count[d];
        if (!cart->off_track) detector_changed(cart->track);
        memset(c,0,sizeof *c);
    }
    ++revision;
    return 1;
}
void cart_blast_hit(int index) {
    if ((unsigned)index>=CART_CAPACITY) return;
    CartRecord *cart=&carts[world_dimension()][index];
    if (cart->occupied && cart->kind==CART_TNT)
        detonate(cart,approach_speed(cart));
    else if (cart->occupied && (capacity(cart->kind) || cart->kind==CART_FURNACE))
        break_cart(index,1);
}
const char *cart_dismount(void) {
    int rider=cart_rider();
    if (rider<0) return NULL;
    Vec3 p=carts[world_dimension()][rider].position;
    const int heights[]={0,1,-1};
    for (int i=0;i<4;++i) for (int j=0;j<3;++j) {
        float x=p.x+1.12f*rail_dx(i),z=p.z+1.12f*rail_dz(i);
        float y=floorf(p.y)+(float)heights[j]+.01f;
        int a=(int)floorf(x), b=(int)floorf(y),c=(int)floorf(z);
        if (!inworld(a,b,c) || !solid(block(a,b-1,c)) || block(a,b,c)==LAVA ||
            block(a,b,c)==WATER || collides(x,y,z)) continue;
        seated=-1; px=x; py=y; pz=z; vy=0; grounded=0; player_rules.fall_origin=y;
        return "已下矿车";
    }
    return "旁边没有安全的下车位置";
}

static int launch(RailCell spot,CartKind kind) {
    RailPiece piece;
    if (!rail_at(spot,&piece)) return 0;
    RailSample s=rail_sample(spot,piece,.5f);
    Vec3 p=position(s);
    if (!room(p,kind) || occupied(p,-1)>=0) return 0;
    CartRecord *bank=carts[world_dimension()];
    for (int i=0;i<CART_CAPACITY;++i) if (!bank[i].occupied) {
        if (!cart_restore(world_dimension(),(CartRecord){p,spot,.5f,0,atan2f(s.dx,-s.dz),1,(uint8_t)kind,0,0,{0,0,0},0}))
            return 0;
        sense_cart(&bank[i],i,piece);
        return 1;
    }
    return 0;
}
int cart_launch_on_rail(RailCell spot) { return launch(spot,CART_RIDEABLE); }
int cart_launch_tnt_on_rail(RailCell spot) { return launch(spot,CART_TNT); }
int cart_launch_chest_on_rail(RailCell spot) { return launch(spot,CART_CHEST); }
int cart_launch_hopper_on_rail(RailCell spot) { return launch(spot,CART_HOPPER); }
int cart_launch_furnace_on_rail(RailCell spot) { return launch(spot,CART_FURNACE); }
static int activate_mine_cart(void *ctx,int x,int y,int z,int raw) {
    const Vec3 *player=(const Vec3 *)ctx;
    float dx=player->x-x-.5f,dz=player->z-z-.5f;
    if (fabsf(player->y-y)>6.f || dx*dx+dz*dz>16.f*16.f) return 1;
    RailPiece piece;
    if (!rail_decode(raw,&piece) || !solid(block(x,y-1,z))) return 1;
    RailCell spot={x,y,z};
    RailSample s=rail_sample(spot,piece,.5f);
    Vec3 p=position(s);
    if (!room(p,CART_CHEST) || occupied(p,-1)>=0 ||
        box_overlap(body_box(p.x,p.y,p.z,CART_BODY_RADIUS,CART_BODY_HEIGHT),
            body_box(player->x,player->y,player->z,.30f,1.8f))) return 1;
    for (int i=0;i<CART_CAPACITY;++i) if (!carts[OVERWORLD][i].occupied) {
        _Static_assert((int)CHEST_SLOTS==(int)CART_STORAGE_SLOTS,"Mineshaft loot inventory sizes");
        /* Reserve the sparse edit first. Once it succeeds the game-thread
           slot and its cargo are fixed storage and cannot fail to install. */
        if (!world_claim_mine_cart(x,y,z)) return 1;
        install_cart(OVERWORLD,i,(CartRecord){p,spot,.5f,0,atan2f(s.dx,-s.dz),1,CART_CHEST,0,0,{0,0,0},0});
        Cargo *box=&cargo[OVERWORLD][i];
        mineshaft_loot((ChestPos){x,y,z},box->slots);
        cargo_weight(box,CART_STORAGE_SLOTS);
        break;
    }
    return 1;
}
void carts_activate_mineshaft(Vec3 player) {
    if (world_generation()<GENERATOR_MINESHAFTS || world_dimension()!=OVERWORLD ||
        world_structures_disabled() || !player_rules.health || player.y>36.f) return;
    world_visit_mine_carts((int)floorf(player.x/CHUNK_SIZE),(int)floorf(player.z/CHUNK_SIZE),1,
        activate_mine_cart,&player);
}
static const char *use_furnace_cart(Dimension d,int slot,ItemStack *held) {
    CartRecord *c=&carts[d][slot];
    CartFurnaceDrive *drive=&furnace_drives[d][slot];
    RailPiece rail;
    if (c->off_track || !rail_at(c->track,&rail)) return "熔炉矿车不在有效铁轨上";
    RailSample s=rail_sample(c->track,rail,c->along);
    float h=hypotf(s.dx,s.dz);
    if (h<=.001f) return "熔炉矿车不在有效铁轨上";
    int fuel=held->count && (held->type==COAL || held->type==CHARCOAL);
    if (fuel && drive->fuel<=CART_FUEL_MAX-CART_FUEL_PER_COAL) {
        drive->fuel+=CART_FUEL_PER_COAL;
        if (player_rules.mode==SURVIVAL) stack_deduct(held,1);
    } else if (!drive->fuel) return "手持煤炭或木炭，轻点熔炉矿车加燃料";
    else if (fuel) return "熔炉矿车燃料已满";
    float along=s.dx*(c->position.x-px)+s.dz*(c->position.z-pz);
    if (fabsf(along)<.04f) along=s.dx*sinf(yaw)-s.dz*cosf(yaw);
    if (fabsf(along)<.04f) along=c->speed;
    if (fabsf(along)<.04f) along=drive->x*s.dx+drive->z*s.dz;
    if (c->speed*along<0.f) c->speed=0.f;
    float sign=along<0.f ? -1.f : 1.f;
    drive->x=clamp(sign*s.dx/h,-1.f,1.f);
    drive->z=clamp(sign*s.dz/h,-1.f,1.f);
    c->heading=atan2f(drive->x,-drive->z);
    ++revision;
    return fuel ? "熔炉矿车已点火" : "已调整熔炉矿车方向";
}
const char *cart_use(void) { return cart_use_with_storage(NULL); }
const char *cart_use_with_storage(int *open_slot) {
    if (open_slot) *open_slot=-1;
    if (!player_rules.health || (unsigned)selected_slot>=HOTBAR_SIZE || boat_rider()>=0 || horse_rider()>=0)
        return NULL;
    if (cart_rider()>=0) return cart_dismount();
    Dimension d=world_dimension();
    ItemStack *held=&inventory[selected_slot];
    int index=target_cart();
    if (index>=0) {
        CartRecord *a=&carts[d][index];
        if (a->kind==CART_TNT) return a->fuse ? "TNT 矿车已点燃" : "TNT 矿车不能乘坐";
        if (a->kind==CART_FURNACE) return use_furnace_cart(d,index,held);
        if (capacity(a->kind)) {
            if (!open_slot) return a->kind==CART_HOPPER ? "漏斗矿车不能乘坐" : "箱子矿车不能乘坐";
            *open_slot=index;
            return NULL;
        }
        if (!room(a->position,CART_RIDEABLE)) return "矿车上方空间不足";
        seat_dim=d; seated=index;
        player_rules.flying=0;
        sync_rider(a);
        return "已上矿车；点「下车」离开";
    }
    if (!held->count || (held->type!=MINECART && held->type!=TNT_MINECART &&
        held->type!=CHEST_MINECART && held->type!=HOPPER_MINECART && held->type!=FURNACE_MINECART)) return NULL;
    CartKind kind=held->type==FURNACE_MINECART ? CART_FURNACE :
        held->type==HOPPER_MINECART ? CART_HOPPER : held->type==CHEST_MINECART ? CART_CHEST :
        held->type==TNT_MINECART ? CART_TNT : CART_RIDEABLE;
    Hit h=target();
    RailCell spot={h.x,h.y,h.z};
    RailPiece piece;
    if (!h.hit || !rail_at(spot,&piece)) return "对准铁轨才能放置矿车";
    RailSample s=rail_sample(spot,piece,.5f);
    Vec3 p=position(s);
    if (!room(p,kind) || occupied(p,-1)>=0) return "铁轨上没有空位";
    if (!launch(spot,kind)) return "这个维度的矿车位已满";
    if (player_rules.mode==SURVIVAL && !--held->count) *held=(ItemStack){0,0,0,0,0};
    return kind==CART_FURNACE ? "已放置熔炉矿车" : kind==CART_HOPPER ? "已放置漏斗矿车" : kind==CART_CHEST ? "已放置箱子矿车" :
        kind==CART_TNT ? "已放置 TNT 矿车" : "已放置矿车";
}
const char *cart_take_down(void) {
    int i=target_cart();
    if (i<0 || i==cart_rider()) return NULL;
    CartRecord *c=&carts[world_dimension()][i];
    if (c->kind==CART_TNT && (detonation_pending(c) || approach_speed(c)>=2.f)) {
        cart_blast_hit(i);
        return "TNT 矿车受到撞击";
    }
    if (!break_cart(i,player_rules.mode==SURVIVAL)) return "掉落物已满，请先拾取";
    return "已收起矿车";
}

static float tangent_length(RailSample s) { return sqrtf(s.dx*s.dx+s.dy*s.dy+s.dz*s.dz); }
static float sign_of(float f) { return f>=0 ? 1.f : -1.f; }

static BlockBox free_cart_box(Vec3 p,int rider) {
    return body_box(p.x,p.y+.012f,p.z,.44f,rider ? 1.96f : CART_BODY_HEIGHT);
}

static int free_barrier(Vec3 p,Vec3 next,int rider) {
    return !cart_in_bounds(next) ||
        world_move_blocked(free_cart_box(p,rider),free_cart_box(next,rider),SPACE_LAVA);
}

static void derail(CartRecord *c,Vec3 v) {
    RailCell old=c->track;
    c->track=(RailCell){0,0,0};
    c->off_track=1;
    c->along=c->speed=0.f;
    c->free_velocity=v;
    c->fall_distance=0.f;
    detector_changed(old);
}
static void land_cart(CartRecord *c,float remaining) {
    if (c->kind!=CART_TNT) return;
    float distance=c->fall_distance+remaining;
    c->fall_distance=0.f;
    /* Use the fall distance to estimate blast speed, bounded by the
       runtime's existing cart-energy limit. */
    if (distance>=3.f) detonate(c,fminf(BLAST_CART_SPEED_LIMIT,distance*2.f));
}
static int cart_in_water(Vec3 p) {
    return water_strength(world_cell((int)floorf(p.x),(int)floorf(p.y+.24f),(int)floorf(p.z)))!=0;
}

/* A rail below the cart catches it only as it crosses the rail surface,
   or when a stationary cart has a line built underneath it. */
static int catch_rail(CartRecord *c,int index,int rider,Vec3 previous) {
    Vec3 p=c->position,v=c->free_velocity;
    if (v.y>.01f) return 0;
    float horizontal=hypotf(v.x,v.z);
    RailCell tile={(int)floorf(p.x),(int)floorf(p.y+.06f),(int)floorf(p.z)};
    for (int k=0;k<2;++k,--tile.y) {
        RailPiece rail;
        if (!rail_at(tile,&rail)) continue;
        float lowest=.23f*.23f,t=0.f,dot=0.f;
        RailSample best={0,0,0,0,0,0};
        for (int j=0;j<=16;++j) {
            float a=j*(1.f/16.f);
            RailSample s=rail_sample(tile,rail,a);
            float dx=s.x-p.x,dz=s.z-p.z;
            float d=dx*dx+dz*dz;
            if (d>=lowest || p.y<s.y-.061f || p.y>s.y+.17f || previous.y<s.y-.061f) continue;
            t=a; best=s; lowest=d;
        }
        if (!best.dx && !best.dz) continue;
        dot=(best.dx*v.x+best.dz*v.z)/hypotf(best.dx,best.dz);
        if (horizontal>.12f && (fabsf(dot)<.71f*horizontal ||
            (t==0.f && dot<0.f) || (t==1.f && dot>0.f))) continue;
        Vec3 next=position(best);
        if (free_barrier(p,next,rider) || occupied(next,index)>=0) continue;
        c->track=tile; c->along=t; c->position=next; c->off_track=0;
        c->speed=clamp(dot,-8.f,8.f);
        c->free_velocity=(Vec3){0,0,0};
        land_cart(c,fmaxf(0.f,p.y-next.y));
        if (c->occupied && !detonation_pending(c)) sense_cart(c,index,rail);
        detector_changed(tile);
        ++revision;
        return 1;
    }
    return 0;
}

static float step_free_axis(CartRecord *c,int index,int rider,int axis,float delta) {
    Vec3 before=c->position,next=before;
    if (axis==0) next.x+=delta;
    else if (axis==1) next.y+=delta;
    else next.z+=delta;
    if (!world_chunk_ready((int)floorf(next.x),(int)floorf(next.z))) return -1.f;
    if (!free_barrier(before,next,rider) && occupied(next,index)<0) {
        c->position=next;
        return 1.f;
    }
    /* Keep the largest collision-free portion, so a stopped cart rests
       against the actual block face rather than one simulation step away. */
    float low=0.f,high=1.f;
    for (int j=0;j<9;++j) {
        float f=.5f*(high+low);
        Vec3 q={before.x+(next.x-before.x)*f,before.y+(next.y-before.y)*f,before.z+(next.z-before.z)*f};
        if (free_barrier(before,q,rider) || occupied(q,index)>=0) high=f;
        else { low=f; c->position=q; }
    }
    return low;
}

static void drive_free(CartRecord *c,int index,int rider,float dt) {
    if (!(dt>0.f) || !world_chunk_ready((int)floorf(c->position.x),(int)floorf(c->position.z))) return;
    Vec3 start=c->position;
    Vec3 *v=&c->free_velocity;
    int riding=index==rider;
    if (c->kind==CART_TNT && c->fall_distance>0.f && cart_in_water(start)) c->fall_distance=0.f;
    if (catch_rail(c,index,riding,start)) return;
    int steps=(int)ceilf(fmaxf(1.f,dt*fmaxf(hypotf(v->x,v->z),fabsf(v->y)+16.f*dt)/.12f));
    const float tick=dt/steps;
    for (int i=0;i<steps;++i) {
        Vec3 before=c->position;
        Vec3 down=before;
        down.y-=.004f;
        int supported=v->y<=0.f && free_barrier(before,down,riding);
        if (supported && c->fall_distance>0.f) {
            land_cart(c,0.f);
            if (!c->occupied || detonation_pending(c)) return;
        }
        if (supported && !v->x && !v->z) { v->y=0.f; break; }
        float drag=powf(supported?.5f:.95f,20.f*tick);
        float x=v->x,z=v->z;
        v->y=fmaxf(-CART_FREE_VERTICAL_LIMIT,supported ? 0.f : v->y-16.f*tick);
        float moved_x=v->x!=0.f ? step_free_axis(c,index,riding,0,v->x*tick) : 1.f;
        float moved_z=v->z!=0.f ? step_free_axis(c,index,riding,2,v->z*tick) : 1.f;
        if (moved_x<0.f || moved_z<0.f) { v->y=0.f; break; }
        int hit_x=moved_x<1.f,hit_z=moved_z<1.f;
        float vy=v->y;
        int blocked_y=vy!=0.f && step_free_axis(c,index,riding,1,vy*tick)<1.f;
        if (blocked_y) v->y=0.f;
        Vec3 p=c->position;
        if (c->kind==CART_TNT && (vy<0.f || c->fall_distance>0.f)) {
            if (cart_in_water(p)) c->fall_distance=0.f;
            else if (p.y<before.y) c->fall_distance=fminf(WY,c->fall_distance+(before.y-p.y));
            if (blocked_y && vy<0.f) {
                land_cart(c,0.f);
                if (!c->occupied || detonation_pending(c)) return;
            }
        }
        if (p.x!=before.x || p.z!=before.z) c->heading=atan2f(x,-z);
        if (c->kind==CART_TNT && (hit_x || hit_z) && hypotf(x,z)>=2.f) {
            detonate(c,hypotf(x,z));
            return;
        }
        v->x=hit_x || fabsf(x*drag)<.016f ? 0.f : x*drag;
        v->z=hit_z || fabsf(z*drag)<.016f ? 0.f : z*drag;
        v->y*=drag;
        if (catch_rail(c,index,riding,before)) return;
    }
    if (c->position.x!=start.x || c->position.y!=start.y || c->position.z!=start.z) ++revision;
}

typedef struct { float x,z,speed; } ContactImpulse;

/* Resolve the collision against rail topology, not by moving either cart
   through the other. Apply queued impulses after all carts have stepped. */
static void shunt(const CartRecord *from,int receiver,int end,RailSample tangent,ContactImpulse *impulses) {
    const CartRecord *to=&carts[world_dimension()][receiver];
    if (to->off_track) return;
    if (!same_track(from->track,to->track)) {
        RailCell next;
        int entry;
        if (!rail_link(from->track,end,&next,&entry) || !same_track(next,to->track)) return;
    }
    const float speed=fabsf(from->speed);
    const float h=hypotf(tangent.dx,tangent.dz);
    const float dx=to->position.x-from->position.x,dz=to->position.z-from->position.z;
    const float gap=hypotf(dx,dz);
    if (h<.001f || gap<.001f || speed<.025f ||
        sign_of(from->speed)*(tangent.dx*dx+tangent.dz*dz)<.20f*h*gap) return;
    const float imparted=speed*(from->kind==CART_FURNACE ? .94f : .77f);
    ContactImpulse *out=&impulses[receiver];
    if (imparted>out->speed)
        *out=(ContactImpulse){dx/gap,dz/gap,imparted};
}
static void apply_impulses(Dimension d,const ContactImpulse *impulses) {
    for (int i=0;i<CART_CAPACITY;++i) {
        const ContactImpulse *in=&impulses[i];
        if (in->speed<=0.f) continue;
        CartRecord *cart=&carts[d][i];
        RailPiece rail;
        if (!cart->occupied || cart->off_track || detonation_pending(cart) ||
            !rail_at(cart->track,&rail)) continue;
        RailSample tangent=rail_sample(cart->track,rail,cart->along);
        const float h=hypotf(tangent.dx,tangent.dz);
        const float dot=tangent.dx*in->x+tangent.dz*in->z;
        if (h<.001f || fabsf(dot)<.24f*h) continue;
        const CartFurnaceDrive *motor=&furnace_drives[d][i];
        if (cart->kind==CART_FURNACE && motor->fuel && motor->x*in->x+motor->z*in->z<-.2f) continue;
        const float pushed=in->speed*dot/h;
        /* Speed alone is not visible. In particular, a powered cart
           pressed against a stopped one must not keep rebuilding meshes. */
        if (sign_of(pushed)*cart->speed<fabsf(pushed)) cart->speed=clamp(pushed,-8.f,8.f);
    }
}
static void drive(CartRecord *c,int index,int rider,float dt,float ix,float iz,ContactImpulse *impulses) {
    if (c->off_track) { drive_free(c,index,rider,dt); return; }
    RailPiece rail;
    if (!rail_at(c->track,&rail)) {
        /* Facing is updated whenever a rail cart moves, even if the old
           rail shape has since been deleted from the world. */
        float speed=fabsf(c->speed);
        derail(c,(Vec3){speed*sinf(c->heading),0.f,-speed*cosf(c->heading)});
        drive_free(c,index,rider,dt);
        return;
    }
    sense_cart(c,index,rail);
    RailSample s=rail_sample(c->track,rail,c->along);
    float horizontal=hypotf(s.dx,s.dz);
    float thrust=rider==index ? clamp((s.dx*ix+s.dz*iz)/horizontal,-1.f,1.f) : 0.f;
    int booster=rail.kind==RAIL_BOOSTER;
    int works=booster && rail_is_energized(c->track);
    float acceleration=thrust*4.6f-s.dy*3.f/tangent_length(s);
    CartFurnaceDrive *motor=c->kind==CART_FURNACE ? &furnace_drives[world_dimension()][index] : NULL;
    float motor_along=motor && motor->fuel ? (s.dx*motor->x+s.dz*motor->z)/horizontal : 0.f;
    if (fabsf(motor_along)>.05f) acceleration+=sign_of(motor_along)*21.5f;
    if (works) {
        if (fabsf(c->speed)>.06f || fabsf(thrust)>.05f) {
            acceleration+=sign_of(fabsf(c->speed)>.06f ? c->speed : thrust)*7.f;
        } else {
            for (int j=0;j<2;++j) {
                RailEnd e=rail.ends[j];
                if (solid(block(c->track.x+rail_dx(e.dir),c->track.y+e.up,c->track.z+rail_dz(e.dir)))) {
                    acceleration+=(j==0?7.f:-7.f);
                    break;
                }
            }
        }
    }
    const Cargo *contents=cargo_for(world_dimension(),index);
    float drag=motor ? (fabsf(motor_along)>.05f ? 5.1f : 1.2f) :
        contents ? .10f+.33f*contents->signal/15.f : .62f;
    float speed=clamp((c->speed+acceleration*dt)/(1.f+dt*(booster&&!works?7.f:drag)),-8.f,8.f);
    if (fabsf(speed)<.015f) speed=0;
    c->speed=speed;
    float distance=fabsf(speed)*dt;
    for (int step=0; step<12 && distance>.00005f; ++step) {
        s=rail_sample(c->track,rail,c->along);
        float stride=fminf(.115f,distance),unit=tangent_length(s);
        int end=c->speed>0 ? 1:0;
        float gap=fabsf((float)end-c->along)*unit;
        RailCell tile=c->track;
        RailPiece next=rail;
        float t;
        int link=0,entry=0;
        if (stride>gap) {
            if (!rail_link(c->track,end,&tile,&entry) || !rail_at(tile,&next)) {
                RailEnd edge=rail.ends[end];
                RailCell neighbor={c->track.x+rail_dx(edge.dir),c->track.y+edge.up,c->track.z+rail_dz(edge.dir)};
                if (!world_chunk_ready(neighbor.x,neighbor.z)) return;
                /* An incompatible neighboring rail (e.g. a closed turnout)
                   is not the same as a gap. Do not short-circuit topology. */
                if (rail_at(neighbor,&next)) { c->speed=0; break; }
                RailSample edge_pose=rail_sample(c->track,rail,(float)end);
                Vec3 p=position(edge_pose);
                float travel=fabsf(c->speed),dir=sign_of(c->speed),len=tangent_length(edge_pose);
                if (free_barrier(c->position,p,index==rider) || occupied(p,index)>=0) {
                    if (c->kind==CART_TNT && travel>=2.f) { cart_blast_hit(index); return; }
                    c->speed=0;
                    break;
                }
                c->position=p;
                if (gap>0.f) ++revision;
                derail(c,(Vec3){dir*travel*edge_pose.dx/len,dir*travel*edge_pose.dy/len,dir*travel*edge_pose.dz/len});
                drive_free(c,index,rider,(distance-gap)/travel);
                return;
            }
            t=(float)entry;
            distance-=gap;
            link=1;
        } else {
            t=clamp(c->along+sign_of(c->speed)*stride/unit,0,1);
            distance-=stride;
        }
        RailSample sample=rail_sample(tile,next,t);
        Vec3 p=position(sample);
        CartKind kind=(CartKind)c->kind;
        if (!cart_in_bounds(p)) { c->speed=0; break; }
        int barrier=world_move_blocked(cart_room(c->position,kind),cart_room(p,kind),SPACE_WATER|SPACE_LAVA);
        int contact=barrier ? -1 : occupied(p,index);
        if (barrier || contact>=0) {
            if (kind==CART_TNT && fabsf(c->speed)>=2.f) { cart_blast_hit(index); return; }
            if (!barrier) shunt(c,contact,end,s,impulses);
            c->speed=barrier ? 0.f : c->speed*.50f;
            break;
        }
        const int follows_motor=motor && motor->fuel && motor_along*c->speed>.0001f;
        if (link) c->speed=fabsf(c->speed)*(entry==0?1.f:-1.f);
        RailCell before=c->track;
        c->track=tile; c->along=t; c->position=p; rail=next;
        if (link) {
            sense_cart(c,index,next);
            if (contents && contents->signal) { detector_changed(before); detector_changed(tile); }
        }
        if (follows_motor) {
            float h=hypotf(sample.dx,sample.dz),sign=sign_of(c->speed);
            if (h>.001f) {
                motor->x=clamp(sign*sample.dx/h,-1.f,1.f);
                motor->z=clamp(sign*sample.dz/h,-1.f,1.f);
                motor_along=sign;
            }
        }
        c->heading=atan2f(sample.dx*sign_of(c->speed),-sample.dz*sign_of(c->speed));
        ++revision;
    }
}
int carts_step(float dt,int side,int front) {
    if (!(dt>0 && dt<=.1f)) return 0;
    if (!player_rules.health) cart_unseat();
    int rider=cart_rider();
    float a=clamp(mx+(float)side,-1,1),b=clamp(-my+(float)front,-1,1);
    float norm=fmaxf(1.f,hypotf(a,b));
    float ix=(cosf(yaw)*a+sinf(yaw)*b)/norm,iz=(sinf(yaw)*a-cosf(yaw)*b)/norm;
    Dimension d=world_dimension();
    CartRecord *bank=carts[d];
    ContactImpulse impulses[CART_CAPACITY]={{0.f,0.f,0.f}};
    for (int i=0;i<CART_CAPACITY;++i) if (bank[i].occupied) {
        if (detonation_pending(&bank[i])) continue;
        if (i!=rider && (fabsf(bank[i].position.x-px)>48.f || fabsf(bank[i].position.z-pz)>48.f)) {
            if (!bank[i].off_track) bank[i].speed=0;
            continue;
        }
        drive(&bank[i],i,rider,dt,ix,iz,impulses);
        if (i==rider) sync_rider(&bank[i]);
    }
    apply_impulses(d,impulses);
    return rider>=0;
}
