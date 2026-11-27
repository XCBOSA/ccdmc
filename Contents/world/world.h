#ifndef VOXEL_WORLD_H
#define VOXEL_WORLD_H

#include <stdint.h>

/* Horizontal coordinates are generated on demand; height remains finite. */
enum { CHUNK_SIZE = 16, LEGACY_WY = 96, WY = 256 };
enum { WORLD_VIEW_CHUNKS=9 };
enum { MOON_PHASES=8 };
static inline unsigned moon_fullness_quarters(unsigned phase) {
    phase%=MOON_PHASES;
    return phase<=4 ? 4-phase : phase-4;
}
enum { GENERATOR_LEGACY = 1, GENERATOR_CLIMATE = 2, GENERATOR_ADVENTURE = 3,
    GENERATOR_FORTRESS = 4, GENERATOR_RAILWAYS = 5, GENERATOR_EQUINE = 6,
    GENERATOR_FARM = 7, GENERATOR_FILTER_ARCHIVE = 8, GENERATOR_BOOKS = 9,
    GENERATOR_VILLAGES = 10, GENERATOR_DOORS = 11, GENERATOR_TREASURE = 12, GENERATOR_HORSE_ARMOR = 13,
    GENERATOR_FORESTRY = 14, GENERATOR_GOLEMS = 15, GENERATOR_DYES = 16, GENERATOR_TEMPLES = 17,
    GENERATOR_FUNGI = 18, GENERATOR_MUSHROOM_ISLAND = 19,
    GENERATOR_DUNGEONS = 20, GENERATOR_MINESHAFTS = 21, GENERATOR_DESERT_PYRAMIDS = 22,
    GENERATOR_WETLANDS = 23, GENERATOR_ARCHITECTURE = 24, GENERATOR_PRE_GEOLOGY = 25,
    GENERATOR_GEOLOGY = 26, GENERATOR_AQUIFER = 27, GENERATOR_PRESSURE_WORKS = 28,
    GENERATOR_SHELL_HABITATS = 29, GENERATOR_SEEP_GARDENS = 30, GENERATOR_WINDCUT = 31,
    GENERATOR_RAIN_GARDEN = 32, GENERATOR_JOINT_COAST = 33,
    GENERATOR_STRATA_GALLERIES = 34, GENERATOR_SPILLWAYS = 35, GENERATOR_CURRENT = GENERATOR_SPILLWAYS };
static inline int generation_valid(int version) { return version >= GENERATOR_LEGACY && version <= GENERATOR_CURRENT; }
enum {
    AIR, GRASS, DIRT, STONE, WOOD, LEAF, BEDROCK,
    SAND, GRAVEL, COAL_ORE, IRON_ORE, SNOW, WATER,
    CLAY, SANDSTONE, GOLD_ORE, DIAMOND_ORE, MOSS_STONE,
    PLANKS, COBBLESTONE, CRAFTING_TABLE, FURNACE,
    PODZOL, RED_SAND, TERRACOTTA, MYCELIUM, ICE, PACKED_ICE,
    BIRCH_LOG, SPRUCE_LOG, JUNGLE_LOG, ACACIA_LOG, DARK_OAK_LOG,
    MUSHROOM_STEM, RED_MUSHROOM_BLOCK, OBSIDIAN, NETHERRACK, ASH_SAND,
    GLOWSTONE, NETHER_QUARTZ_ORE, LAVA, NETHER_PORTAL_X, NETHER_PORTAL_Z,
    ARCHIVE_PALE_BLOCK, ARCHIVE_FRAME, ARCHIVE_FRAME_SET, ARCHIVE_GATE, ARCHIVE_EXIT, STONE_BRICKS,
    NETHER_BRICKS, REDSTONE_ORE, REDSTONE_BLOCK, RAIL, POWERED_RAIL, CHEST, SADDLE_CHEST,
    WOOL, BED, FARMLAND, CROP_WHEAT, CROP_CARROT, CROP_POTATO, TALL_GRASS, TORCH,
    BASE_BLOCK_COUNT,
    /* Logical block IDs and compact chunk bytes are separate namespaces. */
    WOOD_FENCE=156, NETHER_FENCE, FENCE_GATE, GLASS, BREWING_STAND, IRON_BLOCK, BOOKSHELF, ENCHANTING_TABLE,
    SUGAR_CANE=248, ANVIL=255, FILTER_MAT=1024, REDSTONE_WIRE, LEVER, REDSTONE_TORCH, REPEATER, REDSTONE_LAMP, COMPARATOR,
    PRESSURE_PLATE, BUTTON, HOPPER, DROPPER, DISPENSER, PISTON, STICKY_PISTON, PISTON_HEAD, WOOD_DOOR, IRON_DOOR,
    SMITH_CHEST, EMERALD_ORE, EMERALD_BLOCK, SAPLING, PUMPKIN, PUMPKIN_STEM, POPPY,
    LAPIS_ORE, LAPIS_BLOCK, DANDELION, CACTUS, COCOA, GLASS_PANE, IRON_BARS, TRAPPED_CHEST, TRIPWIRE, TRIPWIRE_HOOK,
    TEMPLE_CHEST, DETECTOR_RAIL, ACTIVATOR_RAIL, ENDER_CHEST, AIR_OUTLET, AIR_STATION, GOLD_BLOCK, DIAMOND_BLOCK,
    DUNGEON_CHEST, MONSTER_SPAWNER, DESERT_PYRAMID_CHEST
};
typedef enum { TREE_OAK, TREE_BIRCH, TREE_SPRUCE, TREE_JUNGLE, TREE_ACACIA, TREE_DARK_OAK, TREE_SPECIES } TreeSpecies;
typedef enum { OVERWORLD, NETHER, END, DIMENSION_COUNT } Dimension;
typedef enum {
    WORLD_STANDARD, WORLD_SUPERFLAT, WORLD_LARGE_BIOMES, WORLD_AMPLIFIED, WORLD_PROFILE_COUNT
} WorldProfile;
enum { SUPERFLAT_SURFACE=3 };

