#ifndef VOXEL_FURNACE_H
#define VOXEL_FURNACE_H

#include "game.h"
#include <stddef.h>

enum { SMELT_TICKS = 200, FURNACE_MAX_FUEL_TICKS = 1600 };
typedef enum { FURNACE_INPUT, FURNACE_FUEL, FURNACE_OUTPUT, FURNACE_SLOTS } FurnaceSlot;
typedef struct {
    int x, y, z;
    ItemStack slots[FURNACE_SLOTS];
    uint16_t cook, burn, fuel_duration;
} Furnace;

/* An empty station needs no dynamic state until something is put inside. */
const Furnace *furnace_at(int x, int y, int z);
int furnace_deposit(int x, int y, int z, FurnaceSlot slot, int source);
int furnace_withdraw(int x, int y, int z, FurnaceSlot slot);
int furnace_automation_insert(int x,int y,int z,FurnaceSlot slot,ItemStack item);
void furnace_automation_take_output(int x,int y,int z);
/* Only an adjacent, loaded bench can draw already-lit fuel which cannot
   currently advance a recipe. Positive = units stored, -1 = write failed. */
int furnace_store_idle_heat(int x,int y,int z,int bench_x,int bench_z);
void furnaces_advance(float dt);
void furnaces_reset(void);
/* Remove the contents after breaking the block. Returns empty slots as zero. */
void furnace_forget(int x, int y, int z, ItemStack out[FURNACE_SLOTS]);

/* Read-only enumeration is valid until the next mutation. */
size_t furnaces_count(void);
const Furnace *furnace_at_index(size_t index);
size_t furnaces_count_in(Dimension dimension);
const Furnace *furnace_at_index_in(Dimension dimension, size_t index);
int furnace_restore(const Furnace *record);

#endif
