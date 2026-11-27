#ifndef VOXEL_ITEM_ICONS_H
#define VOXEL_ITEM_ICONS_H

enum { ITEM_ICON_LAYERS=4 };
/* Hexadecimal coordinate pairs in a 16×16 design grid; semicolons
   separate closed polygons in one colour layer. */
typedef struct { const char *path[ITEM_ICON_LAYERS]; unsigned rgb[ITEM_ICON_LAYERS]; } ItemIcon;
int item_icon(int item,ItemIcon *out);

#endif
