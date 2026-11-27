#include "furnace.h"
#include "heat_bench.h"
#include "stack_transfer.h"
#include "experience.h"
#include "../world/redstone.h"
#include "../world/build_blocks.h"
#include <stdlib.h>
#include <string.h>

typedef struct { Furnace *items; size_t length, reserved; float fraction; } FurnaceBank;
static FurnaceBank banks[DIMENSION_COUNT];
#define states (banks[world_dimension()].items)
#define state_count (banks[world_dimension()].length)
#define capacity (banks[world_dimension()].reserved)
#define ticks_fraction (banks[world_dimension()].fraction)
static const ItemStack empty = {0, 0, 0,0,0};

/* Fractional experience per output, in hundredths. All inputs sharing one
   output in this catalog use the same rate, including old saved outputs. */
static const struct { int input, output; unsigned xp; } smelting[] = {
    {WOOD,CHARCOAL,15}, {IRON_ORE,IRON_INGOT,70}, {GOLD_ORE,GOLD_INGOT,100}, {COBBLESTONE,STONE,10},
    {RAW_PORK,COOKED_PORK,35}, {RAW_FISH,COOKED_FISH,35}, {RAW_SALMON,COOKED_SALMON,35}, {POTATO,BAKED_POTATO,35},
    {RAW_BEEF,COOKED_BEEF,35}, {RAW_CHICKEN,COOKED_CHICKEN,35}, {SAND,GLASS,10}, {RED_SAND,GLASS,10},
    {CACTUS_ITEM,CACTUS_GREEN,20},{CLAY,TERRACOTTA,35},{CLAY_BALL,BRICK_ITEM,30},{NETHERRACK,NETHER_BRICK_ITEM,10}
};
static int smelt_result(int id) {
    if (is_log(id)) id=WOOD;
    for (size_t i=0;i<sizeof smelting/sizeof smelting[0];++i)
        if (smelting[i].input==id) return smelting[i].output;
    return AIR;
}
static int valid_output(int id) {
    if (id==AIR) return 1;
    for (size_t i=0;i<sizeof smelting/sizeof smelting[0];++i)
        if (smelting[i].output==id) return 1;
    return 0;
}
static unsigned smelt_experience(ItemStack item) {
    for (size_t i=0;i<sizeof smelting/sizeof smelting[0];++i) if (smelting[i].output==item.type) {
        unsigned amount=smelting[i].xp*item.count,remainder=amount%100u;
        return amount/100u+(remainder && experience_roll(0,99)<remainder);
    }
    return 0;
}

static int fuel_ticks(int id) {
    if (is_log(id)) return 300;
    if (plank_item(id)) return 300;
    const ItemDef *def=item_def(id);
    if (def && build_wooden(def->placed_block)) return slab_item(id) ? 150 : 300;
    if (sapling_item(id)) return 100;
    switch (id) {
        case COAL: case CHARCOAL: return FURNACE_MAX_FUEL_TICKS;
        case CRAFTING_TABLE: case WOOD_FENCE: case FENCE_GATE: case WOOD_TRAPDOOR_ITEM: return 300;
        case WOOD_PICKAXE: case WOOD_SWORD: case WOOD_HOE: case WOOD_SHOVEL: case WOOD_AXE: return 200;
        case STICK: return 100;
        default: return 0;
    }
}

static Furnace *find(int x, int y, int z) {
    for (size_t i = 0; i < state_count; i++)
        if (states[i].x == x && states[i].y == y && states[i].z == z) return &states[i];
    return NULL;
}
const Furnace *furnace_at(int x, int y, int z) { return find(x, y, z); }
size_t furnaces_count(void) { return state_count; }
const Furnace *furnace_at_index(size_t index) { return index < state_count ? &states[index] : NULL; }
size_t furnaces_count_in(Dimension dimension) {
    return (unsigned)dimension < DIMENSION_COUNT ? banks[dimension].length : 0;
}
const Furnace *furnace_at_index_in(Dimension dimension, size_t index) {
    return index < furnaces_count_in(dimension) ? &banks[dimension].items[index] : NULL;
}