typedef enum {
    BIOME_LEGACY, BIOME_DEEP_OCEAN, BIOME_OCEAN, BIOME_RIVER, BIOME_FROZEN_RIVER,
    BIOME_BEACH, BIOME_STONE_SHORE, BIOME_PLAINS, BIOME_FOREST, BIOME_BIRCH,
    BIOME_ROOFED, BIOME_TAIGA, BIOME_COLD_TAIGA, BIOME_MEGA_TAIGA, BIOME_SWAMP,
    BIOME_DESERT, BIOME_SAVANNA, BIOME_MESA, BIOME_JUNGLE, BIOME_MOUNTAINS,
    BIOME_ICE_PLAINS, BIOME_ICE_SPIKES, BIOME_MUSHROOM, BIOME_NETHER, BIOME_END, BIOME_JOINT_COAST, BIOME_COUNT
} Biome;

unsigned hash2(int x, int z);
int terrain_height(int x, int z);
Biome terrain_biome(int x, int z);
const char *biome_name(Biome biome);
int world_sea_level(void);
int inworld(int x, int y, int z);
int block(int x, int y, int z);
int set_block(int x, int y, int z, int type);
int solid(int type);
void world_reset(void);
void world_begin(uint64_t seed);
int world_begin_generated(uint64_t seed, int generation);
int world_begin_profile(uint64_t seed,int generation,WorldProfile profile);
int world_begin_options(uint64_t seed,int generation,WorldProfile profile,int structures_disabled);
uint64_t world_seed(void);
uint32_t world_seed_mix32(void);
int world_generation(void);
WorldProfile world_profile(void);
int world_structures_disabled(void);
int world_profile_valid(int profile);
const char *world_profile_name(WorldProfile profile);
Dimension world_dimension(void);
int world_select_dimension(Dimension dimension);
enum { STRONGHOLD_FLOOR = 32 };
/* Returns 0 if this world has no natural stronghold. */
int nearest_stronghold(int x, int z, int *center_x, int *center_z);
/* Returns the floor of an archived hot chamber, or -1. */
int nether_fortress_floor(int x, int z);
/* Immutable pedestal position; the crystal above it is separate battle state. */
void water_step(void);

/* Save sparse changes, then the queued fluid work in its original order. */
#define WORLD_SAVE_LIMIT 16000000
typedef int (*WorldRecord)(void *ctx, int x, int y, int z, int cell);
int world_visit_edits(WorldRecord visit, void *ctx);
/* Visit placed blocks that need dynamic mesh state, without scanning the chunk. */
int world_visit_chunk_mesh_details(int cx,int cz,WorldRecord visit,void *ctx);
/* Visits live natural spawners in resident chunks without starting loads. */
int world_visit_dungeon_spawners(int cx,int cz,int radius,WorldRecord visit,void *ctx);
int world_visit_mine_carts(int cx,int cz,int radius,WorldRecord visit,void *ctx);
/* Convert the generated launch point to an ordinary rail, with a saved edit. */
int world_claim_mine_cart(int x,int y,int z);
int world_visit_dimension_edits(Dimension dimension, WorldRecord visit, void *ctx);
/* Opaque resident-independent edit groups, in the same order as the
   flat visitor. Read-only; a lease ends when the visitor returns. */
typedef struct WorldEditGroup WorldEditGroup;
typedef int (*WorldEditGroupVisit)(void *ctx,const WorldEditGroup *group,int cx,int cz,
    unsigned long long revision,unsigned count,unsigned wide_count);
int world_visit_edit_groups(Dimension dimension,WorldEditGroupVisit visit,void *ctx);
int world_visit_edit_group(const WorldEditGroup *group,WorldRecord visit,void *ctx);
int water_visit_pending(WorldRecord visit, void *ctx);
int water_visit_dimension_pending(Dimension dimension, WorldRecord visit, void *ctx);
int world_has_edit(int x, int y, int z);
/* Returns -1 if there is no explicit edit. Never loads terrain. */
int world_recorded_cell(Dimension d,int x,int y,int z);
typedef struct { int x, y, z, cell; } BlockEdit;
/* Round-robin farm batch for a resident chunk; never requests or generates it. */
int world_farm_batch(int cx,int cz,BlockEdit *out,int limit);
int world_set_farm_stage(int x,int y,int z,int cell);
int world_has_cisterns(void);
int world_cistern_batch(int cx,int cz,BlockEdit *out,int limit);
int world_set_cistern_level(int x,int y,int z,int expected,int next);
int world_set_heat_bench_level(int x,int y,int z,int expected,int next);
int world_has_sorters(void);
int world_sorter_batch(int cx,int cz,BlockEdit *out,int limit);
/* On allocation failure the group leaves all visible blocks unchanged. */
int world_set_blocks(const BlockEdit *edits, int count);
/* Restore into a reset world; previously generated spawn chunks stay in sync. */
int world_restore_edit(int x, int y, int z, int cell);
int water_restore_pending(int x, int y, int z);

/* Chunk bytes store normal blocks or one of the flowing-water strengths.
   WATER itself is a permanent source; block() hides the distinction. */
enum { POWERED_GATE_BASE=164, POWERED_GATE_END=172,
    FILTER_MAT_BASE=172, FILTER_MAT_END=176,
    TRACK_BASE = 176, TRACK_POWERED = TRACK_BASE + 10, TRACK_END = TRACK_POWERED + 6,
    BED_BASE=192, BED_CELL_END=200,
    FARM_BASE=200, WHEAT_BASE=208, CARROT_BASE=216, POTATO_BASE=224, FARM_END=232,
    GATE_BASE=232, GATE_END=240, FLOW_BASE = 240, WATER_LEVELS = 8,
    ANVIL_BASE=249, ANVIL_END=255 };
/* These values were never valid terrain cells. Item IDs are independent. */
enum { WIRE_BASE=64, WIRE_END=80, LEVER_BASE=80, LEVER_END=96, LEVER_ON=8 };
enum { REPEATER_BASE=96, REPEATER_END=128, REPEATER_ON=16, TORCH_POWER_BASE=132,
       TORCH_POWER_END=142, TORCH_POWER_ON=5 };
enum { LAMP_OFF=142, LAMP_ON=143 };
enum { COMPARATOR_BASE=144, COMPARATOR_SUBTRACT=4, COMPARATOR_END=152 };
/* Compact terrain IDs may overlap inventory IDs, never other raw cells. */
enum { SMITH_CHEST_CELL=128, EMERALD_ORE_CELL, EMERALD_BLOCK_CELL };
/* Internal compact generated-terrain encoding only. Not a valid edit or
   serialized cell (its numeric value happens to equal the inventory egg). */
