#ifndef VOXEL_SPACE_H
#define VOXEL_SPACE_H
#include "barriers.h"
#include "rails.h"

enum { SPACE_WATER=1, SPACE_LAVA=2, SPACE_IGNORE_LILY_PAD=4 };
enum { ANVIL_PARTS=3 };
int anvil_boxes(int x,int y,int z,int raw,BlockBox boxes[ANVIL_PARTS]);
static inline BlockBox ender_chest_box(int x,int y,int z) {
    return (BlockBox){x+.0625f,(float)y,z+.0625f,x+.9375f,y+.875f,z+.9375f};
}
static inline BlockBox air_outlet_box(int x,int y,int z,int raw) {
    float cx=x+.5f, base=(float)y, cz=z+.5f;
    if (raw>=AIR_OUTLET_WALL) {
        int dir=raw-AIR_OUTLET_WALL;
        cx-=rail_dx(dir)*.25f;
        cz-=rail_dz(dir)*.25f;
        base+=.25f;
    }
    return (BlockBox){cx-.25f,base,cz-.25f,cx+.25f,base+.5f,cz+.25f};
}
BlockBox body_box(float x,float y,float z,float radius,float height);
int world_box_blocked(BlockBox b,unsigned hazards);
int world_move_blocked(BlockBox before,BlockBox next,unsigned hazards);
/* Visible geometry for line of sight/projectiles, physical geometry for
   pair formation (prevents animals from breeding through a closed pen). */
int world_point_blocked(float x,float y,float z,int physical);
int dye_plant_box(int raw,int x,int y,int z,BlockBox *out);
int world_cactus_contact(BlockBox body);
int world_cobweb_contact(BlockBox body);

#endif
