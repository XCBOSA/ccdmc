#include "fishing.h"
#include "drops.h"
#include "experience.h"
#include "enchanting.h"
#include "weather.h"
#include "../world/light.h"
#include "../world/space.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    Vec3 point,velocity;
    Dimension dimension;
    FishPhase phase;
    float time,span,age,surface,angle,check,water_clock_rate;
    uint32_t random;
    int slot,water_y;
    uint16_t rod_damage;
    uint64_t rod_enchants;
    ItemStack catch_item;
} Cast;
static Cast cast;
static const char *message;
static char outcome[96];

static Vec3 add(Vec3 a,Vec3 b) { return (Vec3){a.x+b.x,a.y+b.y,a.z+b.z}; }
static Vec3 mul(Vec3 a,float b) { return (Vec3){a.x*b,a.y*b,a.z*b}; }
static Vec3 eye(void) { return (Vec3){px,py+1.55f,pz}; }
static int valid(Vec3 p) {
    return fabsf(p.x)<WORLD_SAVE_LIMIT && fabsf(p.z)<WORLD_SAVE_LIMIT && p.y>=0 && p.y<WY+32;
}
static uint32_t next(uint32_t *random) {
    uint32_t n=*random;
    n^=n<<13; n^=n>>17; n^=n<<5;
    return *random=n;
}
static unsigned draw(uint32_t *random,unsigned range) {
    /* Rejection removes modulo bias without changing the world's RNG. */
    const uint32_t threshold=(uint32_t)(0u-range)%range;
    uint32_t r;
    do { r=next(random); } while (r<threshold);
    return r%range;
}
static unsigned choose(unsigned range) { return draw(&cast.random,range); }

static void phase(FishPhase value,float seconds) {
    cast.phase=value;
    cast.time=cast.span=seconds;
}
static void begin_wait(void) {
    /* Classic casting works in any source/flowing water, not only open ocean. */
    int lure=enchant_level(cast.rod_enchants,ENCH_LURE);
    float wait=5.f+(float)choose(501)*.05f-5.f*lure;
    phase(FISH_WAIT,wait>.05f ? wait : .05f);
}
static void start_approach(void) {
    cast.angle=(float)choose(720)*.00872664626f;
    phase(FISH_APPROACH,.9f+(float)choose(24)*.05f);
    message="鱼靠近了，等浮漂下沉";
}
typedef struct { uint16_t type; uint8_t weight,count; } CatchChoice;
static ItemStack choose_catch(uint32_t *random,const CatchChoice *choices,size_t count) {
    unsigned total=0;
    for (size_t i=0;i<count;++i) total+=choices[i].weight;
    unsigned index=draw(random,total);
    for (size_t i=0;i<count;++i) {
        if (index<choices[i].weight) return (ItemStack){choices[i].type,choices[i].count,0,0,0};
        index-=choices[i].weight;
    }
    return (ItemStack){RAW_FISH,1,0,0,0};
}
ItemStack fishing_roll_catch(uint32_t *random,uint64_t rod_enchants) {
    if (!random) return (ItemStack){0};
    if (!*random) *random=UINT32_C(0xa37412d9);
    static const CatchChoice fish[]={{RAW_FISH,60,1},{RAW_SALMON,25,1},{CLOWNFISH,2,1},{PUFFERFISH,13,1}};
    static const CatchChoice junk[]={{LEATHER_BOOTS,10,1},{LEATHER,10,1},{BONE,10,1},
        {WATER_BOTTLE,10,1},{STRING,5,1},{FISHING_ROD,2,1},{BOWL,10,1},{STICK,5,1},
        {INK_SAC,1,10},{TRIPWIRE_HOOK_ITEM,10,1},{ROTTEN_FLESH,10,1}};
    static const CatchChoice treasure[]={{SADDLE,1,1},{FISHING_ROD,1,1},{BOW,1,1},{ENCHANTED_BOOK,1,1},
        {NAME_TAG,1,1},{LILY_PAD_ITEM,1,1}};
    int luck=enchant_level(rod_enchants,ENCH_LUCK_OF_THE_SEA);
    int lure=enchant_level(rod_enchants,ENCH_LURE);
    int junk_rate=100-25*luck-10*lure,treasure_rate=50+10*luck-10*lure;
    int category=(int)draw(random,1000);
    if (category<junk_rate) {
        ItemStack item=choose_catch(random,junk,sizeof junk/sizeof *junk);
        if (item.type==LEATHER_BOOTS || item.type==FISHING_ROD) {
            const unsigned life=item_def(item.type)->durability;
            item.damage=(uint16_t)(life/2+draw(random,life-life/2));
        }
        return item;
    }
    if (category<(junk_rate>0 ? junk_rate : 0)+treasure_rate) {
        ItemStack item=choose_catch(random,treasure,sizeof treasure/sizeof *treasure);
        if (item.type==FISHING_ROD || item.type==BOW || item.type==ENCHANTED_BOOK) {
            unsigned life=item_def(item.type)->durability;
            if (life) item.damage=(uint16_t)(life/4+draw(random,life/2));
            item.enchants=enchant_fishing_treasure(item.type,random);
        }
        return item;
    }
    return choose_catch(random,fish,sizeof fish/sizeof *fish);
}

