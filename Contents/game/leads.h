#ifndef VOXEL_LEADS_H
#define VOXEL_LEADS_H
#include "game.h"

enum { LEAD_FREE, LEAD_PLAYER, LEAD_FENCE, LEAD_PENDING };
/* Stored alongside the animal, so slot reuse and load order cannot retarget it. */
typedef struct { int kind,x,y,z; } LeadLink;

unsigned lead_revision(void);
int lead_valid(LeadLink link);
void lead_set(LeadLink *where,LeadLink value);
int lead_release(Dimension dim,LeadLink *link,Vec3 feet);
int lead_anchor(LeadLink link,Vec3 *out);
int lead_tick(Dimension dim,LeadLink *link,Vec3 feet,Vec3 *anchor);
int lead_allows(Vec3 from,Vec3 next,Vec3 anchor);
const char *lead_use_animal(LeadLink *link,Vec3 feet);
const char *lead_use_fence(Hit h);
void lead_fence_removed(int x,int y,int z);
void lead_player_left(Dimension dim);

#endif
