#include "game.h"
#include "dyes.h"
#include "fireworks.h"
#include "armor.h"
#include "navigation.h"
#include <stdio.h>
#include <string.h>

ItemStack inventory[SLOT_COUNT];
int selected_slot;

int inventory_creative_pick(int type) {
    const ItemDef *def = item_def(type);
    if (player_rules.mode != CREATIVE || !def || type==ENCHANTED_BOOK || type==FILLED_MAP || type==COMPARATOR_ITEM ||
        selected_slot < 0 || selected_slot >= HOTBAR_SIZE) return 0;
    inventory[selected_slot] = (ItemStack){(uint16_t)type, def->max_stack, 0,0,0};
    return 1;
}

void inventory_reset(void) {
    armor_reset();
    memset(inventory, 0, sizeof inventory);
    inventory[0] = (ItemStack){DIRT, 10, 0,0,0};
    inventory[1] = (ItemStack){WATER, 3, 0,0,0};
    selected_slot = 0;
}

static int add_stack(ItemStack *slots, ItemStack item) {
    if (!item.count || !item_stack_valid(item)) return 0;
    int cap = item_def(item.type)->max_stack;
    int space = 0;
    for (int i = 0; i < SLOT_COUNT; i++) {
        if (!slots[i].count) space += cap;
        else if (item_same_kind(slots[i],item)) space += cap - slots[i].count;
    }
    if (space < item.count) return 0;
    for (int pass = 0; pass < 2; pass++) for (int i = 0; i < SLOT_COUNT && item.count; i++) {
        ItemStack *s = &slots[i];
        if (pass == 0 ? (s->count && item_same_kind(*s,item)) : !s->count) {
            int n = cap - s->count;
            if (n > item.count) n = item.count;
            if (!n) continue;
            s->type=item.type; s->damage=item.damage; s->enchants=item.enchants; s->custom=item.custom; s->count+=n;
            item.count -= n;
        }
    }
    return 1;
}
int inventory_add_stack(ItemStack item) { return add_stack(inventory, item); }
int inventory_plan_exchange(ItemStack after[SLOT_COUNT],int slot,unsigned consume,ItemStack result) {
    if (!after || after==inventory || (unsigned)slot>=SLOT_COUNT || !consume ||
        inventory[slot].count<consume || !item_stack_valid(inventory[slot])) return 0;
    memcpy(after,inventory,sizeof inventory);
    after[slot].count-=(uint8_t)consume;
    if (!after[slot].count) after[slot]=(ItemStack){0,0,0,0,0};
    return add_stack(after,result);
}
int inventory_fill_container(int slot,ItemStack result) {
    if ((unsigned)slot>=SLOT_COUNT || !inventory[slot].count || !item_stack_valid(result) || !result.count) return 0;
    if (player_rules.mode==CREATIVE) return inventory_add_stack(result);
    ItemStack *container=&inventory[slot];
    if (container->count==1) { *container=result; return 1; }
    if (!inventory_add_stack(result)) return 0;
    --container->count;
    return 1;
}
int inventory_collect(ItemStack *item) {
    if (!item || !item->count || !item_stack_valid(*item)) return 0;
    const int cap=item_def(item->type)->max_stack;
    int room=0;
    for (int i=0;i<SLOT_COUNT && room<item->count;++i) {
        ItemStack slot=inventory[i];
        if (!slot.count) room+=cap;
        else if (item_same_kind(slot,*item)) room+=cap-slot.count;
    }
    if (!room) return 0;
    ItemStack taken=*item;
    if (room<taken.count) taken.count=(uint8_t)room;
    if (!inventory_add_stack(taken)) return 0;
    item->count-=taken.count;
    if (!item->count) *item=(ItemStack){0,0,0,0,0};
    return 1;
}
int inventory_add(int type) {
    return item_def(type) && inventory_add_stack((ItemStack){(uint16_t)type, 1, 0,0,0});
}

/* A second tap drops the held stack, stacks like blocks, or swaps unlike ones. */
void inventory_move(int from, int to) {
    if (from < 0 || from >= SLOT_COUNT || to < 0 || to >= SLOT_COUNT || from == to) return;
    ItemStack *a = &inventory[from], *b = &inventory[to];
    if (!a->count) return;
    const ItemDef *def = item_def(a->type);
    if (!def) return;
    if (item_same_kind(*a,*b) && def->max_stack>1) {
        int moved = def->max_stack - b->count;
        if (moved > a->count) moved = a->count;
        a->count -= moved;
        b->count += moved;
        if (!a->count) *a = (ItemStack){0, 0, 0,0,0};
    } else {
        ItemStack tmp = *a;
        *a = *b;
        *b = tmp;
    }
}

typedef struct {
    int width, height;
    uint16_t cells[9];
    ItemStack result;
} Recipe;
#define ARMOR_RECIPES(mat,head,body,legs,feet) \
    {3,2,{mat,mat,mat,mat,0,mat},{head,1,0,0,0}}, \
    {3,3,{mat,0,mat,mat,mat,mat,mat,mat,mat},{body,1,0,0,0}}, \
    {3,3,{mat,mat,mat,mat,0,mat,mat,0,mat},{legs,1,0,0,0}}, \
    {3,2,{mat,0,mat,mat,0,mat},{feet,1,0,0,0}}
