#include "actors.h"
#include "sound.h"
#include "button_arrows.h"
#include "armor.h"
#include "blast.h"
#include "brewing.h"
#include "boats.h"
#include "chests.h"
#include "fire.h"
#include "furnace.h"
#include "heat_bench.h"
#include "weather.h"
#include "pistons.h"
#include "villagers.h"
#include "drops.h"
#include "dungeon.h"
#include "experience.h"
#include "enchanting.h"
#include "horses.h"
#include "item_custom.h"
#include "minecarts.h"
#include "../world/space.h"
#include "../world/light.h"
#include "survey.h"
#include "seep.h"
#include "silt_bed.h"
#include "golems.h"
#include "portals.h"
#include "../world/aquifer_layout.h"
#include "../world/pressure_layout.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    float radius, height, walk;
    int life, damage, drop;
    unsigned dimensions,lava_safe;
} Species;
static const Species species[ACTOR_TYPES] = {
    {0,0,0,0,0,0,0,0}, /* ACTOR_EMPTY */
    {.43f,.9f,1.1f,10,0,RAW_PORK,1u<<OVERWORLD,0}, /* ACTOR_PIG */
    {.31f,1.9f,2.1f,20,3,ROTTEN_FLESH,1u<<OVERWORLD,0}, /* ACTOR_ZOMBIE */
    {.61f,.40f,1.42f,TIDERAY_HEALTH,0,AIR,(1u<<OVERWORLD)|(1u<<END),0},
    {REED_RADIUS,REED_HEIGHT,0.f,REED_HEALTH,5,AIR,1u<<NETHER,1},
    {.27f,.72f,1.6f,4,0,FEATHER,1u<<OVERWORLD,0},
    {.67f,.88f,3.0f,16,2,STRING,1u<<OVERWORLD,0},
    {.46f,1.29f,1.13f,8,0,WOOL,1u<<OVERWORLD,0},
    {.30f,1.85f,1.85f,20,3,BONE,1u<<OVERWORLD,0},
    {.47f,1.38f,1.15f,10,0,RAW_BEEF,1u<<OVERWORLD,0},
    {.43f,.68f,.84f,SILTBACK_HEALTH,0,AIR,1u<<OVERWORLD,0},
    {0,0,0,0,0,0,0,0}, /* Retired hot-region type: save decoding only. */
    {SHELLBACK_RADIUS,SHELLBACK_HEIGHT,.94f,SHELLBACK_HEALTH,0,AIR,
        (1u<<OVERWORLD)|(1u<<NETHER)|(1u<<END),0}, /* ACTOR_SHELLBACK */
    {.30f,1.90f,1.29f,VILLAGER_HEALTH,0,AIR,1u<<OVERWORLD,0},
    {GOLEM_RADIUS,GOLEM_HEIGHT,1.82f,GOLEM_HEALTH,0,IRON_INGOT,(1u<<DIMENSION_COUNT)-1u,0},
    {SNOW_GOLEM_RADIUS,SNOW_GOLEM_HEIGHT,0.f,SNOW_GOLEM_HEALTH,0,STRIATED_GLASS,(1u<<DIMENSION_COUNT)-1u,0},
    {.31f,.85f,2.85f,WOLF_TAMED_HEALTH,0,AIR,(1u<<DIMENSION_COUNT)-1u,0},
    {.295f,.70f,2.4f,CAT_HEALTH,0,AIR,(1u<<DIMENSION_COUNT)-1u,0},
    {.43f,.87f,1.28f,SQUID_HEALTH,0,INK_SAC,1u<<OVERWORLD,0},
    {.31f,1.10f,0.f,STRANDED_HEALTH,0,AIR,1u<<OVERWORLD,0}, /* ACTOR_STRANDED_GUIDE */
    {.43f,.37f,.59f,8,0,AIR,1u<<OVERWORLD,0}, /* ACTOR_TIDE_CRAB */
    {FAULTBACK_RADIUS,FAULTBACK_HEIGHT,0.f,FAULTBACK_HEALTH,5,SINTERED_TILE_ITEM,1u<<NETHER,1}, /* ACTOR_FAULTBACK */
    {.34f,.49f,3.2f,12,2,STRING,1u<<OVERWORLD,0}, /* ACTOR_CAVE_SPIDER */
    {LANTERN_MOTH_RADIUS,LANTERN_MOTH_HEIGHT,0.f,LANTERN_MOTH_HEALTH,0,AIR,(1u<<OVERWORLD)|(1u<<NETHER),0},
    {.40f,.51f,.87f,KILN_MOLE_HEALTH,0,AIR,(1u<<OVERWORLD)|(1u<<NETHER),0},
    {.33f,.42f,1.08f,SEEP_BEETLE_HEALTH,0,AIR,(1u<<DIMENSION_COUNT)-1u,0},
};
static int spider_type(int type) { return type==ACTOR_SPIDER || type==ACTOR_CAVE_SPIDER; }
typedef struct {
    float think, attack, flee, fall, burn, grazing, lure_check, fuse, flame, flame_damage;
    uint32_t random;
    unsigned char angry, lure_visible;
    int mate, courting;
    ActorArmorState armor;
    int target, commanded, perch_x, perch_y, perch_z;
    float target_scan, dampened, anger, rally, order;
    float shell_deposition, perch_scan, perch_time;
    float swim_phase, swim_vertical, dry;
    unsigned char perch, resting, crab_moving, mole_warm, mole_found;
    int door_x,door_y,door_z,door_raw;
    float door_elapsed,door_pause;
    VillagerOrigin origin;
    uint16_t weakness,rest_ticks;
    float vocal_delay;
    float ram_time;
    uint8_t ram_phase;
    Vec3 flight;
} Motion;
typedef struct { Vec3 anchor; float quiet, startled; int active; } FishLure;
static ActorRecord banks[DIMENSION_COUNT][ACTOR_CAPACITY];
/* An evacuation replaces Motion, but the individual keeps its name. */
static uint16_t name_ids[DIMENSION_COUNT][ACTOR_CAPACITY];
static ActorCare care_banks[DIMENSION_COUNT][ACTOR_CAPACITY];
static LeadLink ties[DIMENSION_COUNT][ACTOR_CAPACITY];
static Motion motions[DIMENSION_COUNT][ACTOR_CAPACITY];
static float care_fraction[DIMENSION_COUNT];
static unsigned court_ticks[DIMENSION_COUNT];
static float spawn_time[DIMENSION_COUNT];
static uint32_t spawn_rng[DIMENSION_COUNT];
static uint32_t silt_rng,wolf_rng,cat_rng,squid_rng,shell_rng,tidal_rng;
static unsigned contact_cursor;
static FishLure fish_lure[DIMENSION_COUNT];
static ActorFireball fireballs[DIMENSION_COUNT][ACTOR_FIREBALLS];
static unsigned revision;
static const char *affliction_note;
enum { STRIKE_MELEE, STRIKE_ARROW, STRIKE_ENVIRONMENT, STRIKE_BLAST, STRIKE_FIRE, STRIKE_MOB, STRIKE_ZOMBIE,
    STRIKE_TRAP_ARROW };
enum { PROJECTILE_PLAYER=-1, PROJECTILE_MACHINE=-2 };
enum { DOOR_BREAK_SECONDS=12, DOOR_CRACK_STAGES=10 };
static const char *hurt_actor(int index,int damage,int strike,int *applied);
static void wolves_assist(Dimension d,int victim);
const char *actors_take_affliction_note(void) {
    const char *note=affliction_note;
    affliction_note=NULL;
    return note;
}

unsigned actors_revision(void) { return revision; }
float actors_golem_swing(Dimension d,int index) {
    if ((unsigned)d>=DIMENSION_COUNT || (unsigned)index>=ACTOR_CAPACITY) return 0.f;
    int type=banks[d][index].type;
    float cooldown=type==ACTOR_IRON_GOLEM ? .72f : type==ACTOR_SNOW_GOLEM ? 2.2f : 0.f;
    return cooldown ? fminf(1.f,fmaxf(0.f,(motions[d][index].attack-cooldown+.30f)*4.f)) : 0.f;
}
static unsigned crack_stage(float elapsed) {
    return elapsed>0.f ? 1+(unsigned)fminf(DOOR_CRACK_STAGES-1,elapsed*(DOOR_CRACK_STAGES/(float)DOOR_BREAK_SECONDS)) : 0;
}
static unsigned door_swing_phase(float elapsed) { return (unsigned)(elapsed*12.f)%6u; }
ActorDoorAttack actors_door_attack(Dimension d,int index) {
    if ((unsigned)d>=DIMENSION_COUNT || (unsigned)index>=ACTOR_CAPACITY ||
        !actors_is_zombie(banks[d][index].type)) return (ActorDoorAttack){0};
    const Motion *m=&motions[d][index];
    if (!m->door_raw) return (ActorDoorAttack){0};
    static const float swing[]={0.f,.7f,1.f,.45f,.18f,0.f};
    return (ActorDoorAttack){m->door_x,m->door_y,m->door_z,crack_stage(m->door_elapsed),
        swing[door_swing_phase(m->door_elapsed)]};
}
float actors_squid_pulse(Dimension d,int i) {
    if ((unsigned)d>=DIMENSION_COUNT || (unsigned)i>=ACTOR_CAPACITY ||
        (banks[d][i].type!=ACTOR_SQUID && banks[d][i].type!=ACTOR_TIDERAY)) return 0.f;
    return .5f+.5f*sinf(motions[d][i].swim_phase);
}
int actors_on_fire(Dimension d,int index) {
    return (unsigned)d<DIMENSION_COUNT && (unsigned)index<ACTOR_CAPACITY && banks[d][index].type &&
        motions[d][index].flame>0.f;
}
const ActorRecord *actors_in(Dimension dim) {
    return (unsigned)dim < DIMENSION_COUNT ? banks[dim] : NULL;
}
const ActorCare *actors_care_in(Dimension dim) {
    return (unsigned)dim < DIMENSION_COUNT ? care_banks[dim] : NULL;
}
LeadLink *actors_lead_at(Dimension d,int slot) {
    return (unsigned)d<DIMENSION_COUNT && (unsigned)slot<ACTOR_CAPACITY ? &ties[d][slot] : NULL;
}
static int feed_type(int type) {
    return actors_is_bovine(type) || type==ACTOR_SHEEP ? WHEAT : type==ACTOR_PIG ? CARROT :
        type==ACTOR_CHICKEN ? WHEAT_SEEDS : AIR;
}
int actors_is_livestock(int type) { return feed_type(type)!=AIR; }
static int pet_species(int type) { return type==ACTOR_WOLF || type==ACTOR_OCELOT; }
static int pet_tamed(ActorRecord a) { return pet_species(a.type) && (a.traits&PET_TAMED); }
static int pet_sitting(ActorRecord a) { return pet_species(a.type) && (a.traits&PET_SITTING); }
int actors_has_care(int type) { return actors_is_livestock(type) || pet_species(type) || type==ACTOR_TIDE_CRAB; }
static int tamed_wolf(ActorRecord a) { return a.type==ACTOR_WOLF && pet_tamed(a); }
static int can_lead(ActorRecord a) { return actors_is_livestock(a.type) || pet_tamed(a); }
int actors_cat_sitting(Dimension d,int i) {
    return (unsigned)d<DIMENSION_COUNT && (unsigned)i<ACTOR_CAPACITY &&
        banks[d][i].type==ACTOR_OCELOT && (pet_sitting(banks[d][i]) || motions[d][i].resting);
}
int actors_wolf_angry(Dimension d,int i) {
    return (unsigned)d<DIMENSION_COUNT && (unsigned)i<ACTOR_CAPACITY &&
        banks[d][i].type==ACTOR_WOLF && motions[d][i].anger>0.f;
}
const ActorFireball *actors_fireballs(void) { return fireballs[world_dimension()]; }
void actors_reset(void) {
    dungeon_reset();
    villagers_clear_actors();
    memset(banks,0,sizeof banks);
    memset(name_ids,0,sizeof name_ids);
    memset(motions,0,sizeof motions);
    memset(care_banks,0,sizeof care_banks);
    memset(ties,0,sizeof ties);
    memset(care_fraction,0,sizeof care_fraction);
    memset(court_ticks,0,sizeof court_ticks);
    memset(spawn_time,0,sizeof spawn_time);
    contact_cursor=0;
    memset(fireballs,0,sizeof fireballs);
    memset(fish_lure,0,sizeof fish_lure);
    affliction_note=NULL;
    for (int d = 0; d < DIMENSION_COUNT; ++d) spawn_rng[d] = hash2(4731,4127+d) | 1u;
    silt_rng=(hash2(845,1819)^world_seed_mix32())|1u;
    wolf_rng=hash2(78041,-441)^world_seed_mix32();
    cat_rng=hash2(-3109,80131)^world_seed_mix32();
    squid_rng=hash2(-907,49799)^world_seed_mix32();
    shell_rng=(hash2(1029,5399)^world_seed_mix32())|1u;
    tidal_rng=(hash2(7031,-204)^world_seed_mix32())|1u;
    if (!wolf_rng) wolf_rng=1u;
    if (!cat_rng) cat_rng=1u;
    if (!squid_rng) squid_rng=1u;
    ++revision;
}

static uint16_t egg_interval(unsigned random) {
    return (uint16_t)(CHICKEN_EGG_MIN_TICKS+random%(CHICKEN_EGG_MAX_TICKS-CHICKEN_EGG_MIN_TICKS+1u));
}
static int valid_traits(ActorRecord r) {
    if (r.type==ACTOR_SHEEP) return r.traits<=SHEEP_SHORN;
    if (r.type==ACTOR_SHELLBACK) return r.traits<=SHELLBACK_RIPE;
    if (r.type==ACTOR_REEDLIZARD) return r.traits<=REED_HARVESTED;
    if (r.type==ACTOR_VILLAGER) return (r.traits&~ROAMING_VILLAGER)<VILLAGE_HOMES;
    if (r.type==ACTOR_IRON_GOLEM) return r.traits<=GOLEM_PLAYER_BUILT;
    if (r.type==ACTOR_WOLF) return !r.traits ? r.health<=WOLF_WILD_HEALTH :
        r.traits==WOLF_TAMED || r.traits==(WOLF_TAMED|WOLF_SITTING);
    if (r.type==ACTOR_OCELOT) return !r.traits ||
        ((r.traits&PET_TAMED) && (r.traits&CAT_COAT_MASK) && r.traits<=(CAT_COAT_MASK|PET_TAMED|PET_SITTING));
    if (actors_can_wear_armor(r.type)) return armor_kit_valid(r.traits);
    return r.traits==0;
}
static int valid_care(ActorRecord r, ActorCare c) {
    if (r.type==ACTOR_TIDE_CRAB)
        return !c.grow && !c.love && !c.egg && !c.managed && c.cooldown<=TIDE_CRAB_WORK_TICKS;
    if (pet_species(r.type))
        return !c.egg && c.managed==pet_tamed(r) && c.grow<=ANIMAL_GROW_TICKS &&
            c.cooldown<=ANIMAL_COOLDOWN_TICKS && c.love<=ANIMAL_LOVE_TICKS &&
            (!(c.cooldown || c.love || (r.type==ACTOR_WOLF && c.grow)) || c.managed) &&
            !(c.grow && (c.cooldown || c.love)) && !(c.cooldown && c.love);
    if (!actors_is_livestock(r.type)) return !c.grow && !c.cooldown && !c.love && !c.egg && !c.managed;
    return c.managed<=1 && c.grow<=ANIMAL_GROW_TICKS && c.cooldown<=ANIMAL_COOLDOWN_TICKS &&
        c.love<=ANIMAL_LOVE_TICKS && (!c.egg || (r.type==ACTOR_CHICKEN && !c.grow && c.egg<=CHICKEN_EGG_MAX_TICKS)) &&
        (!(c.grow || c.cooldown || c.love) || c.managed) &&
        !(c.grow && (c.cooldown || c.love || r.traits)) && !(c.love && c.cooldown);
}
ActorArmorState actors_armor_state(Dimension d,int i) {
    return (unsigned)d<DIMENSION_COUNT && (unsigned)i<ACTOR_CAPACITY ? motions[d][i].armor : (ActorArmorState){0};
}
static int valid_armor(ActorRecord r,ActorArmorState state) {
    if (state.door_breaker && (!actors_is_zombie(r.type) || state.door_breaker!=1)) return 0;
    if (!actors_can_wear_armor(r.type)) return !state.helmet_wear && !state.health_debt;
    const ItemDef *head=armor_item_def(armor_kit_item(r.traits,ARMOR_HEAD));
    return state.health_debt<HEALTH_UNITS && (!state.helmet_wear || (head && state.helmet_wear<head->durability));
}
int actors_restore_snapshot(Dimension dim,ActorRecord r,ActorCare care,LeadLink lead,ActorArmorState armor) {
    if ((unsigned)dim >= DIMENSION_COUNT || !r.type || r.type >= ACTOR_TYPES ||
        !(species[r.type].dimensions & (1u<<dim)) ||
        r.health == 0 || r.health > species[r.type].life ||
        !valid_traits(r) || !valid_armor(r,armor) ||
        !(fabsf(r.heading) <= 3.142f) ||
        !(fabsf(r.feet.x) < WORLD_SAVE_LIMIT && fabsf(r.feet.z) < WORLD_SAVE_LIMIT &&
          r.feet.y >= 0 && r.feet.y < WY) || !valid_care(r,care) || !lead_valid(lead) ||
        (lead.kind && (!can_lead(r) || !care.managed)) ||
        (pet_species(r.type) && !pet_tamed(r) && dim!=OVERWORLD)) return 0;
    for (int i = 0; i < ACTOR_CAPACITY; ++i) if (!banks[dim][i].type) {
        banks[dim][i] = r;
        name_ids[dim][i]=0;
        unsigned random=hash2((int)floorf(r.feet.x)+i,(int)floorf(r.feet.z))|1u;
        if (r.type==ACTOR_CHICKEN && !care.grow && !care.egg) care.egg=egg_interval(random);
        care_banks[dim][i]=care;
        lead_set(&ties[dim][i],lead);
        motions[dim][i] = (Motion){.random=random,.mate=-1,.armor=armor,.target=-1,.commanded=-1};
        if (r.type==ACTOR_STRANDED_GUIDE) {
            VillagerSave market={(int)floorf(r.feet.x),(int)floorf(r.feet.y),(int)floorf(r.feet.z),
                (uint8_t)(random%VILLAGE_HOMES),0,1,{0},0};
            motions[dim][i].origin=(VillagerOrigin){market,0};
        }
        ++revision;
        return 1;
    }
    return 0;
}
static int free_actor(Dimension d) {
    if ((unsigned)d>=DIMENSION_COUNT) return -1;
    for (int i=0;i<ACTOR_CAPACITY;++i) if (!banks[d][i].type) return i;
    return -1;
}
StrandedGuideState actors_stranded_state(Dimension d,int i) {
    if ((unsigned)d>=DIMENSION_COUNT || (unsigned)i>=ACTOR_CAPACITY || banks[d][i].type!=ACTOR_STRANDED_GUIDE)
        return (StrandedGuideState){0};
    const Motion *m=&motions[d][i];
    return (StrandedGuideState){m->origin,m->rest_ticks};
}
int actors_restore_stranded(Dimension d,ActorRecord a,StrandedGuideState state) {
    int slot=free_actor(d);
    if (d!=OVERWORLD || a.type!=ACTOR_STRANDED_GUIDE || slot<0 ||
        state.origin.linked>1 || state.origin.market.health || !villagers_market_valid(state.origin.market) ||
        state.rest_ticks>GUIDE_REST_TICKS) return 0;
    if (!actors_restore(d,a)) return 0;
    Motion *m=&motions[d][slot];
    m->origin=state.origin; m->rest_ticks=state.rest_ticks;
    return 1;
}
int actors_restore_roaming(Dimension d,ActorRecord a,VillagerSave market) {
    int slot=free_actor(d);
    if (d!=OVERWORLD || a.type!=ACTOR_VILLAGER || a.traits!=(ROAMING_VILLAGER|market.home) ||
        !market.health || a.health!=market.health || slot<0 || !villagers_market_valid(market)) return 0;
    if (!actors_restore(d,a)) return 0;
    if (villagers_bind_roaming(slot,market)) return 1;
    banks[d][slot].type=ACTOR_EMPTY;
    return 0;
}
int actors_validate_stranded_origins(void) {
    for (int i=0;i<ACTOR_CAPACITY;++i) if (banks[OVERWORLD][i].type==ACTOR_STRANDED_GUIDE) {
        VillagerOrigin origin=motions[OVERWORLD][i].origin;
        if (!villagers_origin_valid(origin)) return 0;
        if (!origin.linked) continue;
        for (int j=0;j<i;++j) if (banks[OVERWORLD][j].type==ACTOR_STRANDED_GUIDE) {
            VillagerOrigin other=motions[OVERWORLD][j].origin;
            if (other.linked && other.market.home==origin.market.home &&
                other.market.x==origin.market.x && other.market.z==origin.market.z) return 0;
        }
    }
    return 1;
}
int actors_restore_with_lead(Dimension dim,ActorRecord r,ActorCare care,LeadLink lead) {
    return actors_restore_snapshot(dim,r,care,lead,(ActorArmorState){0});
}
int actors_restore_with_care(Dimension dim,ActorRecord r,ActorCare care) {
    return actors_restore_with_lead(dim,r,care,(LeadLink){LEAD_FREE,0,0,0});
}
int actors_restore(Dimension dim,ActorRecord r) {
    return actors_restore_with_care(dim,r,(ActorCare){0,0,0,0,(uint8_t)pet_tamed(r)});
}
int actors_spawn_villager(Vec3 point,int role,int health) {
    if (world_dimension()!=OVERWORLD || (unsigned)role>=VILLAGE_HOMES || health<1 || health>VILLAGER_HEALTH) return -1;
    for (int i=0;i<ACTOR_CAPACITY;++i) if (!banks[OVERWORLD][i].type)
        return actors_restore(OVERWORLD,(ActorRecord){point,0,ACTOR_VILLAGER,(uint8_t)health,(uint8_t)role}) ? i : -1;
    return -1;
}

