#include "armor.h"
#include "enchanting.h"
#include <string.h>

typedef struct { ItemDef item; int points; } ArmorPiece;
#define GEAR(label,rgb,life,points) {{label,rgb,1,AIR,0,0,life,0},points}
static const ArmorPiece pieces[ARMOR_MATERIALS][ARMOR_SLOTS]={
    {GEAR("皮革帽",0xa26b44,55,1),GEAR("皮革外套",0xa26b44,80,3),
     GEAR("皮革裤子",0xa26b44,75,2),GEAR("皮革靴子",0xa26b44,65,1)},
    {GEAR("锁链头盔",0x939da5,165,2),GEAR("锁链胸甲",0x939da5,240,5),
     GEAR("锁链护腿",0x939da5,225,4),GEAR("锁链靴子",0x939da5,195,1)},
    {GEAR("铁头盔",0xd1d5d5,165,2),GEAR("铁胸甲",0xd1d5d5,240,6),
     GEAR("铁护腿",0xd1d5d5,225,5),GEAR("铁靴子",0xd1d5d5,195,2)},
    {GEAR("金头盔",0xe5b83d,77,2),GEAR("金胸甲",0xe5b83d,112,5),
     GEAR("金护腿",0xe5b83d,105,3),GEAR("金靴子",0xe5b83d,91,1)},
    {GEAR("钻石头盔",0x57d9cf,363,3),GEAR("钻石胸甲",0x57d9cf,528,8),
     GEAR("钻石护腿",0x57d9cf,495,6),GEAR("钻石靴子",0x57d9cf,429,3)}
};
#undef GEAR
typedef char ArmorIdsAreContiguous[(int)DIAMOND_BOOTS+1-LEATHER_HELMET==ARMOR_MATERIALS*ARMOR_SLOTS ? 1 : -1];
static ItemStack equipped[ARMOR_SLOTS];

static int is_armor(int item) { return item>=LEATHER_HELMET && item<=DIAMOND_BOOTS; }
int armor_slot(int item) { return is_armor(item) ? (item-LEATHER_HELMET)%ARMOR_SLOTS : -1; }
static const ArmorPiece *piece(int item) {
    return is_armor(item) ? &pieces[(item-LEATHER_HELMET)/ARMOR_SLOTS][armor_slot(item)] : NULL;
}
const ItemDef *armor_item_def(int item) { const ArmorPiece *p=piece(item); return p ? &p->item : NULL; }
int armor_item_points(int item) { const ArmorPiece *p=piece(item); return p ? p->points : 0; }
int armor_material_item(ArmorMaterial material,int slot) {
    return (unsigned)material<ARMOR_MATERIALS && (unsigned)slot<ARMOR_SLOTS ?
        LEATHER_HELMET+material*ARMOR_SLOTS+slot : AIR;
}
const char *armor_slot_name(int slot) {
    static const char *names[]={"头部","胸部","腿部","脚部"};
    return (unsigned)slot<ARMOR_SLOTS ? names[slot] : "";
}
void armor_reset(void) { memset(equipped,0,sizeof equipped); }
ItemStack armor_worn(int slot) { return (unsigned)slot<ARMOR_SLOTS ? equipped[slot] : (ItemStack){0,0,0,0,0}; }
int armor_restore(int slot,ItemStack item) {
    if ((unsigned)slot>=ARMOR_SLOTS || !item_stack_valid(item) || (item.count && armor_slot(item.type)!=slot)) return 0;
    equipped[slot]=item;
    return 1;
}
int armor_equip(int source,int slot) {
    if (!player_rules.health || (unsigned)slot>=ARMOR_SLOTS || (unsigned)source>=SLOT_COUNT) return 0;
    ItemStack item=inventory[source];
    if (armor_slot(item.type)!=slot || !item_stack_valid(item)) return 0;
    inventory[source]=equipped[slot];
    equipped[slot]=item;
    return 1;
}
int armor_unequip(int slot) {
    if (!player_rules.health || (unsigned)slot>=ARMOR_SLOTS || !inventory_add_stack(equipped[slot])) return 0;
    equipped[slot]=(ItemStack){0,0,0,0,0};
    return 1;
}
int armor_points(void) {
    int score=0;
    for (int i=0;i<ARMOR_SLOTS;++i) score+=armor_item_points(equipped[i].type);
    return score;
}
void armor_wear(unsigned amount) {
    for (int i=0;i<ARMOR_SLOTS;++i) enchant_wear(&equipped[i],amount,1);
}
void armor_wear_helmet(unsigned amount) { enchant_wear(&equipped[ARMOR_HEAD],amount,1); }
uint8_t armor_make_kit(ArmorMaterial material,unsigned mask) {
    return (unsigned)material<ARMOR_MATERIALS && mask>0 && mask<16 ? (uint8_t)(((material+1)<<4)|mask) : 0;
}
int armor_kit_valid(unsigned kit) {
    return !kit || ((kit>>4)>0 && (kit>>4)<=ARMOR_MATERIALS && (kit&15u));
}
int armor_kit_item(unsigned kit,int slot) {
    if ((unsigned)slot>=ARMOR_SLOTS || !(kit&(1u<<slot)) || !armor_kit_valid(kit)) return AIR;
    return armor_material_item((ArmorMaterial)((kit>>4)-1),slot);
}
int armor_kit_points(unsigned kit) {
    int score=0;
    for (int i=0;i<ARMOR_SLOTS;++i) score+=armor_item_points(armor_kit_item(kit,i));
    return score;
}