static const Recipe recipes[] = {
    {2,2,{SAND,GRAVEL,CLAY_BALL,0},{ASH_SAND,3,0,0,0}},
    {2,1,{SILT_GEL,BROWN_MUSHROOM_ITEM},{FILTER_MOSS,2,0,0,0}},
    {2,2,{LEATHER,STRING,GLASS_BOTTLE,0},{FIELD_FLASK_EMPTY,1,0,0,0}},
    {2,2,{LAMINATE_ITEM,STRIATED_GLASS,CLAY_BALL,PUMP_GASKET},{CISTERN_ITEM,1,0,0,0}},
    {2,2,{BRICK_ITEM,STRIATED_GLASS,IRON_INGOT,TIDE_GRIT},{CISTERN_PROBE_ITEM,2,0,0,0}},
    {2,2,{SINTERED_TILE_ITEM,STRIATED_GLASS,IRON_INGOT,PUMP_GASKET},{HYDRAULIC_SORTER_ITEM,1,0,0,0}},
    {2,2,{LAMINATE_ITEM,STRIATED_GLASS,BRICK_ITEM,IRON_INGOT},{HEAT_BENCH_ITEM,1,0,0,0}},
    {2,2,{LAMINATE_ITEM,STRIATED_GLASS,IRON_INGOT,PUMP_GASKET},{AIR_RELAY_ITEM,1,0,0,0}},
    {2,2,{SHALE_SECTION,LIME_SECTION,STRIATED_GLASS,IRON_INGOT},{STRATA_GAUGE,1,0,0,0}},
    {2,2,{SINTERED_TILE_ITEM,STRIATED_GLASS,GRAVEL,PUMP_GASKET},{FALL_SIEVE_ITEM,1,0,0,0}},
    {2,1,{JOINT_BASALT_ITEM,GRAVEL},{BASALT_PAVING_ITEM,2,0,0,0}},
    {2,1,{JOINT_BASALT_ITEM,TIDE_GRIT},{BASALT_PAVING_ITEM,3,0,0,0}},
    {2,2,{LAMINATE_ITEM,IRON_INGOT,IRON_INGOT,PUMP_GASKET},{CRAWLER_KIT,1,0,0,0}},
    {2,2,{STRIATED_GLASS,IRON_INGOT,SEEP_TILE_ITEM,PUMP_GASKET},{PULSE_KIT,1,0,0,0}},
    {3,3,{IRON_INGOT,IRON_INGOT,IRON_INGOT,IRON_INGOT,IRON_INGOT,IRON_INGOT,IRON_INGOT,IRON_INGOT,IRON_INGOT},{IRON_BLOCK,1,0,0,0}},
    {1,1,{IRON_BLOCK},{IRON_INGOT,9,0,0,0}},
    {3,3,{EMERALD,EMERALD,EMERALD,EMERALD,EMERALD,EMERALD,EMERALD,EMERALD,EMERALD},{EMERALD_BLOCK_ITEM,1,0,0,0}},
    {1,1,{EMERALD_BLOCK_ITEM},{EMERALD,9,0,0,0}},
    {3,3,{GOLD_INGOT,GOLD_INGOT,GOLD_INGOT,GOLD_INGOT,GOLD_INGOT,GOLD_INGOT,GOLD_INGOT,GOLD_INGOT,GOLD_INGOT},{GOLD_BLOCK_ITEM,1,0,0,0}},
    {1,1,{GOLD_BLOCK_ITEM},{GOLD_INGOT,9,0,0,0}},
    {3,3,{DIAMOND,DIAMOND,DIAMOND,DIAMOND,DIAMOND,DIAMOND,DIAMOND,DIAMOND,DIAMOND},{DIAMOND_BLOCK_ITEM,1,0,0,0}},
    {1,1,{DIAMOND_BLOCK_ITEM},{DIAMOND,9,0,0,0}},
    {3,3,{IRON_BLOCK,IRON_BLOCK,IRON_BLOCK,0,IRON_INGOT,0,IRON_INGOT,IRON_INGOT,IRON_INGOT},{ANVIL,1,0,0,0}},
    {3,1,{SUGAR_CANE,SUGAR_CANE,SUGAR_CANE},{PAPER,3,0,0,0}},
    {3,3,{PLANKS,PLANKS,PLANKS,BOOK,BOOK,BOOK,PLANKS,PLANKS,PLANKS},{BOOKSHELF,1,0,0,0}},
    {2,2,{LAMINATE_ITEM,IRON_INGOT,SLATE_ITEM,PLANKS},{ENCHANTING_TABLE,1,0,0,0}},
    ARMOR_RECIPES(LEATHER,LEATHER_HELMET,LEATHER_CHESTPLATE,LEATHER_LEGGINGS,LEATHER_BOOTS),
    ARMOR_RECIPES(IRON_INGOT,IRON_HELMET,IRON_CHESTPLATE,IRON_LEGGINGS,IRON_BOOTS),
    ARMOR_RECIPES(GOLD_INGOT,GOLD_HELMET,GOLD_CHESTPLATE,GOLD_LEGGINGS,GOLD_BOOTS),
    ARMOR_RECIPES(DIAMOND,DIAMOND_HELMET,DIAMOND_CHESTPLATE,DIAMOND_LEGGINGS,DIAMOND_BOOTS),
    {1, 1, {WOOD}, {PLANKS, 4, 0,0,0}},
    {1, 2, {PLANKS, PLANKS}, {STICK, 4, 0,0,0}},
    {2, 2, {PLANKS, PLANKS, PLANKS, PLANKS}, {CRAFTING_TABLE, 1, 0,0,0}},
    {3,3,{PLANKS,PLANKS,PLANKS,PLANKS,0,PLANKS,PLANKS,PLANKS,PLANKS},{CHEST,1,0,0,0}},
    {2,2,{PLANKS,IRON_INGOT,QUARTZ,PLANKS},
        {ENDER_CHEST_ITEM,1,0,0,0}},
    {2,2,{IRON_INGOT,0,0,IRON_INGOT},{SHEARS,1,0,0,0}},
    {2,2,{0,IRON_INGOT,IRON_INGOT,0},{SHEARS,1,0,0,0}},
    {2,2,{STRING,STRING,STRING,STRING},{WOOL,1,0,0,0}},
    {3,2,{WOOL,WOOL,WOOL,PLANKS,PLANKS,PLANKS},{BED,1,0,0,0}},
    {2,3,{PLANKS,PLANKS,0,STICK,0,STICK},{WOOD_HOE,1,0,0,0}},
    {2,3,{COBBLESTONE,COBBLESTONE,0,STICK,0,STICK},{STONE_HOE,1,0,0,0}},
    {2,3,{IRON_INGOT,IRON_INGOT,0,STICK,0,STICK},{IRON_HOE,1,0,0,0}},
    {2,3,{GOLD_INGOT,GOLD_INGOT,0,STICK,0,STICK},{GOLD_HOE,1,0,0,0}},
    {2,3,{DIAMOND,DIAMOND,0,STICK,0,STICK},{DIAMOND_HOE,1,0,0,0}},
    {1,1,{BONE},{BONE_MEAL,3,0,0,0}},
    {3,2,{PLANKS,AIR,PLANKS,AIR,PLANKS,AIR},{BOWL,4,0,0,0}},
    {3,3,{AIR,IRON_INGOT,AIR,IRON_INGOT,REDSTONE_DUST,IRON_INGOT,AIR,IRON_INGOT,AIR},{COMPASS_ITEM,1,0,0,0}},
    {3,3,{AIR,GOLD_INGOT,AIR,GOLD_INGOT,REDSTONE_DUST,GOLD_INGOT,AIR,GOLD_INGOT,AIR},{CLOCK_ITEM,1,0,0,0}},
    {1,1,{GOLD_INGOT},{GOLD_NUGGET,9,0,0,0}},
    {3,3,{GOLD_NUGGET,GOLD_NUGGET,GOLD_NUGGET,GOLD_NUGGET,GOLD_NUGGET,GOLD_NUGGET,
          GOLD_NUGGET,GOLD_NUGGET,GOLD_NUGGET},{GOLD_INGOT,1,0,0,0}},
    {3,3,{GOLD_NUGGET,GOLD_NUGGET,GOLD_NUGGET,GOLD_NUGGET,CARROT,GOLD_NUGGET,
          GOLD_NUGGET,GOLD_NUGGET,GOLD_NUGGET},{GOLDEN_CARROT,1,0,0,0}},
    {3,3,{PAPER,PAPER,PAPER,PAPER,COMPASS_ITEM,PAPER,PAPER,PAPER,PAPER},{EMPTY_MAP,1,0,0,0}},
    {1,1,{PUMPKIN_ITEM},{PUMPKIN_SEEDS,4,0,0,0}},
    {1,1,{POPPY_ITEM},{ROSE_RED,1,0,0,0}},
    {1,1,{DANDELION_ITEM},{DANDELION_YELLOW,1,0,0,0}},
    {1,1,{LAPIS_BLOCK_ITEM},{LAPIS_LAZULI,9,0,0,0}},
    {3,3,{LAPIS_LAZULI,LAPIS_LAZULI,LAPIS_LAZULI,LAPIS_LAZULI,LAPIS_LAZULI,LAPIS_LAZULI,LAPIS_LAZULI,LAPIS_LAZULI,LAPIS_LAZULI},
        {LAPIS_BLOCK_ITEM,1,0,0,0}},
    {3,1,{WHEAT,COCOA_BEANS,WHEAT},{COOKIE,8,0,0,0}},
    {2,2,{SNOWBALL,SNOWBALL,SNOWBALL,SNOWBALL},{SNOW,1,0,0,0}},
    {1,3,{PLANKS,STICK,STICK},{WOOD_SHOVEL,1,0,0,0}},
    {1,3,{COBBLESTONE,STICK,STICK},{STONE_SHOVEL,1,0,0,0}},
    {1,3,{IRON_INGOT,STICK,STICK},{IRON_SHOVEL,1,0,0,0}},
    {1,3,{GOLD_INGOT,STICK,STICK},{GOLD_SHOVEL,1,0,0,0}},
    {1,3,{DIAMOND,STICK,STICK},{DIAMOND_SHOVEL,1,0,0,0}},
    {1,2,{COAL,STICK},{TORCH,4,0,0,0}},
    {1,2,{CHARCOAL,STICK},{TORCH,4,0,0,0}},
    {3,2,{STICK,STICK,STICK,STICK,STICK,STICK},{WOOD_FENCE,2,0,0,0}},
    {3,2,{NETHER_BRICKS,NETHER_BRICKS,NETHER_BRICKS,NETHER_BRICKS,NETHER_BRICKS,NETHER_BRICKS},{NETHER_FENCE,6,0,0,0}},
    {3,2,{STICK,PLANKS,STICK,STICK,PLANKS,STICK},{FENCE_GATE,1,0,0,0}},
    {2,3,{PLANKS,PLANKS,PLANKS,PLANKS,PLANKS,PLANKS},{WOOD_DOOR_ITEM,1,0,0,0}},
    {3,2,{PLANKS,PLANKS,PLANKS,PLANKS,PLANKS,PLANKS},{WOOD_TRAPDOOR_ITEM,2,0,0,0}},
    {3,3,{STICK,0,STICK,STICK,STICK,STICK,STICK,0,STICK},{LADDER_ITEM,3,0,0,0}},
    {3,3,{STICK,STICK,STICK,STICK,LEATHER,STICK,STICK,STICK,STICK},{ITEM_FRAME_ITEM,1,0,0,0}},
    {2,3,{IRON_INGOT,IRON_INGOT,IRON_INGOT,IRON_INGOT,IRON_INGOT,IRON_INGOT},{IRON_DOOR_ITEM,1,0,0,0}},
    {2,2,{STRING,STRING,LEATHER,SILT_GEL},{LEAD,2,0,0,0}},
    {2,2,{STRIATED_GLASS,LIMESTONE_ITEM,IRON_INGOT,LAMINATE_ITEM},{BREWING_STAND,1,0,0,0}},
    {3,2,{GLASS,0,GLASS,0,GLASS,0},{GLASS_BOTTLE,3,0,0,0}},
    {3,1,{WHEAT,WHEAT,WHEAT},{BREAD,1,0,0,0}},
    {3, 3, {PLANKS, PLANKS, PLANKS, 0, STICK, 0, 0, STICK, 0}, {WOOD_PICKAXE, 1, 0,0,0}},
    {3, 3, {COBBLESTONE, COBBLESTONE, COBBLESTONE, 0, STICK, 0, 0, STICK, 0}, {STONE_PICKAXE, 1, 0,0,0}},
    {3, 3, {IRON_INGOT, IRON_INGOT, IRON_INGOT, 0, STICK, 0, 0, STICK, 0}, {IRON_PICKAXE, 1, 0,0,0}},
    {3, 3, {GOLD_INGOT, GOLD_INGOT, GOLD_INGOT, 0, STICK, 0, 0, STICK, 0}, {GOLD_PICKAXE, 1, 0,0,0}},
    {3, 3, {DIAMOND, DIAMOND, DIAMOND, 0, STICK, 0, 0, STICK, 0}, {DIAMOND_PICKAXE, 1, 0,0,0}},
    {2,3,{PLANKS,PLANKS,PLANKS,STICK,0,STICK},{WOOD_AXE,1,0,0,0}},
    {2,3,{COBBLESTONE,COBBLESTONE,COBBLESTONE,STICK,0,STICK},{STONE_AXE,1,0,0,0}},
    {2,3,{IRON_INGOT,IRON_INGOT,IRON_INGOT,STICK,0,STICK},{IRON_AXE,1,0,0,0}},
    {2,3,{GOLD_INGOT,GOLD_INGOT,GOLD_INGOT,STICK,0,STICK},{GOLD_AXE,1,0,0,0}},
    {2,3,{DIAMOND,DIAMOND,DIAMOND,STICK,0,STICK},{DIAMOND_AXE,1,0,0,0}},
    {1, 3, {PLANKS, PLANKS, STICK}, {WOOD_SWORD, 1, 0,0,0}},
    {1, 3, {COBBLESTONE, COBBLESTONE, STICK}, {STONE_SWORD, 1, 0,0,0}},
    {1, 3, {IRON_INGOT, IRON_INGOT, STICK}, {IRON_SWORD, 1, 0,0,0}},
    {1, 3, {GOLD_INGOT, GOLD_INGOT, STICK}, {GOLD_SWORD, 1, 0,0,0}},
    {1, 3, {DIAMOND, DIAMOND, STICK}, {DIAMOND_SWORD, 1, 0,0,0}},
    {3, 3, {0, STICK, STRING, STICK, 0, STRING, 0, STICK, STRING}, {BOW, 1, 0,0,0}},
    {3, 3, {STRING, STICK, 0, STRING, 0, STICK, STRING, STICK, 0}, {BOW, 1, 0,0,0}},
    {3, 3, {0,0,STICK,0,STICK,STRING,STICK,0,STRING}, {FISHING_ROD,1,0,0,0}},
    {1, 3, {FLINT, STICK, FEATHER}, {ARROW, 4, 0,0,0}},
    {3, 2, {PLANKS, 0, PLANKS, PLANKS, PLANKS, PLANKS}, {BOAT, 1, 0,0,0}},
    {3, 2, {IRON_INGOT, 0, IRON_INGOT, IRON_INGOT, IRON_INGOT, IRON_INGOT}, {MINECART, 1, 0,0,0}},
    {3, 3, {IRON_INGOT,0,IRON_INGOT,IRON_INGOT,STICK,IRON_INGOT,IRON_INGOT,0,IRON_INGOT}, {RAIL,16,0,0,0}},
    {3, 3, {GOLD_INGOT,0,GOLD_INGOT,GOLD_INGOT,STICK,GOLD_INGOT,GOLD_INGOT,REDSTONE_DUST,GOLD_INGOT},
        {POWERED_RAIL,6,0,0,0}},
    {3, 3, {IRON_INGOT,0,IRON_INGOT,IRON_INGOT,STONE_PLATE_ITEM,IRON_INGOT,IRON_INGOT,REDSTONE_DUST,IRON_INGOT},
        {DETECTOR_RAIL_ITEM,6,0,0,0}},
    {3, 3, {IRON_INGOT,STICK,IRON_INGOT,IRON_INGOT,REDSTONE_TORCH_ITEM,IRON_INGOT,IRON_INGOT,STICK,IRON_INGOT},
        {ACTIVATOR_RAIL_ITEM,6,0,0,0}},
    {1, 1, {REDSTONE_BLOCK}, {REDSTONE_DUST,9,0,0,0}},
    {1,2,{STICK,COBBLESTONE},{LEVER_ITEM,1,0,0,0}},
    {1,2,{REDSTONE_DUST,STICK},{REDSTONE_TORCH_ITEM,1,0,0,0}},
    {3,2,{REDSTONE_TORCH_ITEM,REDSTONE_DUST,REDSTONE_TORCH_ITEM,STONE,STONE,STONE},{REPEATER_ITEM,1,0,0,0}},
    {2,2,{GLOWSTONE_DUST,GLOWSTONE_DUST,GLOWSTONE_DUST,GLOWSTONE_DUST},{GLOWSTONE,1,0,0,0}},
    {3,3,{0,REDSTONE_DUST,0,REDSTONE_DUST,GLOWSTONE,REDSTONE_DUST,0,REDSTONE_DUST,0},{REDSTONE_LAMP_ITEM,1,0,0,0}},
    {2,1,{STONE,STONE},{STONE_PLATE_ITEM,1,0,0,0}},
    {2,1,{PLANKS,PLANKS},{WOOD_PLATE_ITEM,1,0,0,0}},
    {2,1,{GOLD_INGOT,GOLD_INGOT},{GOLD_PLATE_ITEM,1,0,0,0}},
    {2,1,{IRON_INGOT,IRON_INGOT},{IRON_PLATE_ITEM,1,0,0,0}},
    {1,1,{STONE},{STONE_BUTTON_ITEM,1,0,0,0}},
    {1,1,{PLANKS},{WOOD_BUTTON_ITEM,1,0,0,0}},
    {3,3,{IRON_INGOT,0,IRON_INGOT,IRON_INGOT,CHEST,IRON_INGOT,0,IRON_INGOT,0},{HOPPER_ITEM,1,0,0,0}},
    {1,3,{IRON_INGOT,STICK,PLANKS},{TRIPWIRE_HOOK_ITEM,2,0,0,0}},
    {3,3,{COBBLESTONE,COBBLESTONE,COBBLESTONE,COBBLESTONE,0,COBBLESTONE,COBBLESTONE,REDSTONE_DUST,COBBLESTONE},
        {DROPPER_ITEM,1,0,0,0}},
    {3,3,{COBBLESTONE,COBBLESTONE,COBBLESTONE,COBBLESTONE,BOW,COBBLESTONE,COBBLESTONE,REDSTONE_DUST,COBBLESTONE},
        {DISPENSER_ITEM,1,0,0,0}},
    {3,3,{GUNPOWDER,SAND,GUNPOWDER,SAND,GUNPOWDER,SAND,GUNPOWDER,SAND,GUNPOWDER},{TNT_ITEM,1,0,0,0}},
    {3,3,{PLANKS,PLANKS,PLANKS,COBBLESTONE,IRON_INGOT,COBBLESTONE,COBBLESTONE,REDSTONE_DUST,COBBLESTONE},
        {PISTON_ITEM,1,0,0,0}},
    {1,2,{SILT_GEL,PISTON_ITEM},{STICKY_PISTON_ITEM,1,0,0,0}},
    {2,2,{APPLE,WHEAT,SUGAR,0},{FRUIT_RATION,2,0,0,0}},
    {3, 3, {COBBLESTONE, COBBLESTONE, COBBLESTONE, COBBLESTONE, 0, COBBLESTONE,
             COBBLESTONE, COBBLESTONE, COBBLESTONE}, {FURNACE, 1, 0,0,0}},
    {3, 2, {IRON_INGOT, 0, IRON_INGOT, 0, IRON_INGOT, 0}, {BUCKET, 1, 0,0,0}},
    {2,2,{STONE,STONE,STONE,STONE},{STONE_BRICKS,4,0,0,0}},
    {2,2,{SAND,SAND,SAND,SAND},{SANDSTONE,1,0,0,0}},
    {2,2,{CLAY_BALL,CLAY_BALL,CLAY_BALL,CLAY_BALL},{CLAY,1,0,0,0}},
    {2,2,{BRICK_ITEM,BRICK_ITEM,BRICK_ITEM,BRICK_ITEM},{BRICK_BLOCK_ITEM,1,0,0,0}},
    {2,2,{NETHER_BRICK_ITEM,NETHER_BRICK_ITEM,NETHER_BRICK_ITEM,NETHER_BRICK_ITEM},{NETHER_BRICKS,1,0,0,0}},
    {2,2,{QUARTZ,QUARTZ,QUARTZ,QUARTZ},{QUARTZ_BLOCK_ITEM,1,0,0,0}}
};
#undef ARMOR_RECIPES

