#ifndef VOXEL_GRAVITY_H
#define VOXEL_GRAVITY_H
#include "game.h"
#include <stddef.h>

enum { GRAVITY_FILE_LIMIT=65536, GRAVITY_CHUNK_RADIUS=4 };
typedef struct { int x,y,z; } FallCheck;
typedef struct {
    int x,z,origin;
    float bottom,speed;
    uint16_t cell;
} FallingBlock;

/* Register the potential support change before committing a world edit.
   Checks left by a failed edit are harmless: they re-read the live world. */
int gravity_prepare_edit(int x,int y,int z,int before,int after);
int gravity_seed_legacy_edit(void *context,int x,int y,int z,int cell);
void gravity_reset(void);
void gravity_step(unsigned ticks);
int gravity_block(int raw);
unsigned gravity_revision(void);
const FallingBlock *gravity_falling(Dimension d,size_t *count);
size_t gravity_pending_count(Dimension d);
typedef int (*GravityVisitor)(FallCheck p,void *context);
int gravity_visit_pending(Dimension d,GravityVisitor visit,void *context);
int gravity_restore_pending(Dimension d,FallCheck p);
int gravity_restore_falling(Dimension d,FallingBlock fall);
#endif
