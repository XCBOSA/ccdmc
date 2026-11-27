#ifndef VOXEL_MAP_UI_H
#define VOXEL_MAP_UI_H
#include "ui.h"

void map_ui_reset(AutoTree root);
void map_ui_update(AutoTree root,int width,int height,int left_control,int moving,uint64_t now);
#endif
