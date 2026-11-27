#ifndef VOXEL_ARMOR_H
#define VOXEL_ARMOR_H
#include "game.h"

typedef enum { ARMOR_HEAD, ARMOR_BODY, ARMOR_LEGS, ARMOR_FEET, ARMOR_SLOTS } ArmorSlot;
typedef enum { ARMOR_LEATHER, ARMOR_CHAIN, ARMOR_IRON, ARMOR_GOLD, ARMOR_DIAMOND, ARMOR_MATERIALS } ArmorMaterial;
enum { ARMOR_MAX_POINTS=20 };

const ItemDef *armor_item_def(int item);
int armor_material_item(ArmorMaterial material,int slot);
int armor_slot(int item);
int armor_item_points(int item);
const char *armor_slot_name(int slot);

void armor_reset(void);
ItemStack armor_worn(int slot);
int armor_restore(int slot,ItemStack piece);
int armor_equip(int source,int slot); /* Swap into the original inventory slot without needing a spare. */
int armor_unequip(int slot); /* Leaves equipment unchanged if there is no room. */
int armor_points(void);
void armor_wear(unsigned amount);
void armor_wear_helmet(unsigned amount);

/* Compact durable encoding for hostile mobs: a nonzero material tag and four slot bits. */
int armor_kit_valid(unsigned kit);
int armor_kit_item(unsigned kit,int slot);
int armor_kit_points(unsigned kit);
uint8_t armor_make_kit(ArmorMaterial material,unsigned mask);
#endif
