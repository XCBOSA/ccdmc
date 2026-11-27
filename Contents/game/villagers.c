#include "villagers.h"
#include "actors.h"
#include "boats.h"
#include "horses.h"
#include "item_custom.h"
#include "minecarts.h"
#include "village_routes.h"
#include "weather.h"
#include "../world/space.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { VILLAGE_BANKS=257, SCAN_INTERVAL=20, RESIDENT_RADIUS=38 };
typedef struct VillageResidents {
    VillagePlan plan;
    VillagerSave homes[VILLAGE_HOMES];
    uint16_t names[VILLAGE_HOMES];
    int slots[VILLAGE_HOMES];
    unsigned dirty;
    struct VillageResidents *next;
} VillageResidents;
typedef struct { VillageResidents *village; int home,door_x,door_z,door_dir; } ActorBinding;
typedef struct {
    VillageStep path[VILLAGE_ROUTE_STEPS];
    int target_x,target_z;
    unsigned char intent,count,cursor,has_target;
    float retry;
} ResidentRoute;
typedef struct { int x,y,z,opened; unsigned hold; } DoorLease;
static VillageResidents *bank[VILLAGE_BANKS];
static ActorBinding bindings[ACTOR_CAPACITY];
static VillagerSave roaming[ACTOR_CAPACITY];
static ResidentRoute routes[ACTOR_CAPACITY];
static DoorLease door_leases[ACTOR_CAPACITY];
static unsigned scan_delay,route_budget;
static int speaking=-1;
static VillagerSession session_id;
enum { WALK_IN=1, WALK_OUT, DOOR_HOLD=8, ROUTE_SPACING=4 };

/* The ledger keys are independent of recyclable actor slots. Unmodified
   residents are reconstructed from the generated home and don't occupy saves. */