static Furnace *append(int x, int y, int z) {
    if (state_count == capacity) {
        size_t next = capacity ? capacity * 2 : 8;
        if (next < capacity || next > SIZE_MAX / sizeof *states) return NULL;
        Furnace *grown = (Furnace *)realloc(states, next * sizeof *grown);
        if (!grown) return NULL;
        states = grown;
        capacity = next;
    }
    Furnace *f = &states[state_count++];
    memset(f, 0, sizeof *f);
    f->x = x; f->y = y; f->z = z;
    return f;
}

void furnaces_reset(void) {
    for (int d = 0; d < DIMENSION_COUNT; ++d) free(banks[d].items);
    memset(banks, 0, sizeof banks);
}

static void subtract_one(ItemStack *stack) {
    if (!--stack->count) *stack = empty;
}

int furnace_deposit(int x, int y, int z, FurnaceSlot slot, int source) {
    if ((slot != FURNACE_INPUT && slot != FURNACE_FUEL) || source < 0 || source >= SLOT_COUNT ||
        !inworld(x, y, z) || block(x, y, z) != FURNACE) return 0;
    ItemStack *item = &inventory[source];
    if (!item->count || !item_stack_valid(*item) ||
        !(slot == FURNACE_INPUT ? smelt_result(item->type) : fuel_ticks(item->type))) return 0;
    Furnace *f = find(x, y, z);
    if (!f) f = append(x, y, z);
    if (!f) return 0;
    ItemStack *dest = &f->slots[slot];
    if (dest->count && !item_same_kind(*dest,*item)) return 0;
    int room = item_def(item->type)->max_stack - dest->count;
    if (room <= 0) return 0;
    int n = item->count < room ? item->count : room;
    dest->type=item->type; dest->damage=item->damage; dest->enchants=item->enchants; dest->custom=item->custom; dest->count+=n;
    if (!(item->count -= n)) *item = empty;
    redstone_container_changed(x,y,z);
    return 1;
}

static int remove_if_empty(Furnace *f) {
    if (f->burn || f->slots[0].count || f->slots[1].count || f->slots[2].count) return 0;
    *f = states[--state_count];
    return 1;
}
int furnace_automation_insert(int x,int y,int z,FurnaceSlot slot,ItemStack item) {
    if (block(x,y,z)!=FURNACE || !item.count ||
        (slot==FURNACE_INPUT ? !smelt_result(item.type) : slot!=FURNACE_FUEL || !fuel_ticks(item.type)))
        return 0;
    Furnace *f=find(x,y,z);
    if (!f && !(f=append(x,y,z))) return 0;
    if (!stack_receive(&f->slots[slot],item,1)) return 0;
    redstone_container_changed(x,y,z);
    return 1;
}
void furnace_automation_take_output(int x,int y,int z) {
    Furnace *f=find(x,y,z);
    if (!f || !f->slots[FURNACE_OUTPUT].count) return;
    stack_deduct(&f->slots[FURNACE_OUTPUT],1);
    remove_if_empty(f);
    redstone_container_changed(x,y,z);
}

int furnace_withdraw(int x, int y, int z, FurnaceSlot slot) {
    if (slot < 0 || slot >= FURNACE_SLOTS) return 0;
    Furnace *f = find(x, y, z);
    if (!f || !inventory_add_stack(f->slots[slot])) return 0;
    if (slot==FURNACE_OUTPUT && player_rules.mode==SURVIVAL)
        experience_reward((Vec3){px,py+.30f,pz},smelt_experience(f->slots[slot]));
    f->slots[slot] = empty;
    if (slot == FURNACE_INPUT) f->cook = 0;
    remove_if_empty(f);
    redstone_container_changed(x,y,z);
    return 1;
}

