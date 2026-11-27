#ifndef CCDMC_ITEM_FRAMES_H
#define CCDMC_ITEM_FRAMES_H
#include "game.h"

enum { ITEM_FRAME_LIMIT=512 };
/* Coordinates refer to the supporting block, not its adjacent air cell. */
typedef struct {
    int x,y,z;
    ItemStack item;
    uint8_t face, rotation, present, detached;
} ItemFrame;

void item_frames_reset(void);
const ItemFrame *item_frames_in(Dimension d);
unsigned item_frames_revision(void);
int item_frame_restore(Dimension d,ItemFrame value);
/* Detect support destruction and obstructions; keep failed drops for retry. */
void item_frames_step(unsigned ticks);
const char *item_frame_use(void);
const char *item_frame_attack(void);
int item_frame_aimed(ItemFrame *out);
const char *item_frame_aimed_name(Vec3 *anchor);
void item_frame_basis(const ItemFrame *frame,Vec3 *center,Vec3 *right,Vec3 *normal);

#endif
