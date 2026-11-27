#ifndef VOXEL_DISPENSER_ACTIONS_H
#define VOXEL_DISPENSER_ACTIONS_H
#include "game.h"

typedef struct { int x,y,z,dx,dy,dz; Vec3 muzzle; } DispenseOutlet;
typedef enum { DISPENSE_EJECT, DISPENSE_USED, DISPENSE_BLOCKED } DispenseResult;
/* Caller owns the inventory and handles persistence/notifications. BLOCKED
   leaves it untouched; EJECT asks the caller to drop the original item. */
DispenseResult dispenser_action(ItemStack *slots,int count,int index,DispenseOutlet outlet);
#endif
