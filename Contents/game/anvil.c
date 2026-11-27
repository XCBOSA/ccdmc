#include "anvil.h"
#include "enchanting.h"
#include "experience.h"
#include <string.h>

int anvil_stage(int raw) { return anvil_cell(raw) ? (raw-ANVIL_BASE)/2 : -1; }
static int anvil_raw(int wear,int axis) { return ANVIL_BASE+2*wear+axis; }
int anvil_place(int x,int y,int z,unsigned wear,int axis) {
    int old=block(x,y,z);
    if (!inworld(x,y,z) || (old!=AIR && old!=WATER) || wear>2 || (unsigned)axis>1) return 0;
    const BlockEdit edit={x,y,z,anvil_raw((int)wear,axis)};
    return world_set_blocks(&edit,1);
}

static int larger(int a,int b) { return a>b ? a : b; }
static int smaller(int a,int b) { return a<b ? a : b; }
static int repair_material(int type) {
    int tier=type>=WOOD_PICKAXE && type<=DIAMOND_PICKAXE ? type-WOOD_PICKAXE :
        type>=WOOD_SWORD && type<=DIAMOND_SWORD ? type-WOOD_SWORD :
        type>=WOOD_HOE && type<=DIAMOND_HOE ? type-WOOD_HOE :
        is_shovel(type) ? type-WOOD_SHOVEL : is_axe(type) ? type-WOOD_AXE : -1;
    static const int mats[]={PLANKS,COBBLESTONE,IRON_INGOT,GOLD_INGOT,DIAMOND};
    if (tier>=0) return mats[tier];
    if (type>=LEATHER_HELMET && type<=LEATHER_BOOTS) return LEATHER;
    if ((type>=IRON_HELMET && type<=IRON_BOOTS) || (type>=CHAIN_HELMET && type<=CHAIN_BOOTS)) return IRON_INGOT;
    if (type>=GOLD_HELMET && type<=GOLD_BOOTS) return GOLD_INGOT;
    if (type>=DIAMOND_HELMET && type<=DIAMOND_BOOTS) return DIAMOND;
    return AIR;
}

AnvilResult anvil_preview(int left,int right,const char *rename,AnvilPlan *out) {
    if (!out) return ANVIL_MISSING;
    memset(out,0,sizeof *out);
    if ((unsigned)left>=SLOT_COUNT || right < -1 || right>=SLOT_COUNT || left==right ||
        !inventory[left].count || !item_stack_valid(inventory[left])) return ANVIL_MISSING;
    const ItemStack empty={0,0,0,0,0};
    const ItemStack a=inventory[left],b=right<0 ? empty : inventory[right];
    if (right>=0 && (!b.count || !item_stack_valid(b))) return ANVIL_MISSING;
    out->item=a;
    const int life=a.type==ANVIL ? 0 : item_def(a.type)->durability;
    const int book=b.type==ENCHANTED_BOOK;
    int operation=0,changed=0;
    if (b.count) {
        if (life && repair_material(a.type)==b.type) {
            const int chunk=larger(1,life/4);
            int levels=0;
            for (int i=0;i<ENCH_COUNT;++i) levels+=enchant_level(a.enchants,(Enchantment)i)>0;
            while (out->item.damage && out->materials<b.count) {
                int repair=smaller(chunk,out->item.damage);
                out->item.damage-=(uint16_t)repair;
                ++out->materials;
                operation+=larger(1,repair/100)+levels;
            }
            changed=out->materials>0;
        } else if ((life && b.type==a.type) || book) {
            if (!book) {
                const int combined=larger(0,(int)a.damage+b.damage-life-12*life/100);
                if (combined<a.damage) {
                    out->item.damage=(uint16_t)combined;
                    operation+=larger(1,(a.damage-combined)/100);
                    changed=1;
                }
            }
            for (int i=0;i<ENCH_COUNT;++i) {
                Enchantment e=(Enchantment)i;
                int incoming=enchant_level(b.enchants,e);
                if (!incoming) continue;
                int current=enchant_level(a.enchants,e);
                int next=smaller(enchant_max_level(e),current==incoming ? current+1 : larger(current,incoming));
                if (next==current) continue;
                ItemStack candidate=out->item;
                candidate.enchants=(candidate.enchants&~(UINT64_C(7)<<(3*i)))|enchant_code(e,next);
                if (!enchant_valid(candidate)) { operation+=next-current; continue; }
                out->item=candidate;
                int weight=enchant_anvil_weight(e);
                operation+=(book ? larger(1,weight/2) : weight)*(next-current);
                changed=1;
            }
            if (changed) out->materials=1;
        }
        if (!changed) return ANVIL_INCOMPATIBLE;
    }
    if (rename) {
        if (!item_name_normalize(rename,out->name)) return ANVIL_BAD_NAME;
        out->rename=strcmp(out->name,item_override(a))!=0;
    }
    if (!out->rename && !changed) return ANVIL_NOTHING;
    int cost=operation+(int)item_prior_work(a)+(int)item_prior_work(b);
    if (out->rename) {
        cost+=life ? 7 : 5*a.count;
        if (!changed) cost=smaller(cost,39);
    }
    out->cost=(unsigned)cost;
    int work=larger((int)item_prior_work(a),(int)item_prior_work(b));
    int named=out->rename ? *out->name!=0 : *item_override(a)!=0;
    out->work=(unsigned)smaller(ITEM_PRIOR_MAX,larger(0,work-(named ? 9 : 0))+2);
    if (player_rules.mode==SURVIVAL && cost>=40) return ANVIL_TOO_EXPENSIVE;
    return ANVIL_READY;
}

AnvilResult anvil_apply(int x,int y,int z,int left,int right,const char *rename) {
    if (!player_rules.health || !inworld(x,y,z)) return ANVIL_MISSING;
    int raw=world_cell(x,y,z),wear=anvil_stage(raw);
    if (wear<0) return ANVIL_MISSING;
    AnvilPlan result;
    AnvilResult status=anvil_preview(left,right,rename,&result);
    if (status!=ANVIL_READY) return status;
    int survival=player_rules.mode==SURVIVAL;
    if (survival && experience_progress().level<result.cost) return ANVIL_NEED_LEVELS;
    if (!item_set_custom(&result.item,result.work,result.rename ? result.name : NULL)) return ANVIL_NO_MEMORY;
    if (survival && experience_roll(0,99)<12u) {
        const BlockEdit edit={x,y,z,wear==2 ? AIR : anvil_raw(wear+1,(raw-ANVIL_BASE)&1)};
        if (!world_set_blocks(&edit,1)) return ANVIL_NO_MEMORY;
    }
    inventory[left]=result.item;
    if (result.materials && right>=0) {
        ItemStack *from=&inventory[right];
        from->count-=(uint8_t)result.materials;
        if (!from->count) *from=(ItemStack){0,0,0,0,0};
    }
    if (survival) experience_spend_levels(result.cost);
    return ANVIL_READY;
}
