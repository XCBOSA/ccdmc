#include "enchanting.h"
#include "armor.h"
#include "experience.h"
#include <math.h>
#include <stdio.h>

enum { E_ARMOR=1,E_HEAD=2,E_FEET=4,E_SWORD=8,E_TOOL=16,E_BOW=32,E_WEAR=64,E_ROD=128 };
_Static_assert(3*ENCH_COUNT<64,"Enchantments must fit in the existing save lane.");
typedef struct { const char *name; int max,weight,base,step,span,category,exclusive; } Rule;
/* Stable per-enchantment lanes: three bits each. Future one-level effects
   can be allocated remaining compact lanes without changing these lanes. */
static const Rule rules[ENCH_COUNT]={
    {"保护",4,10,1,11,20,E_ARMOR,1},
    {"火焰保护",4,5,10,8,12,E_ARMOR,1},
    {"摔落保护",4,5,5,6,10,E_FEET,0},
    {"爆炸保护",4,2,5,8,12,E_ARMOR,1},
    {"弹射物保护",4,5,3,6,15,E_ARMOR,1},
    {"水下呼吸",3,2,10,10,30,E_HEAD,0},
    {"锋利",5,10,1,11,20,E_SWORD,2},
    {"亡灵杀手",5,5,5,8,20,E_SWORD,2},
    {"节肢杀手",5,5,5,8,20,E_SWORD,2},
    {"耐久",3,5,5,8,50,E_WEAR,0},
    {"力量",5,10,1,10,15,E_BOW,0},
    {"无限",1,1,20,0,50,E_BOW,0},
    {"精准采集",1,1,15,0,50,E_TOOL,3},
    {"时运",3,2,15,9,50,E_TOOL,3},
    {"饵钓",3,2,15,9,50,E_ROD,0},
    {"海之眷顾",3,2,15,9,50,E_ROD,0},
    {"分层刃",1,2,0,0,0,E_TOOL,4},
    {"取芯夹",1,2,0,0,0,E_TOOL,4}
};
static int calibration_pick(int type) {
    const ItemDef *def=item_def(type);
    return def && def->pick_tier>=2;
}
static int mask_for(int type,int at_table) {
    const ItemDef *def=item_def(type);
    if (!def) return 0;
    int slot=armor_slot(type),mask=0;
    if (slot>=0) mask|=E_ARMOR | (slot==ARMOR_HEAD ? E_HEAD : slot==ARMOR_FEET ? E_FEET : 0);
    if (def->melee && (!at_table || !is_axe(type))) mask|=E_SWORD;
    if (def->pick_tier || is_shovel(type) || is_axe(type)) mask|=E_TOOL;
    if (type==BOW) mask|=E_BOW;
    if (type==FISHING_ROD) mask|=E_ROD;
    /* Only functions with authoritative wear hooks expose it at the table. */
    if (mask || type==FISHING_ROD || type==SHEARS || type==FLINT_AND_STEEL ||
        (type>=WOOD_HOE && type<=DIAMOND_HOE)) mask|=E_WEAR;
    return mask;
}
int enchant_level(uint64_t bits,Enchantment e) {
    return (unsigned)e<ENCH_COUNT ? (int)((bits>>(3u*e))&7u) : 0;
}
uint64_t enchant_code(Enchantment e,int level) {
    return (unsigned)e<ENCH_COUNT && level>0 && level<=rules[e].max ? (uint64_t)level<<(3u*e) : 0;
}
int enchant_max_level(Enchantment e) { return (unsigned)e<ENCH_COUNT ? rules[e].max : 0; }
int enchant_anvil_weight(Enchantment e) {
    if ((unsigned)e>=ENCH_COUNT) return 0;
    int weight=rules[e].weight;
    return weight==1 ? 8 : weight==2 ? 4 : weight==5 ? 2 : 1;
}
int enchant_valid(ItemStack item) {
    if (!item.enchants) return item.type!=ENCHANTED_BOOK;
    if ((item.enchants>>(3u*ENCH_COUNT)) || item.count!=1) return 0;
    unsigned groups=0;
    int mask=item.type==ENCHANTED_BOOK ? ~0 : mask_for(item.type,0);
    for (int e=0;e<ENCH_COUNT;++e) {
        int level=enchant_level(item.enchants,(Enchantment)e);
        if (!level) continue;
        const Rule *r=&rules[e];
        if (!(mask&r->category) || level>r->max) return 0;
        if (e>=ENCH_LAYER_CLEAVER && !calibration_pick(item.type)) return 0;
        unsigned group=r->exclusive ? 1u<<r->exclusive : 0;
        if (groups&group) return 0;
        groups|=group;
    }
    return 1;
}
void enchant_describe(char *out,size_t count,uint64_t bits) {
    if (!out || !count) return;
    out[0]=0;
    size_t used=0;
    static const char *roman[]={"","I","II","III","IV","V"};
    for (int e=0;e<ENCH_COUNT;++e) {
        int level=enchant_level(bits,(Enchantment)e);
        if (!level || level>rules[e].max) continue;
        int len=e>=ENCH_LAYER_CLEAVER ?
            snprintf(out+used,count-used,"%s%s",used ? " · " : "",rules[e].name) :
            snprintf(out+used,count-used,"%s%s %s",used ? " · " : "",rules[e].name,roman[level]);
        if (len<0 || (size_t)len>=count-used) return;
        used+=(size_t)len;
    }
}
static int enchantability(int type) {
    if (type==BOOK || type==BOW || type==FISHING_ROD) return 1;
    if (armor_slot(type)>=0) {
        static const int material[]={15,12,9,25,10};
        return material[(type-LEATHER_HELMET)/ARMOR_SLOTS];
    }
    int tier=type>=WOOD_PICKAXE && type<=DIAMOND_PICKAXE ? type-WOOD_PICKAXE :
        type>=WOOD_SWORD && type<=DIAMOND_SWORD ? type-WOOD_SWORD :
        type>=WOOD_HOE && type<=DIAMOND_HOE ? type-WOOD_HOE :
        is_shovel(type) ? type-WOOD_SHOVEL : is_axe(type) ? type-WOOD_AXE : -1;
    static const int tool[]={15,5,14,22,10};
    return tier>=0 ? tool[tier] : 1;
}
static unsigned roll(uint32_t *stream,unsigned n) {
    uint32_t x=*stream;
    x^=x<<13; x^=x>>17; x^=x<<5;
    *stream=x;
    return x%n;
}
typedef struct { int index,level; } Choice;
static uint64_t enchant_roll(int type,unsigned price,uint32_t *stream) {
    int enchant=enchantability(type),mask=type==BOOK ? ~0 : mask_for(type,1);
    unsigned base=price+1+roll(stream,1+enchant/4);
    base+=roll(stream,1+enchant/4);
    float offset=((float)roll(stream,1001)+(float)roll(stream,1001)-1000.f)*.00015f;
    int power=(int)floorf(.5f+base*(1.f+offset));
    Choice items[ENCH_LAYER_CLEAVER];
    int length=0;
    for (int e=0;e<ENCH_LAYER_CLEAVER;++e) {
        const Rule *r=&rules[e];
        if (!(mask&r->category)) continue;
        for (int lv=r->max;lv>0;--lv) {
            int min=r->base+(lv-1)*r->step;
            if (power>=min && power<=min+r->span) { items[length++]=(Choice){e,lv}; break; }
        }
    }
    uint64_t result=0;
    do {
        unsigned total=0;
        for (int i=0;i<length;++i) total+=rules[items[i].index].weight;
        if (!total) break;
        unsigned pick=roll(stream,total);
        int i=0;
        while (pick>=(unsigned)rules[items[i].index].weight) pick-=rules[items[i++].index].weight;
        Choice c=items[i];
        result|=enchant_code((Enchantment)c.index,c.level);
        int group=rules[c.index].exclusive;
        for (int j=length-1;j>=0;--j)
            if (items[j].index==c.index || (group && group==rules[items[j].index].exclusive)) items[j]=items[--length];
        int more=roll(stream,50)<=((unsigned)power);
        power/=2;
        if (!more) break;
    } while (length && type!=BOOK);
    return result;
}
uint64_t enchant_fishing_treasure(int type,uint32_t *random) {
    if (!random || (type!=BOW && type!=FISHING_ROD && type!=ENCHANTED_BOOK)) return 0;
    if (!*random) *random=1;
    uint64_t enchants=enchant_roll(type==ENCHANTED_BOOK ? BOOK : type,30,random);
    return enchants ? enchants : enchant_code(ENCH_UNBREAKING,3);
}
const CalibrationOption *calibration_option(int choice) {
    static const CalibrationOption options[CALIBRATION_COUNT]={
        {"分层刃","加快页岩和石灰岩的开挖；会替换取芯夹。",SLATE_ITEM},
        {"取芯夹","原生页岩和石灰岩产量翻倍，但额外判定一次磨损；会替换分层刃。",LIMESTONE_ITEM},
        {"砌板保养","恢复最多 16 点耐久，不改变已安装的配件。",LAMINATE_ITEM},
    };
    return (unsigned)choice<CALIBRATION_COUNT ? &options[choice] : NULL;
}
static int find_material(int type) {
    for (int i=0;i<SLOT_COUNT;++i)
        if (inventory[i].count && inventory[i].type==type && item_stack_valid(inventory[i]))
            return i;
    return -1;
}
static Enchantment calibration_profile(int choice) {
    return choice==CALIBRATE_LAYER ? ENCH_LAYER_CLEAVER : ENCH_CORE_CLAMP;
}
CalibrationStatus calibration_preview(int slot,int choice) {
    const CalibrationOption *option=calibration_option(choice);
    if (!option) return CALIBRATION_INCOMPATIBLE;
    if ((unsigned)slot>=SLOT_COUNT || !inventory[slot].count) return CALIBRATION_SELECT;
    ItemStack item=inventory[slot];
    const ItemDef *def=item_def(item.type);
    if (!def || !item_stack_valid(item)) return CALIBRATION_INCOMPATIBLE;
    if (choice==CALIBRATE_LINER) {
        if (!def->durability || def->placed_block!=AIR) return CALIBRATION_INCOMPATIBLE;
        if (!item.damage) return CALIBRATION_UNNEEDED;
    } else {
        if (!calibration_pick(item.type)) return CALIBRATION_INCOMPATIBLE;
        if (enchant_level(item.enchants,calibration_profile(choice))) return CALIBRATION_UNNEEDED;
    }
    return find_material(option->material)>=0 ? CALIBRATION_READY : CALIBRATION_MISSING;
}
int calibration_apply(int x,int y,int z,int slot,int choice) {
    if (player_rules.mode!=SURVIVAL || !player_rules.health || !inworld(x,y,z) ||
        block(x,y,z)!=ENCHANTING_TABLE || calibration_preview(slot,choice)!=CALIBRATION_READY) return 0;
    int material=find_material(calibration_option(choice)->material);
    if (material<0 || material==slot) return 0;
    ItemStack item=inventory[slot];
    if (choice==CALIBRATE_LINER)
        item.damage=item.damage>CALIBRATION_REPAIR ? item.damage-CALIBRATION_REPAIR : 0;
    else {
        item.enchants&=~(enchant_code(ENCH_LAYER_CLEAVER,1)|enchant_code(ENCH_CORE_CLAMP,1));
        item.enchants|=enchant_code(calibration_profile(choice),1);
    }
    if (!item_stack_valid(item)) return 0;
    inventory[slot]=item;
    if (!--inventory[material].count) inventory[material]=(ItemStack){0};
    return 1;
}
void enchant_wear(ItemStack *item,unsigned amount,int armor) {
    if (!item || !item->count || !amount) return;
    const ItemDef *def=item_def(item->type);
    if (!def || !def->durability || item->damage>=def->durability) return;
    int level=enchant_level(item->enchants,ENCH_UNBREAKING);
    if (!level && amount>=(unsigned)def->durability-item->damage) { *item=(ItemStack){0,0,0,0,0}; return; }
    while (amount--) {
        if (level && (!armor || experience_roll(0,99)>=60u) && experience_roll(0,level)!=0) continue;
        if (++item->damage>=def->durability) { *item=(ItemStack){0,0,0,0,0}; return; }
    }
}
int enchant_damage(ItemStack item,int undead,int arthropod) {
    int sharp=enchant_level(item.enchants,ENCH_SHARPNESS);
    int niche=undead ? enchant_level(item.enchants,ENCH_SMITE) : arthropod ? enchant_level(item.enchants,ENCH_BANE) : 0;
    return (5*sharp+2)/4+(5*niche+1)/2;
}
unsigned enchant_protection(DamageSource source) {
    if (source==DAMAGE_VOID || source==DAMAGE_STARVATION) return 0;
    int score=0;
    const int factors[]={3,5,10,6,6};
    for (int i=0;i<ARMOR_SLOTS;++i) {
        uint64_t bits=armor_worn(i).enchants;
        for (int e=ENCH_PROTECTION;e<=ENCH_PROJECTILE_PROTECTION;++e) {
            int level=enchant_level(bits,(Enchantment)e);
            if (!level) continue;
            if (e==ENCH_PROTECTION || (e==ENCH_FIRE_PROTECTION && (source==DAMAGE_DIRECT_FIRE || source==DAMAGE_BURNING)) ||
                (e==ENCH_FEATHER_FALLING && source==DAMAGE_FALL) || (e==ENCH_BLAST_PROTECTION && source==DAMAGE_EXPLOSION) ||
                (e==ENCH_PROJECTILE_PROTECTION && source==DAMAGE_PROJECTILE)) score+=(6+level*level)*factors[e]/12;
        }
    }
    if (score>25) score=25;
    unsigned atten=score ? experience_roll((score+1u)/2u,(unsigned)score) : 0;
    return atten>20 ? 20 : atten;
}
