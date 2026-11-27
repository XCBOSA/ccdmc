#ifndef VOXEL_BUTTON_ARROWS_H
#define VOXEL_BUTTON_ARROWS_H
#include "game.h"
#include <stddef.h>

enum { BUTTON_ARROW_LIMIT=48, BUTTON_ARROW_LIFE=1200 };
typedef struct {
    Vec3 point, direction;
    int x,y,z;
    uint16_t raw,ticks;
    uint8_t recoverable;
} ButtonArrow;
int button_arrow_impact(Vec3 from,Vec3 to,int recoverable);
int button_arrow_present(int x,int y,int z);
void button_arrows_tick(void);
void button_arrows_reset(void);
unsigned button_arrows_revision(void);
const ButtonArrow *button_arrows_in(Dimension d,size_t *count);
int button_arrow_restore(Dimension d,ButtonArrow arrow);
#endif
