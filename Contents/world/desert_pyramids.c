#include "internal.h"
#include <stdlib.h>

enum { FOUNDATION=-4, VAULT_BASE=-14, ROOF=10 };
_Static_assert(TERRAIN_DEVICE_LIMIT>=10,"A complete pyramid trap must fit in one chunk.");

BlockEdit desert_pyramid_position(DesertPyramidPlan p,int dx,int dy,int dz,int raw) {
    for (unsigned r=0;r<(p.rotation&3u);++r) {
        int old=dx; dx=-dz; dz=old;
    }
    return (BlockEdit){p.x+dx,p.y+dy,p.z+dz,raw};
}

static int pyramid_block(int x,int y,int z) {
    int ax=abs(x),az=abs(z);
    if (y<0) {
        /* Shaft starts under the blue tile. The treasure chamber is wider
           than the shaft so the plate is avoidable by digging alongside it. */
        if (ax<=3 && az<=3) {
            if (y==PYRAMID_TNT && ax<=1 && az<=1) return TNT_BLOCK;
            if (y>=PYRAMID_TREASURE && y<=-8 && ax<3 && az<3) {
                if (y==PYRAMID_TREASURE) {
                    if (!x && !z) return PRESSURE_STONE;
                    if ((ax==2 && !z) || (az==2 && !x)) return DESERT_PYRAMID_CHEST_CELL;
                }
                return AIR;
            }
            if (y>-8 && ax<=1 && az<=1) return AIR;
        }
        return SANDSTONE;
    }
    if (y==0) {
        if (!x && !z) return BLUE_STAINED_CLAY;
        if (ax+az==3 || ((ax==az || !x || !z) && ax<=2 && az<=2)) return ORANGE_STAINED_CLAY;
        return SANDSTONE;
    }

    /* Twin five-wide towers at the south facade. The symbols are individual
       recoverable clay blocks set into sandstone, not a painted texture. */
    int wing=abs(ax-8),front=abs(z-8);
    if (wing<=2 && front<=2 && y<=8) {
        if (y>=7) return wing<=1 && front<=1 ? SANDSTONE : AIR;
        if (z==10 && ((y==5 && wing<=1) || (y==4 && wing==1) ||
            (y==3 && wing<=1) || (y==2 && wing==0))) return ORANGE_STAINED_CLAY;
        if (y==5 && wing==2 && z==8) return AIR;
        if (y==1 && front==2 && z==6 && wing==0) return AIR;
        if (wing==2 || front==2 || y==6) return SANDSTONE;
        return AIR;
    }

    int radius=10-y;
    if (radius<0 || az>radius+1 || ax>radius+1) return AIR;
    if (z>0 && ax<=1 && y<=3) return AIR;
    if (y>=3 && y<=4 && ((ax>=radius && !z) || (z<0 && az>=radius && !x)))
        return AIR;
    /* Two blocks of shell room overlap the step on the next layer, so
       the roof and the walls do not leave one-block diagonal seams. */
    if (y<=4) return ax>=radius || az>=radius ? SANDSTONE : AIR;
    return SANDSTONE;
}

void desert_pyramid_generate_chunk(DesertPyramidPlan p,int cx,int cz,uint8_t *cells,uint8_t *tops,
    TerrainWide *wide,TerrainDevices *devices) {
    for (int x=-PYRAMID_RADIUS;x<=PYRAMID_RADIUS;++x) for (int z=-PYRAMID_RADIUS;z<=PYRAMID_RADIUS;++z) {
        BlockEdit at=desert_pyramid_position(p,x,0,z,AIR);
        int local_x=at.x-cx*CHUNK_SIZE,local_z=at.z-cz*CHUNK_SIZE;
        if ((unsigned)local_x>=CHUNK_SIZE || (unsigned)local_z>=CHUNK_SIZE) continue;
        int col=local_x*CHUNK_SIZE+local_z,base=col*WY;
        int bottom=p.y+(abs(x)<=3 && abs(z)<=3 ? VAULT_BASE : FOUNDATION);
        int ceiling=p.y+ROOF,top=tops[col];
        if (wide) for (unsigned i=0;i<wide->count;) {
            unsigned index=wide->cell[i].index;
            if (index/WY==(unsigned)col && (int)(index%WY)>=bottom) wide->cell[i]=wide->cell[--wide->count];
            else ++i;
        }
        for (int y=bottom;y<=top || y<=ceiling;++y) {
            int raw=pyramid_block(x,y-p.y,z),index=base+y;
            if ((pressure_cell(raw) || raw==TNT_BLOCK) && devices && devices->count<TERRAIN_DEVICE_LIMIT) {
                cells[index]=AIR;
                devices->cell[devices->count++]=(TerrainWideCell){(uint16_t)index,(uint16_t)raw};
            } else if (raw>255) {
                if (wide && wide->count<terrain_wide_budget())
                    wide->cell[wide->count++]=(TerrainWideCell){(uint16_t)index,(uint16_t)raw};
                cells[index]=(uint8_t)(raw==DESERT_PYRAMID_CHEST_CELL ? CHEST : SANDSTONE);
            } else cells[index]=(uint8_t)raw;
        }
        top=ceiling;
        while (top>0 && cells[base+top]==AIR) --top;
        tops[col]=(uint8_t)top;
    }
}
