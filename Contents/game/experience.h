#ifndef VOXEL_EXPERIENCE_H
#define VOXEL_EXPERIENCE_H
#include "game.h"
#include <stddef.h>

enum { XP_ORB_LIMIT=8192, XP_ORB_LIFE=6000, XP_MAX_POINTS=INT32_MAX, XP_PICKUP_INTERVAL=2 };
typedef struct { Vec3 point,velocity; uint32_t value; uint16_t age; } XPOrb;
typedef struct { uint32_t points,level,within,next; } XPProgress;

void experience_reset(void);
uint32_t experience_points(void);
uint32_t experience_rng(void);
int experience_restore_player(uint32_t points,uint32_t rng);
XPProgress experience_progress(void);
int experience_spend_levels(unsigned cost);
uint32_t experience_until_next(unsigned level);
unsigned experience_roll(unsigned low,unsigned high);
unsigned experience_ore(int block_type);
void experience_reward(Vec3 at,unsigned amount);
void experience_death(int void_death);
void experience_step(float dt,unsigned ticks);
unsigned experience_revision(void);
const XPOrb *experience_orbs(Dimension dimension,size_t *count);
int experience_restore_orb(Dimension dimension,XPOrb orb);
#endif
