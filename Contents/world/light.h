#ifndef VOXEL_BLOCK_LIGHT_H
#define VOXEL_BLOCK_LIGHT_H
#include "world.h"

/* The outside cell of each edge is included to shade faces at chunk seams.
   The view is borrowed until the next block_light_view call. A ready view
   with no array has no block light; not ready means defer, not darkness. */
typedef struct { const uint8_t *levels; int ready; } LightView;
enum { LIGHT_VIEW_EDGE=CHUNK_SIZE+2 };
LightView block_light_view(int cx,int cz);
/* Rendering-only fallback: last solved sample for this chunk and world.
   Gameplay queries must never use it. Borrow until the next view request. */
const uint8_t *block_light_previous(int cx,int cz);
int block_light_level(LightView view,int x,int y,int z);
/* Returns -1 while an interactive chunk or light result is pending. */
int block_light_at(int x,int y,int z);
/* Like block_light_at, but does not request or generate missing chunks. */
int block_light_at_resident(int x,int y,int z);
int light_has_open_sky(int x,int y,int z);
int light_has_open_sky_resident(int x,int y,int z);
int light_emission(int raw);
int light_opacity(int raw);
int light_start(void);
void light_stop(void);
void light_reset(void);
#endif
