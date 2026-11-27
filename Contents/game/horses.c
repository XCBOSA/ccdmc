#include "horses.h"
#include "actors.h"
#include "drops.h"
#include "experience.h"
#include "enchanting.h"
#include "item_custom.h"
#include "boats.h"
#include "minecarts.h"
#include "sound.h"
#include "../world/space.h"
#include <math.h>
#include <string.h>

static HorseRecord herd[DIMENSION_COUNT][HORSE_CAPACITY];
static uint16_t name_ids[DIMENSION_COUNT][HORSE_CAPACITY];
static LeadLink ties[DIMENSION_COUNT][HORSE_CAPACITY];
static float vertical[DIMENSION_COUNT][HORSE_CAPACITY];
static float vocal_clock[DIMENSION_COUNT][HORSE_CAPACITY];
static int rider=-1;
static Dimension mounted_dim;
static float tame_wait, spawn_clock, day_fraction;
static unsigned serial, revision;
static const float offsets[][2]={{1,0},{-1,0},{0,1},{0,-1},{.71f,.71f},{-.71f,.71f},{.71f,-.71f},{-.71f,-.71f}};
static const ItemStack empty={0,0,0,0,0};

static float clamp(float v,float lo,float hi) { return fminf(fmaxf(v,lo),hi); }
static float seat_y(float y) { return y+1.04f; }
static float radius(const HorseRecord *h) { return h->age ? .37f : .58f; }
static float height(const HorseRecord *h) { return h->age ? 1.f : 1.65f; }
const char *horse_name(int type) {
    return type==EQUINE_HORSE ? "马" : type==EQUINE_DONKEY ? "驴" : type==EQUINE_MULE ? "骡" : "";
}
unsigned horses_revision(void) { return revision; }
const HorseRecord *horses_in(Dimension d) { return (unsigned)d<DIMENSION_COUNT ? herd[d] : NULL; }
uint16_t horse_name_id(Dimension d,int slot) {
    return (unsigned)d<DIMENSION_COUNT && (unsigned)slot<HORSE_CAPACITY && herd[d][slot].type ? name_ids[d][slot] : 0;
}
int horse_set_name(Dimension d,int slot,uint16_t name) {
    if ((unsigned)d>=DIMENSION_COUNT || (unsigned)slot>=HORSE_CAPACITY || !herd[d][slot].type ||
        (name && !*item_name_for_id(name))) return 0;
    name_ids[d][slot]=name;
    return 1;
}
LeadLink *horses_lead_at(Dimension d,int slot) {
    return (unsigned)d<DIMENSION_COUNT && (unsigned)slot<HORSE_CAPACITY ? &ties[d][slot] : NULL;
}
void horse_unseat(void) { rider=-1; tame_wait=0; }
int horse_rider(void) { return player_rules.health && world_dimension()==mounted_dim ? rider : -1; }
void horses_reset(void) {
    memset(herd,0,sizeof herd); memset(vertical,0,sizeof vertical);
    memset(vocal_clock,0,sizeof vocal_clock);
    memset(name_ids,0,sizeof name_ids);
    memset(ties,0,sizeof ties);
    horse_unseat(); spawn_clock=day_fraction=0; serial=0; ++revision;
}

