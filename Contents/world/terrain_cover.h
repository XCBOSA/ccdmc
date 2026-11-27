#ifndef VOXEL_TERRAIN_COVER_H
#define VOXEL_TERRAIN_COVER_H

#include "world.h"

/* Full, opaque, ordinary cubes only. A byte that is merely opaque for
   lighting can still be an open or independently emitted mesh shape. */
#if defined(__cplusplus) && defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc99-designator"
#endif
static const uint8_t terrain_full_cover[256]={
    [GRASS]=1,[DIRT]=1,[STONE]=1,[WOOD]=1,[BEDROCK]=1,[SAND]=1,[GRAVEL]=1,
    [COAL_ORE]=1,[IRON_ORE]=1,[SNOW]=1,[CLAY]=1,[SANDSTONE]=1,[GOLD_ORE]=1,
    [DIAMOND_ORE]=1,[MOSS_STONE]=1,[PLANKS]=1,[COBBLESTONE]=1,[CRAFTING_TABLE]=1,
    [FURNACE]=1,[PODZOL]=1,[RED_SAND]=1,[TERRACOTTA]=1,[MYCELIUM]=1,[PACKED_ICE]=1,
    [BIRCH_LOG]=1,[SPRUCE_LOG]=1,[JUNGLE_LOG]=1,[ACACIA_LOG]=1,[DARK_OAK_LOG]=1,
    [MUSHROOM_STEM]=1,[RED_MUSHROOM_BLOCK]=1,[BROWN_MUSHROOM_TERRAIN_BYTE]=1,
    [OBSIDIAN]=1,[NETHERRACK]=1,[ASH_SAND]=1,[GLOWSTONE]=1,[NETHER_QUARTZ_ORE]=1,
    [STONE_BRICKS]=1,[NETHER_BRICKS]=1,[REDSTONE_ORE]=1,
    [REDSTONE_BLOCK]=1,[WOOL]=1
};
#if defined(__cplusplus) && defined(__clang__)
#pragma clang diagnostic pop
#endif

static inline void terrain_set_cover(TerrainColumnCover *column,int y,int raw) {
    const uint64_t bit=UINT64_C(1)<<(y&63);
    int word=y>>6;
    int wet=water_strength(raw)!=0;
    if (wet || ((unsigned)raw<256 && terrain_full_cover[raw])) column->seal[word]|=bit;
    else column->seal[word]&=~bit;
    if (raw!=AIR && !wet) column->dry[word]|=bit;
    else column->dry[word]&=~bit;
}

static inline uint64_t terrain_cube_word(const TerrainColumnCover *c,int word) {
    return c->dry[word]&c->seal[word];
}

#endif
