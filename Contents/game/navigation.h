#ifndef VOXEL_NAVIGATION_H
#define VOXEL_NAVIGATION_H
#include "game.h"
#include <stddef.h>

enum { MAP_SIDE=128, MAP_PIXELS=MAP_SIDE*MAP_SIDE, MAP_MAX_SCALE=4, MAP_LIMIT=255 };
typedef struct {
    int x,z;
    Dimension dimension;
    unsigned scale;
    const uint8_t *colors;
    unsigned explored;
    uint64_t revision;
} MapView;
typedef struct { int x,z,heading,visible; } MapMarker;
/* Filled maps reserve the lowest custom-data byte; the name and prior work
   still fit in the remaining 24 bits and are orthogonal to the shared map. */
static inline unsigned map_item_id(ItemStack item) { return item.type==FILLED_MAP ? item.custom&255u : 0; }
void navigation_reset(void);
unsigned map_count(void);
int map_view(unsigned id,MapView *out);
int map_restore(int x,int z,Dimension dim,unsigned scale,const uint8_t colors[MAP_PIXELS]);
int map_can_zoom(unsigned id);
unsigned map_zoom(unsigned id);
void map_rollback(unsigned id);
const char *map_activate(void);
void navigation_step(float dt);
MapMarker map_marker(MapView map);
uint32_t map_rgb(uint8_t color);
int navigation_compass_step(void);
int navigation_clock_minutes(void);
void navigation_status(char *out,size_t capacity);
#endif