int horse_armor_points(int type) {
    return type==IRON_HORSE_ARMOR ? 5 : type==GOLD_HORSE_ARMOR ? 7 : type==DIAMOND_HORSE_ARMOR ? 11 : 0;
}
static int can_equip(const HorseRecord *h) { return h->type==EQUINE_HORSE && h->tamed && !h->age; }
static int valid_equipment(const HorseRecord *h,ItemStack armor,unsigned debt) {
    return debt<HEALTH_UNITS && item_stack_valid(armor) && (!armor.count ||
        (can_equip(h) && armor.count==1 && horse_armor_points(armor.type)!=0));
}
int horse_restore_equipment(Dimension d,int slot,ItemStack armor,unsigned debt) {
    if ((unsigned)d>=DIMENSION_COUNT || (unsigned)slot>=HORSE_CAPACITY) return 0;
    HorseRecord *h=&herd[d][slot];
    if (!h->type || !valid_equipment(h,armor,debt)) return 0;
    h->armor=armor; h->health_debt=(uint8_t)debt; ++revision;
    return 1;
}
static int valid_record(HorseRecord h) {
    return h.type>0 && h.type<EQUINE_TYPES && h.coat<7 &&
        h.max_health>=15 && h.max_health<=30 && h.health>0 && h.health<=h.max_health &&
        h.gait<=100 && h.spring<=100 && h.temper<=100 && h.tamed<=1 && h.saddle<=h.tamed &&
        h.age<=HORSE_GROWTH && h.cooldown<=HORSE_COOLDOWN && h.love<=HORSE_LOVE &&
        (!h.age || (!h.saddle && !h.cooldown && !h.love)) &&
        (!h.love || (h.tamed && h.type!=EQUINE_MULE && !h.cooldown)) && valid_equipment(&h,h.armor,h.health_debt) &&
        fabsf(h.heading)<=3.142f && fabsf(h.feet.x)<WORLD_SAVE_LIMIT &&
        fabsf(h.feet.z)<WORLD_SAVE_LIMIT && h.feet.y>=0.f && h.feet.y<=WY+6;
}
int horse_restore_with_lead(Dimension d,HorseRecord h,LeadLink lead) {
    if ((unsigned)d>=DIMENSION_COUNT || !valid_record(h) || !lead_valid(lead)) return 0;
    for (int i=0;i<HORSE_CAPACITY;++i) if (!herd[d][i].type) {
        herd[d][i]=h; vertical[d][i]=0; lead_set(&ties[d][i],lead); ++revision;
        vocal_clock[d][i]=3.7f+((hash2((int)h.feet.x+i,(int)h.feet.z)>>4)&63u)*.1f;
        name_ids[d][i]=0;
        return 1;
    }
    return 0;
}
int horse_restore(Dimension d,HorseRecord h) {
    return horse_restore_with_lead(d,h,(LeadLink){LEAD_FREE,0,0,0});
}
static int fits(const HorseRecord *h,Vec3 v,int mounted) {
    if (!(fabsf(v.x)<WORLD_SAVE_LIMIT && fabsf(v.z)<WORLD_SAVE_LIMIT && v.y>=0 && v.y<=WY+6))
        return 0;
    if (world_box_blocked(body_box(v.x,v.y,v.z,radius(h),height(h)),0)) return 0;
    return !mounted || !collides(v.x,seat_y(v.y),v.z);
}
static int fits_moving(const HorseRecord *h,Vec3 previous,Vec3 v,int mounted) {
    if (!(fabsf(v.x)<WORLD_SAVE_LIMIT && fabsf(v.z)<WORLD_SAVE_LIMIT && v.y>=0 && v.y<=WY+6)) return 0;
    float r=radius(h),hi=height(h);
    if (world_move_blocked(body_box(previous.x,previous.y,previous.z,r,hi),body_box(v.x,v.y,v.z,r,hi),0)) return 0;
    return !mounted || !world_move_blocked(body_box(previous.x,seat_y(previous.y),previous.z,.28f,1.72f),
        body_box(v.x,seat_y(v.y),v.z,.28f,1.72f),0);
}
static int floor_support(const HorseRecord *h,Vec3 v) {
    float r=radius(h)*.75f;
    if (v.y<.06f) return 1;
    return world_box_blocked((BlockBox){v.x-r,v.y-.066f,v.z-r,v.x+r,v.y+.002f,v.z+r},0);
}
static int dry_ground(const HorseRecord *h,Vec3 p,int mounted) {
    if (!fits(h,p,mounted) || !floor_support(h,p)) return 0;
    int below=block((int)floorf(p.x),(int)floorf(p.y),(int)floorf(p.z));
    return below!=WATER && below!=LAVA;
}
static void sync_rider(const HorseRecord *h) {
    px=h->feet.x; py=seat_y(h->feet.y); pz=h->feet.z;
    vy=0; grounded=0; player_rules.fall_origin=py;
}
int horse_restore_rider(int index) {
    Dimension d=world_dimension();
    if (index < -1 || index>=HORSE_CAPACITY) return 0;
    if (index>=0) {
        HorseRecord *h=&herd[d][index];
        if (!h->type || h->age || !player_rules.health || boat_rider()>=0 || cart_rider()>=0 ||
            fabsf(px-h->feet.x)>.25f || fabsf(py-seat_y(h->feet.y))>.25f || fabsf(pz-h->feet.z)>.25f ||
            !fits(h,h->feet,1)) return 0;
        player_rules.flying=0;
        sync_rider(h);
    }
    mounted_dim=d; rider=index; tame_wait=0;
    return 1;
}
static int dismount(void) {
    int index=horse_rider();
    if (index<0) return 0;
    Vec3 p=herd[world_dimension()][index].feet;
    static const int heights[]={0,1,-1,2,-2};
    for (unsigned a=0;a<sizeof offsets/sizeof offsets[0];++a)
        for (unsigned b=0;b<sizeof heights/sizeof heights[0];++b) {
            float x=p.x+1.3f*offsets[a][0],z=p.z+1.3f*offsets[a][1];
            float y=floorf(p.y)+heights[b]+.01f;
            int bx=(int)floorf(x),by=(int)floorf(y),bz=(int)floorf(z);
            if (!inworld(bx,by,bz) || !solid(block(bx,by-1,bz)) || collides(x,y,z) ||
                block(bx,by,bz)==LAVA || block(bx,by,bz)==WATER) continue;
            horse_unseat(); px=x; py=y; pz=z; vy=0; grounded=0; player_rules.fall_origin=y;
            return 1;
        }
    return 0;
}
static int hit_horse(Vec3 q,int omit) {
    const HorseRecord *list=herd[world_dimension()];
    for (int i=0;i<HORSE_CAPACITY;++i) {
        const HorseRecord *h=&list[i];
        if (!h->type || i==omit || q.y<h->feet.y || q.y>h->feet.y+height(h)) continue;
        float x=q.x-h->feet.x,z=q.z-h->feet.z,r=radius(h);
        if (x*x+z*z<r*r) return i;
    }
    return -1;
}
static int aimed_horse(void) {
    const HorseRecord *list=herd[world_dimension()];
    int possible=0;
    for (int i=0;i<HORSE_CAPACITY;++i) if (list[i].type && fabsf(list[i].feet.x-px)<6 &&
        fabsf(list[i].feet.z-pz)<6 && fabsf(list[i].feet.y-py)<7) { possible=1; break; }
    if (!possible) return -1;
    Vec3 f=forward();
    for (float t=.12f;t<=5.f;t+=.09f) {
        Vec3 q={px+t*f.x,py+1.55f+t*f.y,pz+t*f.z};
        if (world_point_blocked(q.x,q.y,q.z,0)) break;
        int i=hit_horse(q,horse_rider());
        if (i>=0) return i;
    }
    return -1;
}
int horse_body_at(Dimension d,int slot,BlockBox *out) {
    if (!out || (unsigned)d>=DIMENSION_COUNT || (unsigned)slot>=HORSE_CAPACITY) return 0;
    const HorseRecord *h=&herd[d][slot];
    if (!h->type || !h->health) return 0;
    *out=body_box(h->feet.x,h->feet.y,h->feet.z,radius(h),height(h));
    return 1;
}
int horse_contact_shift(int slot,float dx,float dz) {
    if ((unsigned)slot>=HORSE_CAPACITY || slot==horse_rider()) return 0;
    Dimension d=world_dimension();
    HorseRecord *h=&herd[d][slot];
    if (!h->type) return 0;
    Vec3 p=h->feet,q={p.x+dx,p.y,p.z+dz},anchor;
    if ((q.x==p.x && q.z==p.z) ||
        (lead_anchor(ties[d][slot],&anchor) && !lead_allows(p,q,anchor)) ||
        !fits_moving(h,p,q,0) || (floor_support(h,p) && !floor_support(h,q))) return 0;
    h->feet=q; ++revision;
    return 1;
}
int horse_overlaps_cell(int x,int y,int z) {
    const HorseRecord *list=herd[world_dimension()];
    for (int i=0;i<HORSE_CAPACITY;++i) if (list[i].type) {
        Vec3 p=list[i].feet;
        float r=radius(&list[i]);
        if (p.x+r>x && p.x-r<x+1 && p.z+r>z && p.z-r<z+1 &&
            p.y<y+1 && p.y+height(&list[i])>y) return 1;
    }
    return 0;
}
static void consume(ItemStack *held) {
    if (player_rules.mode==SURVIVAL && !--held->count) *held=(ItemStack){0,0,0,0,0};
}
static int equip(HorseRecord *h,int slot) {
    if ((unsigned)slot>=SLOT_COUNT || !can_equip(h)) return 0;
    ItemStack *held=&inventory[slot];
    if (!held->count || !horse_armor_points(held->type) || !valid_equipment(h,*held,h->health_debt)) return 0;
    ItemStack previous=h->armor;
    h->armor=*held;
    /* Swapping returns the old, possibly named armor to the chosen slot. */
    if (previous.count || player_rules.mode==SURVIVAL) *held=previous;
    ++revision;
    return 1;
}
static HorseRecord *mounted(void) {
    int i=horse_rider();
    return (unsigned)i<HORSE_CAPACITY ? &herd[world_dimension()][i] : NULL;
}
int horse_can_equip_mounted(void) {
    const HorseRecord *h=mounted();
    return h && can_equip(h);
}
ItemStack horse_mounted_armor(void) {
    const HorseRecord *h=mounted();
    return h && can_equip(h) ? h->armor : empty;
}
int horse_equip_mounted(int inventory_index) {
    HorseRecord *h=mounted();
    return h && equip(h,inventory_index);
}
int horse_unequip_mounted(void) {
    HorseRecord *h=mounted();
    if (!h || !can_equip(h) || !h->armor.count) return 0;
    if (player_rules.mode==CREATIVE) {
        int i=0;
        while (i<HOTBAR_SIZE && inventory[i].count) ++i;
        if (i==HOTBAR_SIZE) return 0;
        inventory[i]=h->armor;
    } else if (!inventory_add_stack(h->armor)) return 0;
    h->armor=empty; ++revision;
    return 1;
}
const char *horse_use(void) {
    if (!player_rules.health || boat_rider()>=0 || cart_rider()>=0) return NULL;
    if (horse_rider()>=0) return dismount() ? "已下坐骑" : "旁边没有安全的下马位置";
    int i=aimed_horse();
    if (i<0 || (unsigned)selected_slot>=HOTBAR_SIZE) return NULL;
    HorseRecord *h=&herd[world_dimension()][i];
    LeadLink *rope=&ties[world_dimension()][i];
    const char *leash=lead_use_animal(rope,h->feet);
    if (leash) return leash;
    ItemStack *held=&inventory[selected_slot];
    if (held->type==APPLE || held->type==FRUIT_RATION) {
        int ration=held->type==FRUIT_RATION;
        int breed=ration && h->tamed && !h->age && !h->cooldown && !h->love && h->type!=EQUINE_MULE;
        if (h->health==h->max_health && !h->health_debt && !h->age && (h->tamed || h->temper>=100) && !breed)
            return "这只坐骑暂时不需要喂食";
        h->health=(uint8_t)clamp(h->health+(ration ? 6 : 3),1,h->max_health);
        if (h->health==h->max_health) h->health_debt=0;
        if (!h->tamed) h->temper=(uint8_t)clamp(h->temper+(ration ? 8 : 3),0,100);
        if (h->age) h->age=(uint16_t)(h->age>(ration ? 3000 : 1200) ? h->age-(ration ? 3000 : 1200) : 0);
        if (breed) h->love=HORSE_LOVE;
        consume(held); ++revision;
        return breed ? "进入繁殖状态，给附近另一只已驯服的成年马或驴喂果麦干粮" : "已给坐骑喂食";
    }
    if (h->age) return "幼崽尚不能骑乘";
    if (held->count && horse_armor_points(held->type)) {
        if (h->type!=EQUINE_HORSE) return "驴和骡不能穿马铠";
        if (!h->tamed) return "先用空手骑乘并驯服，再给马穿铠";
        int had=h->armor.count;
        if (!equip(h,selected_slot)) return "无法装备这件马铠";
        return had ? "已更换马铠，原马铠已放回快捷格" : "已装备马铠；骑马时在物品栏可卸下";
    }
    if (held->type==SADDLE) {
        if (!h->tamed) return "先用空手骑乘并驯服";
        if (h->saddle) return "这只坐骑已装好鞍";
        h->saddle=1; consume(held); ++revision;
        return "已装好鞍；再次对准轻点可骑乘";
    }
    if (rope->kind==LEAD_FENCE) return "先解开栅栏上的绳子，再骑乘";
    if (!h->tamed && held->count && player_rules.mode!=CREATIVE) return "切换至空手再驯服";
    if (!fits(h,h->feet,1)) return "坐骑上方空间不足";
    if (player_rules.mode==CREATIVE) h->tamed=1;
    mounted_dim=world_dimension(); rider=i; tame_wait=0;
    player_rules.flying=0; sync_rider(h); ++revision;
    return !h->tamed ? "正在驯服；如果被甩下，请空手再骑一次" :
        h->saddle ? "已上坐骑；右侧轻点跳跃，向上拖蓄力" : "已上坐骑；需要装鞍才能控制";
}