static unsigned next(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *state = x;
}
/* Spawns and dry landing searches avoid fluids. Movement must not treat
   water as support; it applies liquid drag and buoyancy in step_actor. */
static int obstacle(const Species *s, Vec3 p) {
    return world_box_blocked(body_box(p.x,p.y,p.z,s->radius,s->height),SPACE_WATER|(s->lava_safe ? 0 : SPACE_LAVA));
}
static int motion_blocked(const Species *s,Vec3 p) {
    return world_box_blocked(body_box(p.x,p.y,p.z,s->radius,s->height),s->lava_safe ? 0 : SPACE_LAVA);
}
static int move_obstacle(const Species *s,Vec3 prev,Vec3 next) {
    return world_move_blocked(body_box(prev.x,prev.y,prev.z,s->radius,s->height),
        body_box(next.x,next.y,next.z,s->radius,s->height),s->lava_safe ? 0 : SPACE_LAVA);
}
static float distance2(Vec3 p, Vec3 q) {
    float dx = p.x-q.x, dy = p.y-q.y, dz = p.z-q.z;
    return dx*dx+dy*dy+dz*dz;
}
static Species body_for(int type,int juvenile,int traits) {
    Species body=species[type];
    if (type==ACTOR_WOLF && (traits&WOLF_SITTING)) body.height=.72f;
    if (type==ACTOR_OCELOT && (traits&PET_SITTING)) body.height=.58f;
    if (juvenile) {
        body.radius*=.5f; body.height*=.5f; body.walk*=1.22f;
    }
    return body;
}
static int clear_ray_kind(Vec3 from,Vec3 to,int physical) {
    int n = (int)ceilf(sqrtf(distance2(from,to)) * 8.f);
    for (int i = 1; i < n; ++i) {
        float t = (float)i/n;
        if (world_point_blocked(from.x+(to.x-from.x)*t,from.y+(to.y-from.y)*t,
            from.z+(to.z-from.z)*t,physical)) return 0;
    }
    return 1;
}
static int clear_ray(Vec3 from,Vec3 to) { return clear_ray_kind(from,to,0); }
int actors_mole_warm(Dimension d,int slot) {
    return (unsigned)d<DIMENSION_COUNT && (unsigned)slot<ACTOR_CAPACITY &&
        banks[d][slot].type==ACTOR_KILN_MOLE && motions[d][slot].mole_warm;
}
static int drop_slot(void) {
    for (int i=0;i<MAXDROP;++i) if (!drops[i].item.count) return i;
    return -1;
}
static int animal_near(Vec3 p,Vec3 player) { return distance2(p,player)<=44.f*44.f; }

/* Children need a supported, open cell even when an egg breaks on a wall.
   Do not search a different storey or move births through a solid block. */
static int spawn_child(int type,Vec3 center,uint8_t traits) {
    Dimension d=world_dimension();
    if (!actors_has_care(type) || (!pet_species(type) && d!=OVERWORLD)) return 0;
    Species juvenile=body_for(type,1,0);
    const float offsets[][2]={{0,0},{.38f,0},{-.38f,0},{0,.38f},{0,-.38f}};
    for (unsigned i=0;i<sizeof offsets/sizeof *offsets;++i) {
        float x=center.x+offsets[i][0],z=center.z+offsets[i][1];
        if (fabsf(x)>=WORLD_SAVE_LIMIT || fabsf(z)>=WORLD_SAVE_LIMIT) continue;
        int bx=(int)floorf(x),bz=(int)floorf(z),base=(int)floorf(center.y+.01f);
        for (int y=base+1;y>=base-1;--y) {
            if (y<=0 || y>=WY || !solid(block(bx,y-1,bz))) continue;
            Vec3 child={x,y+.01f,z};
            if (distance2(child,(Vec3){px,py,pz})<.50f*.50f || obstacle(&juvenile,child) ||
                !clear_ray_kind(center,(Vec3){child.x,child.y+juvenile.height*.5f,child.z},1)) continue;
            ActorRecord record={child,0,(uint8_t)type,(uint8_t)species[type].life,traits};
            return actors_restore_with_care(d,record,(ActorCare){ANIMAL_GROW_TICKS,0,0,0,1});
        }
    }
    return 0;
}

static void update_care(Dimension d,unsigned elapsed,Vec3 player) {
    if (!elapsed) return;
    ActorRecord *list=banks[d];
    ActorCare *care=care_banks[d];
    for (int i=0;i<ACTOR_CAPACITY;++i) {
        if (!actors_has_care(list[i].type) || !animal_near(list[i].feet,player)) continue;
        ActorCare *c=&care[i];
        if (c->grow) {
            c->grow=c->grow>elapsed ? (uint16_t)(c->grow-elapsed) : 0;
            if (!c->grow) {
                if (list[i].type==ACTOR_CHICKEN) c->egg=egg_interval(next(&motions[d][i].random));
                ++revision;
            }
            continue;
        }
        c->cooldown=c->cooldown>elapsed ? (uint16_t)(c->cooldown-elapsed) : 0;
        if (c->love) {
            c->love=c->love>elapsed ? (uint16_t)(c->love-elapsed) : 0;
            if (!c->love) { motions[d][i].mate=-1; motions[d][i].courting=0; ++revision; }
        }
        if (list[i].type!=ACTOR_CHICKEN) continue;
        if (c->egg>elapsed) { c->egg=(uint16_t)(c->egg-elapsed); continue; }
        int slot=drop_slot();
        if (slot<0) { c->egg=1; continue; }
        Vec3 feet=list[i].feet;
        drops[slot]=make_drop(feet.x,feet.y+.22f,feet.z,(ItemStack){EGG,1,0,0,0});
        drops_dirty=1;
        c->egg=egg_interval(next(&motions[d][i].random));
    }
}

/* A 5-tick pairing pass keeps mate search and occlusion checks off the
   per-frame loop. Reserve each pair once, and never skip the safe birth test. */
static void breed_near(Dimension d,Vec3 player) {
    ActorRecord *list=banks[d];
    ActorCare *care=care_banks[d];
    Motion *motion=motions[d];
    int chosen[ACTOR_CAPACITY],active[ACTOR_CAPACITY];
    for (int i=0;i<ACTOR_CAPACITY;++i) {
        chosen[i]=-1;
        active[i]=list[i].type && care[i].love && animal_near(list[i].feet,player) &&
            motion[i].flee<=0 && !pet_sitting(list[i]);
    }
    for (int i=0;i<ACTOR_CAPACITY;++i) if (active[i] && chosen[i]<0) {
        int mate=-1;
        float limit=8.f*8.f;
        for (int j=i+1;j<ACTOR_CAPACITY;++j) if (active[j] && chosen[j]<0 && list[j].type==list[i].type) {
            Vec3 a=list[i].feet,b=list[j].feet;
            float distance=distance2(a,b);
            if (distance>=limit || fabsf(a.y-b.y)>1.05f) continue;
            a.y+=species[list[i].type].height*.55f; b.y+=species[list[j].type].height*.55f;
            if (!clear_ray_kind(a,b,1)) continue;
            mate=j; limit=distance;
        }
        if (mate<0) continue;
        chosen[i]=mate; chosen[mate]=i;
        if (motion[i].mate!=mate || limit>1.55f*1.55f) motion[i].courting=0;
        else if (motion[i].courting<60) motion[i].courting+=5;
        if (motion[i].courting>=60) {
            Vec3 a=list[i].feet,b=list[mate].feet;
            Vec3 mid={(a.x+b.x)*.5f,(a.y+b.y)*.5f,(a.z+b.z)*.5f};
            unsigned char traits=list[i].type==ACTOR_WOLF ? WOLF_TAMED : 0;
            if (list[i].type==ACTOR_OCELOT)
                traits=(uint8_t)(PET_TAMED|((next(&motion[i].random)&1u ? list[i].traits : list[mate].traits)&CAT_COAT_MASK));
            if (spawn_child(list[i].type,mid,traits)) {
                care[i].love=care[mate].love=0;
                care[i].cooldown=care[mate].cooldown=ANIMAL_COOLDOWN_TICKS;
                experience_reward((Vec3){mid.x,mid.y+.32f,mid.z},experience_roll(1,7));
                chosen[i]=chosen[mate]=-1;
                ++revision;
            }
        }
    }
    for (int i=0;i<ACTOR_CAPACITY;++i) {
        if (chosen[i]<0) motion[i].courting=0;
        motion[i].mate=chosen[i];
    }
}
static int daylight(void) { return player_rules.day_tick < 12000 && weather_kind()!=WEATHER_THUNDER; }
int actors_spawn_dungeon(int type,Vec3 point,float heading) {
    if ((type!=ACTOR_ZOMBIE && type!=ACTOR_SKELETON && !spider_type(type)) ||
        world_dimension()!=OVERWORLD || game_effective_difficulty()==PEACEFUL) return 0;
    const Species *s=&species[type];
    if (obstacle(s,point)) return 0;
    BlockBox candidate=body_box(point.x,point.y,point.z,s->radius,s->height);
    if (box_overlap(candidate,body_box(px,py,pz,.30f,1.80f))) return 0;
    for (int i=0;i<ACTOR_CAPACITY;++i) if (banks[OVERWORLD][i].type) {
        BlockBox body;
        if (actors_body_at(OVERWORLD,i,&body) && box_overlap(candidate,body)) return 0;
    }
    return actors_restore(OVERWORLD,(ActorRecord){point,heading,(uint8_t)type,(uint8_t)s->life,0});
}
int actors_spawn_egg(int type,Vec3 point,float heading) {
    Dimension d=world_dimension();
    if (type<=0 || type>=ACTOR_TYPES || !(species[type].dimensions&(1u<<d)) ||
        (type==ACTOR_LANTERN_MOTH && d!=OVERWORLD) ||
        !(fabsf(point.x)<WORLD_SAVE_LIMIT && fabsf(point.z)<WORLD_SAVE_LIMIT && point.y>=0 && point.y<WY) ||
        !world_chunk_ready((int)floorf(point.x),(int)floorf(point.z))) return 0;
    int x=(int)floorf(point.x),y=(int)floorf(point.y),z=(int)floorf(point.z);
    unsigned role=hash2(x,z)%VILLAGE_HOMES;
    int traits=type==ACTOR_VILLAGER ? (ROAMING_VILLAGER|role) : 0;
    Species body=body_for(type,0,traits);
    BlockBox candidate=body_box(point.x,point.y,point.z,body.radius,body.height);
    int aquatic=type==ACTOR_SQUID || type==ACTOR_TIDERAY;
    if (world_box_blocked(candidate,((aquatic || type==ACTOR_SILTBACK) ? 0u : SPACE_WATER)|
            (body.lava_safe ? 0u : SPACE_LAVA)) ||
        box_overlap(candidate,body_box(px,py,pz,.29f,1.72f)) || point.y+body.height>WY) return 0;
    if (aquatic &&
        !water_strength(world_cell((int)floorf(point.x),(int)floorf(point.y),(int)floorf(point.z)))) return 0;
    for (int i=0;i<ACTOR_CAPACITY;++i) {
        BlockBox other;
        if (actors_body_at(d,i,&other) && box_overlap(candidate,other)) return 0;
    }
    for (int i=0;i<HORSE_CAPACITY;++i) {
        BlockBox other;
        if (horse_body_at(d,i,&other) && box_overlap(candidate,other)) return 0;
    }
    ActorRecord record={point,remainderf(heading,6.283185307f),
        (uint8_t)type,(uint8_t)body.life,(uint8_t)traits};
    if (type==ACTOR_VILLAGER) {
        VillagerSave market={x,y,z,(uint8_t)role,record.health,1,{0},0};
        return actors_restore_roaming(d,record,market);
    }
    ActorCare care={0};
    care.managed=(uint8_t)actors_is_livestock(type);
    return actors_restore_with_care(d,record,care);
}
static uint8_t spawn_armor(int x,int z) {
    /* Independent of spawn/loot RNG to keep old loot streams stable.
       Probability is a bounded approximation until regional difficulty exists. */
    uint32_t roll=hash2(x+(int)(player_rules.day_tick/100u),z)^world_seed_mix32();
    GameDifficulty difficulty=game_effective_difficulty();
    unsigned chance=difficulty==HARD ? 25u : difficulty==EASY ? 5u : 15u;
    if (next(&roll)%100u>=chance) return 0;
    unsigned material=next(&roll)%1000u;
    ArmorMaterial tier=material<500 ? ARMOR_LEATHER : material<800 ? ARMOR_GOLD :
        material<960 ? ARMOR_CHAIN : material<998 ? ARMOR_IRON : ARMOR_DIAMOND;
    unsigned mask=1u<<ARMOR_FEET;
    for (int slot=ARMOR_LEGS;slot>=ARMOR_HEAD && next(&roll)%100u<75u;--slot) mask|=1u<<slot;
    return armor_make_kit(tier,mask);
}
static unsigned faultback_branch(Vec3 p) { return p.x<.5f ? 1u : 2u; }
static int faultback_calm(Vec3 p) {
    return inside_pressure_works() && (portals_pressure_relief()&faultback_branch(p));
}
int actors_faultback_phase(Dimension d,int i) {
    if ((unsigned)d>=DIMENSION_COUNT || (unsigned)i>=ACTOR_CAPACITY || banks[d][i].type!=ACTOR_FAULTBACK)
        return FAULTBACK_IDLE;
    return d==world_dimension() && faultback_calm(banks[d][i].feet) ?
        FAULTBACK_CALM : motions[d][i].ram_phase;
}
int actors_hostile_near(Vec3 p,float horizontal,float vertical) {
    Dimension d=world_dimension();
    const ActorRecord *list=banks[d];
    for (int i=0;i<ACTOR_CAPACITY;++i) if (list[i].type &&
        game_effective_difficulty()!=PEACEFUL &&
        !(list[i].type==ACTOR_FAULTBACK && faultback_calm(list[i].feet)) &&
        (species[list[i].type].damage || actors_wolf_angry(d,i)) &&
        fabsf(list[i].feet.x-p.x)<=horizontal && fabsf(list[i].feet.z-p.z)<=horizontal &&
        fabsf(list[i].feet.y-p.y)<=vertical) return 1;
    return 0;
}

/* A crab needs a dry foothold next to water, not a liquid surface to
   stand on. Missing neighbors are unavailable, never synthesized here. */
static int crab_shore(Vec3 point,int *water_side) {
    int x=(int)floorf(point.x),y=(int)floorf(point.y-.06f),z=(int)floorf(point.z),raw,above;
    if (y<0 || y>=WY-1 || !world_resident_cell(x,y,z,&raw) ||
        !(coast_stone(raw) || raw==STONE || raw==LIMESTONE_ROCK) ||
        !world_resident_cell(x,y+1,z,&above) || above!=AIR) return 0;
    for (int i=0;i<4;++i) {
        int nx=x+(i==0)-(i==1),nz=z+(i==2)-(i==3);
        if ((world_resident_cell(nx,y,nz,&raw) && water_strength(raw)) ||
            (world_resident_cell(nx,y+1,nz,&raw) && water_strength(raw)) ||
            (y>0 && world_resident_cell(nx,y,nz,&above) && above==AIR &&
                world_resident_cell(nx,y-1,nz,&raw) && water_strength(raw))) {
            if (water_side) *water_side=i;
            return 1;
        }
    }
    return 0;
}
static void spawn_near(Dimension dim) {
    if (py < 1 || py > WY+2 || dim==END) return;
    int counts[ACTOR_TYPES] = {0};
    for (int i = 0; i < ACTOR_CAPACITY; ++i) {
        ActorRecord a = banks[dim][i];
        if (!a.type || distance2(a.feet,(Vec3){px,py,pz}) > 68*68) continue;
        ++counts[a.type];
    }
    unsigned roll = next(&spawn_rng[dim]) % 10;
    int peaceful=game_effective_difficulty()==PEACEFUL;
    if (peaceful && (dim!=OVERWORLD || !daylight())) return;
    ActorType type = dim == NETHER ? ACTOR_REEDLIZARD :
        daylight() ? (roll < 3 ? ACTOR_PIG : roll < 5 ? ACTOR_CHICKEN : roll < 8 ? ACTOR_SHEEP : ACTOR_COW) :
        (roll < 3 ? ACTOR_ZOMBIE : roll < 6 ? ACTOR_SKELETON : ACTOR_SPIDER);
    static const int caps[ACTOR_TYPES] = {0,6,7,0,3,6,6,6,7,6,8,8,0,0,0,0,0,0,0,0,5};
    for (int i = 0; i < (dim == NETHER ? 8 : 5); ++i) {
        unsigned r = next(&spawn_rng[dim]);
        int dx = (int)(r%41)-20, dz = (int)((r>>8)%41)-20;
        if (dx*dx+dz*dz < (dim == NETHER ? 49 : 100)) continue;
        int x = (int)floorf(px)+dx, z = (int)floorf(pz)+dz;
        int floor = dim == NETHER ? nether_fortress_floor(x,z) : terrain_height(x,z);
        if (floor <= 0) continue;
        ActorType candidate=type;
        int y=floor+1;
        Vec3 point={x+.5f,y+.01f,z+.5f};
        int shore=daylight() && dim==OVERWORLD && counts[ACTOR_TIDE_CRAB]<caps[ACTOR_TIDE_CRAB] &&
            fabsf(y-py)<=18 && crab_shore(point,NULL) && terrain_biome(x,z)==BIOME_JOINT_COAST;
        if (shore) candidate=ACTOR_TIDE_CRAB;
        if (counts[candidate]>=caps[candidate]) continue;
        int bottom=block(x,floor,z);
        if (fabsf(y-py)>18 || (!shore && !species[candidate].damage && bottom!=GRASS) ||
            (dim == NETHER && bottom != NETHER_BRICKS)) continue;
        if (obstacle(&species[candidate],point) || !solid(bottom)) continue;
        int occupied = 0;
        for (int n = 0; n < ACTOR_CAPACITY && !occupied; ++n)
            occupied = banks[dim][n].type && distance2(banks[dim][n].feet,point) < 2.f;
        uint8_t traits=actors_can_wear_armor(candidate) ? spawn_armor(x,z) : 0;
        if (!occupied) {
            ActorRecord a={point,0,(uint8_t)candidate,(uint8_t)species[candidate].life,traits};
            if (candidate==ACTOR_ZOMBIE) {
                /* Give only a minority a stable capability at spawn; do not reroll on reload.
                   The global difficulty stands in for the unimplemented regional difficulty. */
                unsigned chance=game_effective_difficulty()==HARD ? 10u :
                    game_effective_difficulty()==NORMAL ? 6u : 3u;
                uint32_t roll=hash2(x,z)^world_seed_mix32()^((player_rules.day_tick/120u+1u)*0x9e3779b1u);
                ActorArmorState armor={.door_breaker=(uint8_t)(next(&roll)%100u<chance)};
                if (actors_restore_snapshot(dim,a,(ActorCare){0},(LeadLink){LEAD_FREE,0,0,0},armor)) return;
            } else if (actors_restore(dim,a)) return;
        }
    }
}
static void spawn_faultback(Vec3 player) {
    if (!inside_pressure_works() || game_effective_difficulty()==PEACEFUL) return;
    /* Each pressurized return alcove is a separate habitat. No cross-chunk
       searches or spawns in front of a player already in the alcove. */
    for (int side=-1;side<=1;side+=2) {
        Vec3 point={.5f+13.f*side,PRESSURE_FLOOR+1.015f,PRESSURE_VALVE_Z-2.5f};
        const unsigned branch=faultback_branch(point);
        const float d2=distance2(point,player);
        if (d2<5.f*5.f || d2>17.f*17.f || faultback_calm(point) ||
            !world_chunk_ready((int)floorf(point.x),(int)floorf(point.z))) continue;
        int already=0;
        for (int i=0;i<ACTOR_CAPACITY && !already;++i)
            already=banks[NETHER][i].type==ACTOR_FAULTBACK && faultback_branch(banks[NETHER][i].feet)==branch;
        int floor;
        if (!already && world_resident_cell((int)floorf(point.x),PRESSURE_FLOOR,(int)floorf(point.z),&floor) &&
            full_top_surface(floor))
            (void)actors_spawn_egg(ACTOR_FAULTBACK,point,side>0 ? -2.1f : 2.1f);
    }
}
static void spawn_reedlizard(Vec3 player) {
    if (!inside_pressure_works() || game_effective_difficulty()==PEACEFUL) return;
    /* Keep the kiln margins distinct from the side alcoves. Resident-only
       checks avoid opening chunks merely to stock the next gallery. */
    const Vec3 point={5.5f,PRESSURE_FLOOR+1.012f,-16.5f};
    float d2=distance2(point,player);
    if (d2<6.2f*6.2f || d2>20.f*20.f) return;
    for (int i=0;i<ACTOR_CAPACITY;++i)
        if (banks[NETHER][i].type==ACTOR_REEDLIZARD && distance2(banks[NETHER][i].feet,point)<36.f) return;
    int floor;
    if (world_resident_cell(5,PRESSURE_FLOOR,-17,&floor) && full_top_surface(floor))
        (void)actors_spawn_egg(ACTOR_REEDLIZARD,point,-1.35f);
}
static const int mole_dx[]={1,-1,0,0},mole_dz[]={0,0,1,-1};
static int mole_nearby(Dimension dim,Vec3 point,float range) {
    for (int i=0;i<ACTOR_CAPACITY;++i)
        if (banks[dim][i].type==ACTOR_KILN_MOLE && distance2(banks[dim][i].feet,point)<range*range) return 1;
    return 0;
}
static int mole_rock(int cell) {
    return coast_stone(cell) || cell==SINTERED_TILE || cell==LAMINATED_MASONRY ||
        cell==SLATE_ROCK || cell==LIMESTONE_ROCK || cell==STONE;
}
int actors_visit_heat_bench(Hit h) {
    Dimension d=world_dimension();
    if (d!=OVERWORLD && !inside_pressure_works()) return 0;
    const Vec3 center={h.x+.5f,h.y+.015f,h.z+.5f};
    if (mole_nearby(d,center,9.f)) return 0;
    for (int i=0;i<4;++i) {
        const int x=h.x+mole_dx[i],z=h.z+mole_dz[i];
        int floor,above;
        if (!world_resident_cell(x,h.y-1,z,&floor) || !mole_rock(floor) ||
            !world_resident_cell(x,h.y,z,&above) || above!=AIR) continue;
        Vec3 at={x+.5f,h.y+.015f,z+.5f};
        float face=atan2f((float)-mole_dx[i],(float)mole_dz[i]);
        if (actors_spawn_egg(ACTOR_KILN_MOLE,at,face)) return 1;
    }
    return 0;
}
static void spawn_kiln_mole(Vec3 player) {
    if (!inside_pressure_works() || portals_pressure_relief()!=3u) return;
    const Vec3 point={-4.5f,PRESSURE_FLOOR+1.015f,PRESSURE_KILN_Z+.5f};
    const float d=distance2(point,player);
    if (d<4.f*4.f || d>17.f*17.f || mole_nearby(NETHER,point,9.f)) return;
    int floor,space;
    if (world_resident_cell(-5,PRESSURE_FLOOR,PRESSURE_KILN_Z,&floor) && full_top_surface(floor) &&
        world_resident_cell(-5,PRESSURE_FLOOR+1,PRESSURE_KILN_Z,&space) && space==AIR)
        (void)actors_spawn_egg(ACTOR_KILN_MOLE,point,1.45f);
}
static int mole_benches(Vec3 feet,Hit *source,int *warm) {
    const int x=(int)floorf(feet.x),y=(int)floorf(feet.y+.035f),z=(int)floorf(feet.z);
    int present=0,charged=0;
    for (int i=0;i<4;++i) {
        const int xx=x+mole_dx[i],zz=z+mole_dz[i];
        int raw;
        if (!world_resident_cell(xx,y,zz,&raw) || !heat_bench_cell(raw)) continue;
        ++present;
        if (raw==HEAT_BENCH_EMPTY) continue;
        ++charged;
        if (source) *source=(Hit){1,xx,y,zz,x,y,z};
    }
    if (warm) *warm=charged;
    return present;
}
static void spawn_shellback(Dimension d,Vec3 player) {
    if (d!=OVERWORLD && !inside_pressure_works()) return;
    if (player.y<2.f || player.y>=WY-4 || next(&shell_rng)%3u) return;
    int nearby=0;
    for (int i=0;i<ACTOR_CAPACITY;++i)
        if (banks[d][i].type==ACTOR_SHELLBACK && distance2(banks[d][i].feet,player)<36.f*36.f)
            if (++nearby>=3) return;
    for (int i=0;i<6;++i) {
        const unsigned sample=next(&shell_rng);
        int dx=(int)(sample%25u)-12,dz=(int)((sample>>8)%25u)-12;
        if (dx*dx+dz*dz<36) continue;
        int x=(int)floorf(player.x)+dx,z=(int)floorf(player.z)+dz,y0=(int)floorf(player.y)-1;
        for (int y=y0-2;y<=y0+2;++y) {
            int floor,above;
            if (!world_resident_cell(x,y,z,&floor) || floor!=LIMESTONE_ROCK ||
                !world_resident_cell(x,y+1,z,&above) || above!=AIR) continue;
            Vec3 point={x+.5f,y+1.02f,z+.5f};
            if (actors_spawn_egg(ACTOR_SHELLBACK,point,(sample>>24)*.02454369f)) return;
        }
    }
}
static void spawn_seep_beetle(Dimension d,Vec3 player) {
    if (!inside_aquifer() || world_generation()<GENERATOR_SEEP_GARDENS) return;
    Vec3 point={SEEP_GARDEN_X+.5f,SEEP_GARDEN_Y+1.012f,SEEP_GARDEN_Z+.5f};
    float range=distance2(point,player);
    /* Only one resident in the wet gallery. No scanning or activating
       nearby chunks, and no appearance directly beneath the player. */
    if (range<2.f*2.f || range>16.f*16.f) return;
    int raw;
    if (!world_resident_cell(SEEP_GARDEN_X,SEEP_GARDEN_Y,SEEP_GARDEN_Z,&raw) ||
        raw!=SEEP_BED || seep_wet_faces(SEEP_GARDEN_X,SEEP_GARDEN_Y,SEEP_GARDEN_Z)<=0) return;
    for (int i=0;i<ACTOR_CAPACITY;++i) {
        const ActorRecord *a=&banks[d][i];
        if (a->type==ACTOR_SEEP_BEETLE && distance2(point,a->feet)<22.f*22.f) return;
    }
    (void)actors_spawn_egg(ACTOR_SEEP_BEETLE,point,-2.6f);
}
static void spawn_siltbacks(Vec3 player) {
    if (player.y<2.f || player.y>=WY-4) return;
    int nearby=0;
    for (int i=0;i<ACTOR_CAPACITY;++i)
        if (banks[OVERWORLD][i].type==ACTOR_SILTBACK &&
            distance2(banks[OVERWORLD][i].feet,player)<32.f*32.f && ++nearby>=3) return;
    /* Bounded shoreline sampling; do not generate a chunk to find wildlife. */
    for (int attempt=0;attempt<18;++attempt) {
        const unsigned seed=next(&silt_rng);
        int dx=(int)(seed%27u)-13,dz=(int)((seed>>9)%27u)-13,top;
        if (dx*dx+dz*dz<25) continue;
        int x=(int)floorf(player.x)+dx,z=(int)floorf(player.z)+dz;
        if (!world_resident_top(x,z,&top) || fabsf(top-player.y)>7.f) continue;
        for (int y=top;y>=top-2;--y) {
            if (!silt_bed_water(x,y,z)) continue;
            Vec3 point={x+.5f,y+1.014f,z+.5f};
            if (actors_spawn_egg(ACTOR_SILTBACK,point,(seed>>24)*.02454369f)) return;
        }
    }
}