enum { BROWN_MUSHROOM_TERRAIN_BYTE=131 };
enum { PUMPKIN_FACE_BASE=544, PUMPKIN_CELL=PUMPKIN_FACE_BASE+2, PUMPKIN_FACE_END=548, POPPY_CELL=548,
    PUMPKIN_STEM_BASE=552, PUMPKIN_STEM_END=560 };
/* Four horizontal output faces, with the empty-flag mode in the second bank. */
enum { CISTERN_PROBE_BASE=560, CISTERN_PROBE_EMPTY=4, CISTERN_PROBE_END=568 };
static inline int cistern_probe_cell(int raw) { return raw>=CISTERN_PROBE_BASE && raw<CISTERN_PROBE_END; }
static inline int cistern_probe_facing(int raw) { return (raw-CISTERN_PROBE_BASE)&3; }
static inline int cistern_probe_empty(int raw) { return ((raw-CISTERN_PROBE_BASE)&CISTERN_PROBE_EMPTY)!=0; }
enum { STAINED_GLASS_BASE=576, STAINED_GLASS_END=592, STAINED_GLASS_LEGACY_END=580 };
static inline int stained_glass_cell(int raw) { return raw>=STAINED_GLASS_BASE && raw<STAINED_GLASS_END; }
enum { GLASS_PANE_CELL=640, IRON_BARS_CELL, STAINED_PANE_BASE=656, STAINED_PANE_END=672 };
enum { TRAPPED_CHEST_CELL=672 };
enum { TRIPWIRE_CELL=673, TRIPWIRE_HOOK_BASE=676, TRIPWIRE_HOOK_END=680, TRIPWIRE_MAX_SPAN=41,
    TRIPWIRE_PULSE_TICKS=10 };
/* Cell 681 is read in legacy saves only. Do not allocate it to another block. */
enum { TEMPLE_CHEST_CELL=680, ARCHIVE_REWARD_CELL=681, BROWN_MUSHROOM=682, RED_MUSHROOM=683, BROWN_MUSHROOM_BLOCK=684 };
enum { DETECTOR_TRACK_BASE=688, DETECTOR_TRACK_END=694 };
enum { ACTIVATOR_TRACK_BASE=694, ACTIVATOR_TRACK_END=700 };
enum { ENDER_CHEST_BASE=700, ENDER_CHEST_END=704 };
static inline int ender_chest_cell(int raw) { return raw>=ENDER_CHEST_BASE && raw<ENDER_CHEST_END; }
enum { AIR_OUTLET_BASE=704, AIR_OUTLET_WALL=720, AIR_OUTLET_END=724 };
static inline int air_outlet_cell(int raw) { return raw>=AIR_OUTLET_BASE && raw<AIR_OUTLET_END; }
/* Historical edited-cell range; current pressure occupies only seven values. */
enum { AIR_STATION_BASE=724, AIR_STATION_MAX=6, AIR_STATION_ARCHIVE_END=740,
       GOLD_BLOCK_CELL=740, DIAMOND_BLOCK_CELL=741 };
enum { DUNGEON_CHEST_CELL=742, DUNGEON_ZOMBIE_CELL=743, DUNGEON_SKELETON_CELL, DUNGEON_SPIDER_CELL };
enum { COBWEB=746, MINE_CAVE_SPIDER_CELL=747, MINE_CART_TRACK_BASE=748, MINE_CART_TRACK_END=758 };
enum { DESERT_PYRAMID_CHEST_CELL=758, STAINED_CLAY_BASE=759, STAINED_CLAY_END=775,
       BLUE_STAINED_CLAY=STAINED_CLAY_BASE+7, ORANGE_STAINED_CLAY=STAINED_CLAY_BASE+15 };
enum { LILY_PAD=STAINED_CLAY_END };
/* Side-mounted building blocks. Low two bits point toward the backing wall. */
enum { WOOD_TRAPDOOR=LILY_PAD+1, TRAPDOOR_OPEN=4, TRAPDOOR_TOP=8, TRAPDOOR_POWERED=16,
    TRAPDOOR_END=WOOD_TRAPDOOR+32, LADDER=TRAPDOOR_END, LADDER_END=LADDER+4 };
/* Building palette order matches the tree species IDs. Only editable cells
   use these wide IDs. Keep old compact terrain and save indices unchanged. */
enum { BUILD_OAK, BUILD_BIRCH, BUILD_SPRUCE, BUILD_JUNGLE, BUILD_ACACIA, BUILD_DARK_OAK,
    BUILD_STONE, BUILD_SANDSTONE, BUILD_COBBLE, BUILD_BRICK, BUILD_STONE_BRICK,
    BUILD_NETHER_BRICK, BUILD_QUARTZ, BUILD_MATERIALS };
enum { BIRCH_PLANK_CELL=LADDER_END, SPRUCE_PLANK_CELL, JUNGLE_PLANK_CELL,
    ACACIA_PLANK_CELL, DARK_OAK_PLANK_CELL, BRICK_CELL, QUARTZ_BLOCK_CELL,
    BUILD_FULL_END, BUILD_SLAB_BASE=BUILD_FULL_END,
    BUILD_SLAB_BOTTOM=0, BUILD_SLAB_TOP=1, BUILD_SLAB_DOUBLE=2,
    BUILD_SLAB_END=BUILD_SLAB_BASE+3*BUILD_MATERIALS, BUILD_STAIRS_BASE=BUILD_SLAB_END,
    BUILD_STAIRS_TOP=4, BUILD_STAIRS_END=BUILD_STAIRS_BASE+8*(BUILD_MATERIALS-1) };
