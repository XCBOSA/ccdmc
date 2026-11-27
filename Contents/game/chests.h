#ifndef VOXEL_CHESTS_H
#define VOXEL_CHESTS_H

#include "game.h"
#include <stddef.h>

enum { CHEST_SLOTS=27, DOUBLE_CHEST_SLOTS=54 };
typedef struct { int x,y,z; } ChestPos;
typedef struct { ChestPos at; ItemStack items[CHEST_SLOTS]; } ChestRecord;
typedef struct {
    Dimension dimension;
    ChestPos halves[2];
    int count;
    int kind;
} ChestView;
typedef enum {
    CHEST_OK, CHEST_MISSING, CHEST_BLOCKED, CHEST_NO_MEMORY, CHEST_NO_MOVE, CHEST_CAT
} ChestResult;
typedef enum { CHEST_ALONE=-1, CHEST_WEST, CHEST_EAST, CHEST_NORTH, CHEST_SOUTH } ChestJoint;

void chests_reset(void);
int chest_can_place(ChestPos p);
int chest_can_place_type(ChestPos p,int type);
int chest_signal(ChestPos p);
ChestJoint chest_joint(ChestPos p);
ChestResult chest_open(ChestPos p,ChestView *view);
/* Only a player entering the inventory panel starts a viewing session. */
int chest_view_valid(const ChestView *view);
void chest_access_begin(const ChestView *view);
void chest_access_end(void);
void chest_trap_removed(ChestPos p);
int chest_trap_power(ChestPos p);
int chest_automation_open(ChestPos p,ChestView *view);
/* Machines on a streaming budget may access only resident ordinary chests. */
int chest_automation_open_resident(ChestPos p,ChestView *view);
int chest_automation_insert(const ChestView *view,ItemStack item);
void chest_automation_remove(const ChestView *view,int index);
/* Container indices: 0 through 53; backpack indices start at 54. */
ItemStack chest_slot(const ChestView *view,int index);
ChestResult chest_move(const ChestView *view,int source,int destination,int single);
const char *chest_break(ChestPos p);
size_t chests_count_in(Dimension dimension);
const ChestRecord *chest_record_in(Dimension dimension,size_t ordinal);
int chest_restore(const ChestRecord *record);
/* New Overworld only: place one normal chest with supplies near initial spawn. */
int chest_create_bonus(void);
void mineshaft_loot(ChestPos p,ItemStack items[CHEST_SLOTS]);
/* Read the contents of this half without materializing generated loot. */
int chest_blast_contents(ChestPos p,ItemStack out[CHEST_SLOTS+2]);
void chest_blast_forget(ChestPos p);

#endif