static int pet_habitat(int type,Biome biome) {
    if (type==ACTOR_OCELOT) return biome==BIOME_JUNGLE;
    return biome==BIOME_FOREST || biome==BIOME_ROOFED || biome==BIOME_TAIGA ||
        biome==BIOME_MEGA_TAIGA || biome==BIOME_COLD_TAIGA;
}
static int pet_landing(Dimension d,int index,int x,int y,int z,Vec3 *out) {
    int support,space;
    if (y<1 || y>=WY || x<=-WORLD_SAVE_LIMIT || x>=WORLD_SAVE_LIMIT ||
        z<=-WORLD_SAVE_LIMIT || z>=WORLD_SAVE_LIMIT ||
        !world_resident_cell(x,y-1,z,&support) || !solid(cell_type(support)) ||
        !world_resident_cell(x,y,z,&space) || (space!=AIR && space!=SNOW_LAYER)) return 0;
    Vec3 at={x+.5f,y+.01f,z+.5f};
    if (boat_overlaps_cell(x,y,z) || cart_overlaps_cell(x,y,z) || horse_overlaps_cell(x,y,z) ||
        distance2(at,(Vec3){px,py,pz})<1.4f*1.4f) return 0;
    for (int i=0;i<ACTOR_CAPACITY;++i) if (i!=index && banks[d][i].type &&
        distance2(banks[d][i].feet,at)<1.1f*1.1f) return 0;
    *out=at;
    return 1;
}
static void spawn_wild_pets(int type) {
    int cat=type==ACTOR_OCELOT;
    uint32_t *stream=cat ? &cat_rng : &wolf_rng;
    if (!daylight() || py<1.f || py>=WY || next(stream)%3u) return;
    Vec3 player={px,py,pz};
    int count=0;
    for (int i=0;i<ACTOR_CAPACITY;++i) {
        ActorRecord a=banks[OVERWORLD][i];
        if (a.type==type && !pet_tamed(a) && distance2(a.feet,player)<56.f*56.f) ++count;
    }
    if (count>=4) return;
    const int near[9][2]={{0,0},{2,0},{0,2},{-2,0},{0,-2},{2,2},{-2,2},{2,-2},{-2,-2}};
    for (int i=0;i<7;++i) {
        unsigned roll=next(stream);
        int dx=(int)(roll%45u)-22,dz=(int)((roll>>8)%45u)-22;
        if (dx*dx+dz*dz<14*14) continue;
        int x=(int)floorf(px)+dx,z=(int)floorf(pz)+dz,y,spawned=0;
        Biome biome;
        if (!world_precipitation_surface(x,z,&y,&biome) || !pet_habitat(type,biome) || fabsf(y+1.f-py)>9.f) continue;
        const int desired=cat ? ((roll>>17)%7u==0 ? 3 : 1) : 2+(int)((roll>>17)%3u);
        for (int j=0;j<9 && spawned<desired && count<4;++j) {
            int nx=x+near[j][0],nz=z+near[j][1],floor,ground;
            Vec3 at;
            if (!world_precipitation_surface(nx,nz,&floor,&biome) || !pet_habitat(type,biome) ||
                abs(floor-y)>2 || !world_resident_cell(nx,floor,nz,&ground) ||
                (cat ? floor<62 || (ground!=GRASS && cell_type(ground)!=LEAF) : ground!=GRASS && ground!=PODZOL) ||
                !pet_landing(OVERWORLD,-1,nx,floor+1,nz,&at)) continue;
            ActorCare care={(uint16_t)(cat && spawned ? ANIMAL_GROW_TICKS : 0),0,0,0,0};
            if (!actors_restore_with_care(OVERWORLD,(ActorRecord){at,0.f,(uint8_t)type,
                (uint8_t)(cat ? CAT_HEALTH : WOLF_WILD_HEALTH),0},care)) return;
            ++count; ++spawned;
        }
        if (spawned) return;
    }
}

/* Missing chunks aren't dry land. Sampling never synchronously generates
   terrain for an animal on the edge of the streaming ring. */
static int resident_water_at(Vec3 p) {
    int x=(int)floorf(p.x),y=(int)floorf(p.y),z=(int)floorf(p.z),raw,above;
    if (y<0 || y>=WY || fabsf(p.x)>=WORLD_SAVE_LIMIT || fabsf(p.z)>=WORLD_SAVE_LIMIT) return 0;
    if (!world_resident_cell(x,y,z,&raw)) return -1;
    if (!water_strength(raw)) return 0;
    above=AIR;
    if (y+1<WY && !world_resident_cell(x,y+1,z,&above)) return -1;
    return p.y<y+water_surface(raw,above);
}
/* Let a swimmer kick up to a low bank rather than trying the dry one-block
   step from feet that may be more than one block below the surface. Only
   probe on a blocked horizontal move and when the water is still beneath
   the actor; normal per-tick collision checks handle the actual ascent. */
static int swim_bank_ahead(const Species *s,Vec3 p,Vec3 next) {
    int x=(int)floorf(p.x),z=(int)floorf(p.z);
    /* Search at most the depth of the floating pose plus one cell. Keep the
       ceiling tied to the water line, not to a moving actor's feet. */
    int first=(int)floorf(p.y-.23f);
    int last=(int)floorf(p.y+fmaxf(.7f,s->height*.75f));
    if (first<0 || last>=WY-1) return 0;
    for (int y=first;y<=last;++y) {
        int raw,above;
        if (!world_resident_cell(x,y,z,&raw)) return 0;
        if (!water_strength(raw)) continue;
        if (!world_resident_cell(x,y+1,z,&above)) return 0;
        if (water_strength(above)) continue;
        float line=y+water_surface(raw,above);
        if (line<p.y-.23f) return 0;
        next.y=line+.55f;
        return resident_water_at(next)==0 && !motion_blocked(s,next);
    }
    return 0;
}
static int swimmer_fits(int type,Vec3 p) {
    const Species *s=&species[type];
    /* Check the mantle stays below the water surface and every touched
       chunk can answer the collision query without a synchronous load. */
    if (resident_water_at((Vec3){p.x,p.y+s->height*.76f,p.z})<=0) return 0;
    int raw;
    for (int x=-1;x<=1;x+=2) for (int z=-1;z<=1;z+=2)
        if (!world_resident_cell((int)floorf(p.x+x*s->radius),(int)floorf(p.y),
            (int)floorf(p.z+z*s->radius),&raw)) return 0;
    return !world_box_blocked(body_box(p.x,p.y,p.z,s->radius,s->height),SPACE_LAVA);
}
static void spawn_squid(void) {
    if (py<SQUID_LOW_Y-18 || py>SQUID_HIGH_Y+18 || next(&squid_rng)%2u) return;
    Vec3 player={px,py,pz};
    int count=0;
    for (int i=0;i<ACTOR_CAPACITY;++i) if (banks[OVERWORLD][i].type==ACTOR_SQUID &&
        distance2(banks[OVERWORLD][i].feet,player)<48.f*48.f) ++count;
    if (count>=6) return;
    const int offsets[5][2]={{0,0},{2,0},{0,2},{-2,0},{0,-2}};
    for (int j=0;j<8;++j) {
        unsigned r=next(&squid_rng);
        int dx=(int)(r%41u)-20,dz=(int)((r>>8)%41u)-20;
        if (dx*dx+dz*dz<8*8) continue;
        int x=(int)floorf(px)+dx,z=(int)floorf(pz)+dz;
        for (int y=SQUID_HIGH_Y;y>=SQUID_LOW_Y;--y) {
            Vec3 start={x+.5f,y+.04f,z+.5f};
            if (fabsf(start.y-py)>18.f || !swimmer_fits(ACTOR_SQUID,start)) continue;
            int spawned=0,limit=2+(int)((r>>17)&1);
            for (int i=0;i<5 && count<6 && spawned<limit;++i) {
                Vec3 p={start.x+offsets[i][0],start.y,start.z+offsets[i][1]};
                if (!swimmer_fits(ACTOR_SQUID,p)) continue;
                int occupied=0;
                for (int k=0;k<ACTOR_CAPACITY && !occupied;++k)
                    occupied=banks[OVERWORLD][k].type && distance2(banks[OVERWORLD][k].feet,p)<1.8f;
                if (occupied) continue;
                if (!actors_restore(OVERWORLD,(ActorRecord){p,0,ACTOR_SQUID,SQUID_HEALTH,0})) return;
                ++count; ++spawned;
            }
            if (spawned) return;
        }
    }
}

static void spawn_tideray(Dimension d,Vec3 player) {
    if ((d!=OVERWORLD && !inside_aquifer()) || player.y<2.f || player.y>=WY-2 || next(&tidal_rng)%2u) return;
    int nearby=0;
    for (int k=0;k<ACTOR_CAPACITY;++k)
        if (banks[d][k].type==ACTOR_TIDERAY && distance2(banks[d][k].feet,player)<35.f*35.f &&
            ++nearby>=2) return;
    int y0=(int)floorf(player.y);
    for (unsigned i=0;i<7;++i) {
        unsigned r=next(&tidal_rng);
        int dx=(int)(r%27u)-13,dz=(int)((r>>10)%27u)-13;
        if (dx*dx+dz*dz<36) continue;
        int x=(int)floorf(player.x)+dx,z=(int)floorf(player.z)+dz;
        for (int y=y0+2;y>=y0-7;--y) {
            int water,above,below;
            if (!world_resident_cell(x,y,z,&water)) break;
            if (!water_strength(water) || !world_resident_cell(x,y+1,z,&above) ||
                !world_resident_cell(x,y-1,z,&below) || !water_strength(below) ||
                /* A near-surface swimmer, not an arbitrary deep-cave mob. */
                above!=AIR || water_surface(water,above)<.70f) continue;
            Vec3 at={x+.5f,y+.12f,z+.5f};
            if (swimmer_fits(ACTOR_TIDERAY,at) &&
                actors_spawn_egg(ACTOR_TIDERAY,at,(r>>24)*.02454369f)) return;
        }
    }
}

static void remove_actor_in(Dimension d,ActorRecord *a) {
    int index=(int)(a-banks[d]);
    if (a->type==ACTOR_VILLAGER) villagers_actor_removed(index);
    /* Clear directed combat before the inline slot can be reused. */
    for (int i=0;i<ACTOR_CAPACITY;++i) if (banks[d][i].type && motions[d][i].commanded==index) {
        motions[d][i].commanded=-1;
        motions[d][i].order=0.f;
    }
    for (int i=0;i<ACTOR_FIREBALLS;++i) if (fireballs[d][i].seconds>0.f && fireballs[d][i].source==index)
        fireballs[d][i].source=PROJECTILE_MACHINE;
    a->type=ACTOR_EMPTY;
    name_ids[d][index]=0;
    ++revision;
}
static void remove_actor(ActorRecord *a) { remove_actor_in(world_dimension(),a); }
void actors_apply_difficulty(void) {
    if (game_effective_difficulty()!=PEACEFUL) return;
    for (int dim=0;dim<DIMENSION_COUNT;++dim) {
        Dimension d=(Dimension)dim;
        for (int i=0;i<ACTOR_CAPACITY;++i) {
            ActorRecord *a=&banks[d][i];
            if (a->type && species[a->type].damage) remove_actor_in(d,a);
        }
        for (int i=0;i<ACTOR_FIREBALLS;++i) {
            ActorFireball *p=&fireballs[d][i];
            if (p->seconds>0.f && (p->kind==HOSTILE_ARROW || p->kind==HOSTILE_FIREBALL)) {
                p->seconds=0.f;
                ++revision;
            }
        }
    }
    blasts_apply_difficulty();
}
static void step_swimmer(ActorRecord *a,Motion *m,int index,float dt) {
    int ray=a->type==ACTOR_TIDERAY;
    const Species *s=&species[a->type];
    Vec3 p=a->feet;
    int water=resident_water_at((Vec3){p.x,p.y+(ray ? .27f : .41f),p.z});
    if (water<0) return;
    if (water) {
        m->dry=0.f; m->fall=0.f;
        m->swim_phase=fmodf(m->swim_phase+(ray ? 3.2f : 4.8f)*dt,6.2831853f);
        if ((m->think-=dt)<=0.f) {
            unsigned r=next(&m->random);
            float turn=(float)(r&65535u)*(6.2831853f/65535.f)-3.14159265f;
            a->heading=ray ? remainderf(a->heading+turn*.65f,6.2831853f) : turn;
            m->swim_vertical=ray ? .28f+((int)((r>>16)&255u)-125)*.0034f :
                ((int)((r>>16)&255u)-125)*.006f;
            m->think=(ray ? 1.65f : 1.1f)+(float)(r>>25)*.018f;
        }
        float speed=s->walk*(.67f+.33f*sinf(m->swim_phase))*dt;
        Vec3 next={p.x+sinf(a->heading)*speed,p.y+m->swim_vertical*dt,p.z-cosf(a->heading)*speed};
        if (swimmer_fits(a->type,next)) a->feet=next;
        else {
            next.y=p.y;
            if (swimmer_fits(a->type,next)) a->feet=next;
            else m->think=0.f;
            m->swim_vertical=-m->swim_vertical*.8f;
        }
        ++revision;
        return;
    }
    m->fall=fminf(6.f,m->fall+14.f*dt);
    Vec3 lower={p.x,p.y-m->fall*dt,p.z};
    if (lower.y<=0.f) { remove_actor(a); return; }
    if (!world_move_blocked(body_box(p.x,p.y,p.z,s->radius,s->height),
        body_box(lower.x,lower.y,lower.z,s->radius,s->height),SPACE_LAVA)) {
        a->feet=lower; ++revision;
    } else m->fall=0.f;
    if (ray) return; /* Retired dry-land records become passive stranded fauna. */
    m->dry+=dt;
    if (m->dry>=16.f) {
        int applied=0;
        m->dry-=1.f;
        hurt_actor(index,2,STRIKE_ENVIRONMENT,&applied);
    }
}
static int launch_projectile_from(Vec3 from,Vec3 target,int kind,int source) {
    ActorFireball *list = fireballs[world_dimension()];
    for (int i = 0; i < ACTOR_FIREBALLS; ++i) if (list[i].seconds <= 0) {
        float speed=kind==HOSTILE_ARROW ? 15.f : kind==THROWN_EGG ? 13.f : kind==THROWN_SNOWBALL ? 18.f :
            actors_splash_item(kind)!=AIR ? 10.f : 8.f;
        float distance = sqrtf(distance2(from,target));
        if (!(distance > .01f)) return 0;
        list[i] = (ActorFireball){from,
            {(target.x-from.x)*speed/distance,(target.y-from.y)*speed/distance+
                (kind==HOSTILE_ARROW ? .32f*distance : 0.f),
             (target.z-from.z)*speed/distance},3.f,kind,source};
        ++revision;
        return 1;
    }
    return 0;
}
static int launch_projectile(Vec3 from,Vec3 target,int kind) {
    return launch_projectile_from(from,target,kind,PROJECTILE_MACHINE);
}
int actors_launch_egg(Vec3 from,Vec3 direction) {
    return launch_projectile(from,(Vec3){from.x+direction.x,from.y+direction.y,from.z+direction.z},THROWN_EGG);
}
int actors_launch_snowball(Vec3 from,Vec3 direction) {
    return launch_projectile(from,(Vec3){from.x+direction.x,from.y+direction.y,from.z+direction.z},THROWN_SNOWBALL);
}
int actors_splash_item(int kind) {
    switch (kind) {
    case THROWN_WEAKNESS: return SPLASH_WEAKNESS;
    case THROWN_WEAKNESS_LONG: return SPLASH_WEAKNESS_LONG;
    case THROWN_WATER_BREATHING: return SPLASH_WATER_BREATHING;
    case THROWN_WATER_BREATHING_LONG: return SPLASH_WATER_BREATHING_LONG;
    case THROWN_NIGHT_VISION: return SPLASH_NIGHT_VISION;
    case THROWN_NIGHT_VISION_LONG: return SPLASH_NIGHT_VISION_LONG;
    default: return AIR;
    }
}
static int splash_projectile(int item) {
    switch (item) {
    case SPLASH_WEAKNESS: return THROWN_WEAKNESS;
    case SPLASH_WEAKNESS_LONG: return THROWN_WEAKNESS_LONG;
    case SPLASH_WATER_BREATHING: return THROWN_WATER_BREATHING;
    case SPLASH_WATER_BREATHING_LONG: return THROWN_WATER_BREATHING_LONG;
    case SPLASH_NIGHT_VISION: return THROWN_NIGHT_VISION;
    case SPLASH_NIGHT_VISION_LONG: return THROWN_NIGHT_VISION_LONG;
    default: return -1;
    }
}
int actors_launch_splash(Vec3 from,Vec3 direction,int item) {
    int kind=splash_projectile(item);
    return kind>=0 && launch_projectile(from,(Vec3){from.x+direction.x,from.y+direction.y,from.z+direction.z},kind);
}
const char *actors_throw_splash(void) {
    if (!player_rules.health || (unsigned)selected_slot>=HOTBAR_SIZE) return NULL;
    ItemStack *held=&inventory[selected_slot];
    int kind=splash_projectile(held->type);
    if (kind<0 || !held->count) return NULL;
    int night=kind==THROWN_NIGHT_VISION || kind==THROWN_NIGHT_VISION_LONG;
    Vec3 aim=forward(),eye={px,py+1.55f,pz};
    Vec3 to={eye.x+aim.x,eye.y+aim.y+.16f,eye.z+aim.z};
    if (!launch_projectile_from(eye,to,kind,PROJECTILE_PLAYER)) return "还不能投出更多雾剂";
    if (player_rules.mode==SURVIVAL && !--held->count) *held=(ItemStack){0};
    if (night) return "已掷出辨暗雾剂";
    return kind==THROWN_WEAKNESS || kind==THROWN_WEAKNESS_LONG ?
        "已掷出缓力雾剂" : "已掷出助潜雾剂";
}
const char *actors_throw_snowball(void) {
    if (!player_rules.health || (unsigned)selected_slot>=HOTBAR_SIZE) return NULL;
    ItemStack *held=&inventory[selected_slot];
    if (held->type!=SNOWBALL || !held->count) return NULL;
    Vec3 aim=forward(),eye={px,py+1.55f,pz};
    Vec3 to={eye.x+aim.x,eye.y+aim.y,eye.z+aim.z};
    if (!launch_projectile_from(eye,to,THROWN_SNOWBALL,PROJECTILE_PLAYER)) return "还不能扔出更多雪球";
    if (player_rules.mode==SURVIVAL && !--held->count) *held=(ItemStack){0};
    return "已掷出雪球";
}
static int actor_at_point(Vec3 point,int ignore) {
    Dimension d=world_dimension();
    const ActorRecord *list=banks[d];
    for (int i=0;i<ACTOR_CAPACITY;++i) if (i!=ignore && list[i].type) {
        Species s=body_for(list[i].type,care_banks[d][i].grow,list[i].traits);
        Vec3 p=list[i].feet;
        if (fabsf(p.x-point.x)<=s.radius && fabsf(p.z-point.z)<=s.radius &&
            point.y>=p.y && point.y<=p.y+s.height) return i;
    }
    return -1;
}
static void set_reed_phase(Motion *m,int phase,float seconds) {
    if (m->ram_phase!=phase) ++revision;
    m->ram_phase=(uint8_t)phase;
    m->ram_time=seconds;
}
int actors_reed_phase(Dimension d,int i) {
    return (unsigned)d<DIMENSION_COUNT && (unsigned)i<ACTOR_CAPACITY && banks[d][i].type==ACTOR_REEDLIZARD ?
        motions[d][i].ram_phase : REED_IDLE;
}
unsigned actors_reed_cooldown(Dimension d,int i) {
    if (actors_reed_phase(d,i)!=REED_COOLED) return 0;
    /* Snap fractional simulation time to the next save tick, without rounding
       an exact tick up because of floating point conversion. */
    float ticks=ceilf(motions[d][i].ram_time*20.f-1e-4f);
    return (unsigned)fminf(fmaxf(ticks,0.f),REED_COOL_TICKS);
}
int actors_restore_reed_cooldown(Dimension d,int i,unsigned ticks) {
    if ((unsigned)d>=DIMENSION_COUNT || (unsigned)i>=ACTOR_CAPACITY ||
        banks[d][i].type!=ACTOR_REEDLIZARD || ticks>REED_COOL_TICKS) return 0;
    set_reed_phase(&motions[d][i],ticks ? REED_COOLED : REED_IDLE,ticks/20.f);
    return 1;
}
static int snowball_impact(Dimension d,const ActorFireball *f) {
    int i=actor_at_point(f->point,f->source);
    if (i<0) return 0;
    ActorRecord *a=&banks[d][i];
    if (a->type==ACTOR_REEDLIZARD) {
        set_reed_phase(&motions[d][i],REED_COOLED,REED_COOL_TICKS/20.f);
        sound_play(SOUND_REEDLIZARD,.64f,3,a->feet);
        if (f->source==PROJECTILE_PLAYER) affliction_note="岩簧蜥已降温 · 可用探锤取鳍片";
    } else {
        float len=hypotf(f->velocity.x,f->velocity.z);
        if (len>.01f) {
            Species body=body_for(a->type,care_banks[d][i].grow,a->traits);
            Vec3 p=a->feet,next={p.x+.28f*f->velocity.x/len,p.y,p.z+.28f*f->velocity.z/len};
            if (!move_obstacle(&body,p,next)) a->feet=next;
        }
    }
    return 1;
}