static inline int build_slab(int raw) { return raw>=BUILD_SLAB_BASE && raw<BUILD_SLAB_END; }
/* Smooth structural stone is only available as a slab. */
static inline int build_stair(int raw) { return raw>=BUILD_STAIRS_BASE && raw<BUILD_STAIRS_END; }
static inline int build_partial(int raw) {
    return build_stair(raw) || (build_slab(raw) && (raw-BUILD_SLAB_BASE)%3!=BUILD_SLAB_DOUBLE);
}
static inline int build_double(int raw) { return build_slab(raw) && !build_partial(raw); }
static inline int build_full(int raw) { return raw>=BIRCH_PLANK_CELL && raw<BUILD_FULL_END; }
static inline int build_is_full_top(int raw) {
    return (build_slab(raw) && (raw-BUILD_SLAB_BASE)%3!=BUILD_SLAB_BOTTOM) ||
        (build_stair(raw) && ((raw-BUILD_STAIRS_BASE)&BUILD_STAIRS_TOP));
}
static inline int build_material(int raw) {
    if (build_slab(raw)) return (raw-BUILD_SLAB_BASE)/3;
    if (build_stair(raw)) {
        int i=(raw-BUILD_STAIRS_BASE)/8;
        return i+(i>=BUILD_STONE);
    }
    return -1;
}
#define LILY_PAD_HEIGHT (1.f/64.f)
static inline int stained_clay_cell(int raw) { return raw>=STAINED_CLAY_BASE && raw<STAINED_CLAY_END; }
static inline int dungeon_spawner_cell(int raw) {
    return (raw>=DUNGEON_ZOMBIE_CELL && raw<=DUNGEON_SPIDER_CELL) ||
        raw==MINE_CAVE_SPIDER_CELL;
}
static inline int mine_cart_track(int raw) { return raw>=MINE_CART_TRACK_BASE && raw<MINE_CART_TRACK_END; }
static inline int air_station_cell(int raw) { return raw>=AIR_STATION_BASE && raw<=AIR_STATION_BASE+AIR_STATION_MAX; }
static inline int detector_track(int raw) { return raw>=DETECTOR_TRACK_BASE && raw<DETECTOR_TRACK_END; }
static inline int activator_track(int raw) { return raw>=ACTIVATOR_TRACK_BASE && raw<ACTIVATOR_TRACK_END; }
static inline int small_mushroom(int raw) { return raw==BROWN_MUSHROOM || raw==RED_MUSHROOM; }
enum { SENSOR_PLATES=1, SENSOR_OBJECT_PLATES=2, SENSOR_TRIPWIRE=4 };
static inline int tripwire_hook_cell(int raw) { return raw>=TRIPWIRE_HOOK_BASE && raw<TRIPWIRE_HOOK_END; }
static inline int tripwire_mount(int raw) { return raw-TRIPWIRE_HOOK_BASE; }
static inline int stained_pane_cell(int raw) { return raw>=STAINED_PANE_BASE && raw<STAINED_PANE_END; }
static inline int pane_cell(int raw) { return raw==GLASS_PANE_CELL || raw==IRON_BARS_CELL || stained_pane_cell(raw); }
enum { LAPIS_ORE_CELL=600, LAPIS_BLOCK_CELL=601, DANDELION_CELL=602,
    CACTUS_BASE=608, CACTUS_END=624, COCOA_BASE=624, COCOA_END=636 };
static inline int cactus_cell(int raw) { return raw>=CACTUS_BASE && raw<CACTUS_END; }
static inline int cocoa_cell(int raw) { return raw>=COCOA_BASE && raw<COCOA_END; }
static inline int cocoa_stage(int raw) { return (raw-COCOA_BASE)/4; }
static inline int cocoa_support_side(int raw) { return (raw-COCOA_BASE)&3; }
static inline int dye_resource_cell(int raw) {
    return (raw>=LAPIS_ORE_CELL && raw<=DANDELION_CELL) || cactus_cell(raw) || cocoa_cell(raw);
}
_Static_assert((int)SMITH_CHEST_CELL>=(int)REPEATER_END && (int)EMERALD_BLOCK_CELL<(int)TORCH_POWER_BASE,
    "Treasure cells must not alias circuit metadata");
enum { PRESSURE_BASE=152, PRESSURE_STONE=152, PRESSURE_WOOD, PRESSURE_GOLD, PRESSURE_IRON, PRESSURE_END };
enum { BUTTON_BASE=256, BUTTON_WOOD=260, BUTTON_END=264 };
/* First four variants use the rail compass; the fifth points down. */
enum { HOPPER_BASE=264, HOPPER_DOWN=268, HOPPER_END=269 };
/* Horizontal faces follow the rail compass. */
enum { DROPPER_BASE=269, DROPPER_DOWN=273, DROPPER_UP=274, DROPPER_END=275 };
enum { DISPENSER_BASE=275, DISPENSER_END=281 };
enum { TNT_BLOCK=281, FIRE_CELL=282, SNOW_LAYER=283 };
enum { EJECTOR_DOWN=4, EJECTOR_UP=5, EJECTOR_FACES=6 };
enum { PISTON_BASE=284, PISTON_STICKY=6, PISTON_EXTENDED=12,
       PISTON_HEAD_BASE=308, PISTON_END=320, PISTON_PUSH_LIMIT=12 };
enum { LAVA_FLOW_BASE=320, LAVA_FALLING=327, LAVA_MAX_STRENGTH=8 };
enum { SLATE_ROCK=336, LIMESTONE_ROCK, LAMINATED_MASONRY };
static inline int geology_block(int raw) { return raw>=SLATE_ROCK && raw<=LAMINATED_MASONRY; }
enum { AQUIFER_WINCH=339, AQUIFER_DOCK, AQUIFER_BRINE, AQUIFER_VENT, AQUIFER_LIGHT };
static inline int aquifer_cell(int raw) { return raw>=AQUIFER_WINCH && raw<=AQUIFER_LIGHT; }
static inline int aquifer_fixture(int raw) { return raw>=AQUIFER_DOCK && raw<=AQUIFER_VENT; }
static inline int uses_aquifer(void) { return world_generation()>=GENERATOR_AQUIFER; }
static inline int inside_aquifer(void) { return world_dimension()==END && uses_aquifer(); }
enum { PRESSURE_HATCH=344, PRESSURE_LIFT, PRESSURE_CLOSED, PRESSURE_OPEN, PRESSURE_KILN, SINTERED_TILE };
enum { RESONANCE_MARKER=350, AIR_PIPE=351, SEEP_BED=352, SEEP_TILE=353, SEEP_TILE_BRIGHT=354 };
static inline int seep_cell(int raw) { return raw>=SEEP_BED && raw<=SEEP_TILE_BRIGHT; }
enum { SETTLING_MOLD=355, SETTLING_LIME, SETTLING_PACKED };
static inline int settling_cell(int raw) { return raw>=SETTLING_MOLD && raw<=SETTLING_PACKED; }
/* Level is the number of flask charges in the collector. Only the empty
   form is placeable. All states keep identical collision and opacity. */
