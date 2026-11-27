#include "blast.h"
#include "../world/build_blocks.h"
#include "forestry.h"
#include "actors.h"
#include "brewing.h"
#include "chests.h"
#include "droppers.h"
#include "drops.h"
#include "furnace.h"
#include "hoppers.h"
#include "horses.h"
#include "pistons.h"
#include "minecarts.h"
#include "../world/redstone.h"
#include "../world/space.h"
#include <math.h>
#include <string.h>

enum { RADIUS=7, CHARGED_RADIUS=11, EDGE=2*CHARGED_RADIUS+1, VOLUME=EDGE*EDGE*EDGE,
       EDIT_CAP=1024, FLASH_CAP=12, RAY_EDGE=16, RAY_COUNT=RAY_EDGE*RAY_EDGE*6-(RAY_EDGE-2)*12-16 };
enum { RAY_DESTROY=1, RAY_PRESERVE_TRACK=2 };
typedef struct {
    PrimedBlast charges[BLAST_CAP];
    BlastFlash flashes[FLASH_CAP];
    size_t count;
    unsigned phase;
} Bank;
typedef struct { int x,y,z,radius,edge,volume; uint16_t cell[VOLUME]; uint8_t selected[VOLUME]; } Grid;
static Bank banks[DIMENSION_COUNT];
static unsigned revision,rays_count;
static Vec3 rays[RAY_COUNT];
static Bank *bank(void) { return &banks[world_dimension()]; }
static int cart_blast(int kind) { return kind==BLAST_CART_TRACK || kind==BLAST_CART_IMPACT; }

