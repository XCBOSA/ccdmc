#include "mineshafts.h"
#include "rails.h"
#include "villages.h"
#include <stdlib.h>

int mineshaft_plan(TerrainKey key,int x,int z,MineshaftPlan *out) {
    if (!out || key.dimension!=OVERWORLD || key.generation<GENERATOR_MINESHAFTS ||
        (key.generation>=GENERATOR_SPILLWAYS && key.profile!=WORLD_SUPERFLAT) ||
        key.structures_disabled || x < -WORLD_SAVE_LIMIT || x>WORLD_SAVE_LIMIT ||
        z < -WORLD_SAVE_LIMIT || z>WORLD_SAVE_LIMIT) return 0;
    int floor=28;
    if (key.profile==WORLD_SUPERFLAT) {
        const FlatLayout *flat=flat_effective(&key.flat);
        if (!(flat->features&FLAT_MINESHAFT) || flat->height<14) return 0;
        if (floor>flat->height-8) floor=flat->height-8;
        for (int y=floor;y<=floor+4;++y)
            if (!solid(flat->blocks[y])) return 0;
    }
    int rx=x/MINESHAFT_REGION-(x%MINESHAFT_REGION<0);
    int rz=z/MINESHAFT_REGION-(z%MINESHAFT_REGION<0);
    unsigned select=hash_for_seed(key.seed,rx^0x4d5348,rz^0x19b7);
    if (select%4u) return 0;
    unsigned r=hash_for_seed(key.seed,rx^0x6e93,rz^0x371a);
    MineshaftPlan p={
        rx*MINESHAFT_REGION+48+(int)(r&31u),
        rz*MINESHAFT_REGION+48+(int)((r>>5)&31u),
        floor,(int)((r>>10)&1u),-33-(int)((r>>11)&7u),33+(int)((r>>14)&7u),
        {-12-(int)((r>>17)&7u),12+(int)((r>>20)&7u)},
        -17-(int)((r>>23)&7u),17+(int)((r>>26)&7u),r
    };
    /* A chunk and all of its columns belong to the same region. The layout
       never touches a region boundary, even after rotation. */
    if (abs(p.x)>WORLD_SAVE_LIMIT-41 || abs(p.z)>WORLD_SAVE_LIMIT-41) return 0;
    *out=p;
    return 1;
}

static int within(int p,int lo,int hi) { return p>=lo && p<=hi; }

void mineshaft_generate_chunk(const MineshaftPlan *p,int cx,int cz,uint8_t *cells,TerrainWide *wide) {
    int u0=(p->rotated ? cz*CHUNK_SIZE-p->z : cx*CHUNK_SIZE-p->x);
    int v0=(p->rotated ? cx*CHUNK_SIZE-p->x : cz*CHUNK_SIZE-p->z);
    if (u0>p->right || u0+CHUNK_SIZE-1<p->left ||
        v0>p->south+1 || v0+CHUNK_SIZE-1<p->north-1) return;
    int main_track=p->rotated ? STRAIGHT_NS : STRAIGHT_EW;
    int branch_track=p->rotated ? STRAIGHT_EW : STRAIGHT_NS;
    for (int x=0;x<CHUNK_SIZE;++x) for (int z=0;z<CHUNK_SIZE;++z) {
        int u=u0+(p->rotated ? z : x),v=v0+(p->rotated ? x : z);
        int branch=abs(u-p->branch[0])<=1 ? 0 : abs(u-p->branch[1])<=1 ? 1 : -1;
        int room=abs(u)<=5 && abs(v)<=4;
        int trunk=within(u,p->left,p->right) && abs(v)<=1;
        int passage=branch>=0 && within(v,p->north,p->south);
        int cross_link=within(u,p->branch[0],p->branch[1]) && abs(v-(p->north+3))<=1;
        if (!(room || trunk || passage || cross_link)) continue;
        int index=(x*CHUNK_SIZE+z)*WY+p->floor;
        uint8_t *col=cells+index;
        /* Bridge caverns rather than fill them. Existing floor rock/ore and
           any cave opening outside the cut stay in place. */
        if (!solid(cell_type(col[0]))) col[0]=PLANKS;
        col[1]=col[2]=col[3]=AIR;
        if (room) {
            if (abs(u)==4 && abs(v)==3) {
                col[1]=col[2]=WOOD_FENCE;
                col[3]=PLANKS;
            } else if (abs(u)==3 && abs(v)==3) col[1]=TORCH;
            continue;
        }
        if ((trunk && passage) || (cross_link && passage)) continue;
        int along=passage ? v : u;
        int across=passage ? u-p->branch[branch] : cross_link ? v-(p->north+3) : v;
        unsigned n=(unsigned)(along+64);
        if (n%6u==0) {
            col[3]=PLANKS;
            if (across) col[1]=col[2]=WOOD_FENCE;
        }
        if (!across && !cross_link && (trunk || (!branch && v<0)) &&
            n%13u!=0) col[1]=(uint8_t)(TRACK_BASE+(trunk ? main_track : branch_track));
        if (trunk && !across && u==p->right-4) {
            col[1]=(uint8_t)(TRACK_BASE+main_track);
            if (wide && wide->count<terrain_wide_budget()-2u*VILLAGE_HOMES)
                wide->cell[wide->count++]=(TerrainWideCell){(uint16_t)(index+1),(uint16_t)(MINE_CART_TRACK_BASE+main_track)};
        }
        if (across==-1 && n%17u==4 && col[1]==AIR &&
            !(passage && branch==1 && v>8)) col[1]=TORCH;
        /* Thickest webbing stays at the unlit far end of the eastern spur.
           Leave some cells open for navigation; only sparse overlays needed. */
        if (wide && passage && branch==1 && v>8 && col[1]==AIR &&
            wide->count<terrain_wide_budget()-2u*VILLAGE_HOMES) {
            if (!across && v==p->south-3)
                wide->cell[wide->count++]=(TerrainWideCell){(uint16_t)(index+1),MINE_CAVE_SPIDER_CELL};
            else if ((n+(unsigned)(across+1)*3u+p->key)%5u==0)
                wide->cell[wide->count++]=(TerrainWideCell){(uint16_t)(index+1),COBWEB};
        }
    }
}