enum { CISTERN_EMPTY=358, CISTERN_MAX=3, CISTERN_FULL=CISTERN_EMPTY+CISTERN_MAX };
static inline int cistern_cell(int raw) { return raw>=CISTERN_EMPTY && raw<=CISTERN_FULL; }
enum { HYDRAULIC_SORTER=362 };
enum { HEAT_BENCH_EMPTY=363, HEAT_BENCH_CAP=4, HEAT_BENCH_FULL=HEAT_BENCH_EMPTY+HEAT_BENCH_CAP };
static inline int heat_bench_cell(int raw) { return raw>=HEAT_BENCH_EMPTY && raw<=HEAT_BENCH_FULL; }
enum { JOINT_BASALT=368, BASALT_PAVING=369 };
static inline int coast_stone(int raw) { return raw==JOINT_BASALT || raw==BASALT_PAVING; }
/* The fixed intake faces opposite the output, on the horizontal track compass. */
enum { AIR_RELAY_BASE=370, AIR_RELAY_END=374 };
enum { FIBROUS_CLAY=374 };
enum { STRATA_SAMPLE_SHALE=376, STRATA_SAMPLE_LIME=377, FALL_SIEVE=378 };
static inline int strata_sample_cell(int raw) { return raw==STRATA_SAMPLE_SHALE || raw==STRATA_SAMPLE_LIME; }
static inline int strata_sample_host(int raw) { return raw==STRATA_SAMPLE_SHALE ? SLATE_ROCK : LIMESTONE_ROCK; }
static inline int air_relay_cell(int raw) { return raw>=AIR_RELAY_BASE && raw<AIR_RELAY_END; }
static inline int air_relay_facing(int raw) { return raw-AIR_RELAY_BASE; }
static inline int pressure_works_cell(int raw) { return raw>=PRESSURE_HATCH && raw<=SINTERED_TILE; }
static inline int pressure_fixture(int raw) { return raw>=PRESSURE_HATCH && raw<=PRESSURE_KILN; }
static inline int inside_pressure_works(void) {
    return world_dimension()==NETHER && world_generation()>=GENERATOR_PRESSURE_WORKS;
}
enum { DOOR_BASE=384, DOOR_HINGE=4, DOOR_OPEN=8, DOOR_POWERED=16, DOOR_IRON=32,
    DOOR_UPPER=64, DOOR_END=512 };
#define BED_HEIGHT .5625f
#define FARMLAND_HEIGHT .9375f
static inline int is_bed_cell(int cell) { return cell>=BED_BASE && cell<BED_CELL_END; }
static inline int filter_mat_cell(int cell) { return cell>=FILTER_MAT_BASE && cell<FILTER_MAT_END; }
enum { FILTER_MAT_SEEDED=FILTER_MAT_BASE, FILTER_MAT_PRIMED, FILTER_MAT_ONE, FILTER_MAT_TWO };
/* 57–61 were intentionally invalid as raw world cells: bed and crops
   already serialize their state in separate ranges. Reuse those five
   bytes only for gen14 foliage so terrain still fits one byte per cell. */
enum { TREE_LEAF_BASE=57, TREE_LEAF_END=62, SAPLING_BASE=512, SAPLING_END=SAPLING_BASE+2*TREE_SPECIES,
    PLACED_LEAF_BASE=SAPLING_END, PLACED_LEAF_END=PLACED_LEAF_BASE+TREE_SPECIES };
_Static_assert((int)TREE_LEAF_BASE==(int)BED && (int)TREE_LEAF_END==(int)TALL_GRASS,
    "Typed leaves may use only formerly invalid raw cells");
static inline int sapling_cell(int cell) { return cell>=SAPLING_BASE && cell<SAPLING_END; }
static inline int sapling_species(int cell) { return (cell-SAPLING_BASE)/2; }
static inline int placed_leaf_cell(int cell) { return cell>=PLACED_LEAF_BASE && cell<PLACED_LEAF_END; }
static inline int pumpkin_cell(int raw) { return raw>=PUMPKIN_FACE_BASE && raw<PUMPKIN_FACE_END; }
static inline int pumpkin_stem_cell(int raw) { return raw>=PUMPKIN_STEM_BASE && raw<PUMPKIN_STEM_END; }
static inline int leaf_species(int raw) {
    if (raw>=TREE_LEAF_BASE && raw<TREE_LEAF_END) return raw-TREE_LEAF_BASE+1;
    return placed_leaf_cell(raw) ? raw-PLACED_LEAF_BASE : TREE_OAK;
}
static inline int natural_leaf(int species) { return species==TREE_OAK ? LEAF : TREE_LEAF_BASE+species-1; }
static inline int tree_log_species(int type) {
    return type==WOOD ? TREE_OAK : type>=BIRCH_LOG && type<=DARK_OAK_LOG ? type-BIRCH_LOG+1 : -1;
}
static inline int tree_log_type(int species) { return species==TREE_OAK ? WOOD : BIRCH_LOG+species-1; }
static inline int is_farm_cell(int cell) {
    return (cell>=FARM_BASE && cell<FARM_END) || filter_mat_cell(cell) || cell==SUGAR_CANE || sapling_cell(cell) ||
        pumpkin_stem_cell(cell) || cactus_cell(cell) || cocoa_cell(cell) || small_mushroom(cell);
}
static inline int brewing_block(int type) { return type==GLASS || type==BREWING_STAND; }
static inline int book_block(int type) { return type==BOOKSHELF || type==ENCHANTING_TABLE || type==SUGAR_CANE; }
static inline int chest_inventory_type(int type) { return type==CHEST || type==TRAPPED_CHEST; }
static inline int is_chest(int type) {
    return chest_inventory_type(type) || type==SADDLE_CHEST || type==SMITH_CHEST || type==TEMPLE_CHEST ||
        type==DUNGEON_CHEST || type==DESERT_PYRAMID_CHEST;
}
/* Geometry and opacity only; Ender storage is never a public container. */
static inline int chest_shape_type(int type) { return is_chest(type) || type==ENDER_CHEST; }
/* Generated loot chests use the same pairing as ordinary chests. */
static inline int chest_pair_type(int type) { return type==TRAPPED_CHEST ? TRAPPED_CHEST : CHEST; }
static inline int anvil_cell(int cell) { return cell>=ANVIL_BASE && cell<ANVIL_END; }
static inline int wire_cell(int cell) { return cell>=WIRE_BASE && cell<WIRE_END; }
static inline int lever_cell(int cell) { return cell>=LEVER_BASE && cell<LEVER_END; }
static inline int repeater_cell(int cell) { return cell>=REPEATER_BASE && cell<REPEATER_END; }
static inline int torch_power_cell(int cell) { return cell>=TORCH_POWER_BASE && cell<TORCH_POWER_END; }
static inline int comparator_cell(int cell) { return cell>=COMPARATOR_BASE && cell<COMPARATOR_END; }
static inline int pressure_cell(int cell) { return cell>=PRESSURE_BASE && cell<PRESSURE_END; }
static inline int button_cell(int cell) { return cell>=BUTTON_BASE && cell<BUTTON_END; }
static inline int hopper_cell(int cell) { return cell>=HOPPER_BASE && cell<HOPPER_END; }
static inline int dropper_cell(int cell) { return cell>=DROPPER_BASE && cell<DROPPER_END; }
static inline int dispenser_cell(int cell) { return cell>=DISPENSER_BASE && cell<DISPENSER_END; }
static inline int piston_base_cell(int raw) { return raw>=PISTON_BASE && raw<PISTON_HEAD_BASE; }
static inline int piston_head_cell(int raw) { return raw>=PISTON_HEAD_BASE && raw<PISTON_END; }
static inline int piston_cell(int raw) { return raw>=PISTON_BASE && raw<PISTON_END; }
static inline int piston_extended(int raw) { return piston_base_cell(raw) && raw>=PISTON_BASE+PISTON_EXTENDED; }
static inline int piston_sticky(int raw) { return piston_cell(raw) && ((raw-PISTON_BASE)%PISTON_EXTENDED)>=PISTON_STICKY; }
static inline int piston_facing(int raw) { return piston_cell(raw) ? (raw-PISTON_BASE)%6 : -1; }
static inline int piston_base_type(int type) { return type==PISTON || type==STICKY_PISTON; }
static inline int ejector_cell(int cell) { return dropper_cell(cell) || dispenser_cell(cell); }
static inline int ejector_type(int type) { return type==DROPPER || type==DISPENSER; }
static inline int ejector_face(int cell) {
    return dropper_cell(cell) ? cell-DROPPER_BASE : dispenser_cell(cell) ? cell-DISPENSER_BASE : -1;
}
static inline int timed_circuit_cell(int cell) { return repeater_cell(cell) || torch_power_cell(cell) || comparator_cell(cell); }
static inline int circuit_cell(int cell) { return wire_cell(cell) || lever_cell(cell) || timed_circuit_cell(cell) ||
    pressure_cell(cell) || button_cell(cell) || cell==TRIPWIRE_CELL || tripwire_hook_cell(cell); }