static int recipe_cell(int expected,int actual) {
    return expected==actual || (expected==PLANKS && plank_item(actual));
}
static ItemStack build_recipe(const uint16_t grid[9],int size,int row,int w,int h) {
    if (w!=3) return (ItemStack){0};
    static const int ingredients[BUILD_MATERIALS]={PLANKS,BIRCH_PLANK_ITEM,SPRUCE_PLANK_ITEM,
        JUNGLE_PLANK_ITEM,ACACIA_PLANK_ITEM,DARK_OAK_PLANK_ITEM,STONE,SANDSTONE,COBBLESTONE,
        BRICK_BLOCK_ITEM,STONE_BRICKS,NETHER_BRICKS,QUARTZ_BLOCK_ITEM};
    int mat=0,input=grid[row*size+ (h==3 ? 0 : 1)];
    if (h==3 && !input) input=grid[2]; /* Other hand of the staircase. */
    while (mat<BUILD_MATERIALS && ingredients[mat]!=input) ++mat;
    if (mat>=BUILD_MATERIALS) return (ItemStack){0};
    if (h==1 && grid[row*size]==input && grid[row*size+2]==input)
        return (ItemStack){(uint16_t)(BUILD_SLAB_ITEMS+mat),6,0,0,0};
    if (h!=3 || mat==BUILD_STONE) return (ItemStack){0};
    int left=1,right=1;
    for (int y=0;y<3;++y) for (int x=0;x<3;++x) {
        left&=grid[y*3+x]==(x<=y ? input : AIR);
        right&=grid[y*3+x]==(2-x<=y ? input : AIR);
    }
    return left || right ? (ItemStack){(uint16_t)building_stair_item(mat),4,0,0,0} : (ItemStack){0};
}

