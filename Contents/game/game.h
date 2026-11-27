#ifndef VOXEL_GAME_H
#define VOXEL_GAME_H
#include <stddef.h>

#include "../world/world.h"

enum { MAXDROP = 128, HOTBAR_SIZE = 9, BAG_SIZE = 27, SLOT_COUNT = 36, STACK_MAX = 64 };
/* Placeable items keep their original IDs for legacy saves. Other inventory
   IDs are not world cells. Never pass one to set_block without conversion. */
enum {
    STICK = 64, COAL, CHARCOAL, IRON_INGOT, GOLD_INGOT, DIAMOND,
    WOOD_PICKAXE, STONE_PICKAXE, IRON_PICKAXE, GOLD_PICKAXE, DIAMOND_PICKAXE,
    APPLE, FLINT, FLINT_AND_STEEL, QUARTZ, GLOWSTONE_DUST, STRIATED_GLASS, REED_GRIT,
    ARCHIVE_BEARING_ITEM, BUCKET, LAVA_BUCKET, WATER_BUCKET,
    RAW_PORK, COOKED_PORK, ROTTEN_FLESH, REED_FIN,
    WOOD_SWORD, STONE_SWORD, IRON_SWORD, GOLD_SWORD, DIAMOND_SWORD,
    STRING, FEATHER, BOW, ARROW, BOAT, REDSTONE_DUST, MINECART, SADDLE, FRUIT_RATION, LEATHER,
    FISHING_ROD, RAW_FISH, COOKED_FISH, RAW_SALMON, COOKED_SALMON, CLOWNFISH, PUFFERFISH,
    SHEARS, WOOD_HOE, STONE_HOE, IRON_HOE, GOLD_HOE, DIAMOND_HOE,
    WHEAT_SEEDS, WHEAT, BREAD, CARROT, POTATO, BAKED_POTATO, BONE, BONE_MEAL,
    RAW_BEEF, COOKED_BEEF, RAW_CHICKEN, COOKED_CHICKEN, MILK_BUCKET, EGG, LEGACY_ITEM_END,
    EXTENDED_ITEM_BASE=256, SILT_GEL=EXTENDED_ITEM_BASE, LEAD, MAGMA_CREAM, FILTER_MOSS,
    GLASS_BOTTLE, WATER_BOTTLE, AWKWARD_POTION, MUNDANE_POTION, POTION_FIRE, POTION_FIRE_LONG,
    LEATHER_HELMET, LEATHER_CHESTPLATE, LEATHER_LEGGINGS, LEATHER_BOOTS,
    CHAIN_HELMET, CHAIN_CHESTPLATE, CHAIN_LEGGINGS, CHAIN_BOOTS,
    IRON_HELMET, IRON_CHESTPLATE, IRON_LEGGINGS, IRON_BOOTS,
    GOLD_HELMET, GOLD_CHESTPLATE, GOLD_LEGGINGS, GOLD_BOOTS,
    DIAMOND_HELMET, DIAMOND_CHESTPLATE, DIAMOND_LEGGINGS, DIAMOND_BOOTS,
    PAPER, BOOK, ENCHANTED_BOOK, LEVER_ITEM, REDSTONE_TORCH_ITEM, REPEATER_ITEM, REDSTONE_LAMP_ITEM, COMPARATOR_ITEM,
    STONE_PLATE_ITEM, WOOD_PLATE_ITEM, GOLD_PLATE_ITEM, IRON_PLATE_ITEM,
    STONE_BUTTON_ITEM, WOOD_BUTTON_ITEM, HOPPER_ITEM, DROPPER_ITEM, DISPENSER_ITEM, TNT_ITEM, GUNPOWDER,
    PISTON_ITEM, STICKY_PISTON_ITEM, EMERALD, WOOD_DOOR_ITEM, IRON_DOOR_ITEM, EMERALD_ORE_ITEM, EMERALD_BLOCK_ITEM,
    IRON_HORSE_ARMOR, GOLD_HORSE_ARMOR, DIAMOND_HORSE_ARMOR,
    OAK_SAPLING, BIRCH_SAPLING, SPRUCE_SAPLING, JUNGLE_SAPLING, ACACIA_SAPLING, DARK_OAK_SAPLING,
    BIRCH_LEAF_ITEM, SPRUCE_LEAF_ITEM, JUNGLE_LEAF_ITEM, ACACIA_LEAF_ITEM, DARK_OAK_LEAF_ITEM,
    PUMPKIN_ITEM, PUMPKIN_SEEDS, POPPY_ITEM,
    SNOWBALL, WOOD_SHOVEL, STONE_SHOVEL, IRON_SHOVEL, GOLD_SHOVEL, DIAMOND_SHOVEL,
    INK_SAC, GRAY_DYE, LIGHT_GRAY_DYE,
    BLACK_GLASS_ITEM, GRAY_GLASS_ITEM, LIGHT_GRAY_GLASS_ITEM, WHITE_GLASS_ITEM,
    RED_GLASS_ITEM, GREEN_GLASS_ITEM, BROWN_GLASS_ITEM, BLUE_GLASS_ITEM,
    PURPLE_GLASS_ITEM, CYAN_GLASS_ITEM, PINK_GLASS_ITEM, LIME_GLASS_ITEM,
    YELLOW_GLASS_ITEM, LIGHT_BLUE_GLASS_ITEM, MAGENTA_GLASS_ITEM, ORANGE_GLASS_ITEM,
    LAPIS_LAZULI, LAPIS_ORE_ITEM, LAPIS_BLOCK_ITEM, DANDELION_ITEM, CACTUS_ITEM, COCOA_BEANS, COOKIE,
    ROSE_RED, CACTUS_GREEN, DANDELION_YELLOW, PURPLE_DYE, CYAN_DYE, PINK_DYE, LIME_DYE,
    LIGHT_BLUE_DYE, MAGENTA_DYE, ORANGE_DYE, GLASS_PANE_ITEM, IRON_BARS_ITEM,
    BLACK_PANE_ITEM, GRAY_PANE_ITEM, LIGHT_GRAY_PANE_ITEM, WHITE_PANE_ITEM,
    RED_PANE_ITEM, GREEN_PANE_ITEM, BROWN_PANE_ITEM, BLUE_PANE_ITEM,
    PURPLE_PANE_ITEM, CYAN_PANE_ITEM, PINK_PANE_ITEM, LIME_PANE_ITEM,
    YELLOW_PANE_ITEM, LIGHT_BLUE_PANE_ITEM, MAGENTA_PANE_ITEM, ORANGE_PANE_ITEM,
    TRIPWIRE_HOOK_ITEM, TRAPPED_CHEST_ITEM, ARCHIVE_REWARD_ITEM,
    SUGAR, SPIDER_EYE, BROWN_MUSHROOM_ITEM, FERMENTED_SPIDER_EYE,
    POTION_WEAKNESS, POTION_WEAKNESS_LONG, SPLASH_WEAKNESS, SPLASH_WEAKNESS_LONG,
    RED_MUSHROOM_ITEM, BOWL, MUSHROOM_STEW, BROWN_MUSHROOM_BLOCK_ITEM,
    COMPASS_ITEM, CLOCK_ITEM, EMPTY_MAP, FILLED_MAP,
    WOOD_AXE, STONE_AXE, IRON_AXE, GOLD_AXE, DIAMOND_AXE, DETECTOR_RAIL_ITEM,
    ACTIVATOR_RAIL_ITEM, TNT_MINECART, CHEST_MINECART, HOPPER_MINECART, FURNACE_MINECART, ENDER_CHEST_ITEM,
    AIR_OUTLET_ITEM, PUMP_GASKET, AIR_STATION_ITEM, GOLD_BLOCK_ITEM, DIAMOND_BLOCK_ITEM, COBWEB_ITEM,
    BLACK_CLAY_ITEM, GRAY_CLAY_ITEM, LIGHT_GRAY_CLAY_ITEM, WHITE_CLAY_ITEM,
    RED_CLAY_ITEM, GREEN_CLAY_ITEM, BROWN_CLAY_ITEM, BLUE_CLAY_ITEM,
    PURPLE_CLAY_ITEM, CYAN_CLAY_ITEM, PINK_CLAY_ITEM, LIME_CLAY_ITEM,
    YELLOW_CLAY_ITEM, LIGHT_BLUE_CLAY_ITEM, MAGENTA_CLAY_ITEM, ORANGE_CLAY_ITEM, NAME_TAG, LANTERN_SCALES,
    POTION_REGEN, POTION_REGEN_LONG, POTION_REGEN_II, GOLD_NUGGET, GOLDEN_CARROT,
    POTION_WATER_BREATHING, POTION_WATER_BREATHING_LONG,
    SPLASH_WATER_BREATHING, SPLASH_WATER_BREATHING_LONG,
    POTION_NIGHT_VISION, POTION_NIGHT_VISION_LONG, SPLASH_NIGHT_VISION, SPLASH_NIGHT_VISION_LONG,
    LILY_PAD_ITEM, WOOD_TRAPDOOR_ITEM, LADDER_ITEM,
    BIRCH_PLANK_ITEM, SPRUCE_PLANK_ITEM, JUNGLE_PLANK_ITEM, ACACIA_PLANK_ITEM, DARK_OAK_PLANK_ITEM,
    BRICK_BLOCK_ITEM, QUARTZ_BLOCK_ITEM, CLAY_BALL, BRICK_ITEM, NETHER_BRICK_ITEM,
    BUILD_SLAB_ITEMS, BUILD_STAIRS_ITEMS=BUILD_SLAB_ITEMS+BUILD_MATERIALS,
    ITEM_FRAME_ITEM=BUILD_STAIRS_ITEMS+BUILD_MATERIALS-1,
    FIRE_CHARGE, FIREWORK_STAR, FIREWORK_ROCKET,
    EGG_PIG, EGG_COW, EGG_SHEEP, EGG_CHICKEN, EGG_WOLF, EGG_OCELOT, EGG_SQUID,
    EGG_HORSE, EGG_TIDE_CRAB, EGG_VILLAGER, EGG_ZOMBIE, EGG_SKELETON,
    EGG_SHELLBACK, EGG_SPIDER, EGG_CAVE_SPIDER, EGG_TIDERAY, EGG_SILTBACK, EGG_ARCHIVE_MAGMA,
    EGG_REEDLIZARD, EGG_LANTERN_MOTH, EGG_KILN_MOLE, EGG_SEEP_BEETLE,
    SLATE_ITEM, LIMESTONE_ITEM, LAMINATE_ITEM, SURVEY_TAPPER,
    WINCH_ITEM, BRINE_CORE, THERMAL_CORE, AQUIFER_LIGHT_ITEM, SINTERED_TILE_ITEM,
    SHELL_FLAKE, RESONANCE_MARKER_ITEM, TIDE_GUIDE, AIR_PIPE_ITEM,
    SEEP_BED_ITEM, SEEP_TILE_ITEM, SEEP_BRIGHT_ITEM, SETTLING_MOLD_ITEM, CRAWLER_KIT, PULSE_KIT,
    EGG_FAULTBACK, FIELD_FLASK_EMPTY, FIELD_FLASK_ONE, FIELD_FLASK_TWO, FIELD_FLASK_FULL, CISTERN_ITEM,
    HYDRAULIC_SORTER_ITEM, HEAT_BENCH_ITEM, JOINT_BASALT_ITEM, BASALT_PAVING_ITEM, TIDE_GRIT,
    CERAMIC_CORE, WARM_CORE, AIR_RELAY_ITEM, SHALE_SECTION, LIME_SECTION, STRATA_GAUGE, FALL_SIEVE_ITEM,
    CISTERN_PROBE_ITEM, ITEM_LIMIT
};
static inline int field_flask_item(int id) { return id>=FIELD_FLASK_EMPTY && id<=FIELD_FLASK_FULL; }
static inline int spawn_egg_item(int id) { return (id>=EGG_PIG && id<=EGG_SEEP_BEETLE) || id==EGG_FAULTBACK; }
static inline int geology_item(int id) { return id>=SLATE_ITEM && id<=LAMINATE_ITEM; }
static inline int aquifer_item(int id) { return id>=WINCH_ITEM && id<=AQUIFER_LIGHT_ITEM; }
static inline int geology_pick_tier(int raw) { return raw==SLATE_ROCK || raw==JOINT_BASALT ? 2 : 1; }
static inline int plank_item(int item) { return item==PLANKS || (item>=BIRCH_PLANK_ITEM && item<=DARK_OAK_PLANK_ITEM); }
static inline int slab_item(int item) { return item>=BUILD_SLAB_ITEMS && item<BUILD_STAIRS_ITEMS; }
static inline int stair_item(int item) { return item>=BUILD_STAIRS_ITEMS && item<ITEM_FRAME_ITEM; }
static inline int building_stair_item(int mat) { return BUILD_STAIRS_ITEMS+mat-(mat>BUILD_STONE); }
static inline int stained_glass_item(int raw) { return stained_glass_cell(raw) ? BLACK_GLASS_ITEM+raw-STAINED_GLASS_BASE : AIR; }
_Static_assert((int)ORANGE_GLASS_ITEM-(int)BLACK_GLASS_ITEM+1==(int)STAINED_GLASS_END-(int)STAINED_GLASS_BASE,
    "Each glass color has its own cell and item");
