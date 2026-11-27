#include "spillways.h"

BlockEdit spillway_cell(SpillwayPlan p,int u,int v,int dy,int cell) {
    return (BlockEdit){p.x+(p.rotated?v:u),p.floor+dy,p.z+(p.rotated?u:v),cell};
}

void spillway_generate_chunk(SpillwayPlan p,int cx,int cz,uint8_t *cells,TerrainWide *wide) {
    const int x0=cx*CHUNK_SIZE,z0=cz*CHUNK_SIZE;
    for (int u=SPILLWAY_BEGIN;u<=SPILLWAY_END;++u) for (int v=SPILLWAY_NEAR;v<=SPILLWAY_FAR;++v) {
        const BlockEdit q=spillway_cell(p,u,v,0,0);
        const int x=q.x-x0,z=q.z-z0;
        if ((unsigned)x>=CHUNK_SIZE || (unsigned)z>=CHUNK_SIZE) continue;
        /* The dry walk and the pool share a stone curb at the walkway's
           floor. The raised source is sealed on its upstream end and sides. */
        uint8_t *base=cells+(x*CHUNK_SIZE+z)*WY+p.floor;
        const int mouth=u==SPILLWAY_END && v>=p.mouth && v<p.mouth+2;
        const int border=u==SPILLWAY_BEGIN || (u==SPILLWAY_END && !mouth) || v==SPILLWAY_NEAR || v==SPILLWAY_FAR;
        for (int y=-2;y<=SPILLWAY_ROOF;++y) {
            int cell=STONE;
            if (!border && v<=0 && y>=1 && y<SPILLWAY_ROOF && (!mouth || y<=2))
                cell=(v==0 && u<=0 && y==3) ? STONE : AIR;
            if (!border && v==1) {
                if (u==0 && y==3) cell=CLAY; /* Plug exposed from the lower walk. */
                else if (u==-1 && y==3) cell=WATER;
                else if (u==1 && y>=-1 && y<=4) cell=AIR;
                else if (u>=2 && u<=5 && y>=-1 && y<=1) cell=AIR;
            }
            base[y]=(uint8_t)cell;
        }
        if (v==-3 && (u==-2 || u==5)) base[1]=TORCH;
        if (!border && wide && wide->count<terrain_wide_budget() &&
            ((v==-1 && u>=0 && u<=5) || (v==0 && u==1))) {
            const int platform=v==0 ? 1 : 0;
            base[platform]=STONE;
            wide->cell[wide->count++]=(TerrainWideCell){
                (uint16_t)((x*CHUNK_SIZE+z)*WY+p.floor+platform),
                (uint16_t)(v==0 ? FALL_SIEVE : LIMESTONE_ROCK)
            };
        }
    }
}