static ItemStack match(const int *sources,int size,unsigned char used[SLOT_COUNT],unsigned *zoom,FireworkSpec *pyro) {
    ItemStack empty = {0, 0, 0,0,0};
    if (!sources || (size != 2 && size != 3)) return empty;
    int lo_x = size, lo_y = size, hi_x = -1, hi_y = -1;
    uint16_t grid[9] = {0};
    ItemStack materials[9]={0};
    for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
        int at = y * size + x, src = sources[at];
        if (src == -1) continue;
        if (src < 0 || src >= SLOT_COUNT || ++used[src] > inventory[src].count ||
            !item_stack_valid(inventory[src])) return empty;
        grid[at] = inventory[src].type;
        materials[at]=inventory[src];
        if (x < lo_x) lo_x = x;
        if (y < lo_y) lo_y = y;
        if (x > hi_x) hi_x = x;
        if (y > hi_y) hi_y = y;
    }
    if (hi_x < 0) return empty;
    ItemStack fireworks=firework_recipe(materials,size*size,pyro);
    if (fireworks.count) return fireworks;
    int slate=0,lime=0,foreign=0,wood=0,stick=0,stone=0,plate=0,iron=0,brine=0,thermal=0,shell=0;
    int laminate_glass=0,optical=0,clay_ball=0,leather=0,gasket=0,abrasive=0;
    for (int i=0;i<size*size;++i) if (grid[i]) {
        if (grid[i]==SLATE_ITEM) ++slate;
        else if (grid[i]==LIMESTONE_ITEM) ++lime;
        else ++foreign;
        wood+=plank_item(grid[i]);
        stick+=grid[i]==STICK;
        stone+=grid[i]==COBBLESTONE;
        plate+=grid[i]==LAMINATE_ITEM;
        iron+=grid[i]==IRON_INGOT;
        brine+=grid[i]==BRINE_CORE;
        thermal+=grid[i]==THERMAL_CORE;
        shell+=grid[i]==SHELL_FLAKE;
        laminate_glass+=grid[i]==GLASS;
        abrasive+=grid[i]==LANTERN_SCALES;
        optical+=grid[i]==STRIATED_GLASS;
        clay_ball+=grid[i]==CLAY_BALL;
        leather+=grid[i]==LEATHER;
        gasket+=grid[i]==PUMP_GASKET;
    }
    if (slate==2 && lime==1 && !foreign) return (ItemStack){LAMINATE_ITEM,3,0,0,0};
    if (!slate && lime==1 && foreign==2 && wood==1 && clay_ball==1)
        return (ItemStack){SETTLING_MOLD_ITEM,1,0,0,0};
    if (slate==1 && lime==1 && foreign==1 && clay_ball==1) return (ItemStack){SEEP_BED_ITEM,1,0,0,0};
    if (slate==1 && !lime && foreign==1 && shell==1) return (ItemStack){RESONANCE_MARKER_ITEM,2,0,0,0};
    if (!slate && lime==1 && foreign==1 && laminate_glass==1) return (ItemStack){STRIATED_GLASS,2,0,0,0};
    if (!slate && !lime && foreign==2 && abrasive==1 && laminate_glass==1) return (ItemStack){STRIATED_GLASS,2,0,0,0};
    if (slate==1 && !lime && foreign==2 && iron==1 && optical==1) return (ItemStack){TIDE_GUIDE,1,0,0,0};
    if (!slate && !lime && foreign==2 && clay_ball==1 && leather==1)
        return (ItemStack){PUMP_GASKET,2,0,0,0};
    if (!slate && !lime && foreign==2 && gasket==1 && iron==1)
        return (ItemStack){AIR_OUTLET_ITEM,1,0,0,0};
    if (slate==1 && !lime && foreign==1 && iron==1)
        return (ItemStack){AIR_PIPE_ITEM,4,0,0,0};
    if (slate==1 && !lime && foreign==3 && gasket==1 && iron==1 && optical==1)
        return (ItemStack){AIR_STATION_ITEM,1,0,0,0};
    if (wood==1 && stick==1 && stone==1 && foreign==3)
        return (ItemStack){SURVEY_TAPPER,1,0,0,0};
    if (uses_aquifer() && foreign==4 && plate==2 && iron==1 && wood==1)
        return (ItemStack){WINCH_ITEM,1,0,0,0};
    if (uses_aquifer() && foreign==3 && plate==1 && brine==1 && thermal==1)
        return (ItemStack){AQUIFER_LIGHT_ITEM,6,0,0,0};
    if (hi_x==lo_x && hi_y==lo_y) {
        int species=tree_log_species(grid[lo_y*size+lo_x]);
        if (species>0) return (ItemStack){(uint16_t)(BIRCH_PLANK_ITEM+species-1),4,0,0,0};
    }
    ItemStack structure=build_recipe(grid,size,lo_y,hi_x-lo_x+1,hi_y-lo_y+1);
    if (structure.count) return structure;
    int map_slot=-1,blanks=0,other=0,paper=0;
    for (int i=0;i<size*size;++i) if (grid[i]) {
        if (grid[i]==FILLED_MAP && map_slot<0) map_slot=sources[i];
        else if (grid[i]==EMPTY_MAP) ++blanks;
        else if (grid[i]==PAPER) ++paper;
        else ++other;
    }
    if (map_slot>=0 && !other) {
        ItemStack original=inventory[map_slot];
        if (blanks && !paper) {
            original.count=(uint8_t)(blanks+1);
            return original;
        }
        if (!blanks && size==3 && grid[4]==FILLED_MAP && paper==8 && map_can_zoom(map_item_id(original))) {
            *zoom=map_item_id(original);
            original.count=1;
            return original;
        }
        return empty;
    }
    /* The two rows must be entirely one kind of glass (or iron), and
       may slide up or down on the workbench. A dye is never mixed in. */
    if (hi_x-lo_x==2 && hi_y-lo_y==1) {
        int input=grid[lo_y*size],result=AIR;
        if (input==GLASS) result=GLASS_PANE_ITEM;
        else if (input==IRON_INGOT) result=IRON_BARS_ITEM;
        else if (input>=BLACK_GLASS_ITEM && input<=ORANGE_GLASS_ITEM)
            result=BLACK_PANE_ITEM+input-BLACK_GLASS_ITEM;
        if (result!=AIR) {
            int equal=1;
            for (int i=0;i<6;++i) equal&=grid[lo_y*size+i]==input;
            if (equal) return (ItemStack){(uint16_t)result,16,0,0,0};
        }
    }
    for (unsigned r = 0; r < sizeof recipes / sizeof *recipes; r++) {
        const Recipe *recipe = &recipes[r];
        if (recipe->width != hi_x - lo_x + 1 || recipe->height != hi_y - lo_y + 1) continue;
        int matches=1,mirrors=1;
        for (int y=0;y<recipe->height;++y) for (int x=0;x<recipe->width;++x) {
            int type=grid[(y+lo_y)*size+x+lo_x];
            matches&=recipe_cell(recipe->cells[y*recipe->width+x],type);
            mirrors&=recipe_cell(recipe->cells[y*recipe->width+recipe->width-1-x],type);
        }
        if (matches || mirrors) return recipe->result;
    }
    int first = 0, second = 0, filled = 0, papers=0, hides=0,glass=0,sugar=0,eye=0,mushroom=0,red=0,bowls=0;
    for (int i = 0; i < size*size; ++i) if (grid[i]) {
        if (++filled == 1) first = grid[i];
        else second = grid[i];
        papers+=grid[i]==PAPER;
        hides+=grid[i]==LEATHER;
        glass+=grid[i]==GLASS;
        sugar+=grid[i]==SUGAR;
        eye+=grid[i]==SPIDER_EYE;
        mushroom+=grid[i]==BROWN_MUSHROOM_ITEM;
        red+=grid[i]==RED_MUSHROOM_ITEM;
        bowls+=grid[i]==BOWL;
    }
    if (size==3 && glass==8) {
        int result=glass_recipe_item(grid[4]);
        if (result!=AIR) return (ItemStack){(uint16_t)result,8,0,0,0};
    }
    if (size==3) {
        int ring=1;
        for (int i=0;i<9;++i) if (i!=4 && grid[i]!=TERRACOTTA) ring=0;
        int color=ring ? glass_recipe_item(grid[4]) : AIR;
        if (color!=AIR) return (ItemStack){(uint16_t)(BLACK_CLAY_ITEM+color-BLACK_GLASS_ITEM),8,0,0,0};
    }
    ItemStack blended=mix_dye(grid,size*size);
    if (blended.count) return blended;
    if (filled==4 && papers==3 && hides==1) return (ItemStack){BOOK,1,0,0,0};
    if (filled==1 && first==SUGAR_CANE) return (ItemStack){SUGAR,1,0,0,0};
    if (filled==3 && sugar==1 && eye==1 && mushroom==1) return (ItemStack){FERMENTED_SPIDER_EYE,1,0,0,0};
    if (filled==3 && mushroom==1 && red==1 && bowls==1) return (ItemStack){MUSHROOM_STEW,1,0,0,0};
    if (filled == 1 && first == REED_FIN)
        return (ItemStack){REED_GRIT,2,0,0,0};
    if (filled == 2) {
        if ((first==TNT_ITEM && second==MINECART) || (first==MINECART && second==TNT_ITEM))
            return (ItemStack){TNT_MINECART,1,0,0,0};
        if ((first==CHEST && second==MINECART) || (first==MINECART && second==CHEST))
            return (ItemStack){CHEST_MINECART,1,0,0,0};
        if ((first==HOPPER_ITEM && second==MINECART) || (first==MINECART && second==HOPPER_ITEM))
            return (ItemStack){HOPPER_MINECART,1,0,0,0};
        if ((first==FURNACE && second==MINECART) || (first==MINECART && second==FURNACE))
            return (ItemStack){FURNACE_MINECART,1,0,0,0};
        if ((first==CHEST && second==TRIPWIRE_HOOK_ITEM) || (first==TRIPWIRE_HOOK_ITEM && second==CHEST))
            return (ItemStack){TRAPPED_CHEST_ITEM,1,0,0,0};
        if (first==second && armor_slot(first)>=0) {
            const ItemDef *def=item_def(first);
            int wear=-def->durability/20;
            for (int i=0;i<size*size;++i) if (grid[i]) wear+=inventory[sources[i]].damage;
            wear-=def->durability;
            return (ItemStack){(uint16_t)first,1,(uint16_t)(wear>0 ? wear : 0),0,0};
        }
        if ((first == FLINT && second == IRON_INGOT) || (first == IRON_INGOT && second == FLINT))
            return (ItemStack){FLINT_AND_STEEL,1,0,0,0};
    }
    return empty;
}

