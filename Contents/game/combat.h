#ifndef VOXEL_COMBAT_H
#define VOXEL_COMBAT_H

#include "game.h"

enum { ARROW_CAPACITY = 24 };
typedef struct { Vec3 position, velocity; float life; int damage; uint8_t recoverable, machine; } FlyingArrow;

void combat_reset(void);
void combat_cancel_draw(void);
void combat_step(float dt);
unsigned combat_revision(void);
const char *combat_take_message(void);
const char *bow_use(void);
int combat_dispenser_arrow(Vec3 position,Vec3 direction);
const FlyingArrow *combat_arrows(void);
#endif