_Static_assert((int)ORANGE_CLAY_ITEM-(int)BLACK_CLAY_ITEM+1==(int)STAINED_CLAY_END-(int)STAINED_CLAY_BASE &&
    (int)BLUE_CLAY_ITEM-(int)BLACK_CLAY_ITEM==(int)BLUE_STAINED_CLAY-(int)STAINED_CLAY_BASE,
    "Each clay color has its own cell and item");
static inline int pane_item(int raw) {
    return raw==GLASS_PANE_CELL ? GLASS_PANE_ITEM : raw==IRON_BARS_CELL ? IRON_BARS_ITEM :
        stained_pane_cell(raw) ? BLACK_PANE_ITEM+raw-STAINED_PANE_BASE : AIR;
}
_Static_assert((int)ORANGE_PANE_ITEM-(int)BLACK_PANE_ITEM+1==(int)STAINED_PANE_END-(int)STAINED_PANE_BASE &&
    (int)STAINED_PANE_END-(int)STAINED_PANE_BASE==(int)STAINED_GLASS_END-(int)STAINED_GLASS_BASE,
    "Glass panes share the glass color order");
static inline int is_shovel(int item) { return item>=WOOD_SHOVEL && item<=DIAMOND_SHOVEL; }
static inline int is_axe(int item) { return item>=WOOD_AXE && item<=DIAMOND_AXE; }
static inline int sapling_item(int item) { return item>=OAK_SAPLING && item<=DARK_OAK_SAPLING; }
static inline int typed_leaf_item(int item) { return item>=BIRCH_LEAF_ITEM && item<=DARK_OAK_LEAF_ITEM; }
typedef char ItemAndFenceIDsDisjoint[(int)LEGACY_ITEM_END<=(int)WOOD_FENCE ? 1 : -1];
typedef struct { uint16_t type; uint8_t count; uint16_t damage; uint64_t enchants; uint32_t custom; } ItemStack;
typedef struct {
    const char *name;
    unsigned color;
    uint8_t max_stack;
    uint16_t placed_block;
    uint8_t pick_tier,food;
    uint16_t durability;
    uint8_t melee;
} ItemDef;
const ItemDef *item_def(int id);
const char *item_name(int id);
int item_stack_valid(ItemStack item);
int item_same_kind(ItemStack a,ItemStack b);
/* Exchange one held container for a result; if there is no room, keep both unchanged. */
int inventory_fill_container(int slot,ItemStack result);
/* Prepare a whole-stack exchange in a separate bag; the caller commits only
   after the paired world action succeeds. The live inventory is not touched. */
