#ifndef VOXEL_FLAT_H
#define VOXEL_FLAT_H

#include "world.h"

enum {
    FLAT_VILLAGE=1, FLAT_DECORATION=2, FLAT_STRONGHOLD=4, FLAT_BIOME_STRUCTURES=8,
    FLAT_MINESHAFT=16, FLAT_DUNGEON=32, FLAT_LAKE=64, FLAT_LAVA_LAKE=128,
    FLAT_PRESETS=8, FLAT_MAX_HEIGHT=WY-3, FLAT_CODE_MAX=2400
};
typedef struct {
    /* Zero height is the implicit Classic Flat layout used in v64–66 saves. */
    uint16_t height;
    uint8_t biome,features,blocks[FLAT_MAX_HEIGHT];
} FlatLayout;
typedef struct { const char *name,*code; } FlatPreset;
extern const FlatPreset flat_presets[FLAT_PRESETS];

/* Format 2: bottom-to-top numeric block layers, optional biome and features. */
const char *flat_parse(const char *code,FlatLayout *out);
int flat_valid(const FlatLayout *layout);
int flat_equal(const FlatLayout *a,const FlatLayout *b);
const FlatLayout *flat_effective(const FlatLayout *layout);
int flat_spawn_platform(const FlatLayout *layout,int x,int z);
const char *flat_name(const FlatLayout *layout);

const FlatLayout *world_flat_layout(void);
int world_begin_layout(uint64_t seed,int generation,WorldProfile profile,int structures_disabled,
    const FlatLayout *layout);

#endif