static inline int lamp_cell(int cell) { return cell==LAMP_OFF || cell==LAMP_ON; }
static inline int fence_type(int type) { return type==WOOD_FENCE || type==NETHER_FENCE; }
static inline int gate_cell(int cell) {
    return (cell>=GATE_BASE && cell<GATE_END) || (cell>=POWERED_GATE_BASE && cell<POWERED_GATE_END);
}
static inline int door_type(int type) { return type==WOOD_DOOR || type==IRON_DOOR; }
static inline int door_cell(int cell) { return cell>=DOOR_BASE && cell<DOOR_END; }
static inline int door_bits(int cell) { return cell-DOOR_BASE; }
static inline int door_upper(int cell) { return door_cell(cell) && (door_bits(cell)&DOOR_UPPER); }
static inline int door_peer(int cell) { return DOOR_BASE+(door_bits(cell)^DOOR_UPPER); }
static inline int trapdoor_cell(int raw) { return raw>=WOOD_TRAPDOOR && raw<TRAPDOOR_END; }
static inline int ladder_cell(int raw) { return raw>=LADDER && raw<LADDER_END; }
static inline int wall_attachment_dir(int raw) {
    return trapdoor_cell(raw) ? (raw-WOOD_TRAPDOOR)&3 : ladder_cell(raw) ? raw-LADDER : -1;
}
static inline int pane_type(int type) { return type==GLASS_PANE || type==IRON_BARS; }
static inline int barrier_type(int type) {
    return fence_type(type) || type==FENCE_GATE || door_type(type) || pane_type(type) ||
        type==WOOD_TRAPDOOR || type==LADDER;
}
static inline int crop_type(int t) { return t>=CROP_WHEAT && t<=CROP_POTATO; }
static inline int rail_type(int type) {
    return type==RAIL || type==POWERED_RAIL || type==DETECTOR_RAIL || type==ACTIVATOR_RAIL;
}
static inline int lava_flow_cell(int cell) { return cell>=LAVA_FLOW_BASE && cell<=LAVA_FALLING; }
static inline int lava_strength(int cell) {
    return cell==LAVA || cell==LAVA_FALLING ? LAVA_MAX_STRENGTH :
        lava_flow_cell(cell) ? cell-LAVA_FLOW_BASE+1 : 0;
}
static inline float lava_surface(int cell,int above) {
    return lava_strength(above) || cell==LAVA_FALLING ? 1.f : .12f+.1f*lava_strength(cell);
}
static inline int cell_type(int cell) {
    if (air_relay_cell(cell)) return AIR_RELAY_BASE;
    if (cistern_probe_cell(cell)) return CISTERN_PROBE_BASE;
    if (cistern_cell(cell)) return CISTERN_EMPTY;
    if (heat_bench_cell(cell)) return HEAT_BENCH_EMPTY;
    if (cell==BROWN_MUSHROOM_TERRAIN_BYTE) return BROWN_MUSHROOM_BLOCK;
    if (cell==SMITH_CHEST_CELL) return SMITH_CHEST;
    if (cell==TEMPLE_CHEST_CELL) return TEMPLE_CHEST;
    if (cell==DUNGEON_CHEST_CELL) return DUNGEON_CHEST;
    if (cell==DESERT_PYRAMID_CHEST_CELL) return DESERT_PYRAMID_CHEST;
    if (dungeon_spawner_cell(cell)) return MONSTER_SPAWNER;
    if (mine_cart_track(cell)) return RAIL;
    if (cell==TRAPPED_CHEST_CELL) return TRAPPED_CHEST;
    if (ender_chest_cell(cell)) return ENDER_CHEST;
    if (air_outlet_cell(cell)) return AIR_OUTLET;
    if (air_station_cell(cell)) return AIR_STATION;
    if (cell==GOLD_BLOCK_CELL) return GOLD_BLOCK;
    if (cell==DIAMOND_BLOCK_CELL) return DIAMOND_BLOCK;
    if (cell==TRIPWIRE_CELL) return TRIPWIRE;
    if (tripwire_hook_cell(cell)) return TRIPWIRE_HOOK;
    if (cell==EMERALD_ORE_CELL) return EMERALD_ORE;
    if (cell==EMERALD_BLOCK_CELL) return EMERALD_BLOCK;
    if ((cell>=TREE_LEAF_BASE && cell<TREE_LEAF_END) || placed_leaf_cell(cell)) return LEAF;
    if (sapling_cell(cell)) return SAPLING;
    if (pumpkin_cell(cell)) return PUMPKIN;
    if (pumpkin_stem_cell(cell)) return PUMPKIN_STEM;
    if (cell==POPPY_CELL) return POPPY;
    if (stained_glass_cell(cell)) return GLASS;
    if (pane_cell(cell)) return cell==IRON_BARS_CELL ? IRON_BARS : GLASS_PANE;
    if (cell==LAPIS_ORE_CELL) return LAPIS_ORE;
    if (cell==LAPIS_BLOCK_CELL) return LAPIS_BLOCK;
    if (cell==DANDELION_CELL) return DANDELION;
    if (cactus_cell(cell)) return CACTUS;
    if (cocoa_cell(cell)) return COCOA;
    if (cell >= TRACK_BASE && cell < TRACK_END) return cell < TRACK_POWERED ? RAIL : POWERED_RAIL;
    if (detector_track(cell)) return DETECTOR_RAIL;
    if (activator_track(cell)) return ACTIVATOR_RAIL;
    if (is_bed_cell(cell)) return BED;
    if (filter_mat_cell(cell)) return FILTER_MAT;
    if (cell>=FARM_BASE && cell<FARM_END) return FARMLAND+(cell-FARM_BASE)/8;
    if (gate_cell(cell)) return FENCE_GATE;
    if (anvil_cell(cell)) return ANVIL;
    if (wire_cell(cell)) return REDSTONE_WIRE;
    if (lever_cell(cell)) return LEVER;
    if (repeater_cell(cell)) return REPEATER;
    if (torch_power_cell(cell)) return REDSTONE_TORCH;
    if (lamp_cell(cell)) return REDSTONE_LAMP;
    if (comparator_cell(cell)) return COMPARATOR;
    if (pressure_cell(cell)) return PRESSURE_PLATE;
    if (button_cell(cell)) return BUTTON;
    if (hopper_cell(cell)) return HOPPER;
    if (dropper_cell(cell)) return DROPPER;
    if (dispenser_cell(cell)) return DISPENSER;
    if (piston_base_cell(cell)) return piston_sticky(cell) ? STICKY_PISTON : PISTON;
    if (piston_head_cell(cell)) return PISTON_HEAD;
    if (lava_flow_cell(cell)) return LAVA;
    if (door_cell(cell)) return (door_bits(cell)&DOOR_IRON) ? IRON_DOOR : WOOD_DOOR;
    if (trapdoor_cell(cell)) return WOOD_TRAPDOOR;
    if (ladder_cell(cell)) return LADDER;
    return cell >= FLOW_BASE && cell<FLOW_BASE+WATER_LEVELS ? WATER : cell;
}
static inline float plant_height(int cell) {
    if (cell==SNOW_LAYER) return .125f;
    if (sapling_cell(cell)) return (cell-SAPLING_BASE)&1 ? .78f : .61f;
    if (pumpkin_stem_cell(cell)) return .20f+.075f*(cell-PUMPKIN_STEM_BASE);
    if (cell==POPPY_CELL) return .69f;
    if (cell==DANDELION_CELL) return .49f;
    if (small_mushroom(cell)) return .43f;
    if (filter_mat_cell(cell)) return .16f;
    if (cell==BREWING_STAND) return .875f;
    if (cell==ENCHANTING_TABLE) return .75f;
    if (cell==SUGAR_CANE) return 1.f;
    if (wire_cell(cell)) return .075f;
    if (repeater_cell(cell)) return .34f;
    if (comparator_cell(cell)) return .34f;
    if (cell>=WHEAT_BASE && cell<FARM_END) return .16f+.09f*((cell-WHEAT_BASE)&7);
    return cell==TALL_GRASS ? .65f : cell==TORCH ? .68f : cell==COBWEB ? 1.f : 0.f;
}
static inline int valid_cell(int cell) {
    return (cell>=0 && cell<BASE_BLOCK_COUNT && cell!=BED && !(cell>=FARMLAND && cell<=CROP_POTATO) &&
        cell!=NETHER_PORTAL_X && cell!=NETHER_PORTAL_Z && !(cell>=ARCHIVE_PALE_BLOCK && cell<=ARCHIVE_EXIT)) ||
        fence_type(cell) || brewing_block(cell) || book_block(cell) || gate_cell(cell) ||
        (cell>=TRACK_BASE && cell<TRACK_END) || detector_track(cell) || activator_track(cell) ||
        is_bed_cell(cell) || is_farm_cell(cell) ||
        cell==IRON_BLOCK || cell==TNT_BLOCK || cell==FIRE_CELL || cell==SNOW_LAYER ||
        (cell>=SMITH_CHEST_CELL && cell<=EMERALD_BLOCK_CELL) || (cell>=TREE_LEAF_BASE && cell<TREE_LEAF_END) ||
        placed_leaf_cell(cell) || pumpkin_cell(cell) || cell==POPPY_CELL || stained_glass_cell(cell) ||
        dye_resource_cell(cell) || pane_cell(cell) || cell==TRAPPED_CHEST_CELL || cell==TEMPLE_CHEST_CELL ||
        ender_chest_cell(cell) || air_outlet_cell(cell) || air_station_cell(cell) ||
        cell==DUNGEON_CHEST_CELL || dungeon_spawner_cell(cell) || cell==COBWEB || mine_cart_track(cell) ||
        cell==DESERT_PYRAMID_CHEST_CELL || stained_clay_cell(cell) ||
        cell==GOLD_BLOCK_CELL || cell==DIAMOND_BLOCK_CELL ||
        small_mushroom(cell) || cell==BROWN_MUSHROOM_BLOCK || cell==LILY_PAD ||
        anvil_cell(cell) || circuit_cell(cell) || lamp_cell(cell) || hopper_cell(cell) || ejector_cell(cell) || piston_cell(cell) ||
        lava_flow_cell(cell) || door_cell(cell) || trapdoor_cell(cell) || ladder_cell(cell) ||
        build_full(cell) || build_slab(cell) || build_stair(cell) || geology_block(cell) || aquifer_cell(cell) ||
        pressure_works_cell(cell) || cell==RESONANCE_MARKER || cell==AIR_PIPE || seep_cell(cell) || settling_cell(cell) ||
        cistern_cell(cell) || cell==HYDRAULIC_SORTER || heat_bench_cell(cell) || coast_stone(cell) || air_relay_cell(cell) ||
        cell==FIBROUS_CLAY || strata_sample_cell(cell) || cell==FALL_SIEVE || cistern_probe_cell(cell) ||
        (cell >= FLOW_BASE && cell < FLOW_BASE + WATER_LEVELS);
}
static inline int water_strength(int cell) {
    return cell >= FLOW_BASE && cell<FLOW_BASE+WATER_LEVELS ? cell - FLOW_BASE + 1 : cell == WATER ? WATER_LEVELS : 0;
}
static inline int full_top_surface(int raw) { return build_is_full_top(raw) || solid(cell_type(raw)); }
static inline float water_surface(int cell, int above) {
    return water_strength(above) ? 1.f : .12f + .095f * water_strength(cell);
}
/* The four samples are ordered (-1,-1), (0,-1), (-1,0), (0,0) around
   a grid corner. Dry columns don't pull down the bank; covered water
   fills the corner to the next layer so a falling column stays sealed. */