static void crack_egg(Vec3 impact) {
    if (world_dimension()!=OVERWORLD) return;
    uint32_t *random=&spawn_rng[OVERWORLD];
    if (next(random)%8u) return;
    int chicks=next(random)%32u ? 1 : 4;
    for (int i=0;i<chicks;++i) {
        Vec3 p={impact.x+(i%2)*.13f,impact.y,impact.z+(i/2)*.13f};
        if (!spawn_child(ACTOR_CHICKEN,p,0)) break;
    }
}

static unsigned splash_duration(Vec3 p,Vec3 impact,unsigned cap,int direct) {
    if (direct) return cap;
    float y=fabsf(p.y-impact.y),r2=distance2(p,impact);
    if (y>2.125f || r2>=16.f) return 0;
    unsigned duration=(unsigned)(cap*(1.f-sqrtf(r2)*.25f)+.5f);
    return duration>20 ? duration : 0;
}
static void impact_potion(Dimension d,Vec3 point,BrewSplash potion,int hit) {
    if (potion.effect==BREW_SPLASH_NIGHT_VISION) {
        unsigned time=splash_duration((Vec3){px,py+.85f,pz},point,potion.duration,hit==-2);
        if (player_grant_night_vision(time)) affliction_note="夜视生效";
        return;
    }
    if (potion.effect==BREW_SPLASH_WATER_BREATHING) {
        unsigned time=splash_duration((Vec3){px,py+.85f,pz},point,potion.duration,hit==-2);
        if (player_grant_water_breathing(time)) affliction_note="水肺生效";
        return;
    }
    if (potion.effect!=BREW_SPLASH_WEAKNESS) return;
    for (int i=0;i<ACTOR_CAPACITY;++i) if (banks[d][i].type && banks[d][i].type!=ACTOR_STRANDED_GUIDE) {
        Vec3 at=banks[d][i].feet; at.y+=.90f;
        unsigned time=splash_duration(at,point,potion.duration,i==hit);
        if (time<=motions[d][i].weakness) continue;
        motions[d][i].weakness=(uint16_t)time;
    }
    if (player_rules.mode==SURVIVAL && player_rules.health) {
        unsigned p=splash_duration((Vec3){px,py+.85f,pz},point,potion.duration,hit==-2);
        if (p>player_effects.weakness) player_effects.weakness=(uint16_t)p;
    }
}
static void step_fireballs(float dt) {
    Dimension dim = world_dimension();
    ActorFireball *list = fireballs[dim];
    for (int i = 0; i < ACTOR_FIREBALLS; ++i) {
        ActorFireball *f = &list[i];
        if (f->seconds <= 0) continue;
        f->seconds -= dt;
        int arrow=f->kind==HOSTILE_ARROW,egg=f->kind==THROWN_EGG,snowball=f->kind==THROWN_SNOWBALL;
        BrewSplash potion=brew_splash(actors_splash_item(f->kind));
        int splashing=potion.effect!=BREW_SPLASH_NONE;
        if (arrow || egg || snowball || splashing) f->velocity.y-=9.6f*dt;
        float speed=sqrtf(f->velocity.x*f->velocity.x+f->velocity.y*f->velocity.y+
                           f->velocity.z*f->velocity.z);
        int steps=1+(int)(speed*dt/.10f);
        for (int s = 0; s < steps && f->seconds > 0; ++s) {
            float portion = dt/steps;
            Vec3 prev=f->point;
            f->point.x += f->velocity.x*portion;
            f->point.y += f->velocity.y*portion;
            f->point.z += f->velocity.z*portion;
            if (f->point.y < 0 || f->point.y > WY || fabsf(f->point.x)>=WORLD_SAVE_LIMIT ||
                fabsf(f->point.z)>=WORLD_SAVE_LIMIT) { f->seconds = 0; break; }
            if (arrow && button_arrow_impact(prev,f->point,0)) { f->seconds=0; break; }
            if (world_point_blocked(f->point.x,f->point.y,f->point.z,0)) {
                if (egg) crack_egg(prev);
                if (splashing) impact_potion(dim,prev,potion,-1);
                f->seconds=0; break;
            }
            if (splashing) {
                int hit=actor_at_point(f->point,-1);
                if (f->source!=PROJECTILE_PLAYER && player_rules.health &&
                    (player_rules.mode==SURVIVAL || potion.effect!=BREW_SPLASH_WEAKNESS) &&
                    box_contains(body_box(px,py,pz,.28f,1.72f),f->point.x,f->point.y,f->point.z)) hit=-2;
                if (hit!=-1) { impact_potion(dim,f->point,potion,hit); f->seconds=0; }
                continue;
            }
            if (egg) {
                if (actor_at_point(f->point,-1)>=0) { crack_egg(prev); f->seconds=0; }
                continue;
            }
            if (snowball) {
                if (snowball_impact(dim,f)) f->seconds=0;
                continue;
            }
            if (player_rules.mode==SURVIVAL &&
                horse_hit_hostile(f->point,arrow ? 3 : 5,arrow ? DAMAGE_PROJECTILE : DAMAGE_DIRECT_FIRE)) {
                f->seconds=0;
                break;
            }
            if (player_rules.mode == SURVIVAL && player_rules.health &&
                fabsf(f->point.x-px) < .4f && fabsf(f->point.z-pz) < .4f &&
                f->point.y >= py-.1f && f->point.y <= py+1.8f) {
                int health=player_rules.health,frac=player_rules.health_debt;
                if (arrow) player_take_mob_damage(DAMAGE_PROJECTILE,3);
                else {
                    player_take_mob_damage(DAMAGE_DIRECT_FIRE,5);
                    if (game_effective_difficulty()!=PEACEFUL) player_catch_fire(60);
                }
                if (player_rules.health!=health || player_rules.health_debt!=frac) wolves_assist(dim,f->source);
                f->seconds = 0;
            }
        }
        ++revision;
    }
}
static int rival_type(int hunter,int target) {
    if (!target) return 0;
    if (hunter==ACTOR_IRON_GOLEM) return species[target].damage>0;
    if (hunter==ACTOR_SNOW_GOLEM) return species[target].damage>0;
    if (actors_is_zombie(hunter)) return target==ACTOR_VILLAGER || target==ACTOR_IRON_GOLEM || target==ACTOR_SNOW_GOLEM;
    return 0;
}
static int rival_in_range(const ActorRecord *a,const ActorRecord *b) {
    if (b->type==ACTOR_FAULTBACK && faultback_calm(b->feet)) return 0;
    float range=a->type==ACTOR_SNOW_GOLEM ? 5.5f : a->type==ACTOR_IRON_GOLEM ? 10.f : 12.f;
    return rival_type(a->type,b->type) && fabsf(a->feet.y-b->feet.y)<=5.f && distance2(a->feet,b->feet)<range*range;
}
static int seek_rival(const ActorRecord *a,Motion *m,Dimension d,float dt) {
    m->target_scan-=dt;
    const ActorRecord *list=banks[d];
    if (m->target<0 || m->target>=ACTOR_CAPACITY || !rival_in_range(a,&list[m->target])) m->target=-1;
    if (m->target_scan>0.f) return m->target;
    m->target_scan=.30f;
    float best=12.f*12.f;
    int target=-1;
    for (int i=0;i<ACTOR_CAPACITY;++i) if (rival_in_range(a,&list[i])) {
        float d2=distance2(a->feet,list[i].feet);
        if (d2<best) { best=d2; target=i; }
    }
    return m->target=target;
}
static int wolf_commandable(ActorRecord a) {
    if (a.type==ACTOR_FAULTBACK && faultback_calm(a.feet)) return 0;
    return a.type &&
        (species[a.type].damage>0 || (a.type==ACTOR_WOLF && !tamed_wolf(a)));
}
static void wolves_assist(Dimension d,int victim) {
    if ((unsigned)victim>=ACTOR_CAPACITY || !wolf_commandable(banks[d][victim])) return;
    for (int i=0;i<ACTOR_CAPACITY;++i) {
        ActorRecord a=banks[d][i];
        if (tamed_wolf(a) && !(a.traits&WOLF_SITTING) && !care_banks[d][i].grow &&
            ties[d][i].kind==LEAD_FREE && distance2(a.feet,(Vec3){px,py,pz})<16.f*16.f) {
            motions[d][i].order=8.f;
            motions[d][i].commanded=victim;
        }
    }
}
static int wolf_prey(ActorRecord wolf,ActorRecord prey) {
    int type=tamed_wolf(wolf) ? ACTOR_SKELETON : ACTOR_SHEEP;
    return prey.type==type && distance2(wolf.feet,prey.feet)<8.f*8.f;
}
static int wolf_rival(Dimension d,int index,float dt) {
    ActorRecord a=banks[d][index];
    Motion *m=&motions[d][index];
    if (care_banks[d][index].grow || (a.traits&WOLF_SITTING) || m->anger>0.f ||
        ties[d][index].kind!=LEAD_FREE) return -1;
    if ((unsigned)m->commanded<ACTOR_CAPACITY && m->order>0.f &&
        wolf_commandable(banks[d][m->commanded]) &&
        distance2(a.feet,banks[d][m->commanded].feet)<16.f*16.f) return m->commanded;
    if ((unsigned)m->target>=ACTOR_CAPACITY || !wolf_prey(a,banks[d][m->target])) m->target=-1;
    if ((m->target_scan-=dt)>0.f) return m->target;
    m->target_scan=.35f;
    float best=8.f*8.f;
    int target=-1;
    for (int i=0;i<ACTOR_CAPACITY;++i) if (wolf_prey(a,banks[d][i])) {
        float dist=distance2(a.feet,banks[d][i].feet);
        if (dist<best) { best=dist; target=i; }
    }
    return m->target=target;
}
enum { CAT_FOOD_STILL_MS=600 };
static int cat_food(int type) {
    return type==RAW_FISH || type==RAW_SALMON || type==CLOWNFISH || type==PUFFERFISH;
}
static int holding_fish(void) {
    return player_rules.health && (unsigned)selected_slot<HOTBAR_SIZE &&
        inventory[selected_slot].count && cat_food(inventory[selected_slot].type);
}
static void update_fish_lure(Dimension d,Vec3 player,float dt) {
    FishLure *l=&fish_lure[d];
    if (!holding_fish()) { l->active=0; return; }
    if (!l->active) { *l=(FishLure){player,0.f,0.f,1}; return; }
    if (distance2(player,l->anchor)>.20f*.20f) {
        l->anchor=player;
        l->quiet=0.f;
        l->startled=.32f;
        return;
    }
    l->quiet=fminf(1.f,l->quiet+dt);
    l->startled=fmaxf(0.f,l->startled-dt);
}
static int fish_ready(Dimension d,Vec3 player) {
    FishLure l=fish_lure[d];
    return l.active && l.quiet>=CAT_FOOD_STILL_MS/1000.f && distance2(l.anchor,player)<=.20f*.20f;
}
static int ocelot_prey(Dimension d,ActorRecord a,Motion *m,float dt) {
    const ActorRecord *list=banks[d];
    if ((unsigned)m->target>=ACTOR_CAPACITY || list[m->target].type!=ACTOR_CHICKEN ||
        distance2(a.feet,list[m->target].feet)>=8.f*8.f) m->target=-1;
    if ((m->target_scan-=dt)>0.f) return m->target;
    m->target_scan=.35f;
    float best=8.f*8.f;
    m->target=-1;
    Vec3 head={a.feet.x,a.feet.y+.49f,a.feet.z};
    for (int i=0;i<ACTOR_CAPACITY;++i) if (list[i].type==ACTOR_CHICKEN) {
        Vec3 p=list[i].feet;
        p.y+=care_banks[d][i].grow ? .23f : .43f;
        float dist=distance2(head,p);
        if (dist<best && clear_ray_kind(head,p,1)) { m->target=i; best=dist; }
    }
    return m->target;
}
static int furniture_perch(int x,int y,int z,Vec3 *point) {
    int raw,above;
    if (y<1 || y+1>=WY || !world_resident_cell(x,y,z,&raw) ||
        !world_resident_cell(x,y+1,z,&above) || above!=AIR) return 0;
    int type=cell_type(raw);
    if (!is_chest(type) && type!=BED) {
        if (type!=FURNACE) return 0;
        const Furnace *f=furnace_at(x,y,z);
        if (!f || !f->burn) return 0;
    }
    *point=(Vec3){x+.5f,y+(type==BED ? BED_HEIGHT : 1.f)+.01f,z+.5f};
    return 1;
}
static void cancel_perch(Motion *m,float delay) {
    if (m->resting) ++revision;
    m->perch=m->resting=0;
    m->perch_scan=delay;
}
static int nearby_perch(Dimension d,int index,Vec3 owner,float dt,Vec3 *goal) {
    ActorRecord a=banks[d][index];
    Motion *m=&motions[d][index];
    const ActorCare *care=&care_banks[d][index];
    if (!pet_tamed(a) || pet_sitting(a) || care->love || m->flee>0.f ||
        player_rules.health<=0 || ties[d][index].kind!=LEAD_FREE ||
        distance2(a.feet,owner)>5.f*5.f) {
        if (m->perch) cancel_perch(m,2.f);
        return 0;
    }
    if (m->perch) {
        if ((m->perch_time-=dt)<=0.f || !furniture_perch(m->perch_x,m->perch_y,m->perch_z,goal) ||
            distance2(*goal,owner)<.9f*.9f) {
            cancel_perch(m,3.f);
            return 0;
        }
        if (distance2(a.feet,*goal)<.22f*.22f && fabsf(a.feet.y-goal->y)<.11f &&
            !m->resting) { m->resting=1; ++revision; }
        return 1;
    }
    if ((m->perch_scan-=dt)>0.f) return 0;
    m->perch_scan=1.f;
    int x=(int)floorf(a.feet.x),y=(int)floorf(a.feet.y+.08f),z=(int)floorf(a.feet.z);
    float best=3.f*3.f;
    for (int dx=-2;dx<=2;++dx) for (int dz=-2;dz<=2;++dz) for (int dy=-1;dy<=0;++dy) {
        Vec3 spot;
        if (!furniture_perch(x+dx,y+dy,z+dz,&spot) || distance2(spot,owner)<.9f*.9f) continue;
        float ds=distance2(a.feet,spot);
        if (ds>=best) continue;
        int occupied=0;
        for (int j=0;j<ACTOR_CAPACITY && !occupied;++j) if (j!=index && banks[d][j].type==ACTOR_OCELOT)
            occupied=distance2(spot,banks[d][j].feet)<.75f*.75f ||
                (motions[d][j].perch && motions[d][j].perch_x==x+dx &&
                    motions[d][j].perch_y==y+dy && motions[d][j].perch_z==z+dz);
        if (occupied) continue;
        m->perch_x=x+dx; m->perch_y=y+dy; m->perch_z=z+dz;
        best=ds;
        *goal=spot;
    }
    if (best>=3.f*3.f) return 0;
    m->perch=1;
    m->perch_time=8.f;
    return 1;
}
int actors_cat_blocks_chest(int x,int y,int z) {
    Dimension d=world_dimension();
    for (int i=0;i<ACTOR_CAPACITY;++i) if (actors_cat_sitting(d,i)) {
        Vec3 p=banks[d][i].feet;
        if (fabsf(p.y-(y+1.f))<.13f && (int)floorf(p.x)==x && (int)floorf(p.z)==z) return 1;
    }
    return 0;
}
static void pet_rally(Dimension d,int index,Vec3 player,float dt,int *budget) {
    ActorRecord *a=&banks[d][index];
    Motion *m=&motions[d][index];
    if (!pet_tamed(*a) || pet_sitting(*a) || ties[d][index].kind!=LEAD_FREE ||
        m->order>0.f || player_rules.flying || player_rules.health<=0 ||
        distance2(a->feet,player)<12.f*12.f || (m->rally-=dt)>0.f || !*budget) return;
    --*budget;
    m->rally=.45f;
    int x=(int)floorf(player.x),y=(int)floorf(player.y+.04f),z=(int)floorf(player.z);
    /* Only look near the owner's floor, never scan/load far-off chunks. */
    for (int radius=2;radius<=3;++radius)
        for (int dx=-radius;dx<=radius;++dx) for (int dz=-radius;dz<=radius;++dz) {
            if (abs(dx)!=radius && abs(dz)!=radius) continue;
            const int order[4]={0,1,-1,-2};
            for (int k=0;k<4;++k) {
                Vec3 at;
                if (!pet_landing(d,index,x+dx,y+order[k],z+dz,&at)) continue;
                a->feet=at; m->fall=0.f; m->target=-1;
                m->resting=m->perch=0;
                ++revision;
                return;
            }
        }
}
static void rival_attack(ActorRecord *a,const Species *s,Motion *m,Dimension d,int target) {
    ActorRecord *victim=&banks[d][target];
    Species body=body_for(victim->type,care_banks[d][target].grow,victim->traits);
    if (m->attack>0.f) return;
    if (a->type==ACTOR_SNOW_GOLEM) {
        if (!camp_machine_cooled((int)floorf(a->feet.x),(int)floorf(a->feet.y+.04f),(int)floorf(a->feet.z))) return;
        Vec3 from={a->feet.x,a->feet.y+.60f,a->feet.z};
        Vec3 to={victim->feet.x,victim->feet.y+body.height*.45f,victim->feet.z};
        if (!clear_ray_kind(from,to,1)) return;
        motions[d][target].dampened=1.35f;
        m->attack=2.2f;
        sound_creature(a->type,0,1,a->feet,m->random);
        ++revision;
        return;
    }
    float reach=s->radius+body.radius+.78f;
    if (distance2(a->feet,victim->feet)>=reach*reach ||
        !clear_ray_kind((Vec3){a->feet.x,a->feet.y+s->height*.64f,a->feet.z},
            (Vec3){victim->feet.x,victim->feet.y+body.height*.63f,victim->feet.z},1)) return;
    int golem=a->type==ACTOR_IRON_GOLEM,wolf=a->type==ACTOR_WOLF,applied=0;
    int damage=golem ? 9 :
        wolf ? tamed_wolf(*a) ? 4 : 2 : a->type==ACTOR_OCELOT ? 3 : s->damage;
    damage=weakened_melee(damage,m->weakness);
    if (damage>0) (void)hurt_actor(target,damage,actors_is_zombie(a->type) ? STRIKE_ZOMBIE : STRIKE_MOB,&applied);
    m->attack=golem ? .72f : 1.15f;
    ++revision;
    if (applied && wolf && actors_is_zombie(victim->type)) {
        motions[d][target].commanded=(int)(a-banks[d]);
        motions[d][target].order=5.f;
    }
    if (applied && golem && victim->type) {
        motions[d][target].dampened=.68f;
        Vec3 p=victim->feet;
        float x=p.x-a->feet.x,z=p.z-a->feet.z,len=hypotf(x,z);
        if (len>.01f) {
            Vec3 pushed={p.x+.30f*x/len,p.y,p.z+.30f*z/len};
            if (!move_obstacle(&body,p,pushed)) victim->feet=pushed;
        }
    }
}
static void clear_door_attack(Motion *m) {
    if (m->door_raw) ++revision;
    m->door_raw=0;
    m->door_elapsed=0.f;
}

