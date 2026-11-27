#include "internal.h"
#include <stdlib.h>

BlockEdit rain_garden_position(RainGardenPlan p,int x,int y,int z) {
    for (unsigned i=0;i<(p.rotation&3u);++i) { const int old=x; x=-z; z=old; }
    return (BlockEdit){p.x+x,p.y+y,p.z+z,0};
}

static int local_point(RainGardenPlan p,int x,int z,int *lx,int *lz) {
    const int64_t dx=(int64_t)x-p.x,dz=(int64_t)z-p.z;
    if (dx<-RAIN_GARDEN_RADIUS || dx>RAIN_GARDEN_RADIUS ||
        dz<-RAIN_GARDEN_RADIUS || dz>RAIN_GARDEN_RADIUS) return 0;
    *lx=(int)dx; *lz=(int)dz;
    for (unsigned i=0;i<(p.rotation&3u);++i) {
        const int old=*lx; *lx=*lz; *lz=-old;
    }
    /* Open crescent under the surviving edge trees, not a building envelope. */
    return abs(*lx)+((abs(*lz+1)+1)/2)<=6;
}

int rain_garden_contains(RainGardenPlan p,int x,int z) {
    int lx,lz;
    return local_point(p,x,z,&lx,&lz);
}

static unsigned retain_deep(TerrainWideCell *entries,unsigned count,const int16_t *cut) {
    unsigned next=0;
    for (unsigned i=0;i<count;++i) {
        const TerrainWideCell old=entries[i];
        const int low=cut[old.index/WY];
        if (low<0 || old.index%WY<low) entries[next++]=old;
    }
    return next;
}

void rain_garden_generate_chunk(RainGardenPlan p,uint64_t seed,int cx,int cz,uint8_t *cells,
    uint8_t *tops,TerrainWide *wide,TerrainDevices *devices) {
    const int water_y=p.y+RAIN_GARDEN_BED_LEVEL;
    if (water_y<4 || water_y>WY-4) return;
    int16_t cut[CHUNK_COLUMNS];
    for (int i=0;i<CHUNK_COLUMNS;++i) cut[i]=-1;
    int bed_index=-1;
    for (int x=0;x<CHUNK_SIZE;++x) for (int z=0;z<CHUNK_SIZE;++z) {
        const int wx=cx*CHUNK_SIZE+x,wz=cz*CHUNK_SIZE+z;
        int a,b;
        if (!local_point(p,wx,wz,&a,&b)) continue;
        const int col=x*CHUNK_SIZE+z,base=col*WY;
        const int channel=abs(a)<=3 && abs(b)<=1;
        const int cut_limit=p.y-(abs(a)+abs(b)<6 ? 2 : 1);
        int top=channel ? water_y+1 : tops[col]<cut_limit ? tops[col] : cut_limit;
        if (top<4 || top>=WY) continue;
        /* Do not clear disconnected edge columns which sit below the hollow. */
        if (!channel && tops[col]<=top) continue;
        const int bottom=channel ? water_y-1 : top-1;
        cut[col]=(int16_t)bottom;
        for (int y=bottom;y<=tops[col] || y<=top;++y)
            cells[base+y]=(uint8_t)(y>top ? AIR : channel ? STONE : DIRT);
        if (!channel) {
            unsigned patch=hash_for_seed(seed,wx/2,wz/2);
            cells[base+top]=(uint8_t)(patch%11u==0 ? CLAY : patch%3u==0 ? STONE : GRASS);
        } else if (!b) {
            /* West source already touches the bed. The high eastern source
               is held by a clay lip; opening it lets water drop one level
               and touch the other side without ever washing over the bed. */
            if (!a) {
                bed_index=base+water_y;
                cells[base+water_y+1]=AIR;
                top=water_y;
            } else if (a==-1) {
                cells[base+water_y]=WATER;
                cells[base+water_y+1]=AIR;
                top=water_y;
            } else if (a==1) {
                cells[base+water_y]=AIR;
                cells[base+water_y+1]=CLAY;
            } else if (a==2) {
                cells[base+water_y+1]=WATER;
            }
        }
        tops[col]=(uint8_t)top;
    }
    if (devices) devices->count=retain_deep(devices->cell,devices->count,cut);
    if (wide) {
        wide->count=retain_deep(wide->cell,wide->count,cut);
        if (bed_index>=0 && wide->count<terrain_wide_budget())
            wide->cell[wide->count++]=(TerrainWideCell){(uint16_t)bed_index,SEEP_BED};
    }
}