typedef struct { float height,dx,dz; int open; } WaterCorner;
static inline WaterCorner water_corner(const int cell[4],const int above[4]) {
    if (cell[0]==WATER && cell[1]==WATER && cell[2]==WATER && cell[3]==WATER &&
        above[0]==AIR && above[1]==AIR && above[2]==AIR && above[3]==AIR)
        return (WaterCorner){water_surface(WATER,AIR),0.f,0.f,1};
    float h[4],sum=0.f;
    int weight=0,full=0;
    for (int i=0;i<4;++i) {
        if (!water_strength(cell[i])) { h[i]=-1.f; continue; }
        int w=cell[i]==WATER ? 4 : 1;
        full|=water_strength(above[i])!=0;
        h[i]=water_surface(cell[i],above[i]);
        sum+=w*h[i]; weight+=w;
    }
    float dx=0.f,dz=0.f;
    int nx=0,nz=0;
    for (int i=0;i<2;++i) {
        if (h[i*2]>=0.f && h[i*2+1]>=0.f) { dx+=h[i*2+1]-h[i*2]; ++nx; }
        if (h[i]>=0.f && h[i+2]>=0.f) { dz+=h[i+2]-h[i]; ++nz; }
    }
    return (WaterCorner){full ? 1.f : weight ? sum/weight : 0.f,nx ? dx/nx : 0.f,nz ? dz/nz : 0.f,0};
}
/* A bounded, resident-only optical query; has no chunk streaming side effects. */
typedef struct { float surface; int submerged,open_sky; } WaterView;
WaterView water_view_sample(float x,float y,float z);
int world_cell(int x, int y, int z);
/* Never starts a load, updates LRU, or invents terrain for a missing chunk. */
int world_resident_cell(int x,int y,int z,int *cell);
int world_resident_web_column(int x,int z,int y0,int y1);
/* Like world_resident_cell, this does not generate or touch the LRU. */
int world_resident_top(int x,int z,int *out);
int world_column_top(int x, int z);
unsigned long long world_edit_version(void);
/* Revisions follow edits, not generation or eviction of the CPU cache. */
unsigned long long chunk_revision(int cx, int cz);
/* A borrowed chunk stays valid while its latest neighbour views are acquired.
   Procedural terrain still stores bytes. Authored high IDs can add a lazily
   allocated upper-byte plane for each occupied 16-block height section. */
