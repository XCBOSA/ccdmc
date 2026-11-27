#ifndef VOXEL_INVENTORY_UI_H
#define VOXEL_INVENTORY_UI_H

#include "ui.h"
#include "../game/game.h"

enum { SLOT_EDGE = 44 };
AutoTree make_item_slot(const char *prefix, int index, CCDMCAction tap);
AutoTree make_storage_slot(int index,CCDMCAction tap,ItemStack item,const char *label,int edge,int selected);
AutoTree make_inventory_slot(int index,CCDMCAction tap,int edge,int selected);
AutoTree make_hotbar_slot(int index,CCDMCAction tap,int edge,int selected);
AutoTree make_item_preview_slot(const char *prefix,int type,CCDMCAction tap,int edge);
void paint_item_slot(AutoTree root, const char *prefix, int index, int highlighted);
void paint_item_stack(AutoTree root, const char *prefix, int index, ItemStack item);
void paint_selected_stack(AutoTree root,const char *prefix,int index,int selected,ItemStack item);
int item_slot_index(AutoTree sender, const char *prefix);

#endif