static int hit_door_in_reach(Vec3 from,Vec3 destination,int *x,int *y,int *z) {
    float dx=destination.x-from.x,dy=destination.y-from.y,dz=destination.z-from.z;
    float length=sqrtf(dx*dx+dy*dy+dz*dz);
    if (length<.01f) return AIR;
    float reach=fminf(.90f,length),fraction=reach/length;
    int steps=(int)ceilf(reach*13.f);
    for (int i=1;i<=steps;++i) {
        float t=fraction*i/steps;
        Vec3 p={from.x+dx*t,from.y+.88f+dy*t,from.z+dz*t};
        if (!world_point_blocked(p.x,p.y,p.z,1)) continue;
        *x=(int)floorf(p.x); *y=(int)floorf(p.y); *z=(int)floorf(p.z);
        /* The first obstruction must be the door; walls or iron block this path. */
        return door_closed_wood(*x,y,*z);
    }
    return AIR;
}

static int zombie_door_step(Motion *m,Vec3 point,Vec3 target,int pursuing,float dt) {
    if (m->door_pause>0.f) m->door_pause=fmaxf(0.f,m->door_pause-dt);
    int x=0,y=0,z=0;
    int raw=pursuing && m->armor.door_breaker && m->door_pause==0.f ?
        hit_door_in_reach(point,target,&x,&y,&z) : AIR;
    if (raw!=m->door_raw || (raw && (x!=m->door_x || y!=m->door_y || z!=m->door_z))) {
        clear_door_attack(m);
        m->door_raw=raw;
        m->door_x=x; m->door_y=y; m->door_z=z;
    }
    if (!raw) return 0;
    float previous=m->door_elapsed;
    m->door_elapsed+=dt;
    if (m->door_elapsed>=DOOR_BREAK_SECONDS) {
        if (game_effective_difficulty()==HARD && door_break_closed_wood(x,y,z,raw)) {
            printf("ZOMBIE BROKE WOODEN DOOR %d %d %d\n",x,y,z);
            fflush(stdout);
        }
        clear_door_attack(m);
        m->door_pause=.25f;
    } else if (crack_stage(previous)!=crack_stage(m->door_elapsed) ||
        door_swing_phase(previous)!=door_swing_phase(m->door_elapsed)) ++revision;
    return 1;
}

/* Only resident cells are inspected. A nursery is a waypoint with a rooted
   flower sharing a side, and it only works on the surface. */
static int moth_station_radius(int x,int y,int z) {
    static const int sides[][2]={{1,0},{0,1},{-1,0},{0,-1}};
    int raw,radius=0;
    if (y<1 || y>=WY-2 || !world_resident_cell(x,y,z,&raw) ||
        raw!=RESONANCE_MARKER || !world_resident_cell(x,y+1,z,&raw) || raw!=AIR) return 0;
    for (unsigned i=0;i<sizeof sides/sizeof *sides;++i) {
        const int sx=x+sides[i][0],sz=z+sides[i][1];
        if (world_resident_cell(sx,y,sz,&raw)) {
            if (raw==POPPY_CELL) return 3;
            if (raw==DANDELION_CELL) radius=2;
        }
    }
    return radius;
}
static int moth_state(Dimension d,int i) {
    return (unsigned)d<DIMENSION_COUNT && (unsigned)i<ACTOR_CAPACITY &&
        banks[d][i].type==ACTOR_LANTERN_MOTH;
}
float actors_moth_wing(Dimension d,int i) {
    return moth_state(d,i) && motions[d][i].perch && player_rules.day_tick>=12000 ?
        .5f+.5f*sinf(motions[d][i].swim_phase*3.f) : 0.f;
}
int actors_moth_pollinating(Dimension d,int i) {
    return moth_state(d,i) && motions[d][i].ram_time>0.f;
}
const char *actors_moth_station(Hit h) {
    int raw;
    if (!h.hit || world_dimension()!=OVERWORLD || !world_resident_cell(h.x,h.y,h.z,&raw) ||
        raw!=RESONANCE_MARKER) return NULL;
    int radius=moth_station_radius(h.x,h.y,h.z);
    if (!radius) return "回声路标 · 旁边种花，上方留空，可布置夜间苗圃";
    for (int i=0;i<ACTOR_CAPACITY;++i) {
        const ActorRecord *a=&banks[OVERWORLD][i];
        const Motion *m=&motions[OVERWORLD][i];
        if (a->type==ACTOR_LANTERN_MOTH &&
            ((m->perch && m->perch_x==h.x && m->perch_y==h.y && m->perch_z==h.z) ||
             distance2(a->feet,(Vec3){h.x+.5f,h.y+1.15f,h.z+.5f})<1.f))
            return player_rules.day_tick>=12000 ? "引光蛾已就位 · 给附近的湿润苗床助长" : "引光蛾在歇息 · 入夜后恢复助长";
    }
    if (player_rules.day_tick<12000) return "夜间苗圃 · 入夜后再轻点路标引来引光蛾";
    int slot=free_actor(OVERWORLD);
    const float heading=atan2f(px-h.x-.5f,h.z+.5f-pz)+.29f;
    if (slot<0 || !actors_spawn_egg(ACTOR_LANTERN_MOTH,(Vec3){h.x+.5f,h.y+1.06f,h.z+.5f},heading))
        return "引光蛾没有落脚空间 · 清空路标上方和周围";
    Motion *m=&motions[OVERWORLD][slot];
    m->perch=1;
    m->perch_x=h.x; m->perch_y=h.y; m->perch_z=h.z;
    m->perch_time=2.2f;
    sound_play(SOUND_LANTERN_MOTH,.71f,0,banks[OVERWORLD][slot].feet);
    return radius==3 ? "引光蛾来了 · 红花苗圃，助长范围三格" : "引光蛾来了 · 黄花苗圃，助长范围两格";
}
/* Verify the short flight path before changing a crop. The sample heights
   avoid plant hit targets but respect glass, soil, walls and missing chunks. */
