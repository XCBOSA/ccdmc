#ifndef VOXEL_RENDERER_H
#define VOXEL_RENDERER_H

#include <SDL3/SDL.h>
#include "../game/game.h"

/* Bootstrap size only; the CCDUI safe-area layout supplies the live size. */
enum { START_W = 320, START_H = 400, TILE_EDGE = 32, EMERALD_ORE_TILE=77, EMERALD_BLOCK_TILE=78,
    PUMPKIN_SIDE_TILE=79, PUMPKIN_FACE_TILE=80, PUMPKIN_TOP_TILE=81,
    LAPIS_ORE_TILE=82, LAPIS_BLOCK_TILE=83, BROWN_MUSHROOM_TILE=84, GOLD_BLOCK_TILE=85, DIAMOND_BLOCK_TILE=86,
    STAINED_CLAY_TILE=87, STAINED_CLAY_LAST_TILE=STAINED_CLAY_TILE+15,
    BUILD_PLANK_TILE=STAINED_CLAY_LAST_TILE+1, BUILD_BRICK_TILE=BUILD_PLANK_TILE+5,
    BUILD_QUARTZ_TILE, SLATE_TILE, LIMESTONE_TILE, LAMINATE_TILE,
    WINCH_TILE, DOCK_TILE, BRINE_TILE, VENT_TILE, CONDUIT_TILE,
    PRESSURE_CLOSED_TILE=118, SINTERED_MATERIAL_TILE=119, PRESSURE_LIFT_TILE=48, SHELL_MARKER_TILE=46,
    TURF_ALT_A=49, TURF_ALT_B, STONE_ALT_A, STONE_ALT_B, SOIL_ALT,
    SEEP_BED_ATLAS=120, SEEP_SOFT_ATLAS, SEEP_BRIGHT_ATLAS, SETTLING_FRAME_ATLAS,
    CISTERN_CASING_ATLAS, CISTERN_DRY_ATLAS, CISTERN_WET_ATLAS, CISTERN_FULL_ATLAS,
    ATLAS_TILES=CISTERN_FULL_ATLAS+1, ATLAS_UV_WIDTH=ATLAS_TILES*TILE_EDGE,
    ATLAS_WIDTH=2048, ATLAS_HEIGHT=128, ATLAS_LEVELS=5,
    ATLAS_DETAIL_EDGE=64, ATLAS_DETAIL_COUNT=8, ATLAS_DETAIL_Y=64,
    ATLAS_DETAIL_WIDTH=ATLAS_DETAIL_EDGE*ATLAS_DETAIL_COUNT };
/* Logical vertex coordinates remain unchanged. Physical packing is shared
   with textured.metal and world_lighting.metal; no extra sampler or pass. */
_Static_assert(ATLAS_TILES==128 && ATLAS_UV_WIDTH==4096 && TILE_EDGE==32 && ATLAS_LEVELS==5 &&
    ATLAS_WIDTH==2048 && ATLAS_HEIGHT==128 && ATLAS_DETAIL_WIDTH==512 &&
    STONE_ALT_A==51 && STONE_ALT_B==52 && SLATE_TILE==110 && LIMESTONE_TILE==111,
    "Material shader ABI");

int renderer_init(void);
int renderer_ready(void);
void renderer_begin_mesh_sample(void);
void renderer_sample_forward_chunks(void);
void renderer_stream_world(int x,int z);
void renderer_finish_mesh_sample(int report);
const char *renderer_startup_error(void);
int renderer_start_meshing(void);
void renderer_stop_meshing(void);
int renderer_mesh_selftest(void);
int renderer_resize(int width, int height);
void renderer_reset_world(void);
int upload_mesh(void);
void renderer_prioritize_edits_since(unsigned long long before);
int draw_scene(void);
int renderer_project(Vec3 point,int width,int height,float *x,float *y);
int tile_for_block(int type, int side);
int tile_variant(int tile,int x,int z);
void texel(int tile, int x, int y, Uint8 *pixel);
int material_atlas_build(Uint8 *pixels,Uint32 byte_count);
int material_atlas_selftest(void);

#endif
