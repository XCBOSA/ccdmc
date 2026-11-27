#ifndef VOXEL_UI_H
#define VOXEL_UI_H

#include "autotree.h"
#include "ccdui.h"
#include "ccduicomp.h"
#include "ui_nodes.h"

/* UI creates touch controls and drives the update/render timer. */
AutoTree build_ui(UIContainer window);
void status(AutoTree root, const char *message);
void show_world_list(AutoTree root, const char *message);
void start_frame_sample(void);

#endif