static VillageResidents *residents(const VillagePlan *p,int add) {
    unsigned bucket=hash2(p->x,p->z)%VILLAGE_BANKS;
    for (VillageResidents *v=bank[bucket];v;v=v->next)
        if (v->plan.x==p->x && v->plan.z==p->z) return v;
    if (!add) return NULL;
    VillageResidents *v=(VillageResidents *)calloc(1,sizeof *v);
    if (!v) return NULL;
    v->plan=*p;
    for (int i=0;i<VILLAGE_HOMES;++i) {
        v->homes[i]=(VillagerSave){p->x,p->floor,p->z,(uint8_t)i,VILLAGER_HEALTH,1,{0},0};
        v->slots[i]=-1;
    }
    v->next=bank[bucket]; bank[bucket]=v;
    return v;
}
const char *villagers_role_name(unsigned role) {
    static const char *const names[]={"苗床匠","地层测绘员","潮陶匠","工坊装配师","行旅补给员"};
    return role<VILLAGE_HOMES ? names[role] : "居民";
}
static int stock(const VillagerSave *v,int index) {
    return 6+(int)(hash2(v->x+5*v->home+9*index,v->z-11*index)%4u);
}
static ItemStack stack(int type,int count) { return (ItemStack){(uint16_t)type,(uint8_t)count,0,0,0}; }
typedef enum { WORKSHOP_INLAND, WORKSHOP_WET, WORKSHOP_DRY, WORKSHOP_REGIONS } WorkshopRegion;
static WorkshopRegion workshop_region(const VillagerSave *v) {
    switch (terrain_biome(v->x,v->z)) {
    case BIOME_DEEP_OCEAN: case BIOME_OCEAN: case BIOME_RIVER: case BIOME_FROZEN_RIVER:
    case BIOME_BEACH: case BIOME_STONE_SHORE: case BIOME_SWAMP: case BIOME_JOINT_COAST:
        return WORKSHOP_WET;
    case BIOME_DESERT: case BIOME_SAVANNA: case BIOME_MESA:
        return WORKSHOP_DRY;
    default:
        return WORKSHOP_INLAND;
    }
}
typedef struct {
    const char *name;
    uint16_t first,second,result;
    uint8_t first_count,second_count,yield;
} WorkshopOrder;
static VillagerOffer quote(const VillagerSave *v,int index) {
    /* Two general orders, then a specialty set by the shop's origin climate,
       not the camera, a random roll, or whichever chunk is loaded. */
    static const WorkshopOrder basic[VILLAGE_HOMES][VILLAGER_OFFERS-1]={
        {{"营地干粮",WHEAT,SUGAR,FRUIT_RATION,5,2,3},
         {"扩建苗床",WHEAT_SEEDS,CLAY_BALL,FILTER_MOSS,6,3,2}},
        {{"起步探具",COBBLESTONE,STICK,SURVEY_TAPPER,6,3,1},
         {"抛光层纹片",SLATE_ITEM,GLASS_PANE_ITEM,STRIATED_GLASS,2,3,4}},
        {{"窑火烧砖",CLAY_BALL,COAL,BRICK_ITEM,6,2,6},
         {"备好压坯模",LIMESTONE_ITEM,BRICK_ITEM,SETTLING_MOLD_ITEM,3,3,1}},
        {{"压制密封垫",CLAY_BALL,LEATHER,PUMP_GASKET,4,2,5},
         {"成批接管",SLATE_ITEM,IRON_INGOT,AIR_PIPE_ITEM,3,1,6}},
        {{"途中热食",RAW_FISH,COAL,COOKED_FISH,4,1,4},
         {"缝制行旅水囊",LEATHER,STRING,FIELD_FLASK_EMPTY,2,3,1}}
    };
    static const WorkshopOrder specialty[VILLAGE_HOMES][WORKSHOP_REGIONS]={
        {{"陆地雨水储备",FILTER_MOSS,GLASS_BOTTLE,CISTERN_ITEM,2,2,1},
         {"潮湿苗床补光",WHEAT,CLAY_BALL,SEEP_TILE_ITEM,6,5,3},
         {"干路集水罐",BRICK_ITEM,GLASS_PANE_ITEM,CISTERN_ITEM,5,3,1}},
        {{"洞口声标",SLATE_ITEM,SHELL_FLAKE,RESONANCE_MARKER_ITEM,3,1,4},
         {"水路测量",SHELL_FLAKE,STRIATED_GLASS,TIDE_GUIDE,2,2,1},
         {"矿阶夜间导引",LIMESTONE_ITEM,STRIATED_GLASS,AQUIFER_LIGHT_ITEM,4,2,3}},
        {{"水边亮砖底座",SLATE_ITEM,LIMESTONE_ITEM,SEEP_BED_ITEM,2,5,1},
         {"落水筛组装",TIDE_GRIT,STRIATED_GLASS,FALL_SIEVE_ITEM,3,2,1},
         {"保温工台",TERRACOTTA,IRON_INGOT,HEAT_BENCH_ITEM,5,2,1}},
        {{"护营备件",LAMINATE_ITEM,PUMP_GASKET,CRAWLER_KIT,3,2,1},
         {"潮槽重装",TIDE_GRIT,STRIATED_GLASS,HYDRAULIC_SORTER_ITEM,4,3,1},
         {"长途气路",SINTERED_TILE_ITEM,PUMP_GASKET,AIR_STATION_ITEM,3,2,1}},
        {{"山路口粮",APPLE,WHEAT,FRUIT_RATION,4,4,5},
         {"潮路清水补给",LEATHER,PUMP_GASKET,FIELD_FLASK_TWO,2,1,1},
         {"热路隔热补给",LIMESTONE_ITEM,WATER_BOTTLE,POTION_FIRE,3,2,2}}
    };
    const WorkshopOrder *o=index<VILLAGER_OFFERS-1 ? &basic[v->home][index] :
        &specialty[v->home][workshop_region(v)];
    return (VillagerOffer){{stack(o->first,o->first_count),stack(o->second,o->second_count)},
        stack(o->result,o->yield),stock(v,index)-v->uses[index],o->name};
}
int villagers_visit(Dimension d,VillagerVisitor visit,void *ctx) {
    if ((unsigned)d>=DIMENSION_COUNT || !visit) return 0;
    if (d!=OVERWORLD) return 1;
    for (int h=0;h<VILLAGE_BANKS;++h) for (VillageResidents *v=bank[h];v;v=v->next)
        for (int i=0;i<VILLAGE_HOMES;++i) if ((v->dirty&(1u<<i)) && !visit(&v->homes[i],ctx)) return 0;
    return 1;
}
int villagers_market_valid(VillagerSave s) {
    if (s.x<=-WORLD_SAVE_LIMIT || s.x>=WORLD_SAVE_LIMIT || s.z<=-WORLD_SAVE_LIMIT ||
        s.z>=WORLD_SAVE_LIMIT || s.y<0 || s.y>=WY || s.home>=VILLAGE_HOMES || s.health>VILLAGER_HEALTH ||
        !s.unlocked || s.unlocked>VILLAGER_OFFERS || s.restock>VILLAGER_RESTOCK_TICKS) return 0;
    for (int i=0;i<VILLAGER_OFFERS;++i)
        if (s.uses[i]>stock(&s,i) || (i>=s.unlocked && s.uses[i])) return 0;
    return 1;
}
static int origin_plan(VillagerSave s,VillagePlan *plan) {
    return village_plan_for(s.x,s.z,plan) && plan->x==s.x && plan->z==s.z && plan->floor==s.y;
}
int villagers_restore_named(VillagerSave s,uint16_t name) {
    if (world_dimension()!=OVERWORLD || !villagers_market_valid(s) ||
        (name && (!s.health || !*item_name_for_id(name)))) return 0;
    VillagePlan plan;
    if (!origin_plan(s,&plan)) return 0;
    VillageResidents *v=residents(&plan,1);
    if (!v || (v->dirty&(1u<<s.home))) return 0;
    v->homes[s.home]=s;
    v->names[s.home]=name;
    v->dirty|=1u<<s.home;
    return 1;
}
int villagers_restore(VillagerSave s) { return villagers_restore_named(s,0); }
uint16_t villagers_resident_name(VillagerSave s) {
    if (s.home>=VILLAGE_HOMES) return 0;
    unsigned bucket=hash2(s.x,s.z)%VILLAGE_BANKS;
    for (const VillageResidents *v=bank[bucket];v;v=v->next)
        if (v->plan.x==s.x && v->plan.z==s.z && v->plan.floor==s.y) return v->names[s.home];
    return 0;
}
void villagers_set_actor_name(int slot,uint16_t name) {
    if ((unsigned)slot>=ACTOR_CAPACITY) return;
    ActorBinding b=bindings[slot];
    if (!b.village || !b.village->homes[b.home].health) return;
    b.village->names[b.home]=name;
    b.village->dirty|=1u<<b.home;
}