static int hurt(int i,int damage,DamageSource source,int player_caused,int bypass) {
    Dimension d=world_dimension();
    HorseRecord *h=&herd[d][i];
    if (damage<=0) return 0;
    int armor=!bypass && (source==DAMAGE_MELEE || source==DAMAGE_PROJECTILE || source==DAMAGE_EXPLOSION ||
        source==DAMAGE_DIRECT_FIRE || source==DAMAGE_ANVIL) ? horse_armor_points(h->armor.type) : 0;
    int64_t health=(int64_t)h->health*HEALTH_UNITS-h->health_debt-(int64_t)damage*(HEALTH_UNITS-armor);
    if (health>0) {
        sound_play(SOUND_HORSE,.87f,128u+(unsigned)h->type,h->feet);
        h->health=(uint8_t)((health+HEALTH_UNITS-1)/HEALTH_UNITS);
        h->health_debt=(uint8_t)(h->health*HEALTH_UNITS-health);
        ++revision; return 1;
    }
    if (player_rules.mode==SURVIVAL) {
        int n=0,slots[4],available=0;
        ItemStack items[4];
        if (h->saddle) items[n++]=(ItemStack){SADDLE,1,0,0,0};
        if (h->type==EQUINE_HORSE && !h->age) items[n++]=(ItemStack){LEATHER,1,0,0,0};
        if (ties[d][i].kind) items[n++]=(ItemStack){LEAD,1,0,0,0};
        if (h->armor.count) items[n++]=h->armor;
        for (int s=0;s<MAXDROP && available<n;++s) if (!drops[s].item.count) slots[available++]=s;
        if (available<n) return 0;
        for (int s=0;s<n;++s) drops[slots[s]]=make_drop(h->feet.x,h->feet.y+.3f,h->feet.z,items[s]);
        if (n) drops_dirty=1;
        if (!h->age && player_caused) experience_reward((Vec3){h->feet.x,h->feet.y+.34f,h->feet.z},experience_roll(1,3));
    }
    sound_play(SOUND_HORSE,.95f,256u+(unsigned)h->type,h->feet);
    if (i==horse_rider()) { if (!dismount()) horse_unseat(); }
    lead_set(&ties[d][i],(LeadLink){LEAD_FREE,0,0,0});
    memset(h,0,sizeof *h); vertical[world_dimension()][i]=0; ++revision;
    return 1;
}
void horses_anvil_impact(BlockBox strike,int damage) {
    const HorseRecord *list=herd[world_dimension()];
    for (int i=0;i<HORSE_CAPACITY;++i) if (list[i].type) {
        const HorseRecord *h=&list[i];
        if (box_overlap(strike,body_box(h->feet.x,h->feet.y,h->feet.z,radius(h),height(h))))
            hurt(i,damage,DAMAGE_ANVIL,0,0);
    }
}
void horses_explosion_hits(const int damage[HORSE_CAPACITY]) {
    for (int i=0;i<HORSE_CAPACITY;++i)
        if (damage[i]>0 && horses_in(world_dimension())[i].type) (void)hurt(i,damage[i],DAMAGE_EXPLOSION,0,0);
}
const char *horse_attack(void) {
    int i=aimed_horse();
    if (i<0 || (unsigned)selected_slot>=HOTBAR_SIZE) return NULL;
    ItemStack *held=&inventory[selected_slot];
    const ItemDef *def=item_def(held->type);
    if (!hurt(i,player_rules.mode==CREATIVE ? 30 : (def && def->melee ? def->melee : 1)+enchant_damage(*held,0,0),
        DAMAGE_MELEE,1,player_rules.mode==CREATIVE))
        return "掉落物已满";
    if (player_rules.mode==SURVIVAL && def && def->melee) enchant_wear(held,is_axe(held->type) ? 2u : 1u,0);
    return herd[world_dimension()][i].type ? "击中了坐骑" : "坐骑已死亡";
}
int horse_hit_arrow(Vec3 q,int damage) {
    int i=hit_horse(q,horse_rider());
    if (i<0) return 0;
    hurt(i,damage,DAMAGE_PROJECTILE,1,0);
    return 1;
}
int horse_hit_hostile(Vec3 q,int damage,DamageSource source) {
    int i=hit_horse(q,-1);
    if (i<0) return 0;
    (void)hurt(i,damage,source,0,0);
    return 1;
}