int inventory_plan_exchange(ItemStack after[SLOT_COUNT],int slot,unsigned consume,ItemStack result);
int is_log(int id);
int mining_drop(int block_id, int pick_tier);
unsigned axe_chop_milliseconds(int item);

typedef struct { float x, y, z; } Vec3;
typedef struct { float x, y, z; ItemStack item; uint16_t age, pickup_delay; } Drop;
typedef struct { int hit, x, y, z, ax, ay, az; } Hit;
typedef enum { SURVIVAL, CREATIVE } GameMode;
typedef enum { PEACEFUL, EASY, NORMAL, HARD } GameDifficulty;

enum { MAX_HEALTH = 20, MAX_FOOD = 20, MAX_AIR = 20, DAY_TICKS = 24000 };
enum { HEALTH_UNITS = 25 };
typedef struct {
    GameMode mode;
    int hardcore;
    int health, food, air, flying;
    unsigned day_tick;
    uint32_t world_day;
    float exhaustion, submerged_time, hurt_time, fall_origin;
    uint8_t health_debt; /* Fraction below the displayed, rounded-up health. */
} PlayerRules;
extern PlayerRules player_rules;
static inline unsigned world_moon_phase(void) { return player_rules.world_day%MOON_PHASES; }
void world_set_dawn(uint32_t day);
enum { PUFFER_POISON_TICKS=1200, PUFFER_HUNGER_TICKS=300, CHICKEN_HUNGER_TICKS=600,
       FIRE_POTION_TICKS=3600, FIRE_POTION_LONG_TICKS=9600,
       WEAKNESS_TICKS=1800, WEAKNESS_LONG_TICKS=4800,
       CAVE_POISON_NORMAL=140,CAVE_POISON_HARD=300,CAVE_POISON_PERIOD=25,
       REGEN_TICKS=900, REGEN_LONG_TICKS=2400, REGEN_II_TICKS=450, REGEN_PERIOD=50,
       WATER_BREATHING_TICKS=3600, WATER_BREATHING_LONG_TICKS=9600,
       NIGHT_VISION_TICKS=3600, NIGHT_VISION_LONG_TICKS=9600, NIGHT_VISION_FADE_TICKS=160 };