void villagers_actor_removed(int slot) {
    if ((unsigned)slot>=ACTOR_CAPACITY) return;
    ActorBinding bound=bindings[slot];
    if (bound.village) bound.village->slots[bound.home]=-1;
    bindings[slot]=(ActorBinding){0};
    roaming[slot]=(VillagerSave){0};
    routes[slot]=(ResidentRoute){0};
    if (speaking==slot) speaking=-1;
}
void villagers_clear_actors(void) {
    for (int i=0;i<ACTOR_CAPACITY;++i) villagers_actor_removed(i);
    memset(door_leases,0,sizeof door_leases);
    speaking=-1; scan_delay=route_budget=0;
}
void villagers_reset(void) {
    villagers_clear_actors();
    for (int h=0;h<VILLAGE_BANKS;++h) while (bank[h]) {
        VillageResidents *v=bank[h]; bank[h]=v->next; free(v);
    }
}
void villagers_hurt(int slot,unsigned health) {
    if ((unsigned)slot>=ACTOR_CAPACITY || health>VILLAGER_HEALTH) return;
    ActorBinding b=bindings[slot];
    if (!b.village) { if (roaming[slot].unlocked) roaming[slot].health=(uint8_t)health; return; }
    if (health<b.village->homes[b.home].health) routes[slot]=(ResidentRoute){0};
    b.village->homes[b.home].health=(uint8_t)health;
    if (!health) b.village->names[b.home]=0;
    b.village->dirty|=1u<<b.home;
}
int villagers_roaming_market(int slot,VillagerSave *out) {
    if ((unsigned)slot>=ACTOR_CAPACITY || !roaming[slot].unlocked) return 0;
    if (out) *out=roaming[slot];
    return 1;
}
int villagers_bind_roaming(int slot,VillagerSave market) {
    if ((unsigned)slot>=ACTOR_CAPACITY || bindings[slot].village || roaming[slot].unlocked ||
        !market.health || !villagers_market_valid(market)) return 0;
    roaming[slot]=market;
    return 1;
}
int villagers_mark_stranded(int slot,VillagerOrigin *out) {
    if ((unsigned)slot>=ACTOR_CAPACITY || !out) return 0;
    ActorBinding b=bindings[slot];
    if (b.village) *out=(VillagerOrigin){b.village->homes[b.home],1};
    else if (roaming[slot].unlocked) *out=(VillagerOrigin){roaming[slot],0};
    else return 0;
    out->market.health=0;
    villagers_hurt(slot,0);
    villagers_actor_removed(slot);
    return 1;
}
int villagers_origin_valid(VillagerOrigin source) {
    if (source.linked>1 || source.market.health || !villagers_market_valid(source.market)) return 0;
    if (!source.linked) return 1;
    VillagerSave key=source.market;
    unsigned bucket=hash2(key.x,key.z)%VILLAGE_BANKS;
    for (const VillageResidents *v=bank[bucket];v;v=v->next)
        if (v->plan.x==key.x && v->plan.z==key.z && v->plan.floor==key.y)
            return (v->dirty&(1u<<key.home)) && !v->homes[key.home].health && v->slots[key.home]<0;
    return 0;
}
int villagers_rejoin(int slot,VillagerOrigin source) {
    if ((unsigned)slot>=ACTOR_CAPACITY || !villagers_origin_valid(source)) return 0;
    VillagerSave healed=source.market;
    healed.health=VILLAGER_HEALTH;
    if (!source.linked) return villagers_bind_roaming(slot,healed);
    VillagePlan p;
    int x,z,dir;
    if (!origin_plan(healed,&p) || !village_home_door(&p,healed.home,&x,&z,&dir)) return 0;
    VillageResidents *v=residents(&p,0);
    if (!v) return 0;
    v->homes[healed.home].health=VILLAGER_HEALTH;
    v->slots[healed.home]=slot;
    v->dirty|=1u<<healed.home;
    bindings[slot]=(ActorBinding){v,healed.home,x,z,dir};
    return 1;
}
int villagers_can_walk(int slot,Vec3 to) {
    if ((unsigned)slot>=ACTOR_CAPACITY) return 0;
    if (!bindings[slot].village) return slot!=speaking && roaming[slot].health;
    const VillageResidents *v=bindings[slot].village;
    return slot!=speaking && fabsf(to.x-(v->plan.x+.5f))<=VILLAGE_WALK_RADIUS && fabsf(to.z-(v->plan.z+.5f))<=VILLAGE_WALK_RADIUS;
}