static int moth_has_path(Vec3 from,Vec3 to) {
    for (int i=1;i<=12;++i) {
        float t=(float)i/12.f;
        Vec3 p={from.x+(to.x-from.x)*t,from.y+(to.y-from.y)*t,from.z+(to.z-from.z)*t};
        int raw;
        if (!world_resident_cell((int)floorf(p.x),(int)floorf(p.y),(int)floorf(p.z),&raw) ||
            world_point_blocked(p.x,p.y,p.z,0)) return 0;
    }
    return 1;
}
static int moth_pollinate(Motion *m,int radius,Vec3 from) {
    const int x=m->perch_x,y=m->perch_y,z=m->perch_z;
    int best=7,bx=x,bz=z,stage_raw=0;
    if (radius<2 || radius>3) return 0;
    const int width=2*radius+1, area=width*width;
    const unsigned start=next(&m->random)%(unsigned)area;
    for (int i=0;i<area;++i) {
        unsigned at=(start+(unsigned)i)%(unsigned)area;
        int sx=x+(int)(at%(unsigned)width)-radius,sz=z+(int)(at/(unsigned)width)-radius;
        int raw,soil;
        if (!world_resident_cell(sx,y,sz,&raw) || !crop_type(cell_type(raw)) ||
            !world_resident_cell(sx,y-1,sz,&soil) || soil<=FARM_BASE || soil>=WHEAT_BASE) continue;
        int stage=(raw-FARM_BASE)&7;
        if (stage>=best || !moth_has_path(from,(Vec3){sx+.5f,y+.82f,sz+.5f})) continue;
        best=stage; bx=sx; bz=sz; stage_raw=raw;
    }
    return stage_raw!=0 && world_set_farm_stage(bx,y,bz,stage_raw+1);
}
static void step_lantern_moth(ActorRecord *a,Motion *m,float dt,int *nursery_budget) {
    const Vec3 p=a->feet;
    const int night=player_rules.day_tick>=12000;
    if (m->ram_time>0.f) {
        m->ram_time=fmaxf(0.f,m->ram_time-dt);
        if (!m->ram_time) ++revision;
    }
    if ((m->perch_scan-=dt)<=0.f) {
        m->perch_scan=.63f;
        m->target=m->perch ? moth_station_radius(m->perch_x,m->perch_y,m->perch_z) : 0;
        if (!m->target) {
            m->perch=0;
            int x=(int)floorf(p.x), y=(int)floorf(p.y)-1, z=(int)floorf(p.z);
            int near=moth_station_radius(x,y,z);
            if (near) {
                m->target=near;
                m->perch=1;
                m->perch_x=x; m->perch_y=y; m->perch_z=z;
                m->perch_time=2.2f;
            }
        }
        ++revision;
    }
    if (!m->perch || !m->target) return;
    if (night) m->swim_phase=fmodf(m->swim_phase+3.3f*dt,6.2831853f);
    const float sway=night ? sinf(m->swim_phase) : 0.f;
    const Vec3 q={m->perch_x+.5f+.21f*sway,m->perch_y+1.045f+(night ? .13f+.075f*sinf(2.f*m->swim_phase) : 0.f),
        m->perch_z+.5f+.18f*sway};
    const Species *body=&species[ACTOR_LANTERN_MOTH];
    if (!obstacle(body,q) && !move_obstacle(body,p,q)) {
        if (distance2(q,p)>.00001f || night) ++revision;
        a->feet=q;
    }
    if (!night) { m->perch_time=2.2f; return; }
    if ((m->perch_time-=dt)>0.f) return;
    if (!*nursery_budget) return; /* Stagger work after loading a farm with many stations. */
    --*nursery_budget;
    int radius=moth_station_radius(m->perch_x,m->perch_y,m->perch_z);
    m->perch_time=radius==3 ? 5.f : 3.2f;
    if (moth_pollinate(m,radius,(Vec3){a->feet.x,a->feet.y+.24f,a->feet.z})) {
        m->ram_time=.92f;
        sound_play(SOUND_LANTERN_MOTH,.98f,1,a->feet);
        ++revision;
    }
}
static void faultback_phase(Motion *m,int phase,float time) {
    if (m->ram_phase!=phase) ++revision;
    m->ram_phase=(uint8_t)phase;
    m->ram_time=time;
}
static void step_faultback(ActorRecord *a,Motion *m,Vec3 player,float dt,int index) {
    const Species *s=&species[ACTOR_FAULTBACK];
    Vec3 p=a->feet,below={p.x,p.y-.10f,p.z};
    if (!world_chunk_ready((int)floorf(p.x),(int)floorf(p.z))) return;
    m->dampened=fmaxf(0.f,m->dampened-dt);
    /* Depressurization cancels even a charge in progress. Let the creature
       settle on the ground when a player removes its footing. */
    const int calm=faultback_calm(p),ground=motion_blocked(s,below);
    if (!ground) {
        m->fall=fminf(8.f,m->fall+16.f*dt);
        Vec3 down={p.x,p.y-m->fall*dt,p.z};
        if (down.y<=0.f) { remove_actor(a); return; }
        if (!move_obstacle(s,p,down)) { a->feet=down; ++revision; }
        else m->fall=0.f;
    } else m->fall=0.f;
    if (calm || !ground || player_rules.mode!=SURVIVAL || !player_rules.health ||
        game_effective_difficulty()==PEACEFUL) {
        faultback_phase(m,calm ? FAULTBACK_CALM : FAULTBACK_IDLE,0.f);
        return;
    }
    m->ram_time=fmaxf(0.f,m->ram_time-dt);
    if (m->ram_phase==FAULTBACK_RECOVER) {
        if (!m->ram_time) faultback_phase(m,FAULTBACK_IDLE,0.f);
        return;
    }
    if (m->ram_phase==FAULTBACK_WINDUP) {
        if (!m->ram_time) faultback_phase(m,FAULTBACK_CHARGE,.82f);
        return;
    }
    if (m->ram_phase==FAULTBACK_CHARGE) {
        const float travel=dt*(m->dampened>0.f ? .55f : 1.f);
        Vec3 q={p.x+m->flight.x*travel,p.y,p.z+m->flight.z*travel};
        Vec3 support={q.x,q.y-.10f,q.z};
        if (!m->ram_time || faultback_branch(q)!=faultback_branch(p) ||
            !world_chunk_ready((int)floorf(q.x),(int)floorf(q.z)) ||
            move_obstacle(s,p,q) || !motion_blocked(s,support)) {
            faultback_phase(m,FAULTBACK_RECOVER,2.8f);
            sound_play(SOUND_FAULTBACK,.92f,2,q);
            return;
        }
        if (box_overlap(body_box(q.x,q.y,q.z,s->radius,s->height),
                        body_box(player.x,player.y,player.z,.29f,1.72f))) {
            int health=player_rules.health,fraction=player_rules.health_debt;
            player_take_mob_damage(DAMAGE_CONTACT,s->damage);
            if (player_rules.health!=health || player_rules.health_debt!=fraction) wolves_assist(world_dimension(),index);
            faultback_phase(m,FAULTBACK_RECOVER,2.8f);
            return;
        }
        a->feet=q; ++revision;
        return;
    }
    const float dx=player.x-p.x,dz=player.z-p.z,range=dx*dx+dz*dz;
    m->target_scan-=dt;
    if (m->target_scan>0.f) return;
    m->target_scan=.22f;
    if (range<.05f || range>5.2f*5.2f || fabsf(player.y-p.y)>.9f ||
        !clear_ray_kind((Vec3){p.x,p.y+.59f,p.z},(Vec3){player.x,player.y+.71f,player.z},1)) return;
    const float speed=5.8f/sqrtf(range);
    a->heading=atan2f(dx,-dz);
    m->flight=(Vec3){dx*speed,0.f,dz*speed};
    faultback_phase(m,FAULTBACK_WINDUP,1.10f);
    sound_play(SOUND_FAULTBACK,1.16f,1,p);
}
static void step_reedlizard(ActorRecord *a,Motion *m,Vec3 player,float dt,int index) {
    Vec3 p=a->feet;
    const Species *body=&species[ACTOR_REEDLIZARD];
    if (!motion_blocked(body,(Vec3){p.x,p.y-.065f,p.z})) {
        m->fall=fminf(6.f,m->fall+14.f*dt);
        Vec3 below={p.x,p.y-m->fall*dt,p.z};
        if (below.y<=0.f) { remove_actor(a); return; }
        if (!move_obstacle(body,p,below)) { a->feet=below; ++revision; }
        else m->fall=0;
    } else m->fall=0;
    int active=player_rules.mode==SURVIVAL && player_rules.health && game_effective_difficulty()!=PEACEFUL;
    if (m->ram_phase!=REED_IDLE) {
        m->ram_time-=dt;
        if (m->ram_time>0.f) return;
        if (m->ram_phase==REED_WARN && active) {
            set_reed_phase(m,REED_VENT,.58f);
            sound_play(SOUND_REEDLIZARD,.93f,2,p);
            const float dx=player.x-p.x,dz=player.z-p.z;
            float axis=sinf(a->heading),front=-cosf(a->heading);
            float reach=dx*axis+dz*front,offset=dx*front-dz*axis;
            if (reach>.18f && reach<=REED_REACH && fabsf(offset)<=reach*REED_SPREAD &&
                fabsf(player.y-p.y)<1.08f &&
                clear_ray((Vec3){p.x,p.y+.58f,p.z},(Vec3){player.x,player.y+.68f,player.z})) {
                int health=player_rules.health,debt=player_rules.health_debt;
                player_take_mob_damage(DAMAGE_DIRECT_FIRE,body->damage);
                if (health!=player_rules.health || debt!=player_rules.health_debt) {
                    affliction_note="灼热汽浪 · 看到鳍片展开时向侧面躲避";
                    wolves_assist(world_dimension(),index);
                }
            }
            return;
        }
        set_reed_phase(m,REED_IDLE,0.f);
        m->target_scan=2.65f;
    }
    if (!active) return;
    m->target_scan-=dt;
    if (m->target_scan>0.f) return;
    m->target_scan=.22f;
    const float dx=player.x-p.x,dz=player.z-p.z,range=dx*dx+dz*dz;
    if (range<.20f || range>REED_REACH*REED_REACH || fabsf(player.y-p.y)>=1.08f ||
        !clear_ray((Vec3){p.x,p.y+.58f,p.z},(Vec3){player.x,player.y+.68f,player.z})) return;
    a->heading=atan2f(dx,-dz);
    set_reed_phase(m,REED_WARN,1.45f);
    sound_play(SOUND_REEDLIZARD,.90f,1,p);
}
static void step_tide_crab(ActorRecord *a,Motion *m,float dt) {
    Vec3 p=a->feet;
    int side,x=(int)floorf(p.x),z=(int)floorf(p.z),support;
    m->crab_moving=0;
    if (!world_resident_cell(x,(int)floorf(p.y-.06f),z,&support)) return;
    const Species *body=&species[ACTOR_TIDE_CRAB];
    if (!solid(cell_type(support))) {
        Vec3 to=p;
        m->fall=fminf(6.f,m->fall+8.f*dt);
        to.y=fmaxf(0.f,p.y-m->fall*dt);
        if (!move_obstacle(body,p,to)) a->feet=to;
        else m->fall=0;
        return;
    }
    m->fall=0;
    if (!crab_shore(p,&side)) return;
    static const float face[]={1.5707963f,-1.5707963f,3.1415926f,0};
    a->heading=face[side];
    if ((m->think-=dt)<=0) {
        m->think=.95f+(next(&m->random)%120)*.01f;
        m->perch=(uint8_t)((m->random>>9)&1u);
    }
    if (m->think<.35f) return;
    const float speed=body->walk*dt*(m->perch ? 1.f : -1.f);
    Vec3 to=p;
    if (side<=1) to.z+=speed;
    else to.x+=speed;
    if (!crab_shore(to,NULL) || obstacle(body,to)) return;
    a->feet=to;
    m->crab_moving=1;
    m->swim_phase+=dt*18.f;
    if (m->swim_phase>6.2831853f) m->swim_phase-=6.2831853f;
}
float actors_tide_crab_stride(Dimension d,int index) {
    if ((unsigned)d>=DIMENSION_COUNT || (unsigned)index>=ACTOR_CAPACITY ||
        banks[d][index].type!=ACTOR_TIDE_CRAB || !motions[d][index].crab_moving) return 0;
    return sinf(motions[d][index].swim_phase);
}
float actors_siltback_raking(Dimension d,int index) {
    if ((unsigned)d>=DIMENSION_COUNT || (unsigned)index>=ACTOR_CAPACITY ||
        banks[d][index].type!=ACTOR_SILTBACK) return 0.f;
    float work=motions[d][index].grazing;
    return work>0.f ? .50f+.50f*sinf(work*16.f) : 0.f;
}
static int rake_silt_bed(ActorRecord *a,Motion *m,float dt) {
    const Vec3 p=a->feet;
    const int x=(int)floorf(p.x),y=(int)floorf(p.y-.062f),z=(int)floorf(p.z);
    if (m->flee>0.f || fabsf(p.y-y-1.f)>.17f || !silt_bed_water(x,y,z)) {
        if (m->grazing>0.f) { m->grazing=0.f; ++revision; }
        return 0;
    }
    if (m->perch_x!=x || m->perch_y!=y || m->perch_z!=z) {
        m->grazing=0.f; m->perch_x=x; m->perch_y=y; m->perch_z=z;
    }
    a->feet.y=y+1.012f;
    m->fall=0.f;
    m->grazing+=dt;
    if (m->grazing>=SILTBACK_WORK_SECONDS) {
        /* Commit one block transition, not a pile of dropped items.
           A fault allocating an edit leaves the original patch intact. */
        const BlockEdit fiber={x,y,z,FIBROUS_CLAY};
        if (world_set_blocks(&fiber,1)) sound_play(SOUND_SILTBACK,.53f,1,p);
        m->grazing=0.f;
    }
    ++revision;
    return 1;
}
static void step_actor(ActorRecord *a, Motion *m, Vec3 player, float dt,int *rally_budget,int *nursery_budget) {
    Dimension d=world_dimension();
    int index=(int)(a-banks[d]);
    if (a->type==ACTOR_TIDE_CRAB) {
        float d2=distance2(a->feet,player);
        if (d2>80.f*80.f && !name_ids[d][index]) remove_actor(a);
        else {
            const ActorRecord previous=*a;
            const int moving=m->crab_moving;
            if (d2<=24.f*24.f) step_tide_crab(a,m,dt);
            else m->crab_moving=0;
            if (previous.feet.x!=a->feet.x || previous.feet.y!=a->feet.y ||
                previous.feet.z!=a->feet.z || previous.heading!=a->heading || moving!=m->crab_moving) ++revision;
        }
        return;
    }
    if (a->type==ACTOR_REEDLIZARD) {
        if (distance2(a->feet,player)<=24.f*24.f)
            step_reedlizard(a,m,player,dt,index);
        else if (m->ram_phase==REED_WARN || m->ram_phase==REED_VENT) set_reed_phase(m,REED_IDLE,0.f);
        return;
    }
    if (a->type==ACTOR_FAULTBACK) {
        if (distance2(a->feet,player)<=24.f*24.f) step_faultback(a,m,player,dt,index);
        else faultback_phase(m,FAULTBACK_IDLE,0.f);
        return;
    }
    if (a->type==ACTOR_LANTERN_MOTH) {
        if (d==OVERWORLD && distance2(a->feet,player)<=22.f*22.f) step_lantern_moth(a,m,dt,nursery_budget);
        return;
    }
    if (a->type==ACTOR_SQUID || a->type==ACTOR_TIDERAY) {
        float d2=distance2(a->feet,player);
        if (d2>80.f*80.f && !name_ids[d][index]) remove_actor(a);
        else if (d2<=44.f*44.f) step_swimmer(a,m,index,dt);
        return;
    }
    const ActorCare *c=&care_banks[d][index];
    Species body=body_for(a->type,c->grow,a->traits);
    const Species *s=&body;
    int zombie=actors_is_zombie(a->type);
    int wolf=a->type==ACTOR_WOLF,cat=a->type==ACTOR_OCELOT,sitting=pet_sitting(*a);
    if (a->type==ACTOR_SNOW_GOLEM || a->type==ACTOR_STRANDED_GUIDE) sitting=1;
    if (m->dampened>0.f) m->dampened=fmaxf(0.f,m->dampened-dt);
    if (m->order>0.f) m->order=fmaxf(0.f,m->order-dt);
    if (wolf) {
        if (m->anger>0.f) {
            m->anger=fmaxf(0.f,m->anger-dt);
            if (!m->anger) ++revision;
        }
    }
    if (wolf || cat) pet_rally(d,index,player,dt,rally_budget);
    Vec3 anchor={0,0,0};
    LeadLink *rope=&ties[d][index];
    int led=rope->kind ? lead_tick(d,rope,a->feet,&anchor) : 0;
    float d2 = distance2(a->feet,player);
    if (d2 > 80*80) {
        int resident=a->type==ACTOR_VILLAGER && !(a->traits&ROAMING_VILLAGER);
        if (!c->managed && a->type!=ACTOR_STRANDED_GUIDE &&
            (!name_ids[d][index] || resident) &&
            !(a->type==ACTOR_VILLAGER && (a->traits&ROAMING_VILLAGER)) &&
            a->type!=ACTOR_IRON_GOLEM && a->type!=ACTOR_SNOW_GOLEM) remove_actor(a);
        return;
    }
    if (d2 > 44*44) { clear_door_attack(m); return; }
    if (a->type==ACTOR_SILTBACK && d2<=24.f*24.f && rake_silt_bed(a,m,dt)) return;
    if (a->type==ACTOR_KILN_MOLE) {
        if ((m->perch_scan-=dt)<=0.f) {
            int warm=0;
            m->mole_found=mole_benches(a->feet,NULL,&warm)!=0;
            if (!warm && inside_pressure_works() && portals_pressure_relief()==3u &&
                distance2(a->feet,(Vec3){-4.5f,PRESSURE_FLOOR+1.015f,PRESSURE_KILN_Z+.5f})<20.f) warm=1;
            if (!!warm!=m->mole_warm) { m->mole_warm=(uint8_t)!!warm; ++revision; }
            m->perch_scan=.35f;
        }
        if ((m->mole_found || (inside_pressure_works() && m->mole_warm)) && m->flee<=0.f) sitting=1;
    }
    int survival = player_rules.mode == SURVIVAL && player_rules.health;
    int wild_cat=cat && !pet_tamed(*a);
    int evasive=wild_cat && player_rules.health && d2<6.f*6.f &&
        (!fish_lure[d].active || fish_lure[d].startled>0.f);
    int cat_lured=wild_cat && d2<9.f*9.f && fish_ready(d,player);
    int hostile_wolf=wolf && m->anger>0.f;
    int chasing = game_effective_difficulty()!=PEACEFUL &&
        (s->damage || hostile_wolf) && survival && d2 < 18*18 &&
        (a->type != ACTOR_SPIDER || !daylight() || m->angry);
    int rival=wolf ? wolf_rival(d,index,dt) :
        a->type==ACTOR_IRON_GOLEM || a->type==ACTOR_SNOW_GOLEM || zombie ?
        seek_rival(a,m,d,dt) : -1;
    if (a->type==ACTOR_IRON_GOLEM && rival<0) sitting=1;
    if (wild_cat && !evasive && !cat_lured && !c->grow && m->flee<=0.f) rival=ocelot_prey(d,*a,m,dt);
    if (zombie && (unsigned)m->commanded<ACTOR_CAPACITY && m->order>0.f &&
        banks[d][m->commanded].type==ACTOR_WOLF && distance2(a->feet,banks[d][m->commanded].feet)<9.f*9.f)
        rival=m->commanded;
    if (rival>=0 && zombie && chasing && d2+.25f<distance2(a->feet,banks[d][rival].feet)) rival=-1;
    if (rival>=0) chasing=0;
    if ((a->type==ACTOR_IRON_GOLEM && m->attack>.42f) ||
        (a->type==ACTOR_SNOW_GOLEM && m->attack>1.9f)) ++revision;
    m->attack = fmaxf(0,m->attack-dt);
    m->flee = fmaxf(0,m->flee-dt);
    m->think -= dt;
    Vec3 p = a->feet;
    int feet_wet=resident_water_at((Vec3){p.x,p.y+.04f,p.z})>0;
    if (a->type==ACTOR_SEEP_BEETLE && !feet_wet && m->flee<=0.f) {
        int x=(int)floorf(p.x),y=(int)floorf(p.y-.07f),z=(int)floorf(p.z),ground;
        sitting=world_resident_cell(x,y,z,&ground) && ground==SEEP_BED &&
            seep_wet_faces(x,y,z)>0;
    }
    if (a->type==ACTOR_SHELLBACK) {
        int base;
        int limestone=!feet_wet && fabsf(m->fall)<.10f && m->flee<=0.f &&
            world_resident_cell((int)floorf(p.x),(int)floorf(p.y-.06f),(int)floorf(p.z),&base) &&
            base==LIMESTONE_ROCK;
        if (limestone) {
            sitting=1;
            if (!a->traits && (m->shell_deposition+=dt)>=SHELLBACK_GROW_SECONDS) {
                a->traits=SHELLBACK_RIPE;
                sound_creature(a->type,0,1,p,m->random);
                ++revision;
            }
        } else m->shell_deposition=0.f;
    }
    Vec3 perch={0,0,0};
    int perching=cat && nearby_perch(d,index,player,dt,&perch);
    if (perching && m->resting) sitting=1;
    float old_heading = a->heading;
    int courting=rival<0 && !sitting && m->mate>=0 && c->love && care_banks[d][m->mate].love &&
        banks[d][m->mate].type==a->type && m->flee<=0.f;
    int tempted=!courting && m->flee<=0.f && !s->damage && player_rules.health && d2<9.f*9.f &&
        (cat_lured || (selected_slot>=0 && selected_slot<HOTBAR_SIZE && inventory[selected_slot].count &&
        inventory[selected_slot].type==feed_type(a->type)));
    if (tempted) {
        m->lure_check-=dt;
        if (m->lure_check<=0.f) {
            m->lure_visible=(unsigned char)clear_ray((Vec3){p.x,p.y+s->height*.83f,p.z},(Vec3){px,py+1.f,pz});
            m->lure_check=.25f;
        }
        tempted=m->lure_visible;
    } else m->lure_check=0.f;
    int pull=led && hypotf(p.x-anchor.x,p.z-anchor.z)>4.25f;
    int following=pet_tamed(*a) && !sitting && !led && player_rules.health && rival<0 && !courting;
    int running_away=evasive || m->flee>0.f;
    Vec3 destination=pull ? anchor : perching ? perch :
        courting ? banks[d][m->mate].feet : rival>=0 ? banks[d][rival].feet : player;
    int nav=a->type==ACTOR_VILLAGER && m->flee<=0.f ? villagers_navigate(index,p,dt,&destination) : VILLAGER_WANDER;
    if (!sitting && (nav==VILLAGER_ADVANCE || chasing || rival>=0 || courting || tempted || pull ||
        following || running_away || perching)) {
        float dx = destination.x-p.x, dz = destination.z-p.z;
        float sign = running_away && !pull ? -1.f : 1.f;
        a->heading = fabsf(dx)+fabsf(dz)<.001f ? a->heading : atan2f(dx*sign,-dz*sign);
    } else if (!sitting && m->think <= 0) {
        unsigned r = next(&m->random);
        a->heading = (float)(r & 0xffff) * (6.2831853f / 65535.f) - 3.14159265f;
        m->think = 1.f+(float)((r>>16)&255)/85.f;
    }
    float reach=1.75f;
    int bashing=zombie && zombie_door_step(m,p,destination,
        (chasing || rival>=0) && !running_away,dt);
    if (!bashing && rival>=0) rival_attack(a,s,m,d,rival);
    if (!bashing && chasing && (s->damage || hostile_wolf) && a->type!=ACTOR_SKELETON &&
        d2 < reach*reach && m->attack == 0.f &&
        clear_ray_kind((Vec3){p.x,p.y+s->height*.68f,p.z},(Vec3){px,py+1.1f,pz},zombie)) {
        int health=player_rules.health,frac=player_rules.health_debt;
        player_take_mob_damage(DAMAGE_MELEE,weakened_melee(hostile_wolf ? 2 : s->damage,m->weakness));
        if (player_rules.health!=health || player_rules.health_debt!=frac) {
            if (a->type==ACTOR_CAVE_SPIDER) player_afflict_cave_spider_poison();
            wolves_assist(d,index);
        }
        m->attack = 1.15f;
    }
    if (chasing && a->type==ACTOR_SKELETON && d2<16.f*16.f && m->attack==0.f) {
        Vec3 from={p.x,p.y+1.56f,p.z},to={px,py+1.f,pz};
        if (clear_ray(from,to) && launch_projectile_from(from,to,HOSTILE_ARROW,index)) m->attack=1.65f;
    }
    float approach = a->type==ACTOR_SKELETON ? 7.f : 1.2f;
    int can_swim=a->type!=ACTOR_IRON_GOLEM && a->type!=ACTOR_SNOW_GOLEM;
    int push_up_bank=0;
    int directed=nav==VILLAGER_ADVANCE || chasing || rival>=0 || courting || tempted || pull ||
        following || running_away || perching;
    int web=!spider_type(a->type) && world_cobweb_contact(body_box(p.x,p.y,p.z,s->radius,s->height));
    float stop=nav==VILLAGER_ADVANCE || running_away ? 0.f : perching ? .16f : courting ? .92f : tempted ? 1.65f :
        rival>=0 && a->type==ACTOR_IRON_GOLEM ? 1.13f :
        following ? 2.3f : approach;
    if (!bashing && !sitting && nav!=VILLAGER_WAIT && m->fuse==0.f &&
        (!directed || distance2(p,destination)>stop*stop) &&
        (directed || m->flee > 0 || m->think < 1.65f)) {
        float speed = s->walk * dt * (running_away ? 2.f :
            cat_lured && tempted ? .55f : directed ? 1.f : .47f);
        if (web) speed*=.25f;
        if (m->dampened>0.f) speed*=.36f;
        if (feet_wet) speed*=.51f;
        if (nav==VILLAGER_ADVANCE || perching) speed=fminf(speed,hypotf(destination.x-p.x,destination.z-p.z));
        Vec3 next = {p.x + sinf(a->heading)*speed,p.y,p.z - cosf(a->heading)*speed};
        if (move_obstacle(s,p,next)) {
            if (nav==VILLAGER_ADVANCE) {
                if (!villagers_open_path_door(index,next,s->radius,s->height) || move_obstacle(s,p,next)) {
                    next=p;
                    villagers_path_blocked(index);
                }
            } else if (can_swim && (feet_wet || resident_water_at((Vec3){p.x,p.y-.21f,p.z})>0)) {
                push_up_bank=swim_bank_ahead(s,p,next);
                next=p;
            } else {
                next.y += 1.f;
                Vec3 above = {p.x,p.y+1.f,p.z};
                if (motion_blocked(s,above) || motion_blocked(s,next)) next = p;
            }
        }
        if ((!led || lead_allows(p,next,anchor)) && distance2(next,player) > .65f*.65f &&
            (a->type!=ACTOR_VILLAGER || villagers_can_walk(index,next))) p = next;
    }
    feet_wet=resident_water_at((Vec3){p.x,p.y+.04f,p.z})>0;
    if (push_up_bank) {
        m->fall+=(-2.8f-m->fall)*fminf(1.f,dt*11.f);
    } else if (feet_wet && can_swim) {
        /* Follow the actual fluid height (also for flowing levels), not
           the top of the block cell. Hold the upper body near the surface. */
        m->swim_phase=fmodf(m->swim_phase+dt*5.f,6.2831853f);
        float offset=s->height*(.62f+.041f*sinf(m->swim_phase));
        int chest_wet=resident_water_at((Vec3){p.x,p.y+offset,p.z})>0;
        float speed=chest_wet ? -.90f : .68f;
        m->fall+=(speed-m->fall)*fminf(1.f,dt*11.f);
    } else {
        const float limit=feet_wet ? 2.35f : a->type==ACTOR_CHICKEN ? 2.1f : 9.f;
        m->fall=fminf(limit,m->fall+16.f*dt);
    }
    Vec3 lower = {p.x,p.y-m->fall*dt*(web ? .05f : 1.f),p.z};
    if (!move_obstacle(s,p,lower)) p = lower;
    else m->fall = 0;
    if (p.y <= 0.f) {
        if (a->type==ACTOR_VILLAGER) villagers_hurt(index,0);
        remove_actor(a); return;
    }
    if (a->type==ACTOR_SHEEP && (a->traits&SHEEP_SHORN)) {
        int x=(int)floorf(p.x),y=(int)floorf(p.y-.07f),z=(int)floorf(p.z);
        if (block(x,y,z)==GRASS) {
            m->grazing+=dt;
            if (m->grazing>=16.f && set_block(x,y,z,DIRT)) {
                a->traits=0; m->grazing=0; ++revision;
            }
        }
    }
    int wet=weather_wets((int)floorf(p.x),(int)floorf(p.y+s->height),(int)floorf(p.z));
    int exposed=0;
    if (!wet && (zombie || a->type==ACTOR_SKELETON) && daylight()) {
        int x=(int)floorf(p.x),z=(int)floorf(p.z),top=world_column_top(x,z);
        /* Grass, crops and torches don't shade a mob standing beside them. */
        while (top>0) {
            int raw=world_cell(x,top,z);
            if (raw!=AIR && cell_type(raw)!=GLASS && plant_height(raw)==0.f) break;
            --top;
        }
        exposed=p.y+.1f>=top+1.f;
    }
    if (exposed) {
        m->burn += dt;
        if (m->burn >= 1.f) {
            m->burn -= 1.f;
            const ItemDef *helmet=armor_item_def(armor_kit_item(a->traits,ARMOR_HEAD));
            if (helmet) {
                if (++m->armor.helmet_wear>=helmet->durability) {
                    a->traits&=(uint8_t)~(1u<<ARMOR_HEAD);
                    if (!(a->traits&15u)) a->traits=0;
                    m->armor.helmet_wear=0;
                    ++revision;
                }
            } else if (!--a->health) { remove_actor(a); return; }
        }
    } else m->burn = 0;
    if (!s->lava_safe) {
        int was=m->flame>0.f,x=(int)floorf(p.x),y=(int)floorf(p.y+.1f),z=(int)floorf(p.z);
        int lower=world_cell(x,y,z);
        if (water_strength(lower)) m->flame=0.f;
        else if (lava_strength(lower)) m->flame=15.f;
        else if (wet) m->flame=0.f;
        else if (fire_contact(body_box(p.x,p.y,p.z,s->radius,s->height))) m->flame=8.f;
        else m->flame=fmaxf(0.f,m->flame-dt);
        if (was!=(m->flame>0.f)) ++revision;
        if (m->flame>0.f) {
            m->flame_damage+=dt;
            if (m->flame_damage>=1.f) {
                int applied=0;
                m->flame_damage-=1.f;
                a->feet=p;
                hurt_actor(index,lava_strength(lower) ? 4 : 1,STRIKE_FIRE,&applied);
                if (!a->type) return;
                p=a->feet;
            }
        } else m->flame_damage=0.f;
    }
    if (p.x != a->feet.x || p.y != a->feet.y || p.z != a->feet.z || old_heading != a->heading) ++revision;
    a->feet = p;
}
enum { GUIDE_NEEDS_CLEARANCE, GUIDE_NEEDS_MARKER, GUIDE_READY };
static int guide_site(Vec3 p) {
    const int x=(int)floorf(p.x),y=(int)floorf(p.y+.06f),z=(int)floorf(p.z);
    int floor,at,head;
    if (y<1 || y>=WY-1 || !world_resident_cell(x,y-1,z,&floor) || !full_top_surface(floor) ||
        !world_resident_cell(x,y,z,&at) || !world_resident_cell(x,y+1,z,&head) || at!=AIR || head!=AIR)
        return GUIDE_NEEDS_CLEARANCE;
    for (int d=0;d<4;++d) {
        int marker;
        if (world_resident_cell(x+rail_dx(d),y,z+rail_dz(d),&marker) && marker==RESONANCE_MARKER)
            return GUIDE_READY;
    }
    return GUIDE_NEEDS_MARKER;
}
static void advance_afflictions(Dimension d,unsigned elapsed) {
    if (!elapsed) return;
    Vec3 player={px,py,pz};
    for (int i=0;i<ACTOR_CAPACITY;++i) if (banks[d][i].type) {
        Motion *m=&motions[d][i];
        uint16_t prior=m->weakness;
        m->weakness=m->weakness>elapsed ? (uint16_t)(m->weakness-elapsed) : 0;
        if (prior && !m->weakness) ++revision;
        ActorRecord *a=&banks[d][i];
        if (a->type!=ACTOR_STRANDED_GUIDE || !m->rest_ticks || distance2(a->feet,player)>24.f*24.f ||
            guide_site(a->feet)!=GUIDE_READY) continue;
        if (m->rest_ticks>elapsed) { m->rest_ticks=(uint16_t)(m->rest_ticks-elapsed); continue; }
        if (!villagers_rejoin(i,m->origin)) { m->rest_ticks=1; continue; }
        a->type=ACTOR_VILLAGER;
        a->health=VILLAGER_HEALTH;
        a->traits=m->origin.market.home|(m->origin.linked ? 0 : ROAMING_VILLAGER);
        if (name_ids[d][i]) villagers_set_actor_name(i,name_ids[d][i]);
        *m=(Motion){.random=m->random,.mate=-1,.target=-1,.commanded=-1};
        ++revision;
        if (distance2(a->feet,player)<144.f) affliction_note="居民已恢复行动 · 轻点可交易";
    }
}

/* AI avoids terrain, but several mobs can still pick the same route or
   arrive from a saved world at exactly the same point. Resolve contacts
   after movement; keep the existing AI, perches and hitboxes. */
enum { CONTACT_ACTOR, CONTACT_HORSE };
typedef struct { BlockBox box; int kind,slot,priority; float remaining; } ActorContact;

static int actor_contact_shift(Dimension d,int slot,float dx,float dz) {
    ActorRecord *a=&banks[d][slot];
    Motion *m=&motions[d][slot];
    Vec3 p=a->feet,q={p.x+dx,p.y,p.z+dz},anchor;
    if ((q.x==p.x && q.z==p.z) || fabsf(q.x)>=WORLD_SAVE_LIMIT || fabsf(q.z)>=WORLD_SAVE_LIMIT ||
        (lead_anchor(ties[d][slot],&anchor) && !lead_allows(p,q,anchor))) return 0;
    if (!world_chunk_ready((int)floorf(q.x),(int)floorf(q.z))) return 0;
    Species s=body_for(a->type,care_banks[d][slot].grow,a->traits);
    if (a->type==ACTOR_TIDE_CRAB && crab_shore(p,NULL) && !crab_shore(q,NULL)) return 0;
    if ((a->type==ACTOR_SQUID || a->type==ACTOR_TIDERAY) &&
        resident_water_at((Vec3){p.x,p.y+(a->type==ACTOR_SQUID ? .41f : .27f),p.z})>0) {
        if (!swimmer_fits(a->type,q)) return 0;
    } else {
        if (move_obstacle(&s,p,q)) return 0;
        if (motion_blocked(&s,(Vec3){p.x,p.y-.07f,p.z}) &&
            !motion_blocked(&s,(Vec3){q.x,q.y-.07f,q.z})) return 0;
        /* Do not push a cat which is resting on furniture off the seat. */
        if (m->resting && ((int)floorf(q.x)!=m->perch_x || (int)floorf(q.z)!=m->perch_z)) return 0;
    }
    a->feet=q; ++revision;
    return 1;
}