void furnace_forget(int x, int y, int z, ItemStack out[FURNACE_SLOTS]) {
    Furnace *f = find(x, y, z);
    if (out) {
        if (f) memcpy(out, f->slots, sizeof f->slots);
        else memset(out, 0, FURNACE_SLOTS * sizeof *out);
    }
    if (f) *f = states[--state_count];
}

static int ready_product(const Furnace *f) {
    const int type=smelt_result(f->slots[FURNACE_INPUT].type);
    const ItemStack output=f->slots[FURNACE_OUTPUT];
    return type && (!output.count ||
        (output.type==type && output.count<item_def(type)->max_stack)) ? type : AIR;
}

int furnace_store_idle_heat(int x,int y,int z,int bench_x,int bench_z) {
    const int64_t dx=(int64_t)x-bench_x,dz=(int64_t)z-bench_z;
    int raw,source;
    if (!(((dx==1 || dx==-1) && !dz) || ((dz==1 || dz==-1) && !dx)) || !inworld(x,y,z) ||
        !world_resident_cell(x,y,z,&source) || source!=FURNACE ||
        !world_resident_cell(bench_x,y,bench_z,&raw) || !heat_bench_cell(raw)) return 0;
    Furnace *f=find(x,y,z);
    if (!f || ready_product(f)) return 0;
    const int room=HEAT_BENCH_FULL-raw,available=f->burn/HEAT_BENCH_TICKS_PER_UNIT;
    const int units=room<available ? room : available;
    if (!units) return 0;
    if (!world_set_heat_bench_level(bench_x,y,bench_z,raw,raw+units)) return -1;
    f->burn-=(uint16_t)(units*HEAT_BENCH_TICKS_PER_UNIT);
    return units;
}

static int advance_one(Furnace *f) {
    ItemStack *input = &f->slots[FURNACE_INPUT], *fuel = &f->slots[FURNACE_FUEL],
        *output = &f->slots[FURNACE_OUTPUT];
    const int result=ready_product(f);
    int changed=0;
    if (!f->burn && result) {
        int ticks = fuel_ticks(fuel->type);
        if (ticks) {
            f->burn = f->fuel_duration = (uint16_t)ticks;
            subtract_one(fuel);
            changed=1;
        }
    }
    if (f->burn) {
        --f->burn;
        if (result) {
            if (++f->cook == SMELT_TICKS) {
                f->cook = 0;
                output->type = (uint16_t)result;
                ++output->count;
                subtract_one(input);
                changed=1;
            }
            return changed;
        }
    }
    if (f->cook) --f->cook;
    return changed;
}

void furnaces_advance(float dt) {
    if (!(dt > 0 && dt <= 1)) return;
    ticks_fraction += dt * 20.f;
    unsigned ticks = (unsigned)ticks_fraction;
    ticks_fraction -= ticks;
    for (size_t i = 0; ticks && i < state_count;) {
        int changed=0;
        for (unsigned t = 0; t < ticks; t++) changed|=advance_one(&states[i]);
        if (changed) redstone_container_changed(states[i].x,states[i].y,states[i].z);
        if (!remove_if_empty(&states[i])) ++i;
    }
}

int furnace_restore(const Furnace *record) {
    if (!record || !inworld(record->x, record->y, record->z) ||
        block(record->x, record->y, record->z) != FURNACE ||
        find(record->x, record->y, record->z) || record->cook >= SMELT_TICKS ||
        record->burn > record->fuel_duration || record->fuel_duration > FURNACE_MAX_FUEL_TICKS)
        return 0;
    for (int i = 0; i < FURNACE_SLOTS; i++)
        if (!item_stack_valid(record->slots[i])) return 0;
    int input = record->slots[FURNACE_INPUT].type, fuel = record->slots[FURNACE_FUEL].type;
    int output = record->slots[FURNACE_OUTPUT].type;
    if ((input && !smelt_result(input)) || (!input && record->cook) || (fuel && !fuel_ticks(fuel)) ||
        !valid_output(output))
        return 0;
    Furnace *f = append(record->x, record->y, record->z);
    if (!f) return 0;
    *f = *record;
    return 1;
}
