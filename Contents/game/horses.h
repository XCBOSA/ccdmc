#ifndef VOXEL_HORSES_H
#define VOXEL_HORSES_H

#include "game.h"
#include "../world/barriers.h"
#include "leads.h"

enum { HORSE_CAPACITY=32, HORSE_GROWTH=24000, HORSE_COOLDOWN=6000, HORSE_LOVE=600 };
typedef enum { EQUINE_EMPTY, EQUINE_HORSE, EQUINE_DONKEY, EQUINE_MULE, EQUINE_TYPES } EquineType;
typedef struct {
    Vec3 feet;
    float heading;
    uint16_t age, cooldown, love;
    uint8_t type, coat, max_health, health, gait, spring, temper, tamed, saddle;
    ItemStack armor;
    uint8_t health_debt;
} HorseRecord;

const HorseRecord *horses_in(Dimension d);
uint16_t horse_name_id(Dimension d,int slot);
int horse_set_name(Dimension d,int slot,uint16_t name);
unsigned horses_revision(void);
void horses_anvil_impact(BlockBox strike,int damage);
void horses_explosion_hits(const int damage[HORSE_CAPACITY]);
void horses_reset(void);
int horse_restore(Dimension d, HorseRecord h);
int horse_spawn_egg(Vec3 point,float heading);
int horse_restore_with_lead(Dimension d,HorseRecord h,LeadLink lead);
LeadLink *horses_lead_at(Dimension dim,int slot);
int horse_restore_rider(int index);
int horse_rider(void);
void horse_unseat(void);
int horses_step(float dt, int side, int front);
void horse_jump(float charge);
const char *horse_use(void);
const char *horse_attack(void);
int horse_hit_arrow(Vec3 point,int damage);
int horse_hit_hostile(Vec3 point,int damage,DamageSource source);
int horse_overlaps_cell(int x,int y,int z);
int horse_body_at(Dimension d,int slot,BlockBox *out);
int horse_contact_shift(int slot,float dx,float dz);
const char *horse_name(int type);
int horse_armor_points(int type);
int horse_restore_equipment(Dimension dim,int slot,ItemStack armor,unsigned debt);
int horse_can_equip_mounted(void);
ItemStack horse_mounted_armor(void);
int horse_equip_mounted(int inventory_index);
int horse_unequip_mounted(void);

#endif