typedef struct {
    uint16_t poison,hunger,fire,weakness,regeneration,water_breathing,night_vision;
    uint8_t poison_slow,regeneration_ii;
} PlayerEffects;
extern PlayerEffects player_effects;
int player_grant_water_breathing(unsigned duration);
int player_grant_night_vision(unsigned duration);
float player_night_vision_lift(void);
void player_afflict_cave_spider_poison(void);
int weakened_melee(int damage,unsigned weakness);
enum { PLAYER_MAX_BURN=300, PLAYER_BURN_PERIOD=20 };
typedef struct { uint16_t remaining; uint8_t due; } PlayerBurn;
PlayerBurn player_burn_state(void);
int player_restore_burn(PlayerBurn state);
void player_catch_fire(unsigned ticks);
void player_extinguish(void);
const char *game_mode_name(GameMode mode);
void player_reset_rules(GameMode mode);
int game_difficulty_valid(int value);
GameDifficulty game_selected_difficulty(void);
GameDifficulty game_effective_difficulty(void);
/* Global to the game project. Hardcore worlds always run at Hard. */
int game_set_difficulty(GameDifficulty value);
typedef enum {
    DAMAGE_MELEE, DAMAGE_PROJECTILE, DAMAGE_EXPLOSION, DAMAGE_DIRECT_FIRE, DAMAGE_BURNING,
    DAMAGE_FALL, DAMAGE_DROWNING, DAMAGE_VOID, DAMAGE_STARVATION, DAMAGE_POISON, DAMAGE_ANVIL, DAMAGE_SUFFOCATION,
    DAMAGE_CONTACT
} DamageSource;
void player_take_damage(DamageSource source,int amount);
void player_take_mob_damage(DamageSource source,int amount);
void player_blast_push(Vec3 direction,float strength);
typedef enum { RESPAWN_OK, RESPAWN_RETAINED, RESPAWN_HARDCORE } RespawnResult;
RespawnResult player_respawn(void);
void player_jump(void);
int player_on_ladder(void);
void player_toggle_flight(void);
void player_feed(int amount);
float world_daylight(void);
/* Advance movement, vitals and drops. Returns nonzero on pickup. */
int game_step(float dt, int side, int front, int ascend, int descend);
float game_tick_fraction(void);
extern float touch_altitude;
int inventory_creative_pick(int type);

