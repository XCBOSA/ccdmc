#include "internal.h"
#include <stdlib.h>

/* A leeward hollow with a curved axis, exposed tilted beds and a walkable
   lip on the downwind end. The ramps are part of the geology, not stairs. */
static int cut_depth(int across,int along) {
    const int bend=along*along/36-1;
    const int flank=abs(across-bend);
    const int beyond=abs(along)>4 ? abs(along)-4 : 0;
    const int distance=flank+beyond;
    int depth=distance<9 ? 9-distance : 0;
    if (depth>6) depth=6;
    if (along>5 && depth>11-along) depth=11-along;
    return depth;
}

static void wind_coordinates(WindcutPlan p,int *across,int *along) {
    for (unsigned i=0;i<(p.rotation&3u);++i) {
        const int old=*across; *across=*along; *along=-old;
    }
}

int windcut_contains(WindcutPlan p,int x,int z) {
    int64_t dx=(int64_t)x-p.x,dz=(int64_t)z-p.z;
    if (dx<-WINDCUT_RADIUS || dx>WINDCUT_RADIUS || dz<-WINDCUT_RADIUS || dz>WINDCUT_RADIUS) return 0;
    int across=(int)dx,along=(int)dz;
    wind_coordinates(p,&across,&along);
    return cut_depth(across,along)>0;
}

static unsigned keep_below(TerrainWideCell *entries,unsigned count,unsigned column,int elevation) {
    unsigned next=0;
    for (unsigned i=0;i<count;++i) {
        const TerrainWideCell cell=entries[i];
        if (cell.index/WY!=column || cell.index%WY<elevation) entries[next++]=cell;
    }
    return next;
}

void windcut_generate_chunk(WindcutPlan p,uint64_t seed,int cx,int cz,uint8_t *cells,
    uint8_t *tops,TerrainWide *wide,TerrainDevices *devices) {
    const int start_x=cx*CHUNK_SIZE,start_z=cz*CHUNK_SIZE;
    for (int x=0;x<CHUNK_SIZE;++x) for (int z=0;z<CHUNK_SIZE;++z) {
        const int wx=start_x+x,wz=start_z+z;
        int across=wx-p.x,along=wz-p.z;
        if (abs(across)>WINDCUT_RADIUS || abs(along)>WINDCUT_RADIUS) continue;
        wind_coordinates(p,&across,&along);
        const int depth=cut_depth(across,along);
        if (!depth) continue;
        const unsigned col=(unsigned)(x*CHUNK_SIZE+z);
        const int reference=p.y-1-depth;
        const int floor=tops[col]<reference ? tops[col] : reference;
        if (floor<4 || floor>=WY) continue;
        const int base=(int)col*WY;
        if (wide) wide->count=keep_below(wide->cell,wide->count,col,floor-2);
        if (devices) devices->count=keep_below(devices->cell,devices->count,col,floor-2);
        for (int y=floor+1;y<=tops[col];++y) cells[base+y]=AIR;
        tops[col]=(uint8_t)floor;
        for (int y=floor-2;y<=floor;++y) cells[base+y]=SANDSTONE;
        if (depth<3) continue;

        /* Slanted exposures on opposite flanks yield useful samples without
           turning the whole basin into a mine. */
        const unsigned flecks=hash_for_seed(seed,wx^0x7c9d,wz^0x312f);
        const int tilt=across+along/3;
        const int lime=tilt>=0;
        const int exposed=(lime ? tilt%3==1 : (-tilt)%3==1) || ((flecks>>9)&7u)==0u;
        const int rock=lime ? LIMESTONE_ROCK : SLATE_ROCK;
        cells[base+floor]=(uint8_t)(lime ? SANDSTONE : STONE);
        if (!wide || wide->count>=terrain_wide_budget()) continue;
        const int sample=floor-!exposed;
        wide->cell[wide->count++]=(TerrainWideCell){(uint16_t)(base+sample),(uint16_t)rock};
    }
}
