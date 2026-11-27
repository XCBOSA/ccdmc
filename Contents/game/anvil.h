#ifndef VOXEL_ANVIL_H
#define VOXEL_ANVIL_H
#include "item_custom.h"

typedef enum { ANVIL_READY, ANVIL_MISSING, ANVIL_NOTHING, ANVIL_INCOMPATIBLE,
    ANVIL_BAD_NAME, ANVIL_TOO_EXPENSIVE, ANVIL_NEED_LEVELS, ANVIL_NO_MEMORY } AnvilResult;
typedef struct {
    ItemStack item;
    unsigned cost, materials, work;
    int rename;
    char name[ITEM_NAME_BYTES+1];
} AnvilPlan;
int anvil_place(int x,int y,int z,unsigned wear,int axis);
int anvil_stage(int raw);
AnvilResult anvil_preview(int left,int right,const char *rename,AnvilPlan *out);
/* Recompute from the live bag; commit only once the output and block edit are ready. */
AnvilResult anvil_apply(int x,int y,int z,int left,int right,const char *rename);
#endif
