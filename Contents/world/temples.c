#include "temples.h"
#include "internal.h"
#include "rails.h"
#include "redstone.h"
#include "build_blocks.h"

enum { WIDTH=13, DEPTH=15, BASE=-5, ROOF=7 };
typedef struct { int x,y,z,raw; } Blueprint;
static const Blueprint traps[]={
    {5,-4,6,TRIPWIRE_HOOK_BASE+WEST}, {6,-4,6,TRIPWIRE_CELL}, {7,-4,6,TRIPWIRE_CELL},
    {8,-4,6,TRIPWIRE_CELL}, {9,-4,6,TRIPWIRE_HOOK_BASE+EAST},
    {5,-4,5,WIRE_BASE}, {5,-4,4,WIRE_BASE}, {6,-4,4,WIRE_BASE},
    {7,-4,4,DISPENSER_BASE+SOUTH},
    {5,-4,2,TRIPWIRE_HOOK_BASE+WEST}, {6,-4,2,TRIPWIRE_CELL}, {7,-4,2,TRIPWIRE_CELL},
    {8,-4,2,TRIPWIRE_CELL}, {9,-4,2,TRIPWIRE_HOOK_BASE+EAST},
    {9,-4,1,WIRE_BASE}, {8,-4,1,WIRE_BASE}, {7,-4,1,DISPENSER_BASE+SOUTH}
};
static const Blueprint puzzle[]={
    {1,-2,4,LEVER_BASE+LEVER_WALL+EAST},{2,-1,4,TORCH_POWER_BASE+TORCH_POWER_ON+4},
    {3,-1,4,PISTON_BASE+PISTON_STICKY+EJECTOR_DOWN},
    {1,-1,6,LEVER_BASE+LEVER_ON+LEVER_WALL+EAST},{3,-1,6,PISTON_BASE+PISTON_STICKY+EJECTOR_DOWN},
    {1,-2,8,LEVER_BASE+LEVER_WALL+EAST},{2,-1,8,TORCH_POWER_BASE+TORCH_POWER_ON+4},
    {3,-1,8,PISTON_BASE+PISTON_STICKY+EJECTOR_DOWN}
};
_Static_assert(sizeof traps/sizeof *traps+sizeof puzzle/sizeof *puzzle<=TERRAIN_DEVICE_LIMIT,
    "Temple activation metadata must fit even when all devices share one chunk.");

