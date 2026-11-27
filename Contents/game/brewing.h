#ifndef VOXEL_BREWING_H
#define VOXEL_BREWING_H

#include "game.h"
#include <stddef.h>

enum { BREW_QUICK, BREW_RESERVE, BREW_MIST, BREW_BOTTLES, BREW_INGREDIENT=BREW_BOTTLES, BREW_SLOTS };
typedef struct {
    int x,y,z;
    ItemStack slots[BREW_SLOTS];
    uint16_t progress;
    uint8_t active; /* 0 when waiting, lane + 1 while a bottle is running */
} BrewingStand;

typedef struct { int output; unsigned cost,ticks; } BrewDose;
BrewDose brew_dose(int lane,int material);
const char *brew_slot_name(int slot);
int brew_pending(const BrewingStand *stand,BrewDose *dose);
int brew_is_potion(int item);
typedef enum { BREW_SPLASH_NONE, BREW_SPLASH_WEAKNESS, BREW_SPLASH_WATER_BREATHING, BREW_SPLASH_NIGHT_VISION } BrewSplashEffect;
typedef struct { BrewSplashEffect effect; unsigned duration; } BrewSplash;
BrewSplash brew_splash(int item);
const BrewingStand *brew_at(int x,int y,int z);
int brew_deposit(int x,int y,int z,int slot,int source);
int brew_withdraw(int x,int y,int z,int slot);
int brew_automation_insert(int x,int y,int z,int top,ItemStack item);
void brew_automation_take_bottle(int x,int y,int z,int slot);
void brewing_advance(float seconds);
void brewing_reset(void);
void brew_forget(int x,int y,int z);
size_t brewing_count_in(Dimension dim);
const BrewingStand *brewing_at_index(Dimension dim,size_t i);
int brewing_restore(const BrewingStand *record);

#endif