enum { CELL_SECTION_HEIGHT=16, CELL_SECTIONS=WY/CELL_SECTION_HEIGHT, CELL_SECTION_SIZE=CHUNK_SIZE*CHUNK_SIZE*CELL_SECTION_HEIGHT };
typedef struct { uint8_t *planes[CELL_SECTIONS]; } ChunkHigh;
enum { TERRAIN_COVER_WORDS=WY/64 };
typedef struct {
    /* Two bitplanes: 00 empty, dry only other geometry, seal only water,
       both a full opaque cube. No extra storage for underwater culling. */
    uint64_t dry[TERRAIN_COVER_WORDS];
    uint64_t seal[TERRAIN_COVER_WORDS];
} TerrainColumnCover;
typedef struct {
    const uint8_t *cells, *tops, *biomes;
    const int16_t *sky_tops;
    unsigned emitter_mask;
    const ChunkHigh *high;
    const TerrainColumnCover *cover;
} ChunkView;
static inline int chunk_snapshot_cell(const uint8_t *low,const ChunkHigh *high,int index) {
    int cell=low[index];
    if (high) {
        int y=index%WY;
        const uint8_t *plane=high->planes[y/CELL_SECTION_HEIGHT];
        if (plane) cell|=(int)plane[(index/WY)*CELL_SECTION_HEIGHT+y%CELL_SECTION_HEIGHT]<<8;
    }
    return cell==BROWN_MUSHROOM_TERRAIN_BYTE ? BROWN_MUSHROOM_BLOCK : cell;
}
ChunkView chunk_view(int cx, int cz);
ChunkView chunk_cached_view(int cx,int cz);
/* Read an installed view without renewing its cache age or requesting terrain. */
ChunkView chunk_peek_view(int cx,int cz);
/* Borrow a resident column without generating or requesting any terrain. */
int world_precipitation_surface(int x,int z,int *top,Biome *biome);
/* Interactive lookups may return a closed boundary until their chunk is ready.
   World creation, import, and explicit dimension landing can query synchronously. */
int world_stream_start(void);
void world_stream_stop(void);
int world_set_interactive(int active);
void world_stream_pump(int x,int z);
void world_stream_pump_directional(int x,int z,int hx,int hz);
typedef struct {
    unsigned installs, evictions, near_evictions, ahead_evictions, generated_reused;
} ChunkCacheSample;
void world_begin_cache_sample(void);
ChunkCacheSample world_finish_cache_sample(void);
int world_chunk_ready(int x,int z);

#endif