void fishing_cancel(void) {
    phase(FISH_IDLE,0);
    cast.catch_item=(ItemStack){0,0,0,0,0};
    message=NULL;
}
void fishing_reset(void) { memset(&cast,0,sizeof cast); message=NULL; }
int fishing_active(void) { return cast.phase!=FISH_IDLE && cast.dimension==world_dimension(); }
const char *fishing_take_message(void) {
    const char *m=message;
    message=NULL;
    return m;
}
static int equipped(void) {
    if (!player_rules.health || cast.dimension!=world_dimension() ||
        cast.slot!=selected_slot || selected_slot<0 || selected_slot>=HOTBAR_SIZE) return 0;
    const ItemStack *rod=&inventory[selected_slot];
    return rod->type==FISHING_ROD && rod->count && rod->damage==cast.rod_damage && rod->enchants==cast.rod_enchants;
}
static int surface_at(int x,int y,int z,float *surface) {
    int cell=world_cell(x,y,z), above=world_cell(x,y+1,z);
    if (!water_strength(cell) || water_strength(above) || solid(cell_type(above))) return 0;
    *surface=y+water_surface(cell,above);
    return 1;
}
static int still_in_water(void) {
    int x=(int)floorf(cast.point.x),z=(int)floorf(cast.point.z);
    return surface_at(x,cast.water_y,z,&cast.surface);
}
static int line_clear(void) {
    Vec3 a=eye(), b=cast.point;
    if (!valid(a) || !valid(b)) return 0;
    Vec3 d={b.x-a.x,b.y-a.y,b.z-a.z};
    float distance=sqrtf(d.x*d.x+d.y*d.y+d.z*d.z);
    if (distance>33.f) return 0;
    int n=(int)ceilf(distance*7.f);
    for (int i=1;i<n;++i) {
        Vec3 q=add(a,mul(d,(float)i/n));
        if (world_point_blocked(q.x,q.y,q.z,0)) return 0;
    }
    return 1;
}
static void wear(int amount) {
    ItemStack *rod=&inventory[selected_slot];
    if (player_rules.mode==SURVIVAL) enchant_wear(rod,(unsigned)amount,0);
}
static const char *land_catch(void) {
    ItemStack item=cast.catch_item;
    int dropped=0;
    if (!inventory_add_stack(item)) {
        int slot=0;
        while (slot<MAXDROP && drops[slot].item.count) ++slot;
        if (slot==MAXDROP) {
            /* Use remains retryable. No reroll and no double consumption. */
            phase(FISH_LANDED,0);
            return "背包和掉落区都满了，腾出空间后再收下鱼获";
        }
        drops[slot]=make_drop(px,py+.35f,pz,item);
        drops_dirty=1;
        dropped=1;
    }
    wear(1);
    experience_reward((Vec3){px,py+.32f,pz},experience_roll(1,6));
    fishing_cancel();
    if (item.count>1)
        snprintf(outcome,sizeof outcome,"钓到了 %u 个%s%s",(unsigned)item.count,item_name(item.type),dropped ? "，已落在身边" : "");
    else snprintf(outcome,sizeof outcome,"钓到了%s%s",item_name(item.type),dropped ? "，已落在身边" : "");
    return outcome;
}

const char *fishing_use(void) {
    if (selected_slot<0 || selected_slot>=HOTBAR_SIZE || !player_rules.health) return NULL;
    const ItemStack *rod=&inventory[selected_slot];
    if (rod->type!=FISHING_ROD || !rod->count) return NULL;
    if (fishing_active() && equipped()) {
        if (cast.phase==FISH_LANDED) return land_catch();
        if (cast.phase==FISH_BITE && still_in_water() && line_clear()) {
            cast.catch_item=fishing_roll_catch(&cast.random,cast.rod_enchants);
            return land_catch();
        }
        int stuck=cast.phase==FISH_GROUNDED;
        if (stuck) wear(2);
        fishing_cancel();
        return stuck ? "从障碍物上收回鱼钩" : "收竿太早或已错过咬钩";
    }
    fishing_cancel();
    Vec3 p=eye(), direction=forward();
    if (!valid(p)) return "现在无法抛竿";
    if (!cast.random) {
        cast.random=world_seed_mix32()^hash2((int)floorf(px),(int)floorf(pz))^player_rules.day_tick;
        if (!cast.random) cast.random=UINT32_C(0xa37412d9);
    }
    cast.point=p;
    cast.velocity=mul(direction,15.f);
    cast.velocity.y+=1.1f;
    cast.slot=selected_slot; cast.rod_damage=rod->damage; cast.rod_enchants=rod->enchants; cast.dimension=world_dimension();
    cast.check=0; cast.age=0; cast.water_clock_rate=1.f;
    phase(FISH_CAST,3.6f);
    return "已抛竿，观察浮漂";
}