static HorseRecord wild(Vec3 p,EquineType type,unsigned hash) {
    HorseRecord h;
    memset(&h,0,sizeof h);
    h.feet=p; h.type=(uint8_t)type; h.coat=(uint8_t)(hash%7);
    h.max_health=(uint8_t)(15+(hash>>4)%16); h.health=h.max_health;
    h.gait=(uint8_t)((hash>>10)%101); h.spring=(uint8_t)((hash>>20)%101);
    return h;
}
static int already_here(Vec3 p,float distance) {
    const HorseRecord *list=herd[world_dimension()];
    for (int i=0;i<HORSE_CAPACITY;++i) if (list[i].type &&
        fabsf(list[i].feet.y-p.y)<3.f && hypotf(list[i].feet.x-p.x,list[i].feet.z-p.z)<distance) return 1;
    return 0;
}
int horse_spawn_egg(Vec3 point,float heading) {
    if (!(fabsf(point.x)<WORLD_SAVE_LIMIT && fabsf(point.z)<WORLD_SAVE_LIMIT && point.y>=0 && point.y<WY) ||
        !world_chunk_ready((int)floorf(point.x),(int)floorf(point.z))) return 0;
    unsigned roll=hash2((int)floorf(point.x)^((int)(++serial%4096)*97),(int)floorf(point.z));
    HorseRecord h=wild(point,roll%5u ? EQUINE_HORSE : EQUINE_DONKEY,roll);
    h.heading=remainderf(heading,6.283185307f);
    BlockBox box=body_box(point.x,point.y,point.z,radius(&h),height(&h));
    if (box.y1>WY || world_box_blocked(box,SPACE_WATER|SPACE_LAVA) ||
        box_overlap(box,body_box(px,py,pz,.29f,1.72f))) return 0;
    Dimension d=world_dimension();
    for (int i=0;i<ACTOR_CAPACITY;++i) {
        BlockBox other;
        if (actors_body_at(d,i,&other) && box_overlap(box,other)) return 0;
    }
    for (int i=0;i<HORSE_CAPACITY;++i) {
        BlockBox other;
        if (horse_body_at(d,i,&other) && box_overlap(box,other)) return 0;
    }
    return horse_restore(d,h);
}
static void spawn_herd(void) {
    if (world_dimension()!=OVERWORLD || !player_rules.health ||
        !(py>1 && py<WY+6 && fabsf(px)<WORLD_SAVE_LIMIT-64 && fabsf(pz)<WORLD_SAVE_LIMIT-64)) return;
    int x=(int)floorf(px),z=(int)floorf(pz),near=0,room_left=0;
    for (int i=0;i<HORSE_CAPACITY;++i) {
        const HorseRecord *h=&herd[OVERWORLD][i];
        if (!h->type) ++room_left;
        else if (hypotf(h->feet.x-px,h->feet.z-pz)<62) ++near;
    }
    if (near>=5 || !room_left) return;
    for (int attempt=0;attempt<10;++attempt) {
        int n=(int)(++serial%4096);
        unsigned h=hash2(x+n*19,z-n*13);
        int a=x+(int)(h%65)-32,b=z+(int)((h>>8)%65)-32;
        if (hypotf(a-px,b-pz)<15) continue;
        Biome biome=terrain_biome(a,b);
        if (biome!=BIOME_PLAINS && biome!=BIOME_SAVANNA) continue;
        Vec3 p={a+.5f,terrain_height(a,b)+1.01f,b+.5f};
        if (fabsf(p.y-py)>16 || block(a,(int)p.y-1,b)!=GRASS || already_here(p,10)) continue;
        EquineType type=(h>>22)%5==0 ? EQUINE_DONKEY : EQUINE_HORSE;
        int born=0;
        for (int j=0;j<3;++j) {
            int tx=a+j*2,tz=b+(j==2);
            p=(Vec3){tx+.5f,terrain_height(tx,tz)+1.01f,tz+.5f};
            HorseRecord v=wild(p,type,hash2(tx,tz));
            if (block(tx,(int)p.y-1,tz)==GRASS && dry_ground(&v,p,0) && !already_here(p,1.4f))
                born+=horse_restore(OVERWORLD,v);
        }
        if (born) return;
    }
}
static void breed(HorseRecord *list,int index) {
    HorseRecord *a=&list[index];
    if (!a->love) return;
    for (int i=index+1;i<HORSE_CAPACITY;++i) {
        HorseRecord *b=&list[i];
        if (!b->love || fabsf(a->feet.y-b->feet.y)>1.1f ||
            hypotf(a->feet.x-b->feet.x,a->feet.z-b->feet.z)>4) continue;
        int blocked=0;
        for (int j=1;j<40 && !blocked;++j) {
            float t=j/40.f;
            blocked=world_point_blocked(a->feet.x+t*(b->feet.x-a->feet.x),
                a->feet.y+.75f+t*(b->feet.y-a->feet.y),a->feet.z+t*(b->feet.z-a->feet.z),1);
        }
        if (blocked) continue;
        unsigned hash=hash2((int)a->feet.x,(int)b->feet.z+(int)player_rules.day_tick);
        HorseRecord child=wild(a->feet,a->type!=b->type ? EQUINE_MULE : (EquineType)a->type,hash);
        child.age=HORSE_GROWTH;
        child.gait=(uint8_t)((child.gait+a->gait+b->gait)/3);
        child.spring=(uint8_t)((child.spring+a->spring+b->spring)/3);
        child.max_health=(uint8_t)((child.max_health+a->max_health+b->max_health)/3);
        child.health=child.max_health;
        if (a->type==b->type && hash%5) child.coat=hash&1u ? a->coat : b->coat;
        for (unsigned j=0;j<sizeof offsets/sizeof offsets[0];++j) {
            child.feet=(Vec3){a->feet.x+1.7f*offsets[j][0],a->feet.y,a->feet.z+1.7f*offsets[j][1]};
            if (!dry_ground(&child,child.feet,0) || already_here(child.feet,1.2f)) continue;
            if (horse_restore(world_dimension(),child)) {
                a->love=b->love=0; a->cooldown=b->cooldown=HORSE_COOLDOWN;
                experience_reward((Vec3){child.feet.x,child.feet.y+.32f,child.feet.z},experience_roll(1,7));
            }
            return;
        }
    }
}

