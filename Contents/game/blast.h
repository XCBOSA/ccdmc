#ifndef VOXEL_BLAST_H
#define VOXEL_BLAST_H
#include "game.h"
#include <stddef.h>

enum { BLAST_CAP=128, TNT_FUSE=80, CHAIN_FUSE_MIN=10, CHAIN_FUSE_MAX=29,
       BLAST_FLASH_TICKS=9, BLAST_TOP=WY+40, BLAST_CART_SPEED_LIMIT=100 };
enum { BLAST_TNT=1, BLAST_ARCHIVE_SMALL=2, BLAST_ARCHIVE_LARGE=3, BLAST_CART_TRACK=4, BLAST_CART_IMPACT=5,
       BLAST_ARCHIVE_BOSS_FIRST=6, BLAST_ARCHIVE_BOSS_LAST=9, BLAST_ARCHIVE_VENT=10 };
typedef struct { Vec3 point,speed; uint16_t ticks; uint8_t kind; } PrimedBlast;
typedef struct { Vec3 point; uint8_t ticks,underwater; } BlastFlash;

void blasts_reset(void);
void blasts_apply_difficulty(void);
unsigned blasts_revision(void);
const PrimedBlast *blasts_in(Dimension d,size_t *count);
const BlastFlash *blast_flashes(Dimension d,size_t *count);
int blast_restore(Dimension d,PrimedBlast charge);
int blast_prime(int x,int y,int z);
int blast_dispense(int x,int y,int z);
int blast_queue_cart(Vec3 point,float speed,int protect_track);
void blasts_step(unsigned ticks);
#endif