static int rotate(int raw,unsigned turn) {
    if (tripwire_hook_cell(raw)) return TRIPWIRE_HOOK_BASE+((tripwire_mount(raw)+turn)&3);
    if (dispenser_cell(raw)) return DISPENSER_BASE+((ejector_face(raw)+turn)&3);
    if (lever_cell(raw)) {
        int code=(raw-LEVER_BASE)&7;
        return raw-code+LEVER_WALL+((code-LEVER_WALL+turn)&3);
    }
    return raw; /* Floor torches, downward pistons and dust have no compass face. */
}
BlockEdit temple_position(TemplePlan plan,int x,int y,int z,int raw) {
    int dx=x-WIDTH/2,dz=z-DEPTH/2;
    for (unsigned i=0;i<(plan.rotation&3);++i) { int old=dx; dx=-dz; dz=old; }
    return (BlockEdit){plan.x+dx,plan.y+y,plan.z+dz,rotate(raw,plan.rotation)};
}
static int floor_wall(int x,int y,int z,int brick) {
    if (y<0) {
        if (y==BASE || x==0 || z==0 || x==WIDTH-1 || z==DEPTH-1) return brick;
        /* Walkable descent off the south entrance. */
        if (x>=6 && x<=7 && z>=9 && z<=13) return y>z-13 ? AIR : brick;
        /* Three basement lanes: lever gallery, secret shaft, trapped hall.
           The rear of the gallery joins the stairs; its front is walled off. */
        if (z>=10 && z<=12 && x>=1 && x<=8 && y>=-4 && y<=-2) return AIR;
        if (x==1 && z>=3 && z<=11) return AIR;
        if (x==3 && z>=2 && z<=11) {
            if (y==-2 && (z==4 || z==6 || z==8)) return MOSS_STONE;
            return AIR;
        }
        if (x==2 && z==10) return AIR;
        if (x>=6 && x<=8 && z>=1 && z<=8) return AIR;
        /* Service paths for the dust. Hook backing stays in the masonry. */
        if (y==-4 && ((x==5 && z>=4 && z<=6) || (x==6 && z==4) ||
            (x==9 && (z==1 || z==2 || z==6)) || (x==5 && z==2))) return AIR;
        return brick;
    }
    if (y==0) return (x>=6 && x<=7 && z>=8 && z<=12) ? AIR : brick;
    if (y==1 && x>=6 && x<=7 && z==DEPTH-1) return AIR;
    if (y==2 && x>=6 && x<=7 && z==DEPTH-1) return AIR;
    if (y<=3) return x==0 || z==0 || x==WIDTH-1 || z==DEPTH-1 ||
        ((x==3 || x==9) && (z==4 || z==10)) ? brick : AIR;
    if (y==4) return x>=1 && x<=11 && z>=1 && z<=13 ? brick : AIR;
    if (y==5) return x>=2 && x<=10 && z>=2 && z<=12 ? brick : AIR;
    if (y==6) return x>=3 && x<=9 && z>=3 && z<=11 ? brick : AIR;
    return x>=4 && x<=8 && z>=4 && z<=10 ? brick : AIR;
}
static void devices_for_column(TemplePlan plan,int x,int z,int column,uint8_t *cells,TerrainDevices *devices,
    const Blueprint *blueprint,unsigned count) {
    if (!devices) return;
    for (unsigned i=0;i<count;++i) {
        Blueprint p=blueprint[i];
        if (p.x!=x || p.z!=z) continue;
        int index=column*WY+plan.y+p.y;
        /* Device positions replace their one block of masonry only. Their
           supports are already complete when the chunk is delivered. */
        cells[index]=AIR;
        devices->cell[devices->count++]=(TerrainWideCell){(uint16_t)index,(uint16_t)rotate(p.raw,plan.rotation)};
    }
}
void temple_generate_chunk(TemplePlan plan,uint64_t seed,int cx,int cz,uint8_t *cells,uint8_t *tops,
    TerrainWide *wide,TerrainDevices *devices) {
    for (int x=0;x<WIDTH;++x) for (int z=0;z<DEPTH;++z) {
        BlockEdit at=temple_position(plan,x,0,z,AIR);
        int lx=at.x-cx*CHUNK_SIZE,lz=at.z-cz*CHUNK_SIZE;
        if ((unsigned)lx>=CHUNK_SIZE || (unsigned)lz>=CHUNK_SIZE) continue;
        int col=lx*CHUNK_SIZE+lz,base=col*WY,top=tops[col];
        if (wide) {
            for (unsigned i=0;i<wide->count;) {
                unsigned index=wide->cell[i].index;
                if (index/WY==(unsigned)col && (int)(index%WY)>=plan.y+BASE)
                    wide->cell[i]=wide->cell[--wide->count];
                else ++i;
            }
        }
        /* Clear trees from the temple envelope, not from neighbouring chunks. */
        for (int y=plan.y+BASE;y<=top || y<=plan.y+ROOF;++y) {
            int dy=y-plan.y;
            int brick=hash_for_seed(seed^(unsigned)y,at.x,at.z)%5<3 ? MOSS_STONE : COBBLESTONE;
            cells[base+y]=(uint8_t)(dy>ROOF ? AIR : floor_wall(x,dy,z,brick));
            if (terrain_build_generation()>=GENERATOR_ARCHITECTURE && (x==6 || x==7) && z>=9 && z<=13 && dy==z-13)
                terrain_build_shape(cells,base+y,build_stair_raw(BUILD_COBBLE,(SOUTH+plan.rotation)&3,0),wide);
        }
        devices_for_column(plan,x,z,col,cells,devices,traps,sizeof traps/sizeof *traps);
        devices_for_column(plan,x,z,col,cells,devices,puzzle,sizeof puzzle/sizeof *puzzle);
        if ((x==6 && z==1) || (x==3 && z==2)) {
            int index=base+plan.y-4;
            cells[index]=CHEST;
            if (wide && wide->count<terrain_wide_budget())
                wide->cell[wide->count++]=(TerrainWideCell){(uint16_t)index,TEMPLE_CHEST_CELL};
        }
        /* Small interior lanterns use existing torches; no new visual assets. */
        if ((x==1 && (z==5 || z==9)) || (x==7 && z==7)) cells[base+plan.y-4]=TORCH;
        top=plan.y+ROOF;
        while (top>0 && cells[base+top]==AIR) --top;
        tops[col]=(uint8_t)top;
    }
}