static VillageWalk walking_area(ActorBinding b) {
    return (VillageWalk){b.village->plan.x,b.village->plan.z,b.village->plan.floor+1,b.door_x,b.door_z};
}
static float remain(Vec3 p,float x,float z) { return fabsf(p.x-x)+fabsf(p.z-z); }
static Vec3 route_waypoint(const VillageWalk *w,int x,int z,Vec3 p) {
    Vec3 next={x+.5f,p.y,z+.5f};
    int raw;
    if (world_resident_cell(x,w->y,z,&raw) && door_cell(raw)) {
        /* A .60-wide resident only has .0125 clearance beside an open
           leaf. Centre across the frame before advancing along its axis. */
        if (door_bits(raw)&1) {
            if (fabsf(p.z-next.z)>.006f) next.x=p.x;
        } else if (fabsf(p.x-next.x)>.006f) next.z=p.z;
    }
    return next;
}
static int choose_stand(ActorBinding b,const VillageWalk *w,int intent,int *x,int *z) {
    static const int interior[][2]={{-2,0},{-2,1},{-2,-1},{-1,0},{-1,1},{-1,-1}};
    static const int exterior[][2]={{3,0},{2,0},{2,1},{2,-1},{4,0}};
    int side=(b.door_dir+1)&3;
    const int (*candidates)[2]=intent==WALK_IN ? interior : exterior;
    int n=intent==WALK_IN ? (int)(sizeof interior/sizeof *interior) : (int)(sizeof exterior/sizeof *exterior);
    for (int i=0;i<n;++i) {
        int normal=candidates[i][0],across=candidates[i][1];
        int tx=b.door_x+normal*rail_dx(b.door_dir)+across*rail_dx(side);
        int tz=b.door_z+normal*rail_dz(b.door_dir)+across*rail_dz(side);
        if (abs(tx-w->center_x)>VILLAGE_WALK_RADIUS || abs(tz-w->center_z)>VILLAGE_WALK_RADIUS ||
            (intent==WALK_IN && !village_home_contains(&b.village->plan,b.home,tx,tz)) || !village_route_cell(w,tx,tz)) continue;
        *x=tx; *z=tz;
        return 1;
    }
    return 0;
}
int villagers_navigate(int slot,Vec3 p,float dt,Vec3 *goal) {
    if ((unsigned)slot>=ACTOR_CAPACITY || !bindings[slot].village || !goal) return VILLAGER_WANDER;
    if (slot==speaking) return VILLAGER_WAIT;
    ActorBinding b=bindings[slot];
    VillageWalk w=walking_area(b);
    ResidentRoute *r=&routes[slot];
    int x=(int)floorf(p.x),z=(int)floorf(p.z);
    if (fabsf(p.y-w.y)>.35f) { *r=(ResidentRoute){0}; return VILLAGER_WANDER; }
    int indoors=village_home_contains(&b.village->plan,b.home,x,z);
    int shelter=player_rules.day_tick>=12000 ||
        (weather_kind()!=WEATHER_CLEAR && weather_climate(b.village->plan.style,w.y)!=PRECIP_NONE);
    int intent=shelter ? WALK_IN : indoors || r->intent==WALK_OUT ? WALK_OUT : 0;
    if (intent!=r->intent) { *r=(ResidentRoute){0}; r->intent=(unsigned char)intent; }
    if (!intent) return VILLAGER_WANDER;
    if (r->retry>0.f) { r->retry=fmaxf(0.f,r->retry-dt); return VILLAGER_WAIT; }
    if (!r->has_target || !village_route_cell(&w,r->target_x,r->target_z)) {
        r->count=r->cursor=0;
        r->has_target=(unsigned char)choose_stand(b,&w,intent,&r->target_x,&r->target_z);
        if (!r->has_target) { r->retry=1.f; return VILLAGER_WAIT; }
    }
    int tx=r->target_x,tz=r->target_z;
    if (remain(p,tx+.5f,tz+.5f)<.09f) {
        r->count=r->cursor=0;
        if (shelter) return VILLAGER_WAIT;
        r->intent=0;
        return VILLAGER_WANDER;
    }
    for (;r->cursor<r->count;++r->cursor) {
        VillageStep node=r->path[r->cursor];
        int nx=w.center_x+node.x,nz=w.center_z+node.z;
        if (remain(p,nx+.5f,nz+.5f)<.09f) continue;
        if (!village_route_cell(&w,nx,nz)) { villagers_path_blocked(slot); return VILLAGER_WAIT; }
        *goal=route_waypoint(&w,nx,nz,p);
        return VILLAGER_ADVANCE;
    }
    if (x==tx && z==tz && village_route_cell(&w,x,z)) {
        *goal=(Vec3){tx+.5f,p.y,tz+.5f};
        return VILLAGER_ADVANCE;
    }
    if (route_budget) return VILLAGER_WAIT;
    route_budget=ROUTE_SPACING;
    r->cursor=0;
    r->count=(unsigned char)village_route(&w,x,z,tx,tz,r->path);
    if (!r->count) { r->retry=1.f; return VILLAGER_WAIT; }
    *goal=route_waypoint(&w,w.center_x+r->path[0].x,w.center_z+r->path[0].z,p);
    return VILLAGER_ADVANCE;
}
void villagers_path_blocked(int slot) {
    if ((unsigned)slot>=ACTOR_CAPACITY) return;
    routes[slot].count=routes[slot].cursor=0;
    routes[slot].retry=.5f;
}
void villagers_door_changed(int x,int y,int z) {
    for (int i=0;i<ACTOR_CAPACITY;++i) {
        DoorLease *l=&door_leases[i];
        if (l->opened && l->x==x && l->y==y && l->z==z) l->opened=0;
    }
}
int villagers_open_path_door(int slot,Vec3 next,float radius,float height) {
    if ((unsigned)slot>=ACTOR_CAPACITY || slot==speaking || !bindings[slot].village) return 0;
    ActorBinding b=bindings[slot];
    int x=b.door_x,y=b.village->plan.floor+1,z=b.door_z,raw,upper;
    if (!world_resident_cell(x,y,z,&raw) || !door_cell(raw) ||
        (door_bits(raw)&(DOOR_IRON|DOOR_POWERED|DOOR_OPEN|DOOR_UPPER)) ||
        !world_resident_cell(x,y+1,z,&upper) || upper!=door_peer(raw)) return 0;
    BlockBox leaf[BARRIER_PARTS];
    if (!barrier_world_boxes(x,y,z,raw,BARRIER_BODY,leaf)) return 0;
    leaf[0].y1+=1.f;
    DoorLease *lease=NULL;
    for (int i=0;i<ACTOR_CAPACITY;++i) if (!door_leases[i].opened) { lease=&door_leases[i]; break; }
    if (!lease) return 0;
    if (!box_overlap(leaf[0],body_box(next.x,next.y,next.z,radius,height)) || !door_toggle(x,y,z)) return 0;
    *lease=(DoorLease){x,y,z,raw+DOOR_OPEN,DOOR_HOLD};
    return 1;
}
static int occupied(int x,int y,int z) {
    BlockBox doorway={(float)x,(float)y,(float)z,x+1.f,y+2.f,z+1.f};
    if (box_overlap(doorway,body_box(px,py,pz,.28f,1.72f))) return 1;
    for (int h=0;h<2;++h) if (actors_overlap_cell(x,y+h,z) || horse_overlaps_cell(x,y+h,z) ||
        boat_overlaps_cell(x,y+h,z) || cart_overlaps_cell(x,y+h,z)) return 1;
    return 0;
}
static void close_used_doors(unsigned elapsed) {
    for (int i=0;i<ACTOR_CAPACITY;++i) {
        DoorLease *l=&door_leases[i];
        if (!l->opened) continue;
        int raw;
        if (!world_resident_cell(l->x,l->y,l->z,&raw) || raw!=l->opened) { l->opened=0; continue; }
        if (l->hold>elapsed) { l->hold-=elapsed; continue; }
        l->hold=3;
        if (!occupied(l->x,l->y,l->z) && door_toggle(l->x,l->y,l->z)) l->opened=0;
    }
}