extern Drop dimension_drops[DIMENSION_COUNT][MAXDROP];
#define drops (dimension_drops[world_dimension()])
/* Slots 0..8 are the hotbar; the next three rows form the backpack. */
extern ItemStack inventory[SLOT_COUNT];
extern int selected_slot;
void inventory_reset(void);
int inventory_add(int type);
int inventory_add_stack(ItemStack item);
int inventory_collect(ItemStack *item); /* Accepts as much of a valid stack as fits. */
void inventory_move(int from, int to);

/* Grid entries reference inventory slots; -1 is empty. Preview and craft use
   the same recipe matcher; craft consumes and inserts atomically. */
ItemStack craft_preview(const int *sources, int size);
ItemStack craft_preview_details(const int *sources,int size,char *description,size_t capacity);
int craft_take(const int *sources, int size);
int player_eat(void);
const char *player_use_bucket(void);
const char *player_use_bottle(void);
const char *player_use_gate(Hit hit);
extern float px, py, pz, vy, yaw, pitch, mx, my;
extern int grounded, drops_dirty;

int player_in_water(void);
int collides(float x, float y, float z);
Vec3 forward(void);
Vec3 player_world_spawn(void);
void generate(void);
Hit target(void);
int player_clear_view(float distance);
const char *mine(void);
const char *mine_quick_chop(unsigned held_ms);
const char *mine_quick_chop_tracked(unsigned held_ms,int *removed);
unsigned mine_hold_milliseconds(void);
const char *mine_hold_action(int *repeat_block);
const char *place(void);

#endif
