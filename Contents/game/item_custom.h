#ifndef VOXEL_ITEM_CUSTOM_H
#define VOXEL_ITEM_CUSTOM_H
#include "game.h"

enum { ITEM_NAME_UNITS=30, ITEM_NAME_BYTES=120, ITEM_PRIOR_MAX=255 };
void item_names_reset(void);
const char *item_override(ItemStack item);
const char *item_display_name(ItemStack item);
unsigned item_prior_work(ItemStack item);
int item_custom_valid(ItemStack item);
int item_name_normalize(const char * input,char result[ITEM_NAME_BYTES+1]);
/* IDs share the world's item-name table. Zero is the absent name. */
int item_name_intern(const char *input,uint16_t *out);
const char *item_name_for_id(uint16_t id);
int item_set_custom(ItemStack *item,unsigned work,const char *name);
#endif