static int reachable(int slot) {
    if (world_dimension()!=OVERWORLD || !player_rules.health || (unsigned)slot>=ACTOR_CAPACITY ||
        (!bindings[slot].village && !roaming[slot].health)) return 0;
    const ActorRecord *a=&actors_in(OVERWORLD)[slot];
    float x=a->feet.x-px,y=a->feet.y-py,z=a->feet.z-pz;
    return a->type==ACTOR_VILLAGER && x*x+y*y+z*z<=25.f;
}
static VillagerSave *current(VillagerSession s) {
    if (!s || s!=session_id || !reachable(speaking)) return NULL;
    ActorBinding bound=bindings[speaking];
    return bound.village ? &bound.village->homes[bound.home] : &roaming[speaking];
}
VillagerSession villagers_open(int actor) {
    if (!reachable(actor)) return 0;
    if (++session_id==0) ++session_id;
    speaking=actor;
    return session_id;
}
void villagers_close(VillagerSession s) { if (s && s==session_id) speaking=-1; }
int villagers_count(VillagerSession s) { VillagerSave *v=current(s); return v ? v->unlocked : 0; }
int villagers_role(VillagerSession s) { VillagerSave *v=current(s); return v ? v->home : -1; }
const char *villagers_workshop_region(VillagerSession s) {
    static const char *const names[]={"内陆", "潮岸与湿地", "干热地带"};
    VillagerSave *v=current(s);
    return v ? names[workshop_region(v)] : "";
}
int villagers_handoffs_needed(VillagerSession s) {
    VillagerSave *v=current(s);
    if (!v || v->unlocked>=VILLAGER_OFFERS) return 0;
    const int left=WORKSHOP_HANDOFFS-v->uses[v->unlocked-1];
    return left>0 ? left : 1; /* Legacy saves may already have two or more handoffs. */
}
int villagers_offer(VillagerSession s,int index,VillagerOffer *out) {
    VillagerSave *v=current(s);
    if (!v || !out || (unsigned)index>=v->unlocked) return 0;
    *out=quote(v,index);
    return 1;
}