ItemStack craft_preview(const int *sources, int size) {
    return craft_preview_details(sources,size,NULL,0);
}
ItemStack craft_preview_details(const int *sources,int size,char *description,size_t capacity) {
    unsigned char used[SLOT_COUNT] = {0};
    unsigned zoom=0;
    FireworkSpec spec={0};
    ItemStack out=match(sources,size,used,&zoom,&spec);
    firework_describe(firework_item(out.type) ? &spec : NULL,description,capacity);
    if (out.type==SURVEY_TAPPER && description && capacity)
        snprintf(description,capacity,"手持探锤轻点岩面，找浅层石灰岩与页岩");
    if (out.type==TIDE_GUIDE && description && capacity)
        snprintf(description,capacity,"潜水时轻点，找连通的换气水面");
    if (out.type==SEEP_BED_ITEM && description && capacity)
        snprintf(description,capacity,"接水后拿黏土轻点制砖");
    if (out.type==SETTLING_MOLD_ITEM && description && capacity)
        snprintf(description,capacity,"水岸投料，排水收板 · 每批少用一份页岩");
    if (out.type==CISTERN_ITEM && description && capacity)
        snprintf(description,capacity,"露天蓄雨 · 最多 3 口 · 手持水囊轻点装入");
    if (out.type==CISTERN_PROBE_ITEM && description && capacity)
        snprintf(description,capacity,"装在集雨罐侧面 · 轻点切换备水信号与空罐告警");
    if (out.type==ASH_SAND && description && capacity)
        snprintf(description,capacity,"顶面铺滤苔 · 床基进水对侧留空");
    if (out.type==FILTER_MOSS && description && capacity)
        snprintf(description,capacity,"铺在灰砂床基 · 滤水后用水囊轻点取水");
    if (out.type==HYDRAULIC_SORTER_ITEM && description && capacity)
        snprintf(description,capacity,"水驱物流 · 顶部进料 · 对岸出轻料，下方落重料");
    if (out.type==HEAT_BENCH_ITEM && description && capacity)
        snprintf(description,capacity,"接旧炉余火 · 修补铁工具或熔回玻璃板");
    if (out.type==AIR_RELAY_ITEM && description && capacity)
        snprintf(description,capacity,"贴供气站侧面安装 · 空手轻点发出定向脉冲");
    if (out.type==STRATA_GAUGE && description && capacity)
        snprintf(description,capacity,"对墙轻点，逐条读取左、中、右三条测线");
    if (out.type==FALL_SIEVE_ITEM && description && capacity)
        snprintf(description,capacity,"装在落水侧面，三份砂砾洗成两份潮磨砂");
    if (out.type==BASALT_PAVING_ITEM && description && capacity)
        snprintf(description,capacity,"铺成实心路面 · 贴地步行快 18%%");
    return out;
}
int craft_take(const int *sources, int size) {
    unsigned char used[SLOT_COUNT] = {0};
    unsigned zoom=0,new_id=0;
    FireworkSpec spec={0};
    int created=0;
    ItemStack out=match(sources,size,used,&zoom,&spec);
    if (!out.count) return 0;
    ItemStack next[SLOT_COUNT];
    memcpy(next, inventory, sizeof next);
    for (int i = 0; i < SLOT_COUNT; i++)
        if (used[i] && !(next[i].count -= used[i])) next[i] = (ItemStack){0, 0, 0,0,0};
    if (zoom) {
        new_id=map_zoom(zoom);
        if (!new_id) return 0;
        out.custom=(out.custom&~255u)|new_id;
    }
    if (firework_item(out.type) && !firework_intern(&out,spec,&created)) return 0;
    if (!add_stack(next, out)) { map_rollback(new_id); firework_rollback(created); return 0; }
    memcpy(inventory, next, sizeof next);
    return 1;
}
