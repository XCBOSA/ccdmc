#ifndef VOXEL_FISHING_H
#define VOXEL_FISHING_H

#include "game.h"

typedef enum { FISH_IDLE, FISH_CAST, FISH_GROUNDED, FISH_WAIT, FISH_APPROACH, FISH_BITE, FISH_LANDED } FishPhase;
typedef struct {
    FishPhase phase;
    Vec3 point;                 /* Bobber, including the dip during a bite. */
    Vec3 wake;
    float ripple;               /* Water ripple radius; zero out of water. */
} FishingView;

void fishing_reset(void);
void fishing_cancel(void);
int fishing_active(void);
void fishing_step(float dt);
const char *fishing_use(void);
const char *fishing_take_message(void);
FishingView fishing_view(void);
/* Uses only the cast's RNG and enchantments, never the world RNG. */
ItemStack fishing_roll_catch(uint32_t *random,uint64_t rod_enchants);

#endif