static float try_contact_shift(Dimension d,ActorContact *c,int axis,float sign,float distance,int *budget) {
    distance=fminf(distance,c->remaining);
    if (distance<=.001f || !*budget || c->priority==2) return 0.f;
    --*budget;
    float dx=axis ? 0.f : distance*sign,dz=axis ? distance*sign : 0.f;
    int moved=c->kind==CONTACT_ACTOR ? actor_contact_shift(d,c->slot,dx,dz) :
        horse_contact_shift(c->slot,dx,dz);
    if (!moved) return 0.f;
    c->box.x0+=dx; c->box.x1+=dx; c->box.z0+=dz; c->box.z1+=dz;
    c->remaining-=distance;
    return distance;
}

static float contact_axis(Dimension d,ActorContact *a,ActorContact *b,int axis,
    float signed_center,float overlap,int *budget) {
    float sign=signed_center<0.f ? -1.f : 1.f;
    float needed=overlap+.012f;
    if (a->priority<b->priority) return try_contact_shift(d,a,axis,sign,needed,budget);
    if (b->priority<a->priority) return try_contact_shift(d,b,axis,-sign,needed,budget);
    float first=try_contact_shift(d,a,axis,sign,needed*.5f,budget);
    float second=try_contact_shift(d,b,axis,-sign,needed-first,budget);
    if (!second && first) first+=try_contact_shift(d,a,axis,sign,needed-first,budget);
    return first+second;
}

static void separate_near(Dimension d,Vec3 player,float dt) {
    ActorContact list[ACTOR_CAPACITY+HORSE_CAPACITY];
    int count=0;
    float limit=dt*3.f;
    for (int i=0;i<ACTOR_CAPACITY;++i) if (banks[d][i].type && animal_near(banks[d][i].feet,player)) {
        BlockBox box;
        if (!actors_body_at(d,i,&box)) continue;
        int quiet=banks[d][i].type==ACTOR_SNOW_GOLEM ? 2 : pet_sitting(banks[d][i]) || motions[d][i].resting;
        list[count++]=(ActorContact){box,CONTACT_ACTOR,i,quiet,limit};
    }
    const HorseRecord *horses=horses_in(d);
    int rider=horse_rider();
    for (int i=0;i<HORSE_CAPACITY;++i) if (horses[i].type && animal_near(horses[i].feet,player)) {
        BlockBox box;
        if (horse_body_at(d,i,&box)) list[count++]=(ActorContact){box,CONTACT_HORSE,i,i==rider ? 2 : 0,limit};
    }
    if (count<2) return;
    /* Bound terrain probes in dense farms. Advance the starting slot so a
       crowded pen cannot starve unrelated contacts on later ticks. */
    unsigned start=contact_cursor++%(unsigned)count;
    int budget=32;
    for (int i=0;i<count-1 && budget;++i) for (int j=i+1;j<count && budget;++j) {
        ActorContact *a=&list[(i+start)%count],*b=&list[(j+start)%count];
        if ((a->remaining<=.001f && b->remaining<=.001f) ||
            a->box.y1<=b->box.y0+.015f || b->box.y1<=a->box.y0+.015f ||
            a->box.x1<=b->box.x0 || b->box.x1<=a->box.x0 || a->box.z1<=b->box.z0 || b->box.z1<=a->box.z0) continue;
        float dx=(a->box.x0-b->box.x0+a->box.x1-b->box.x1)*.5f;
        float dz=(a->box.z0-b->box.z0+a->box.z1-b->box.z1)*.5f;
        float x=fminf(a->box.x1-b->box.x0,b->box.x1-a->box.x0);
        float z=fminf(a->box.z1-b->box.z0,b->box.z1-a->box.z0);
        /* The zero-vector case has no normal. Choose a stable direction,
           not RNG (which would also change loot and spawn streams). */
        int order=a->kind==b->kind ? a->slot<b->slot : a->kind<b->kind;
        if (fabsf(dx)<.0001f) dx=order ? -.001f : .001f;
        if (fabsf(dz)<.0001f) dz=order ? -.001f : .001f;
        int along_z=z<x;
        if (!contact_axis(d,a,b,along_z,along_z ? dz : dx,along_z ? z : x,&budget))
            contact_axis(d,a,b,!along_z,along_z ? dx : dz,along_z ? x : z,&budget);
    }
}

void actors_step(float dt) {
    Dimension d = world_dimension();
    if (!(dt > 0 && dt <= .1f)) return;
    spawn_time[d] += dt;
    if (spawn_time[d]>=2.f) {
        spawn_time[d]-=2.f;
        if (!inside_pressure_works()) spawn_near(d);
        spawn_faultback((Vec3){px,py,pz});
        spawn_reedlizard((Vec3){px,py,pz});
        spawn_kiln_mole((Vec3){px,py,pz});
        spawn_shellback(d,(Vec3){px,py,pz});
        spawn_tideray(d,(Vec3){px,py,pz});
        spawn_seep_beetle(d,(Vec3){px,py,pz});
        if (d==OVERWORLD) {
            spawn_siltbacks((Vec3){px,py,pz});
            spawn_wild_pets(ACTOR_WOLF); spawn_wild_pets(ACTOR_OCELOT); spawn_squid();
        }
    }
    Vec3 player={px,py,pz};
    update_fish_lure(d,player,dt);
    care_fraction[d]+=20.f*dt;
    unsigned elapsed=(unsigned)care_fraction[d];
    care_fraction[d]-=elapsed;
    advance_afflictions(d,elapsed);
    if (d==OVERWORLD) dungeon_step(elapsed,player);
    if (d==OVERWORLD) villagers_step(elapsed);
    update_care(d,elapsed,player);
    court_ticks[d]+=elapsed;
    if (court_ticks[d]>=5) { court_ticks[d]-=5; breed_near(d,player); }
    int rally_budget=3,nursery_budget=2;
    for (int i = 0; i < ACTOR_CAPACITY; ++i) if (banks[d][i].type) {
        step_actor(&banks[d][i],&motions[d][i],player,dt,&rally_budget,&nursery_budget);
        ActorRecord a=banks[d][i];
        Motion *m=&motions[d][i];
        if (a.type && a.type!=ACTOR_LANTERN_MOTH && distance2(a.feet,player)<16.f*16.f) {
            if (!m->vocal_delay) m->vocal_delay=1.3f+(m->random%230u)*.05f;
            m->vocal_delay-=dt;
            if (m->vocal_delay<=0.f) {
                sound_creature(a.type,0,a.type==ACTOR_OCELOT && pet_tamed(a) &&
                    (pet_sitting(a) || m->resting),a.feet,m->random);
                m->vocal_delay=4.8f+((m->random^(player_rules.day_tick*47u))%171u)*.055f;
            }
        }
    }
    separate_near(d,player,dt);
    step_fireballs(dt);
}

/* Slab intersection uses the same dimensions as movement and placing blocks. */
static float box_time(Vec3 eye, Vec3 ray, Vec3 min, Vec3 max, float limit) {
    const float lo[] = {min.x,min.y,min.z};
    const float hi[] = {max.x,max.y,max.z};
    const float origin[] = {eye.x,eye.y,eye.z}, dir[] = {ray.x,ray.y,ray.z};
    float enter = 0.f;
    for (int i = 0; i < 3; ++i) {
        if (fabsf(dir[i]) < 1e-6f) {
            if (origin[i] < lo[i] || origin[i] > hi[i]) return -1.f;
            continue;
        }
        float u = (lo[i]-origin[i])/dir[i], v = (hi[i]-origin[i])/dir[i];
        enter = fmaxf(enter,fminf(u,v));
        limit = fminf(limit,fmaxf(u,v));
        if (enter >= limit) return -1.f;
    }
    return enter;
}
static const char *hurt_actor(int index, int damage, int strike, int *applied) {
    Dimension d = world_dimension();
    ActorRecord *a = &banks[d][index];
    Motion *motion = &motions[d][index];
    int environment=strike==STRIKE_ENVIRONMENT || strike==STRIKE_BLAST || strike==STRIKE_FIRE ||
        strike==STRIKE_MOB || strike==STRIKE_ZOMBIE || strike==STRIKE_TRAP_ARROW;
    int armor=actors_can_wear_armor(a->type) ? armor_kit_points(a->traits) : 0;
    int health=a->health*HEALTH_UNITS-motion->armor.health_debt;
    uint64_t hit=(uint64_t)damage*(HEALTH_UNITS-armor);
    if (strike==STRIKE_ENVIRONMENT && armor_kit_item(a->traits,ARMOR_HEAD)) hit=(hit*3u+2u)/4u;
    int killed=(strike==STRIKE_MELEE && player_rules.mode==CREATIVE) || hit>=(unsigned)health;
    int drop = species[a->type].drop;
    /* A migrated record can exist in an archived region, where the
       newer ceramic item is not legal in that world's save layout. */
    if (a->type==ACTOR_FAULTBACK && world_generation()<GENERATOR_PRESSURE_WORKS) drop=COBBLESTONE;
    int cooked=motion->flame>0.f;
    if (cooked && drop==RAW_PORK) drop=COOKED_PORK;
    if (cooked && drop==RAW_BEEF) drop=COOKED_BEEF;
    uint32_t roll = spawn_rng[d];
    int shorn=a->type==ACTOR_SHEEP && (a->traits&SHEEP_SHORN);
    int young=care_banks[d][index].grow,rope=ties[d][index].kind!=LEAD_FREE;
    int type=a->type;
    if (killed && type==ACTOR_VILLAGER && (strike==STRIKE_ZOMBIE || strike==STRIKE_MOB)) {
        VillagerOrigin origin;
        if (villagers_mark_stranded(index,&origin)) {
            a->type=ACTOR_STRANDED_GUIDE; a->health=STRANDED_HEALTH; a->traits=0;
            *motion=(Motion){.random=motion->random,.mate=-1,.target=-1,.commanded=-1,.origin=origin};
            ++revision; *applied=1;
            if (distance2(a->feet,(Vec3){px,py,pz})<144.f) affliction_note="居民遇险 · 在身旁放回声路标，再用水囊补给";
            return "居民遇险";
        }
    }
    if (killed && player_rules.mode==SURVIVAL) {
        ItemStack loot[4+ARMOR_SLOTS];
        int n=0;
        if (rope) loot[n++]=(ItemStack){LEAD,1,0,0,0};
        if (drop!=AIR && !shorn && !young) {
            unsigned count=a->type==ACTOR_SKELETON ? next(&roll)%3u :
                a->type==ACTOR_PIG || actors_is_bovine(a->type) || a->type==ACTOR_SQUID ? 1+next(&roll)%3u : 1;
            if (count) loot[n++]=(ItemStack){(uint16_t)drop,(uint8_t)count,0,0,0};
        }
        if (actors_is_zombie(a->type)) {
            unsigned r=next(&roll);
            if (r%40u==0) loot[n++]=(ItemStack){(uint16_t)((r>>8)&1u ? CARROT : POTATO),1,0,0,0};
        } else if (spider_type(a->type) && !environment && next(&roll)%3u==0u) {
            loot[n++]=(ItemStack){SPIDER_EYE,1,0,0,0};
        } else if (a->type==ACTOR_SKELETON) {
            unsigned arrows=next(&roll)%3u;
            if (arrows) loot[n++]=(ItemStack){ARROW,(uint8_t)arrows,0,0,0};
        } else if (a->type==ACTOR_CHICKEN && !young)
            loot[n++]=(ItemStack){(uint16_t)(cooked ? COOKED_CHICKEN : RAW_CHICKEN),1,0,0,0};
        else if (actors_is_bovine(a->type) && !young) {
            unsigned leather=next(&roll)%3u;
            if (leather) loot[n++]=(ItemStack){LEATHER,(uint8_t)leather,0,0,0};
        }
        if (armor) for (int slot=0;slot<ARMOR_SLOTS;++slot) {
            int id=armor_kit_item(a->traits,slot);
            if (!id || next(&roll)%1000u>=85u) continue;
            unsigned max_wear=armor_item_def(id)->durability-1u;
            unsigned wear=max_wear/2u+next(&roll)%(max_wear-max_wear/2u+1u);
            if (slot==ARMOR_HEAD && wear<motion->armor.helmet_wear) wear=motion->armor.helmet_wear;
            loot[n++]=(ItemStack){(uint16_t)id,1,(uint16_t)wear,0,0};
        }
        int slots[4+ARMOR_SLOTS],j=0;
        for (int i=0;i<MAXDROP && j<n;++i) if (!drops[i].item.count) slots[j++]=i;
        if (j!=n) return "掉落物已满，请先拾取";
        for (int i=0;i<n;++i)
            drops[slots[i]]=make_drop(a->feet.x,a->feet.y+.45f,a->feet.z,loot[i]);
        if (n) drops_dirty=1;
        spawn_rng[d]=roll;
    }
    *applied = 1;
    if (hit) sound_creature(a->type,1,0,a->feet,(unsigned)index);
    if (actors_is_zombie(type) && motion->door_raw) {
        clear_door_attack(motion);
        motion->door_pause=.85f;
    }
    if (!environment && player_rules.mode==SURVIVAL && type==ACTOR_WOLF && !tamed_wolf(*a)) {
        for (int i=0;i<ACTOR_CAPACITY;++i) if (banks[d][i].type==ACTOR_WOLF &&
            !tamed_wolf(banks[d][i]) && distance2(a->feet,banks[d][i].feet)<12.f*12.f) {
            motions[d][i].anger=22.f;
            motions[d][i].target=-1;
        }
        ++revision;
    }
    if (killed) {
        Vec3 p=a->feet;
        if (player_rules.mode==SURVIVAL && !young && !environment && type!=ACTOR_VILLAGER &&
            type!=ACTOR_IRON_GOLEM && type!=ACTOR_SNOW_GOLEM && type!=ACTOR_SEEP_BEETLE &&
            type!=ACTOR_LANTERN_MOTH && type!=ACTOR_KILN_MOLE && type!=ACTOR_SILTBACK) {
            unsigned reward=actors_is_livestock(type) || type==ACTOR_SQUID ? experience_roll(1,3) : 5u;
            experience_reward((Vec3){p.x,p.y+.32f,p.z},reward);
        }
        printf("MOB DEFEATED %u\n",(unsigned)a->type);
        fflush(stdout);
        if (type==ACTOR_VILLAGER) villagers_hurt(index,0);
        remove_actor(a);
        lead_set(&ties[d][index],(LeadLink){LEAD_FREE,0,0,0});
        return type==ACTOR_VILLAGER ? "村民已死亡" : player_rules.mode != SURVIVAL ? "已移除生物" :
            type==ACTOR_FAULTBACK ? (drop==SINTERED_TILE_ITEM ? "裂岩甲兽倒下，掉出一块烧结岩砖" : "裂岩甲兽倒下，掉出圆石") :
            type==ACTOR_REEDLIZARD ? "岩簧蜥倒下了 · 完整鳍片只能在降温后轻敲取下" :
            young ? "幼崽没有成年动物的掉落物" :
            shorn ? "已击败剪过毛的羊，没有羊毛掉落" : "已击败生物，靠近拾取";
    }
    health-=(int)hit;
    a->health=(uint8_t)((health+HEALTH_UNITS-1)/HEALTH_UNITS);
    if (a->type==ACTOR_VILLAGER) villagers_hurt(index,a->health);
    motion->armor.health_debt=(uint8_t)(a->health*HEALTH_UNITS-health);
    ++revision;
    if (actors_is_livestock(a->type) || a->type==ACTOR_VILLAGER || a->type==ACTOR_OCELOT ||
        a->type==ACTOR_SEEP_BEETLE || a->type==ACTOR_KILN_MOLE || a->type==ACTOR_SILTBACK) {
        motion->flee=2.25f;
        if (motion->perch) cancel_perch(motion,3.f);
    }
    if (!environment) wolves_assist(d,index);
    if (spider_type(a->type) && !environment) motion->angry = 1;
    static const char *const hits[ACTOR_TYPES] = {0,"击中了猪","击中了苔壳行者","击中了潮帆鳐","击中了岩簧蜥",
        "击中了鸡","击中了蜘蛛","击中了羊","击中了骷髅","击中了牛","泥脊兽受惊跑开了",NULL,"击中了鸣壳兽","击中了村民",
        "击中巡护机外壳","击中脉冲机外壳","击中了狼","击中了猫科动物","击中了鱿鱼","击中了遇险居民","击中了潮砾蟹",
        "击中裂岩甲兽","击中了洞穴蜘蛛","击中了引光蛾","灰窑鼹受惊跑开了","击中了润脉甲"};
    return hits[a->type];
}
void actors_explosion_hits(const int damage[ACTOR_CAPACITY]) {
    for (int i=0;i<ACTOR_CAPACITY;++i) if (damage[i]>0 && banks[world_dimension()][i].type) {
        int applied=0;
        hurt_actor(i,damage[i],STRIKE_BLAST,&applied);
    }
}
void actors_piston_push(BlockBox sweep,int dir) {
    Dimension d=world_dimension();
    for (int i=0;i<ACTOR_CAPACITY;++i) {
        ActorRecord *a=&banks[d][i];
        if (!a->type) continue;
        Species s=body_for(a->type,care_banks[d][i].grow,a->traits);
        Vec3 v=a->feet,shift;
        int pushed=piston_nudge(sweep,dir,body_box(v.x,v.y,v.z,s.radius,s.height),&shift);
        if (pushed>0) {
            a->feet=(Vec3){v.x+shift.x,v.y+shift.y,v.z+shift.z};
            if (a->feet.y>=WY) a->feet.y=nextafterf((float)WY,0.f);
            if (shift.y) motions[d][i].fall=0;
            ++revision;
        } else if (pushed<0) {
            int applied=0;
            hurt_actor(i,1,STRIKE_ENVIRONMENT,&applied);
        }
    }
}
void actors_lightning(Vec3 where) {
    if (world_dimension()!=OVERWORLD) return;
    for (int i=0;i<ACTOR_CAPACITY;++i) {
        ActorRecord *a=&banks[OVERWORLD][i];
        if (!a->type) continue;
        Vec3 p=a->feet;
        float head=p.y+species[a->type].height;
        if (fabsf(p.x-where.x)>=3.f || fabsf(p.z-where.z)>=3.f ||
            p.y<where.y-1.f || p.y>=where.y+6.f ||
            !weather_wets((int)floorf(p.x),(int)floorf(head),(int)floorf(p.z))) continue;
        int applied=0;
        motions[OVERWORLD][i].flame=8.f;
        hurt_actor(i,5,STRIKE_FIRE,&applied);
    }
}
void actors_anvil_impact(BlockBox strike,int damage) {
    if (damage<=0) return;
    Dimension d=world_dimension();
    for (int i=0;i<ACTOR_CAPACITY;++i) {
        ActorRecord a=banks[d][i];
        if (!a.type) continue;
        Species s=body_for(a.type,care_banks[d][i].grow,a.traits);
        if (!box_overlap(strike,body_box(a.feet.x,a.feet.y,a.feet.z,s.radius,s.height))) continue;
        int applied=0;
        hurt_actor(i,damage,STRIKE_ENVIRONMENT,&applied);
    }
}
static float blocked_distance(Hit blocking,Vec3 eye,Vec3 ray,float distance) {
    if (blocking.hit) {
        int raw=world_cell(blocking.x,blocking.y,blocking.z),type=cell_type(raw);
        BlockBox b[BARRIER_PARTS];
        int count=1;
        if (barrier_type(type)) count=barrier_world_boxes(blocking.x,blocking.y,blocking.z,raw,
            door_type(type) ? BARRIER_BODY : BARRIER_VISUAL,b);
        else b[0]=(BlockBox){(float)blocking.x,(float)blocking.y,(float)blocking.z,
            blocking.x+1.f,blocking.y+1.f,blocking.z+1.f};
        for (int i=0;i<count;++i) {
            float t=box_time(eye,ray,(Vec3){b[i].x0,b[i].y0,b[i].z0},(Vec3){b[i].x1,b[i].y1,b[i].z1},distance);
            if (t>=0) distance=t;
        }
    }
    return distance;
}
static int aimed_actor_before(Vec3 eye,Vec3 ray,float *distance,int ignore_origin) {
    Dimension d=world_dimension();
    int index=-1;
    for (int i = 0; i < ACTOR_CAPACITY; ++i) if (banks[d][i].type) {
        Species s=body_for(banks[d][i].type,care_banks[d][i].grow,banks[d][i].traits);
        Vec3 p = banks[d][i].feet;
        Vec3 low={p.x-s.radius,p.y,p.z-s.radius},high={p.x+s.radius,p.y+s.height,p.z+s.radius};
        /* A trade needs an actual forward hit. A resident overlapping the
           camera must not swallow every tap meant for the world behind it. */
        if (ignore_origin && box_contains((BlockBox){low.x,low.y,low.z,high.x,high.y,high.z},eye.x,eye.y,eye.z)) continue;
        float t=box_time(eye,ray,low,high,*distance);
        if (t >= 0) { index = i; *distance = t; }
    }
    return index;
}
static int aimed_actor(Hit blocking,float distance,int ignore_origin) {
    if (!player_rules.health) return -1;
    Vec3 eye={px,py+1.55f,pz},ray=forward();
    distance=blocked_distance(blocking,eye,ray,distance);
    return aimed_actor_before(eye,ray,&distance,ignore_origin);
}
const char *actors_camp_machine_use(Hit blocking) {
    int i=aimed_actor(blocking,4.f,1);
    if (i<0 || (unsigned)selected_slot>=HOTBAR_SIZE) return NULL;
    Dimension d=world_dimension();
    ActorRecord *a=&banks[d][i];
    int pulse=a->type==ACTOR_SNOW_GOLEM;
    if (!pulse && a->type!=ACTOR_IRON_GOLEM) return NULL;
    ItemStack *held=&inventory[selected_slot];
    if (!held->count) {
        if (!pulse) return "岩甲巡护机 · 可用砌板修补";
        Vec3 p=a->feet;
        int cooled=camp_machine_cooled((int)floorf(p.x),(int)floorf(p.y+.04f),(int)floorf(p.z));
        return cooled ? "水冷脉冲机 · 冷却正常" : "水冷脉冲机 · 缺少冷却水";
    }
    if (held->type!=(pulse ? PUMP_GASKET : LAMINATE_ITEM)) return NULL;
    int max=pulse ? SNOW_GOLEM_HEALTH : CRAWLER_FACTORY_HEALTH;
    if (a->health>=max) return "设备完好，无需修补";
    int health=a->health+(pulse ? 8 : 24);
    a->health=(uint8_t)(health>max ? max : health);
    if (player_rules.mode==SURVIVAL && !--held->count) *held=(ItemStack){0};
    ++revision;
    return "已修补营地设备";
}
const char *actors_collect_shell(Hit blocking) {
    int i=aimed_actor(blocking,3.85f,1);
    const Dimension d=world_dimension();
    if (i<0 || banks[d][i].type!=ACTOR_SHELLBACK) return NULL;
    ActorRecord *a=&banks[d][i];
    if (a->traits!=SHELLBACK_RIPE) return "壳片还在生长 · 鸣壳兽需要站在裸露的石灰岩上";
    if (!inventory_add(SHELL_FLAKE)) return "背包已满，壳片还在";
    a->traits=0;
    motions[d][i].shell_deposition=0.f;
    sound_creature(a->type,0,1,a->feet,motions[d][i].random);
    ++revision;
    return "轻敲取下一片鸣壳片 · 鸣壳兽未受伤";
}
const char *actors_collect_reed(Hit blocking) {
    int i=aimed_actor(blocking,4.1f,1);
    Dimension d=world_dimension();
    if (i<0 || banks[d][i].type!=ACTOR_REEDLIZARD) return NULL;
    ActorRecord *a=&banks[d][i];
    if (a->traits&REED_HARVESTED) return "这只岩簧蜥的松鳍已经取下";
    if (motions[d][i].ram_phase==REED_WARN) return "岩簧蜥正在蓄汽 · 侧移或用雪球打断";
    if (motions[d][i].ram_phase!=REED_COOLED) return "用雪球让岩簧蜥降温，再轻敲松开的陶鳍";
    if (!inventory_add(REED_FIN)) return "背包已满，陶鳍还在";
    a->traits=REED_HARVESTED;
    sound_play(SOUND_REEDLIZARD,.71f,3,a->feet);
    ++revision;
    return "已取下陶鳍片 · 岩簧蜥未受伤";
}
const char *actors_sound_ray(Hit blocking) {
    int i=aimed_actor(blocking,4.15f,1);
    const Dimension d=world_dimension();
    if (i<0 || banks[d][i].type!=ACTOR_TIDERAY) return NULL;
    Vec3 p=banks[d][i].feet;
    p.y+=.25f;
    return tide_report(p,1);
}
const char *actors_probe_faultback(Hit blocking) {
    int i=aimed_actor(blocking,4.15f,1);
    const Dimension d=world_dimension();
    if (i<0 || banks[d][i].type!=ACTOR_FAULTBACK) return NULL;
    int phase=actors_faultback_phase(d,i);
    if (phase==FAULTBACK_CALM) return "裂岩甲兽 · 已排压，背脊冷却";
    if (phase==FAULTBACK_RECOVER) return "裂岩甲兽 · 冲刺后正在恢复";
    if (phase==FAULTBACK_WINDUP || phase==FAULTBACK_CHARGE) return "裂岩甲兽 · 脊纹过热，侧移避开直线";
    return inside_pressure_works() ? "裂岩甲兽 · 侧翼离开冲刺路线，开启本侧排压阀可平息" :
        "裂岩甲兽 · 脊纹亮起时会沿直线冲刺";
}
uint16_t actors_name_id(Dimension d,int slot) {
    return (unsigned)d<DIMENSION_COUNT && (unsigned)slot<ACTOR_CAPACITY && banks[d][slot].type ?
        name_ids[d][slot] : 0;
}
int actors_set_name(Dimension d,int slot,uint16_t name) {
    if ((unsigned)d>=DIMENSION_COUNT || (unsigned)slot>=ACTOR_CAPACITY || !banks[d][slot].type ||
        (name && !*item_name_for_id(name))) return 0;
    name_ids[d][slot]=name;
    if (banks[d][slot].type==ACTOR_VILLAGER) villagers_set_actor_name(slot,name);
    return 1;
}
typedef struct { int actor, horse; } NameTarget;
/* Include unnamed mobs when picking so the label or tag never goes through
   another mob or a solid block to the named one behind it. */
