#include "build_blocks.h"
#include <stddef.h>

int build_material_block(int mat) {
    static const int block[BUILD_MATERIALS]={PLANKS,BIRCH_PLANK_CELL,SPRUCE_PLANK_CELL,JUNGLE_PLANK_CELL,
        ACACIA_PLANK_CELL,DARK_OAK_PLANK_CELL,STONE,SANDSTONE,COBBLESTONE,BRICK_CELL,
        STONE_BRICKS,NETHER_BRICKS,QUARTZ_BLOCK_CELL};
    return (unsigned)mat<BUILD_MATERIALS ? block[mat] : AIR;
}
int build_slab_raw(int mat,int height) {
    return (unsigned)mat<BUILD_MATERIALS && (unsigned)height<=BUILD_SLAB_DOUBLE ?
        BUILD_SLAB_BASE+mat*3+height : AIR;
}
int build_stair_raw(int mat,int dir,int inverted) {
    return (unsigned)mat<BUILD_MATERIALS && mat!=BUILD_STONE && (unsigned)dir<4 ?
        BUILD_STAIRS_BASE+8*(mat-(mat>BUILD_STONE))+dir+(inverted ? BUILD_STAIRS_TOP : 0) : AIR;
}
int build_wooden(int raw) {
    return (raw>=BIRCH_PLANK_CELL && raw<=DARK_OAK_PLANK_CELL) ||
        (unsigned)build_material(raw)<BUILD_STONE;
}
static int stair_dir(int raw) { return (raw-BUILD_STAIRS_BASE)&3; }
static int upside_down(int raw) { return !!((raw-BUILD_STAIRS_BASE)&BUILD_STAIRS_TOP); }
static int joins(int a,int b) { return build_stair(b) && upside_down(a)==upside_down(b); }
static int same_run(int a,int b) { return joins(a,b) && stair_dir(a)==stair_dir(b); }
unsigned build_mask(int raw,const int neighbors[4]) {
    if (build_slab(raw)) {
        int half=(raw-BUILD_SLAB_BASE)%3;
        return half==BUILD_SLAB_DOUBLE ? 255u : half==BUILD_SLAB_TOP ? 240u : 15u;
    }
    if (!build_stair(raw)) return 0;
    static const unsigned tall[]={3u,10u,12u,5u};
    unsigned d=(unsigned)stair_dir(raw),shape=tall[d];
    if (neighbors) {
        const int front=neighbors[d],back=neighbors[(d+2u)&3u];
        /* Outer corner: trim the upper tread where it meets the turn in
           front. A continuing row on the open side suppresses the join. */
        if (joins(raw,front) && ((d^stair_dir(front))&1u) && !same_run(raw,neighbors[(stair_dir(front)+2)&3]))
            shape&=tall[stair_dir(front)];
        else if (joins(raw,back) && ((d^stair_dir(back))&1u) && !same_run(raw,neighbors[stair_dir(back)]))
            shape|=tall[stair_dir(back)];
    }
    return upside_down(raw) ? 240u|shape : 15u|(shape<<4);
}
void build_neighbors(int x,int y,int z,int out[4]) {
    for (int d=0;d<4;++d) {
        out[d]=AIR;
        world_resident_cell(x+rail_dx(d),y,z+rail_dz(d),&out[d]);
    }
}
unsigned build_world_mask(int x,int y,int z,int raw) {
    if (!build_stair(raw)) return build_mask(raw,NULL);
    int next[4]; build_neighbors(x,y,z,next);
    return build_mask(raw,next);
}
/* A full half is one box. Other halves contain at most three quarters.
   Shared boundaries are exact binary fractions, including at negative x/z. */
int build_boxes(int x,int y,int z,unsigned mask,BlockBox out[BUILD_BOXES]) {
    if (!out) return 0;
    int count=0;
    for (int h=0;h<2;++h) {
        unsigned m=(mask>>(h*4))&15u;
        float bottom=y+h*.5f,top=bottom+.5f;
        if (m==15u) out[count++]=(BlockBox){(float)x,bottom,(float)z,x+1.f,top,z+1.f};
        else for (int q=0;q<4;++q) if (m&(1u<<q)) {
            float a=x+(q&1)*.5f,b=z+(q>>1)*.5f;
            out[count++]=(BlockBox){a,bottom,b,a+.5f,top,b+.5f};
        }
    }
    return count;
}
int build_world_boxes(int x,int y,int z,int raw,BlockBox out[BUILD_BOXES]) {
    return build_boxes(x,y,z,build_world_mask(x,y,z,raw),out);
}