void blasts_reset(void) { memset(banks,0,sizeof banks); ++revision; }
void blasts_apply_difficulty(void) {
    if (game_effective_difficulty()!=PEACEFUL) return;
    for (int d=0;d<DIMENSION_COUNT;++d) {
        Bank *b=&banks[d];
        size_t n=0;
        for (size_t i=0;i<b->count;++i) if (b->charges[i].kind==BLAST_TNT || cart_blast(b->charges[i].kind))
            b->charges[n++]=b->charges[i];
        if (n!=b->count) { b->count=n; ++revision; }
    }
}
unsigned blasts_revision(void) { return revision; }
const PrimedBlast *blasts_in(Dimension d,size_t *count) {
    if (count) *count=(unsigned)d<DIMENSION_COUNT ? banks[d].count : 0;
    return (unsigned)d<DIMENSION_COUNT ? banks[d].charges : NULL;
}
const BlastFlash *blast_flashes(Dimension d,size_t *count) {
    if (count) *count=(unsigned)d<DIMENSION_COUNT ? FLASH_CAP : 0;
    return (unsigned)d<DIMENSION_COUNT ? banks[d].flashes : NULL;
}
static int valid(PrimedBlast p) {
    return (p.kind==BLAST_TNT || cart_blast(p.kind)) &&
        (cart_blast(p.kind) ? p.ticks==0 && p.speed.x>=0.f && p.speed.x<=BLAST_CART_SPEED_LIMIT &&
            p.speed.y==0.f && p.speed.z==0.f : p.ticks<=TNT_FUSE &&
            fabsf(p.speed.x)<=16.f && fabsf(p.speed.z)<=16.f && fabsf(p.speed.y)<=20.f) &&
        fabsf(p.point.x)<WORLD_SAVE_LIMIT && fabsf(p.point.z)<WORLD_SAVE_LIMIT &&
        p.point.y>=0.f && p.point.y<=BLAST_TOP;
}
int blast_restore(Dimension d,PrimedBlast p) {
    if ((unsigned)d>=DIMENSION_COUNT ||
        banks[d].count>=BLAST_CAP || !valid(p)) return 0;
    banks[d].charges[banks[d].count++]=p; ++revision;
    return 1;
}
static unsigned mix(unsigned x) {
    x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; return x^(x>>16);
}
static PrimedBlast armed(int x,int y,int z,unsigned random,int fuse) {
    float a=(random&255u)*.0245436926f;
    return (PrimedBlast){{x+.5f,(float)y,z+.5f},{.40f*cosf(a),4.f,.40f*sinf(a)},(uint16_t)fuse,BLAST_TNT};
}
int blast_prime(int x,int y,int z) {
    if (bank()->count>=BLAST_CAP || !inworld(x,y,z) || !world_chunk_ready(x,z) ||
        world_cell(x,y,z)!=TNT_BLOCK) return 0;
    PrimedBlast p=armed(x,y,z,mix(hash2(x,z)^(unsigned)y^bank()->phase),TNT_FUSE);
    if (!valid(p) || !set_block(x,y,z,AIR)) return 0;
    return blast_restore(world_dimension(),p);
}
int blast_dispense(int x,int y,int z) {
    if (!inworld(x,y,z) || !world_chunk_ready(x,z) || solid(block(x,y,z))) return 0;
    return blast_restore(world_dimension(),armed(x,y,z,mix(hash2(x,z)^bank()->phase),TNT_FUSE));
}
int blast_queue_cart(Vec3 p,float speed,int protect_track) {
    /* A stationary entry carries the speed equivalent for blast power.
       A landing uses fall distance instead of horizontal approach speed. */
    return blast_restore(world_dimension(),(PrimedBlast){p,{fabsf(speed),0,0},0,
        (uint8_t)(protect_track ? BLAST_CART_TRACK : BLAST_CART_IMPACT)});
}
static int chunk_for(int x) { return x/CHUNK_SIZE-(x%CHUNK_SIZE<0); }
static int halo_ready(int x,int z,int radius) {
    int ready=1;
    for (int a=chunk_for(x-radius);a<=chunk_for(x+radius);++a)
        for (int b=chunk_for(z-radius);b<=chunk_for(z+radius);++b)
            ready&=world_chunk_ready(a*CHUNK_SIZE,b*CHUNK_SIZE)!=0;
    return ready;
}
static void prepare_rays(void) {
    if (rays_count) return;
    for (int x=0;x<RAY_EDGE;++x) for (int y=0;y<RAY_EDGE;++y) for (int z=0;z<RAY_EDGE;++z) {
        if (x && x<RAY_EDGE-1 && y && y<RAY_EDGE-1 && z && z<RAY_EDGE-1) continue;
        Vec3 d={2.f*x/(RAY_EDGE-1)-1.f,2.f*y/(RAY_EDGE-1)-1.f,2.f*z/(RAY_EDGE-1)-1.f};
        float step=.3f/sqrtf(d.x*d.x+d.y*d.y+d.z*d.z);
        rays[rays_count++]=(Vec3){d.x*step,d.y*step,d.z*step};
    }
}
static int index_at(const Grid *g,int x,int y,int z) {
    unsigned edge=(unsigned)g->edge;
    x-=g->x-g->radius; y-=g->y-g->radius; z-=g->z-g->radius;
    if ((unsigned)x>=edge || (unsigned)y>=edge || (unsigned)z>=edge) return -1;
    return (x*g->edge+y)*g->edge+z;
}
static BlockEdit grid_edit(const Grid *g,int i) {
    int e=g->edge,r=g->radius;
    return (BlockEdit){g->x-r+i/(e*e),g->y-r+(i/e)%e,g->z-r+i%e,AIR};
}
static int protected_cell(int raw) {
    return raw==BEDROCK || raw==OBSIDIAN || ender_chest_cell(raw) || raw==NETHER_PORTAL_X || raw==NETHER_PORTAL_Z ||
        aquifer_fixture(raw) || pressure_fixture(raw) || lava_strength(raw) || water_strength(raw);
}
static float resistance(int raw) {
    if (protected_cell(raw)) return 10000.f;
    int t=cell_type(raw);
    if (t==TNT_BLOCK) return 0.f;
    if (build_stair(raw) || build_slab(raw) || build_full(raw)) return build_wooden(raw) ? 3.f : 6.f;
    if (t==GLASS || t==LEAF || !solid(t)) return .3f;
    if (t==DIRT || t==GRASS || t==SAND || t==RED_SAND || t==GRAVEL || t==SNOW || t==CLAY) return .6f;
    if (t==WOOD || t==PLANKS || t==WOOL || is_chest(t) || t==BOOKSHELF) return 3.f;
    return 6.f;
}
static int protected_track_support(int x,int y,int z,int raw) {
    return rail_type(cell_type(raw)) || (y+1<WY && rail_type(block(x,y+1,z)));
}
static void mark_rays(Grid *g,Vec3 center,float power,unsigned seed,int kind) {
    prepare_rays();
    for (unsigned i=0;i<rays_count;++i) {
        Vec3 p=center,v=rays[i];
        float force=power*(.7f+(mix(seed^i)&1023u)*(.6f/1023.f));
        while (force>0.f) {
            int x=(int)floorf(p.x),y=(int)floorf(p.y),z=(int)floorf(p.z);
            int id=index_at(g,x,y,z);
            if (id<0) break;
            uint16_t *raw=&g->cell[id];
            if (*raw==UINT16_MAX) {
                int inside=inworld(x,y,z);
                *raw=(uint16_t)(inside ? world_cell(x,y,z) : BEDROCK);
                if (inside && *raw!=AIR && kind==BLAST_CART_TRACK && protected_track_support(x,y,z,*raw))
                    g->selected[id]=RAY_PRESERVE_TRACK;
            }
            if (*raw!=AIR) {
                if (g->selected[id]==RAY_PRESERVE_TRACK) force-=.09f;
                else {
                    force-=(resistance(*raw)+.3f)*.3f;
                    if (force>0.f) g->selected[id]=RAY_DESTROY;
                }
            }
            p.x+=v.x; p.y+=v.y; p.z+=v.z;
            force-=.225f;
        }
    }
}
static void keep_bed_pairs(Grid *g) {
    for (int i=0;i<g->volume;++i) if (g->selected[i]==RAY_DESTROY && is_bed_cell(g->cell[i])) {
        int code=g->cell[i]-BED_BASE,dir=code&3,sign=code&4 ? -1 : 1;
        BlockEdit e=grid_edit(g,i);
        int x=e.x+sign*rail_dx(dir),y=e.y,z=e.z+sign*rail_dz(dir);
        int mate=world_cell(x,y,z);
        if (mate!=BED_BASE+(code^4)) continue;
        int j=index_at(g,x,y,z);
        if (j<0) { g->selected[i]=0; continue; }
        g->selected[j]=RAY_DESTROY; g->cell[j]=(uint16_t)mate;
    }
}
static int visible(Vec3 a,Vec3 b) {
    float x=b.x-a.x,y=b.y-a.y,z=b.z-a.z;
    int n=1+(int)ceilf(sqrtf(x*x+y*y+z*z)/.26f);
    for (int i=1;i<n;++i) {
        float t=(float)i/n;
        if (world_point_blocked(a.x+t*x,a.y+t*y,a.z+t*z,0)) return 0;
    }
    return 1;
}
static int exposure(Vec3 center,BlockBox body,float range,float *impact,Vec3 *dir) {
    Vec3 mid={(body.x0+body.x1)*.5f,(body.y0+body.y1)*.5f,(body.z0+body.z1)*.5f};
    float x=mid.x-center.x,y=mid.y-center.y,z=mid.z-center.z,d=sqrtf(x*x+y*y+z*z);
    if (d>=range) return 0;
    int clear=0;
    for (int a=0;a<3;++a) for (int b=0;b<3;++b) for (int c=0;c<3;++c) {
        Vec3 q={body.x0+(body.x1-body.x0)*(.15f+.35f*a),body.y0+(body.y1-body.y0)*(.15f+.35f*b),
            body.z0+(body.z1-body.z0)*(.15f+.35f*c)};
        clear+=visible(center,q);
    }
    if (!clear) return 0;
    float amount=(1.f-d/range)*(clear/27.f);
    if (impact) *impact=amount;
    if (dir) *dir=d>.01f ? (Vec3){x/d,y/d,z/d} : (Vec3){0,1,0};
    return (int)((amount*amount+amount)*.5f*8.f*range+1.f);
}
static int container_contents(int raw,int x,int y,int z,ItemStack out[CHEST_SLOTS+2]) {
    int type=cell_type(raw),n=0,slots=0;
    const ItemStack *items=NULL;
    if (is_chest(type)) return chest_blast_contents((ChestPos){x,y,z},out);
    if (type==FURNACE) {
        const Furnace *f=furnace_at(x,y,z); if (f) { items=f->slots; slots=FURNACE_SLOTS; }
    } else if (type==BREWING_STAND) {
        const BrewingStand *b=brew_at(x,y,z); if (b) { items=b->slots; slots=BREW_SLOTS; }
    } else if (type==HOPPER) {
        const Hopper *h=hopper_at(x,y,z); if (h) { items=h->slots; slots=HOPPER_SLOTS; }
    } else if (ejector_type(type)) {
        const Dropper *d=dropper_at(x,y,z); if (d) { items=d->slots; slots=DROPPER_SLOTS; }
    }
    for (int i=0;i<slots;++i) if (items[i].count) out[n++]=items[i];
    return n;
}
static int harvestable(int raw) {
    if (button_cell(raw)) return raw>=BUTTON_WOOD ? WOOD_BUTTON_ITEM : STONE_BUTTON_ITEM;
    if (pressure_cell(raw)) return redstone_plate_item(raw);
    if (is_bed_cell(raw)) return ((raw-BED_BASE)&4) ? AIR : BED;
    if (door_cell(raw)) return door_upper(raw) ? AIR : door_item(raw);
    if (sapling_cell(raw)) return tree_sapling_drop(raw);
    return mining_drop(cell_type(raw),4);
}
static void flash(Bank *b,Vec3 center,int water) {
    int best=0;
    for (int i=1;i<FLASH_CAP;++i) if (b->flashes[i].ticks<b->flashes[best].ticks) best=i;
    b->flashes[best]=(BlastFlash){center,BLAST_FLASH_TICKS,(uint8_t)water};
}
static int explode(PrimedBlast p,size_t spent) {
    Bank *b=bank();
    Vec3 center={p.point.x,p.point.y+.46f,p.point.z};
    Grid g;
    int is_cart=cart_blast(p.kind);
    g.radius=is_cart && p.speed.x>16.f ?
        CHARGED_RADIUS : is_cart ? 9 : RADIUS;
    g.edge=2*g.radius+1; g.volume=g.edge*g.edge*g.edge;
    g.x=(int)floorf(center.x);g.y=(int)floorf(center.y);g.z=(int)floorf(center.z);
    if (!halo_ready(g.x,g.z,g.radius)) return 0;
    int wet=water_strength(world_cell(g.x,g.y,g.z))!=0;
    /* The saved position, not a transient session clock, fixes the crater. */
    unsigned seed=mix(hash2(g.x,g.z)^(unsigned)g.y^((unsigned)p.kind<<24));
    float power=p.kind==BLAST_TNT ? 4.f : 3.f;
    if (is_cart) power=4.f+1.5f*(float)(mix(seed^0xe37bu)&65535u)/65535.f*(p.speed.x/20.f);
    float range=2.f*power;
    memset(g.cell,255,g.volume*sizeof g.cell[0]);memset(g.selected,0,g.volume*sizeof g.selected[0]);
    if (!wet) { mark_rays(&g,center,power,seed,p.kind); keep_bed_pairs(&g); }
    int free_drop[MAXDROP],free_count=0;
    for (int i=0;i<MAXDROP;++i) if (!drops[i].item.count) free_drop[free_count++]=i;
    Drop staged[MAXDROP];
    BlockEdit edits[EDIT_CAP];
    uint16_t old[EDIT_CAP];
    PrimedBlast chain[BLAST_CAP];
    int n=0,n_drop=0,n_chain=0;
    for (int i=0;i<g.volume;++i) if (g.selected[i]==RAY_DESTROY) {
        int raw=g.cell[i];
        if (protected_cell(raw) || raw==AIR || n==EDIT_CAP) continue;
        BlockEdit edit=grid_edit(&g,i);
        int x=edit.x,y=edit.y,z=edit.z;
        CircuitPos peer;
        int expected;
        int paired;
        if (door_cell(raw)) {
            peer=(CircuitPos){x,y+(door_upper(raw) ? -1 : 1),z};
            expected=door_peer(raw); paired=1;
        } else paired=piston_peer((CircuitPos){x,y,z},raw,&peer,&expected);
        if (paired && world_cell(peer.x,peer.y,peer.z)==expected) {
            int j=index_at(&g,peer.x,peer.y,peer.z);
            /* Piston heads and door leaves cannot be split by a ray or edit cap. */
            if (j<0 || n+2>EDIT_CAP || (p.kind==BLAST_CART_TRACK &&
                protected_track_support(peer.x,peer.y,peer.z,expected))) continue;
            g.selected[j]=0;
            edits[n]=grid_edit(&g,j); old[n++]=(uint16_t)expected;
            edits[n]=edit; old[n++]=(uint16_t)raw;
            continue;
        }
        if (raw==TNT_BLOCK) {
            if (b->count-1+(size_t)n_chain>=BLAST_CAP) continue;
            unsigned rng=mix(seed^(unsigned)i);
            PrimedBlast fresh=armed(x,y,z,rng,CHAIN_FUSE_MIN+rng%(CHAIN_FUSE_MAX-CHAIN_FUSE_MIN+1));
            if (!valid(fresh)) continue;
            chain[n_chain++]=fresh;
        } else {
            ItemStack loot[CHEST_SLOTS+2];
            int items=container_contents(raw,x,y,z,loot);
            if (items>free_count-n_drop) continue;
            for (int j=0;j<items;++j) staged[n_drop++]=make_drop(x+.5f,y+.4f,z+.5f,loot[j]);
        }
        edits[n]=edit; old[n++]=(uint16_t)raw;
    }
    /* Reserve contents before allowing ordinary block drops to use capacity. */
    for (int i=0;i<n && n_drop<free_count;++i) {
        if ((old[i]==TNT_BLOCK) || mix(seed^(unsigned)(i+g.volume))%((unsigned)power)) continue;
        int item=harvestable(old[i]);
        BlockEdit e=edits[i];
        if (item!=AIR) staged[n_drop++]=make_drop(e.x+.5f,e.y+.4f,e.z+.5f,
            (ItemStack){(uint16_t)item,(uint8_t)(build_double(old[i]) ? 2 : old[i]==CLAY ? 4 : 1),0,0,0});
    }
    int actor_hits[ACTOR_CAPACITY]={0},horse_hits[HORSE_CAPACITY]={0};
    int cart_hits[CART_CAPACITY]={0};
    BlockBox box;
    for (int i=0;i<ACTOR_CAPACITY;++i) if (actors_body_at(world_dimension(),i,&box))
        actor_hits[i]=exposure(center,box,range,NULL,NULL);
    for (int i=0;i<HORSE_CAPACITY;++i) if (horse_body_at(world_dimension(),i,&box))
        horse_hits[i]=exposure(center,box,range,NULL,NULL);
    const CartRecord *carts=carts_in(world_dimension());
    for (int i=0;i<CART_CAPACITY;++i) if (carts[i].occupied && carts[i].kind!=CART_RIDEABLE) {
        Vec3 q=carts[i].position;
        cart_hits[i]=exposure(center,body_box(q.x,q.y,q.z,.38f,.76f),range,NULL,NULL);
    }
    float impact=0.f;
    Vec3 direction={0,0,0};
    int damage=player_rules.health ? exposure(center,body_box(px,py,pz,.28f,1.72f),range,&impact,&direction) : 0;
    Vec3 kicks[BLAST_CAP]={0};
    for (size_t i=0;i<b->count;++i) if (i!=spent && b->charges[i].kind==BLAST_TNT) {
        Vec3 q=b->charges[i].point;
        q.y+=.48f;
        float x=q.x-center.x,y=q.y-center.y,z=q.z-center.z,d=sqrtf(x*x+y*y+z*z);
        if (d>=range || !visible(center,q)) continue;
        float k=20.f*(1.f-d/range);
        if (d>.01f) kicks[i]=(Vec3){k*x/d,k*y/d,k*z/d};
        else kicks[i].y=k;
    }
    if (n && !world_set_blocks(edits,n)) return 0;
    for (int i=0;i<n;++i) {
        BlockEdit e=edits[i];
        if (is_chest(cell_type(old[i]))) chest_blast_forget((ChestPos){e.x,e.y,e.z});
        else if (old[i]==FURNACE) furnace_forget(e.x,e.y,e.z,NULL);
    }
    for (size_t i=0;i<b->count;++i) if (i!=spent && b->charges[i].kind==BLAST_TNT) {
        Vec3 v=b->charges[i].speed,k=kicks[i];
        b->charges[i].speed=(Vec3){fminf(16.f,fmaxf(-16.f,v.x+k.x)),
            fminf(20.f,fmaxf(-20.f,v.y+k.y)),fminf(16.f,fmaxf(-16.f,v.z+k.z))};
    }
    b->charges[spent]=b->charges[--b->count];
    for (int i=0;i<n_chain;++i) b->charges[b->count++]=chain[i];
    for (int i=0;i<n_drop;++i) drops[free_drop[i]]=staged[i];
    drops_dirty|=n_drop!=0;
    flash(b,center,wet);
    actors_explosion_hits(actor_hits);
    horses_explosion_hits(horse_hits);
    for (int i=0;i<CART_CAPACITY;++i) if (cart_hits[i]) cart_blast_hit(i);
    if (damage) {
        if (p.kind==BLAST_TNT || is_cart) player_take_damage(DAMAGE_EXPLOSION,damage);
        else player_take_mob_damage(DAMAGE_EXPLOSION,damage);
        player_blast_push(direction,impact);
    }
    return 1;
}
static int tnt_solid(Vec3 p) { return world_box_blocked(body_box(p.x,p.y,p.z,.47f,.96f),0); }
static int move_tnt(PrimedBlast *p) {
    if (!world_chunk_ready((int)floorf(p->point.x),(int)floorf(p->point.z))) return 0;
    Vec3 q=p->point, v=p->speed;
    v.y=fmaxf(-19.5f,v.y-.8f);
    q.x+=v.x*.05f;
    if (tnt_solid(q)) { q.x=p->point.x; v.x*=-.4f; }
    q.z+=v.z*.05f;
    if (tnt_solid(q)) { q.z=p->point.z; v.z*=-.4f; }
    q.y+=v.y*.05f;
    if (q.y>BLAST_TOP || tnt_solid(q)) { q.y=p->point.y; v.y*=-.5f; v.x*=.7f;v.z*=.7f; }
    p->speed=(Vec3){v.x*.98f,v.y*.98f,v.z*.98f}; p->point=q;
    return 1;
}
void blasts_step(unsigned ticks) {
    Bank *b=bank();
    for (unsigned tick=0;tick<ticks;++tick) {
        ++b->phase;
        for (int f=0;f<FLASH_CAP;++f) if (b->flashes[f].ticks) { --b->flashes[f].ticks; ++revision; }
        /* A new chain ignition starts ticking on the next game tick.
           Bursts queue due blasts instead of stalling one frame. */
        unsigned budget=2;
        for (size_t i=b->count;i-->0;) {
            PrimedBlast *p=&b->charges[i];
            if (p->ticks) {
                if (p->kind==BLAST_TNT && !move_tnt(p)) continue;
                --p->ticks; ++revision;
            }
            if (p->point.y<0.f || fabsf(p->point.x)>=WORLD_SAVE_LIMIT || fabsf(p->point.z)>=WORLD_SAVE_LIMIT) {
                b->charges[i]=b->charges[--b->count]; ++revision; continue;
            }
            if (!p->ticks && budget && explode(*p,i)) { --budget; ++revision; }
        }
    }
}