void horse_jump(float charge) {
    int i=horse_rider();
    if (i<0) return;
    HorseRecord *h=&herd[world_dimension()][i];
    float *v=&vertical[world_dimension()][i];
    if (h->saddle && *v==0 && floor_support(h,h->feet))
        *v=5.1f+(1.3f+.036f*h->spring)*clamp(charge,.25f,1.f);
}
static void move_equine(Dimension d,int i,float dt,int mounted,float dx,float dz,int led,Vec3 anchor) {
    HorseRecord *h=&herd[d][i];
    Vec3 old=h->feet,p=old;
    float *v=&vertical[d][i];
    int pieces=1+(int)(hypotf(dx,dz)/.16f);
    for (int j=0;j<pieces;++j) {
        Vec3 q={p.x+dx/pieces,p.y,p.z};
        if ((!led || lead_allows(p,q,anchor)) && fits_moving(h,p,q,mounted)) p.x=q.x;
        q=(Vec3){p.x,p.y,p.z+dz/pieces};
        if ((!led || lead_allows(p,q,anchor)) && fits_moving(h,p,q,mounted)) p.z=q.z;
        if (*v==0 && j==0 && floor_support(h,p) && (p.x==old.x && p.z==old.z)) {
            q=(Vec3){p.x+dx/pieces,p.y+1.f,p.z+dz/pieces};
            if ((dx || dz) && (!led || lead_allows(p,q,anchor)) &&
                fits(h,(Vec3){p.x,p.y+1.f,p.z},mounted) && dry_ground(h,q,mounted)) p=q;
        }
    }
    if (*v!=0 || !floor_support(h,p)) {
        *v=fmaxf(*v-17.f*dt,-15.f);
        float distance=*v*dt;
        int slices=1+(int)(fabsf(distance)/.10f);
        for (int j=0;j<slices;++j) {
            Vec3 q={p.x,p.y+distance/slices,p.z};
            if (!fits_moving(h,p,q,mounted)) { *v=0; break; }
            p=q;
        }
    }
    if (p.x!=old.x || p.y!=old.y || p.z!=old.z) { h->feet=p; ++revision; }
}
static uint16_t count_down(uint16_t n,unsigned ticks) { return (uint16_t)(n>ticks ? n-ticks : 0); }
int horses_step(float dt,int side,int front) {
    if (!(dt>0 && dt<=.1f)) return 0;
    if (!player_rules.health) horse_unseat();
    Dimension d=world_dimension();
    HorseRecord *list=herd[d];
    int index=horse_rider();
    day_fraction+=dt*20.f;
    unsigned ticks=(unsigned)day_fraction;
    day_fraction-=ticks;
    spawn_clock+=dt;
    if (spawn_clock>=3.f) { spawn_clock-=3.f; spawn_herd(); }
    if (index>=0) {
        HorseRecord *h=&list[index];
        if (!h->tamed && (tame_wait+=dt)>=1.3f) {
            tame_wait=0;
            unsigned draw=hash2((int)h->feet.x+(int)(++serial%4096), (int)h->feet.z)%100;
            if (draw<h->temper) { h->tamed=1; ++revision; }
            else {
                h->temper=(uint8_t)clamp(h->temper+5,0,100);
                if (dismount()) index=-1;
            }
        }
    }
    for (int i=0;i<HORSE_CAPACITY;++i) {
        HorseRecord *h=&list[i];
        if (!h->type) continue;
        Vec3 anchor={0,0,0};
        int led=ties[d][i].kind ? lead_tick(d,&ties[d][i],h->feet,&anchor) : 0;
        if (fabsf(h->feet.x-px)>64 || fabsf(h->feet.z-pz)>64) continue;
        if (fabsf(h->feet.x-px)<15.f && fabsf(h->feet.z-pz)<15.f && fabsf(h->feet.y-py)<8.f &&
            (vocal_clock[d][i]-=dt)<=0.f) {
            sound_play(SOUND_HORSE,.66f,(unsigned)h->type,h->feet);
            vocal_clock[d][i]=9.f+(hash2(i,(int)player_rules.day_tick)%96u)*.12f;
        }
        if (ticks) {
            if (h->age && h->age<=ticks) {
                HorseRecord adult=*h; adult.age=0;
                if (fits(&adult,h->feet,0)) { h->age=0; ++revision; }
            } else h->age=count_down(h->age,ticks);
            h->cooldown=count_down(h->cooldown,ticks);
            h->love=count_down(h->love,ticks);
            breed(list,i);
        }
        float dx=0,dz=0;
        if (i==index && h->saddle) {
            float sx=clamp(mx+side,-1,1),sz=clamp(-my+front,-1,1);
            float v=(h->type==EQUINE_HORSE ? 4.6f+.065f*h->gait : 4.9f+.029f*h->gait)*dt;
            v/=fmaxf(1,hypotf(sx,sz));
            int b=block((int)floorf(h->feet.x),(int)floorf(h->feet.y),(int)floorf(h->feet.z));
            if (b==WATER) v*=.32f;
            dx=(cosf(yaw)*sx+sinf(yaw)*sz)*v;
            dz=(sinf(yaw)*sx-cosf(yaw)*sz)*v;
            if (fabsf(dx)+fabsf(dz)>.0005f) {
                float theta=atan2f(dx,-dz);
                if (theta!=h->heading) { h->heading=theta; ++revision; }
            }
        } else if (led) {
            float x=anchor.x-h->feet.x,z=anchor.z-h->feet.z,distance=hypotf(x,z);
            if (distance>4.3f) {
                float step=dt*3.f/distance;
                dx=x*step; dz=z*step;
                float theta=atan2f(dx,-dz);
                if (theta!=h->heading) { h->heading=theta; ++revision; }
            }
        }
        move_equine(d,i,dt,i==index,dx,dz,led && i!=index,anchor);
        if (i==index) sync_rider(h);
    }
    return index>=0;
}