static void step_cast(float dt) {
    cast.velocity.y-=17.f*dt;
    Vec3 movement=mul(cast.velocity,dt);
    int steps=(int)(sqrtf(movement.x*movement.x+movement.y*movement.y+movement.z*movement.z)/.11f)+1;
    Vec3 step=mul(movement,1.f/steps);
    for (int i=0;i<steps;++i) {
        Vec3 q=add(cast.point,step);
        if (!valid(q) || hypotf(q.x-px,q.z-pz)>33.f) {
            fishing_cancel(); message="鱼线超出范围"; return;
        }
        int x=(int)floorf(q.x),y=(int)floorf(q.y),z=(int)floorf(q.z);
        int block_type=block(x,y,z);
        if (block_type==LAVA) {
            fishing_cancel(); message="浮漂被熔岩烧毁"; return;
        }
        if (world_point_blocked(q.x,q.y,q.z,0)) {
            phase(FISH_GROUNDED,0); message="鱼钩没有落入水中"; return;
        }
        cast.point=q;
        if (water_strength(block_type) && surface_at(x,y,z,&cast.surface) && q.y<=cast.surface) {
            cast.water_y=y; cast.point.y=cast.surface;
            begin_wait(); message="浮漂入水，等待咬钩"; return;
        }
    }
    if (cast.time<=0) { fishing_cancel(); message="浮漂未落入可钓的水面"; }
}
void fishing_step(float dt) {
    if (cast.phase==FISH_IDLE || !(dt>0.f && dt<=.1f)) return;
    if (!equipped()) { fishing_cancel(); return; }
    if (cast.phase==FISH_LANDED) return;
    Vec3 p=eye();
    float dx=cast.point.x-p.x,dy=cast.point.y-p.y,dz=cast.point.z-p.z;
    if (dx*dx+dy*dy+dz*dz>33.f*33.f) {
        fishing_cancel(); message="离浮漂太远，已收线"; return;
    }
    cast.age+=dt;
    if (cast.phase==FISH_CAST) { cast.time-=dt; step_cast(dt); return; }
    if (cast.phase==FISH_GROUNDED) return;
    cast.check-=dt;
    if (cast.check<=0.f) {
        cast.check=.18f;
        if (!still_in_water() || !line_clear()) {
            fishing_cancel(); message="水面或鱼线被挡住，已收线"; return;
        }
        cast.point.y=cast.surface;
        int x=(int)floorf(cast.point.x),z=(int)floorf(cast.point.z),air=cast.water_y+1;
        cast.water_clock_rate=!light_has_open_sky(x,air,z) ? .5f : weather_wets(x,air,z) ? 1.25f : 1.f;
    }
    cast.time-=dt*(cast.phase==FISH_BITE ? 1.f : cast.water_clock_rate);
    if (cast.time>0) return;
    if (cast.phase==FISH_WAIT) start_approach();
    else if (cast.phase==FISH_APPROACH) {
        phase(FISH_BITE,1.f+(float)choose(13)*.05f);
        message="浮漂下沉！现在收竿";
    } else {
        begin_wait(); message="鱼跑了，继续等下一次";
    }
}
FishingView fishing_view(void) {
    FishingView out={FISH_IDLE,{0,0,0},{0,0,0},0};
    if (!fishing_active() || cast.phase==FISH_LANDED) return out;
    out.phase=cast.phase; out.point=cast.point; out.wake=cast.point;
    if (cast.phase>=FISH_WAIT) {
        out.point.y+=cast.phase==FISH_BITE ? -.16f : .02f+sinf(cast.age*5.f)*.013f;
        if (cast.phase==FISH_BITE) out.ripple=.16f+.32f*(1.f-cast.time/cast.span);
        if (cast.phase==FISH_APPROACH) {
            float path=cast.time/cast.span;
            out.wake.x+=cosf(cast.angle)*path*1.55f;
            out.wake.z+=sinf(cast.angle)*path*1.55f;
            out.ripple=.14f;
        }
        if (out.ripple>0) {
            float sample;
            if (surface_at((int)floorf(out.wake.x),cast.water_y,(int)floorf(out.wake.z),&sample))
                out.wake.y=sample+.012f;
            else out.ripple=0;
        }
    }
    return out;
}
