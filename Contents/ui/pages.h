#ifndef VOXEL_PAGES_H
#define VOXEL_PAGES_H

#include "ui.h"

int playing(void);
void layout_page(AutoTree root, int w, int h);
void open_inventory(AutoTree root, AutoTree sender);
void open_workbench(AutoTree root);
void open_furnace(AutoTree root, int x, int y, int z);
void open_chest(AutoTree root,int x,int y,int z);
void open_ender_chest(AutoTree root,int x,int y,int z);
void open_cart_storage(AutoTree root,int slot);
void open_hopper(AutoTree root,int x,int y,int z);
void open_ejector(AutoTree root,int x,int y,int z);
void open_villager(AutoTree root,int actor);
void close_villager(AutoTree root);
int villager_panel_visible(void);
void update_villager_panel(AutoTree root);
int chest_panel_visible(void);
void close_chest(AutoTree root);
int furnace_panel_visible(void);
int brewing_panel_visible(void);
void open_brewing(AutoTree root,int x,int y,int z);
void open_enchanting(AutoTree root,int x,int y,int z);
void open_anvil(AutoTree root,int x,int y,int z);
void update_brewing_panel(AutoTree root);
void update_furnace_panel(AutoTree root);
void update_storage_panel(AutoTree root);
void pause_game(AutoTree root, AutoTree sender);
void show_death(AutoTree root);
void note_autosave(AutoTree root, int success);
void restart_game_ui(AutoTree root);

#endif