static int charge(ItemStack cost) {
    unsigned left=cost.count;
    for (int i=0;i<SLOT_COUNT && left;++i) {
        ItemStack *item=&inventory[i];
        if (!item->count || !item_same_kind(*item,cost)) continue;
        unsigned n=item->count<left ? item->count : left;
        left-=n; item->count-=(uint8_t)n;
        if (!item->count) *item=stack(AIR,0);
    }
    return left==0;
}
const char *villagers_exchange(VillagerSession s,int index,int *succeeded) {
    if (succeeded) *succeeded=0;
    VillagerSave *v=current(s);
    if (!v || (unsigned)index>=v->unlocked) return "居民已离开，或这项订单尚未解锁";
    if (player_rules.mode!=SURVIVAL) return "切换到生存模式才能交付订单";
    VillagerOffer o=quote(v,index);
    if (!o.remaining) return "暂缺货。先返回游戏，让工坊补货";
    ItemStack backup[SLOT_COUNT];
    memcpy(backup,inventory,sizeof backup);
    if (!charge(o.cost[0]) || !charge(o.cost[1])) {
        memcpy(inventory,backup,sizeof backup);
        return "供货材料不足，未扣除材料";
    }
    if (!inventory_add_stack(o.result)) {
        memcpy(inventory,backup,sizeof backup);
        return "背包空间不足，未扣除材料";
    }
    ++v->uses[index];
    const int advance=index==v->unlocked-1 && v->unlocked<VILLAGER_OFFERS &&
        v->uses[index]>=WORKSHOP_HANDOFFS;
    if (advance) ++v->unlocked;
    if (v->uses[index]>=stock(v,index) && !v->restock) v->restock=VILLAGER_RESTOCK_TICKS;
    ActorBinding bound=bindings[speaking];
    if (bound.village) bound.village->dirty|=1u<<bound.home;
    if (succeeded) *succeeded=1;
    return advance ? "供货完成，已解锁下一项订单" : "供货完成";
}