static NameTarget name_target(Hit blocking,float reach) {
    NameTarget hit={-1,-1};
    if (!player_rules.health) return hit;
    Dimension d=world_dimension();
    Vec3 eye={px,py+1.55f,pz},ray=forward();
    float distance=blocked_distance(blocking,eye,ray,reach);
    hit.actor=aimed_actor_before(eye,ray,&distance,1);
    int ridden=horse_rider();
    for (int i=0;i<HORSE_CAPACITY;++i) if (i!=ridden) {
        BlockBox b;
        if (!horse_body_at(d,i,&b) || box_contains(b,eye.x,eye.y,eye.z)) continue;
        float t=box_time(eye,ray,(Vec3){b.x0,b.y0,b.z0},(Vec3){b.x1,b.y1,b.z1},distance);
        if (t>=0) { hit.horse=i; hit.actor=-1; distance=t; }
    }
    return hit;
}
const char *actors_aimed_name(Vec3 *head) {
    if (!head) return "";
    Dimension d=world_dimension();
    Vec3 player={px,py,pz};
    int any=0;
    for (int i=0;i<ACTOR_CAPACITY && !any;++i)
        any=actors_name_id(d,i) && distance2(player,banks[d][i].feet)<9.f*9.f;
    const HorseRecord *herd=horses_in(d);
    for (int i=0;i<HORSE_CAPACITY && !any;++i)
        any=horse_name_id(d,i) && distance2(player,herd[i].feet)<9.f*9.f;
    if (!any) return "";
    NameTarget hit=name_target(target(),5.f);
    uint16_t id=hit.actor>=0 ? actors_name_id(d,hit.actor) : horse_name_id(d,hit.horse);
    if (!id) return "";
    BlockBox b;
    if (!(hit.actor>=0 ? actors_body_at(d,hit.actor,&b) : horse_body_at(d,hit.horse,&b))) return "";
    *head=(Vec3){(b.x0+b.x1)*.5f,b.y1+.18f,(b.z0+b.z1)*.5f};
    return item_name_for_id(id);
}
const char *actors_use_name_tag(Hit blocking) {
    if ((unsigned)selected_slot>=HOTBAR_SIZE) return NULL;
    ItemStack *held=&inventory[selected_slot];
    if (held->type!=NAME_TAG || !held->count) return NULL;
    NameTarget hit=name_target(blocking,player_rules.mode==CREATIVE ? 5.f : 3.2f);
    if (hit.actor<0 && hit.horse<0) return NULL;
    uint16_t id=(uint16_t)(held->custom>>16);
    if (!id) return "先在铁砧上给命名牌写名字";
    Dimension d=world_dimension();
    if (!(hit.actor>=0 ? actors_set_name(d,hit.actor,id) : horse_set_name(d,hit.horse,id))) return NULL;
    if (player_rules.mode==SURVIVAL && !--held->count) *held=(ItemStack){0};
    return "已为生物命名";
}
int actors_aimed_villager(Hit blocking) {
    if (world_dimension()!=OVERWORLD) return -1;
    int i=aimed_actor(blocking,4.5f,1);
    return i>=0 && banks[OVERWORLD][i].type==ACTOR_VILLAGER ? i : -1;
}
const char *actors_resupply_guide(Hit blocking) {
    if ((unsigned)selected_slot>=HOTBAR_SIZE) return NULL;
    ItemStack *held=&inventory[selected_slot];
    if (held->count && !field_flask_item(held->type)) return NULL;
    int i=aimed_actor(blocking,4.f,0);
    Dimension d=world_dimension();
    if (i<0 || banks[d][i].type!=ACTOR_STRANDED_GUIDE) return NULL;
    Motion *m=&motions[d][i];
    const int site=guide_site(banks[d][i].feet);
    if (site==GUIDE_NEEDS_CLEARANCE) return "接应暂停 · 居民脚下要干燥，头顶留空";
    if (site==GUIDE_NEEDS_MARKER) return "在居民旁边一格、同一高度放回声路标";
    if (m->rest_ticks) return "补给已接收 · 请保留路标，等居民起身";
    if (held->count!=1 || held->type<=FIELD_FLASK_EMPTY) return "拿装过水的水囊轻点居民，给一口补给";
    m->rest_ticks=GUIDE_REST_TICKS;
    --held->type;
    ++revision;
    sound_play(SOUND_SPLASH,.26f,1.f,banks[d][i].feet);
    return "已交给居民一口水 · 原处休息后恢复交易";
}
const char *actors_shear(Hit blocking) {
    if (selected_slot<0 || selected_slot>=HOTBAR_SIZE ||
        inventory[selected_slot].type!=SHEARS || !inventory[selected_slot].count) return NULL;
    int index=aimed_actor(blocking,3.2f,0);
    if (index<0) return NULL;
    Dimension d=world_dimension();
    ActorRecord *a=&banks[d][index];
    if (a->type!=ACTOR_SHEEP) return NULL;
    if (care_banks[d][index].grow) return "小羊还不能剪毛";
    if (a->traits&SHEEP_SHORN) return "这只羊还没长出新毛";
    int slot=0;
    while (slot<MAXDROP && drops[slot].item.count) ++slot;
    if (slot==MAXDROP) return "掉落区已满，未剪掉羊毛";
    unsigned count=1+next(&motions[d][index].random)%3u;
    drops[slot]=make_drop(a->feet.x,a->feet.y+.35f,a->feet.z,
        (ItemStack){WOOL,(uint8_t)count,0,0,0});
    a->traits=SHEEP_SHORN; motions[d][index].grazing=0;
    drops_dirty=1; ++revision;
    ItemStack *held=&inventory[selected_slot];
    if (player_rules.mode==SURVIVAL) enchant_wear(held,1,0);
    return "已剪下羊毛，走近拾取";
}

const char *actors_use_lead(Hit blocking) {
    if (selected_slot<0 || selected_slot>=HOTBAR_SIZE ||
        (inventory[selected_slot].count && inventory[selected_slot].type!=LEAD)) return NULL;
    int i=aimed_actor(blocking,3.2f,0);
    if (i<0) return NULL;
    Dimension d=world_dimension();
    if (!can_lead(banks[d][i])) return NULL;
    const char *result=lead_use_animal(&ties[d][i],banks[d][i].feet);
    if (result && ties[d][i].kind==LEAD_PLAYER) care_banks[d][i].managed=1;
    return result;
}

static int wolf_food(int type) {
    switch (type) {
    case RAW_PORK: case COOKED_PORK: case RAW_BEEF: case COOKED_BEEF:
    case RAW_CHICKEN: case COOKED_CHICKEN: case ROTTEN_FLESH:
        return item_def(type)->food;
    default: return 0;
    }
}
static void pet_eat(ItemStack *held) {
    if (player_rules.mode==SURVIVAL && !--held->count) *held=(ItemStack){0};
}
const char *actors_use_pet(Hit blocking) {
    if (!player_rules.health || (unsigned)selected_slot>=HOTBAR_SIZE) return NULL;
    ItemStack *held=&inventory[selected_slot];
    int bone=held->count && held->type==BONE,meat=held->count ? wolf_food(held->type) : 0;
    int fish=held->count && cat_food(held->type);
    if (held->count && !bone && !meat && !fish) return NULL;
    int i=aimed_actor(blocking,3.2f,1);
    Dimension d=world_dimension();
    if (i<0 || !pet_species(banks[d][i].type)) return NULL;
    ActorRecord *a=&banks[d][i];
    ActorCare *c=&care_banks[d][i];
    Motion *m=&motions[d][i];
    int cat=a->type==ACTOR_OCELOT,food=cat ? fish : meat;
    if (!pet_tamed(*a)) {
        if (cat) {
            if (!fish) return NULL;
            Vec3 player={px,py,pz};
            if (m->flee>0.f || !fish_ready(d,player) || distance2(a->feet,player)>3.f*3.f)
                return "拿着生鱼站稳，等豹猫靠近再喂";
        } else {
            if (!bone) return NULL;
            if (m->anger>0.f) return "狼群正在警戒，等它平静再用骨头";
        }
        pet_eat(held);
        if (next(&m->random)%3u) return cat ? "这次没有驯服豹猫，可以再喂一条生鱼" :
            "这次没有驯服狼，可以再给一根骨头";
        a->traits=PET_TAMED|PET_SITTING;
        if (cat) a->traits|=(uint8_t)(CAT_TUXEDO*(1u+next(&m->random)%3u));
        a->health=cat ? CAT_HEALTH : WOLF_TAMED_HEALTH;
        c->managed=1;
        m->target=m->commanded=-1;
        m->target_scan=m->attack=m->order=0.f;
        ++revision;
        sound_creature(a->type,0,cat,a->feet,(unsigned)i);
        return cat ? "豹猫成了家猫；切到空手轻点让它跟随" : "已驯服；切到空手轻点让它跟随";
    }
    if (held->count && !food)
        return !cat && bone ? "已驯服的狼吃肉；空手轻点可以坐下或跟随" : NULL;
    if (food) {
        const char *result;
        if (!cat && a->health<WOLF_TAMED_HEALTH) {
            a->health=(uint8_t)fminf(a->health+food,WOLF_TAMED_HEALTH);
            result="已喂肉，恢复生命";
        } else if (c->grow) {
            if (cat) return "幼猫还没长大";
            unsigned speed=1u+c->grow/10u;
            c->grow=c->grow>speed ? (uint16_t)(c->grow-speed) : 0;
            result="已喂幼狼，加快成长";
        } else {
            if (pet_sitting(*a) || m->resting) return cat ? "先空手轻点站起，再喂鱼繁殖" : "先空手轻点站起，再喂肉繁殖";
            if (c->cooldown || c->love) return "暂时不能再次繁殖";
            c->love=ANIMAL_LOVE_TICKS;
            m->mate=-1;
            result=cat ? "再给附近另一只站立的成年猫喂生鱼即可繁殖" : "再给附近另一只站立的成年狼喂肉即可繁殖";
        }
        pet_eat(held);
        ++revision;
        return result;
    }
    if (cat && m->resting) {
        cancel_perch(m,4.f);
        sound_creature(a->type,0,0,a->feet,(unsigned)i);
        return "猫从家具上站起来了";
    }
    if (cat && m->perch) cancel_perch(m,4.f);
    a->traits^=PET_SITTING;
    m->target=m->commanded=m->mate=-1;
    m->order=m->anger=m->target_scan=m->think=0.f;
    ++revision;
    sound_creature(a->type,0,cat && pet_sitting(*a),a->feet,(unsigned)i);
    return cat ? (pet_sitting(*a) ? "猫会坐在这里等你" : "猫现在会跟随你") :
        pet_sitting(*a) ? "狼会坐在这里等你" : "狼现在会跟随你";
}

const char *actors_use_tide_crab(Hit blocking) {
    if (!player_rules.health || (unsigned)selected_slot>=HOTBAR_SIZE) return NULL;
    ItemStack held=inventory[selected_slot];
    if (held.count && held.type!=GRAVEL) return NULL;
    int i=aimed_actor(blocking,3.6f,0);
    Dimension d=world_dimension();
    if (i<0 || banks[d][i].type!=ACTOR_TIDE_CRAB) return NULL;
    Vec3 feet=banks[d][i].feet;
    if (!crab_shore(feet,NULL)) return "潮砾蟹只在水边的岩岸筛砂";
    ActorCare *care=&care_banks[d][i];
    if (care->cooldown) return "潮砾蟹还在筛砂，稍等再来";
    if (!held.count) return "潮砾蟹 · 拿一份砂砾轻点，可换潮磨砂";
    ItemStack after[SLOT_COUNT];
    if (!inventory_plan_exchange(after,selected_slot,1,(ItemStack){TIDE_GRIT,1,0,0,0}))
        return "背包放不下潮磨砂，未使用砂砾";
    memcpy(inventory,after,sizeof after);
    care->cooldown=TIDE_CRAB_WORK_TICKS;
    sound_play(SOUND_TIDE_CRAB,.72f,1,feet);
    ++revision;
    return "已用砂砾换得潮磨砂";
}
const char *actors_use_kiln_mole(Hit blocking) {
    if (!player_rules.health || (unsigned)selected_slot>=HOTBAR_SIZE) return NULL;
    ItemStack held=inventory[selected_slot];
    if (held.count && held.type!=CLAY_BALL) return NULL;
    int i=aimed_actor(blocking,3.4f,1);
    Dimension d=world_dimension();
    if (i<0 || banks[d][i].type!=ACTOR_KILN_MOLE) return NULL;
    if (motions[d][i].flee>0.f) return "灰窑鼹还没平静下来";
    int charged=0;
    Hit source={0};
    (void)mole_benches(banks[d][i].feet,&source,&charged);
    if (!charged) return "灰窑鼹 · 旁边需要一台有蓄热的余热回温台";
    if (charged>1) return "灰窑鼹 · 旁边只能有一台带蓄热的回温台";
    if (!held.count) return "灰窑鼹 · 拿黏土球轻点，压制一枚暖陶芯";
    return heat_bench_core(source,CLAY_BALL);
}
const char *actors_use_livestock(Hit blocking) {
    if (!player_rules.health || selected_slot<0 || selected_slot>=HOTBAR_SIZE) return NULL;
    ItemStack *held=&inventory[selected_slot];
    if (!held->count) return NULL;
    if (held->type==MILK_BUCKET) {
        player_effects=(PlayerEffects){0};
        if (player_rules.mode==SURVIVAL) *held=(ItemStack){BUCKET,1,0,0,0};
        return "已喝牛奶，清除了状态效果";
    }
    if (held->type==EGG) {
        Vec3 aim=forward();
        Vec3 eye={px,py+1.55f,pz};
        Vec3 to={eye.x+aim.x*10.f,eye.y+aim.y*10.f,eye.z+aim.z*10.f};
        if (!launch_projectile(eye,to,THROWN_EGG)) return "还不能扔出更多鸡蛋";
        if (player_rules.mode==SURVIVAL && !--held->count) *held=(ItemStack){0,0,0,0,0};
        return "已掷出鸡蛋";
    }
    if (held->type!=WHEAT && held->type!=WHEAT_SEEDS && held->type!=CARROT && held->type!=BUCKET) return NULL;
    int index=aimed_actor(blocking,3.2f,0);
    if (index<0) return NULL;
    Dimension d=world_dimension();
    ActorRecord *a=&banks[d][index];
    ActorCare *c=&care_banks[d][index];
    if (held->type==BUCKET && actors_is_bovine(a->type)) {
        if (c->grow) return "小牛还不能产奶";
        ItemStack milk={MILK_BUCKET,1,0,0,0};
        if (!inventory_fill_container(selected_slot,milk)) return "背包没有位置放牛奶桶";
        return "已装满牛奶桶";
    }
    if (held->type!=feed_type(a->type)) return NULL;
    if (c->grow) return "这只幼崽还没长大";
    if (c->cooldown) return "刚繁殖过，暂时不能再喂";
    if (c->love) return "已经喂过这只动物了";
    c->managed=1;
    c->love=ANIMAL_LOVE_TICKS;
    motions[d][index].mate=-1;
    ++revision;
    if (player_rules.mode==SURVIVAL && !--held->count) *held=(ItemStack){0,0,0,0,0};
    return "已喂养，再喂一只同类即可繁殖";
}
const char *actors_attack(Hit blocking) {
    if ((unsigned)selected_slot>=HOTBAR_SIZE) return NULL;
    int index=aimed_actor(blocking,player_rules.mode==CREATIVE ? 5.f : 3.1f,0);
    if (index<0) return NULL;
    ItemStack *held = &inventory[selected_slot];
    const ItemDef *item = item_def(held->type);
    int damage = item && item->melee ? item->melee : 1 + (item ? item->pick_tier : 0);
    int type=banks[world_dimension()][index].type;
    damage+=enchant_damage(*held,actors_is_zombie(type) || type==ACTOR_SKELETON,
        spider_type(type));
    if (player_rules.mode!=CREATIVE) damage=weakened_melee(damage,player_effects.weakness);
    if (damage<=0) return "虚弱状态下没有造成伤害";
    int applied = 0;
    const char *result = hurt_actor(index,damage,STRIKE_MELEE,&applied);
    if (applied && player_rules.mode == SURVIVAL) {
        player_rules.exhaustion += .1f;
        if (held->count && item && item->durability && (item->pick_tier || item->melee)) {
            enchant_wear(held,item->melee && !is_axe(held->type) ? 1u : 2u,0);
        }
    }
    return result;
}

int actors_hit_arrow_from(Vec3 point,int damage,int player) {
    if (damage<=0) return 0;
    int i=actor_at_point(point,-1);
    if (i<0) return 0;
    int applied=0;
    hurt_actor(i,damage,player ? STRIKE_ARROW : STRIKE_TRAP_ARROW,&applied);
    return 1;
}
int actors_hit_arrow(Vec3 point,int damage) { return actors_hit_arrow_from(point,damage,1); }
int actors_body_at(Dimension d,int slot,BlockBox *out) {
    if (!out || (unsigned)d>=DIMENSION_COUNT || (unsigned)slot>=ACTOR_CAPACITY) return 0;
    const ActorRecord *a=&banks[d][slot];
    if (!a->type || !a->health) return 0;
    Species s=body_for(a->type,care_banks[d][slot].grow,a->traits);
    *out=body_box(a->feet.x,a->feet.y,a->feet.z,s.radius,s.height);
    return 1;
}
int actors_overlap_cell(int x, int y, int z) {
    Dimension d=world_dimension();
    const ActorRecord *bank=banks[d];
    for (int i = 0; i < ACTOR_CAPACITY; ++i) if (bank[i].type) {
        Species s=body_for(bank[i].type,care_banks[d][i].grow,bank[i].traits);
        Vec3 p = bank[i].feet;
        if (p.x+s.radius > x && p.x-s.radius < x+1 && p.z+s.radius > z &&
            p.z-s.radius < z+1 && p.y+s.height > y && p.y < y+1) return 1;
    }
    return 0;
}
