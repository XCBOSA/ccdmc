#ifndef VOXEL_FIELD_SUPPLIES_H
#define VOXEL_FIELD_SUPPLIES_H

#include "game.h"

enum { FIELD_WATER_MAX=100, FIELD_WATER_LOW=20, FIELD_WATER_DRY=10,
       FIELD_HEAT_MAX=80, FIELD_DRAIN_UNIT=400, FIELD_SAVE_BYTES=4 };
typedef struct { uint8_t water,heat; uint16_t drain; } FieldSave;

void field_reset(void);
FieldSave field_record(void);
int field_restore(FieldSave saved);
int field_enabled(void);
int field_in_sun(void);
float field_stride_scale(void);
int field_allows_recovery(void);
void field_advance(unsigned ticks,int moved,int immersed);

/* Shared deterministic rule, independent of world generation and rendering. */
void field_apply_exposure(unsigned ticks,int moved,int immersed,int sun,int hat);
const char *field_use_water(ItemStack *held,int aimed_at_source);

#endif