static void populate(VillagePlan plan) {
    VillageResidents *v=residents(&plan,1);
    if (!v) return;
    for (int i=0;i<VILLAGE_HOMES;++i) if (v->slots[i]<0 && v->homes[i].health) {
        int ex,ez,dx,dz,dir,body,head,floor;
        if (!village_home_entry(&plan,i,&ex,&ez) || !village_home_door(&plan,i,&dx,&dz,&dir)) continue;
        for (int n=0;n<9;++n) {
            int x=ex+(n ? (n-1)%3-1 : 0),z=ez+(n ? (n-1)/3-1 : 0);
            if (!world_resident_cell(x,plan.floor,z,&floor) || !solid(cell_type(floor)) ||
                !world_resident_cell(x,plan.floor+1,z,&body) || body!=AIR ||
                !world_resident_cell(x,plan.floor+2,z,&head) || head!=AIR) continue;
            int slot=actors_spawn_villager((Vec3){x+.5f,plan.floor+1.01f,z+.5f},i,v->homes[i].health);
            if (slot>=0) {
                v->slots[i]=slot;
                bindings[slot]=(ActorBinding){v,i,dx,dz,dir};
                if (v->names[i]) actors_set_name(OVERWORLD,slot,v->names[i]);
            }
            break;
        }
    }
}
void villagers_step(unsigned elapsed) {
    if (!elapsed || world_dimension()!=OVERWORLD) return;
    route_budget=route_budget>elapsed ? route_budget-elapsed : 0;
    close_used_doors(elapsed);
    if (speaking>=0 && !reachable(speaking)) speaking=-1;
    for (int i=0;i<ACTOR_CAPACITY;++i) if (i!=speaking && (bindings[i].village || roaming[i].health)) {
        ActorBinding bound=bindings[i];
        VillagerSave *v=bound.village ? &bound.village->homes[bound.home] : &roaming[i];
        if (!v->restock) for (int n=0;n<v->unlocked;++n) {
            if (v->uses[n]<stock(v,n)) continue;
            /* Preserve even old ledgers that saved a sold-out earlier order
               without the now-independent replenishment timer. */
            v->restock=VILLAGER_RESTOCK_TICKS;
            break;
        }
        if (!v->restock) continue;
        v->restock=v->restock>elapsed ? (uint8_t)(v->restock-elapsed) : 0;
        if (!v->restock) for (int n=0;n<v->unlocked;++n) if (v->uses[n]>=stock(v,n)) {
            /* A legacy latest row may be exhausted without being unlocked.
               Refill it without losing its completed handoff progress. */
            v->uses[n]=(uint8_t)(n==v->unlocked-1 && v->unlocked<VILLAGER_OFFERS ? WORKSHOP_HANDOFFS-1 : 0);
        }
    }
    if (world_generation()<GENERATOR_VILLAGES) return;
    if (scan_delay>elapsed) { scan_delay-=elapsed; return; }
    scan_delay=SCAN_INTERVAL;
    if (!(fabsf(px)<WORLD_SAVE_LIMIT && fabsf(pz)<WORLD_SAVE_LIMIT && py>0 && py<WY)) return;
    int x=(int)floorf(px),z=(int)floorf(pz);
    /* Planned centres are at least 40 blocks from region boundaries, so
       one region lookup covers every villager that can spawn within 38. */
    VillagePlan plan;
    if (village_plan_for(x,z,&plan) && abs(plan.x-x)<=RESIDENT_RADIUS &&
        abs(plan.z-z)<=RESIDENT_RADIUS && fabsf(plan.floor+1.f-py)<=24.f)
        populate(plan);
}
