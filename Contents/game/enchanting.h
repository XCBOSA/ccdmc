#ifndef VOXEL_ENCHANTING_H
#define VOXEL_ENCHANTING_H
#include "game.h"
#include <stddef.h>

typedef enum {
    ENCH_PROTECTION, ENCH_FIRE_PROTECTION, ENCH_FEATHER_FALLING, ENCH_BLAST_PROTECTION,
    ENCH_PROJECTILE_PROTECTION, ENCH_RESPIRATION, ENCH_SHARPNESS, ENCH_SMITE, ENCH_BANE,
    ENCH_UNBREAKING, ENCH_POWER, ENCH_INFINITY, ENCH_SILK_TOUCH, ENCH_FORTUNE,
    ENCH_LURE, ENCH_LUCK_OF_THE_SEA, ENCH_LAYER_CLEAVER, ENCH_CORE_CLAMP, ENCH_COUNT
} Enchantment;
typedef enum { CALIBRATE_LAYER, CALIBRATE_CORE, CALIBRATE_LINER, CALIBRATION_COUNT } Calibration;
typedef enum {
    CALIBRATION_SELECT, CALIBRATION_INCOMPATIBLE, CALIBRATION_UNNEEDED,
    CALIBRATION_MISSING, CALIBRATION_READY
} CalibrationStatus;
typedef struct { const char *name, *detail; uint16_t material; } CalibrationOption;
enum { CALIBRATION_REPAIR=16 };

int enchant_level(uint64_t bits,Enchantment enchantment);
int enchant_max_level(Enchantment enchantment);
int enchant_anvil_weight(Enchantment enchantment);
uint64_t enchant_code(Enchantment enchantment,int level);
int enchant_valid(ItemStack item);
void enchant_describe(char *out,size_t count,uint64_t bits);
const CalibrationOption *calibration_option(int choice);
CalibrationStatus calibration_preview(int slot,int choice);
int calibration_apply(int x,int y,int z,int slot,int choice);
uint64_t enchant_fishing_treasure(int type,uint32_t *random);
void enchant_wear(ItemStack *item,unsigned amount,int armor);
int enchant_damage(ItemStack item,int undead,int arthropod);
unsigned enchant_protection(DamageSource source);
#endif
