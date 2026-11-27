#include "villages.h"
#include "internal.h"
#include "build_blocks.h"
#include "rails.h"
#include <stdlib.h>

/* Width and depth are half-extents of the room. Entrances face the road. */
typedef struct { int a,b,width,depth,height; } Home;
enum { TOWER_HOME=2, SMITH_HOME=3 };
static const Home homes[VILLAGE_HOMES]={
    {-11,8,3,2,3}, {10,-8,4,3,3}, {0,-16,2,3,5}, {-11,-8,4,3,3}, {11,8,3,2,3}
};

static void rotate(unsigned turn,int a,int b,int *x,int *z) {
    switch (turn&3u) {
    case 1: *x=-b; *z=a; break;
    case 2: *x=-a; *z=-b; break;
    case 3: *x=b; *z=-a; break;
    default: *x=a; *z=b; break;
    }
}
void village_global(const VillagePlan *p,int a,int b,int *x,int *z) {
    rotate(p->turn,a,b,x,z);
    *x+=p->x; *z+=p->z;
}
static int entry_side(Home h) { return h.b>0 ? -1 : 1; }
static int tower_ladder_b(Home h) { return -entry_side(h)*(h.depth-1); }
static int ladder_toward_back(Home h,unsigned turn) { return ((entry_side(h)>0 ? NORTH : SOUTH)+turn)&3; }
int village_natural_ladder(int x,int y,int z) {
    VillagePlan plan;
    if (terrain_build_generation()<GENERATOR_ARCHITECTURE || !village_plan_for(x,z,&plan)) return -1;
    Home h=homes[TOWER_HOME];
    int anchor_x,anchor_z;
    village_global(&plan,h.a+1,h.b+tower_ladder_b(h),&anchor_x,&anchor_z);
    return x==anchor_x && z==anchor_z && y>plan.floor && y<=plan.floor+h.height+1 ?
        LADDER+ladder_toward_back(h,plan.turn) : -1;
}
static int road_at(int a,int b);
int village_home_entry(const VillagePlan *p,int home,int *x,int *z) {
    if (!p || !x || !z || (unsigned)home>=VILLAGE_HOMES) return 0;
    Home h=homes[home];
    village_global(p,h.a,h.b+entry_side(h)*(h.depth+1),x,z);
    return 1;
}
int village_home_door(const VillagePlan *p,int home,int *x,int *z,int *facing) {
    if (!p || !x || !z || !facing || (unsigned)home>=VILLAGE_HOMES) return 0;
    Home h=homes[home];
    village_global(p,h.a,h.b+entry_side(h)*h.depth,x,z);
    *facing=((entry_side(h)<0 ? NORTH : SOUTH)+p->turn)&3;
    return 1;
}
int village_home_contains(const VillagePlan *p,int home,int x,int z) {
    if (!p || (unsigned)home>=VILLAGE_HOMES) return 0;
    int a,b;
    rotate((4-p->turn)&3u,x-p->x,z-p->z,&a,&b);
    Home h=homes[home];
    return abs(a-h.a)<=h.width && abs(b-h.b)<=h.depth;
}
int village_smith_chest(const VillagePlan *p,int *x,int *y,int *z) {
    if (!p || !x || !y || !z) return 0;
    Home h=homes[SMITH_HOME];
    village_global(p,h.a+1,h.b-entry_side(h)*(h.depth-1),x,z);
    *y=p->floor+1;
    return 1;
}

static void prepare(uint8_t *col,int top,int y,int fill) {
    for (int h=y+1;h<=top;++h) col[h]=AIR;
    /* Support a terrace from below without filling deep caves. */
    for (int h=y-1;h>0 && h>=y-9 && !solid(cell_type(col[h]));--h) col[h]=(uint8_t)fill;
}
static int gable_steps(int b,unsigned turn) {
    return !b ? build_slab_raw(BUILD_OAK,BUILD_SLAB_BOTTOM) :
        build_stair_raw(BUILD_OAK,((b<0 ? SOUTH : NORTH)+turn)&3,0);
}
static int house_column(uint8_t *cells,int column,int top,int y,int a,int b,int home,Biome style,
        int smith_loot,unsigned turn,TerrainWide *wide) {
    const int base=column*WY,architecture=terrain_build_generation()>=GENERATOR_ARCHITECTURE;
    uint8_t *c=cells+base;
    Home h=homes[home];
    a-=h.a; b-=h.b;
    int aa=abs(a),bb=abs(b),stone=style==BIOME_DESERT ? SANDSTONE : COBBLESTONE;
    int beam=style==BIOME_DESERT ? SANDSTONE : WOOD;
    int side=style==BIOME_DESERT ? SANDSTONE : PLANKS;
    if (aa>h.width+1 || bb>h.depth+1) return -1;
    int roof=y+h.height+1,peak=roof;
    int gable=architecture && style!=BIOME_DESERT && home!=2 && home!=SMITH_HOME;
    int caps=architecture && (style==BIOME_DESERT || home==SMITH_HOME);
    if (aa>h.width || bb>h.depth) {
        /* Overhanging roof along the front/back, but no raised walk barrier. */
        if (aa>h.width && bb>h.depth) return -1;
        if (road_at(a+h.a,b+h.b)) {
            prepare(c,top,y,stone);
            c[y]=(uint8_t)(style==BIOME_DESERT ? SANDSTONE : GRAVEL);
        } else for (int i=roof;i<=top;++i) c[i]=AIR;
        if (gable) {
            int at=roof+h.depth+1-bb;
            terrain_build_shape(cells,base+at,gable_steps(b,turn),wide);
            return at;
        }
        if (caps) terrain_build_shape(cells,base+roof,
            build_slab_raw(style==BIOME_DESERT ? BUILD_SANDSTONE : BUILD_STONE,BUILD_SLAB_BOTTOM),wide);
        else c[roof]=(uint8_t)side;
        return roof;
    }
    prepare(c,top,y,stone);
    c[y]=(uint8_t)(home==2 || home==3 || style==BIOME_DESERT ? stone : PLANKS);
    int perimeter=aa==h.width || bb==h.depth;
    int door=(!a && b==entry_side(h)*h.depth);
    for (int i=1;i<=h.height;++i) {
        if (door && i<=2) continue;
        if (!perimeter) continue;
        int corner=aa==h.width && bb==h.depth;
        int window=i==2 && !door && !corner && ((aa==h.width && !b) || (bb==h.depth && aa==2));
        c[y+i]=(uint8_t)(window ? GLASS : corner ? beam : home==2 || (home==3 && i==1) ? stone : side);
    }
    if (!perimeter && b== -entry_side(h)*(h.depth-1)) {
        if (a==0) c[y+1]=(uint8_t)(home==0 ? CRAFTING_TABLE : home==1 ? BOOKSHELF :
            home==2 ? STONE_BRICKS : home==3 ? FURNACE : CRAFTING_TABLE);
        if (a==1 && home==SMITH_HOME) c[y+1]=(uint8_t)(smith_loot ? SMITH_CHEST_CELL : CHEST);
        if (a==-1 && h.depth>2) c[y+1]=TORCH;
    }
    if (architecture && home==TOWER_HOME && a==1 && b==tower_ladder_b(h)) {
        /* Clear access through the roof but keep the back wall intact all
           the way to the final rung. No ladder is placed in the doorway. */
        int toward_back=ladder_toward_back(h,turn);
        for (int i=y+1;i<=roof;++i) terrain_build_shape(cells,base+i,LADDER+toward_back,wide);
        return roof;
    }
    if (gable) {
        peak=roof+h.depth+1-bb;
        for (int i=roof;i<peak;++i) c[i]=(uint8_t)side;
        terrain_build_shape(cells,base+peak,gable_steps(b,turn),wide);
        return peak;
    }
    if (caps && home!=2) {
        terrain_build_shape(cells,base+roof,
            build_slab_raw(style==BIOME_DESERT ? BUILD_SANDSTONE : BUILD_STONE,BUILD_SLAB_BOTTOM),wide);
        return roof;
    }
    c[roof]=(uint8_t)(home==2 ? stone : side);
    if (style!=BIOME_DESERT && home!=2 && bb<h.depth) {
        int rise=h.depth-bb;
        for (int i=1;i<=rise;++i) c[roof+i]=(uint8_t)side;
        peak=roof+rise;
    } else if (home==2 && perimeter && ((aa+bb)&1)) {
        c[++peak]=(uint8_t)stone;
    }
    return peak;
}

static int between(int value,int a,int b) { return value>=(a<b?a:b) && value<=(a>b?a:b); }
static int road_at(int a,int b) {
    if ((abs(b)<=2 && abs(a)<=21) || (abs(a)<=2 && between(b,-20,7))) return 1;
    for (int i=0;i<VILLAGE_HOMES;++i) {
        Home h=homes[i];
        if (abs(a-h.a)<=1 && between(b,0,h.b+entry_side(h)*h.depth)) return 1;
    }
    return abs(a)<=1 && b==8;
}

static int village_column(const VillagePlan *p,uint8_t *cells,int column,int top,int a,int b,int smith_loot,TerrainWide *wide) {
    uint8_t *c=cells+column*WY;
    int y=p->floor,desert=p->style==BIOME_DESERT,stone=desert ? SANDSTONE : COBBLESTONE;
    if (abs(a)>VILLAGE_RADIUS || abs(b)>VILLAGE_RADIUS) return -1;
    /* The innermost 3x3 well stays encased, including its bottom. */
    if (abs(a-5)<=1 && abs(b)<=1) {
        prepare(c,top,y,stone);
        if (c[y-3]!=BEDROCK) c[y-3]=(uint8_t)stone;
        for (int i=-2;i<=0;++i) if (c[y+i]!=BEDROCK) c[y+i]=(uint8_t)(a==5 && !b ? WATER : stone);
        int rim=a!=5 || b!=0;
        if (rim) c[y+1]=(uint8_t)stone;
        return y+rim;
    }
    for (int n=0;n<VILLAGE_HOMES;++n) {
        int roof=house_column(cells,column,top,y,a,b,n,p->style,smith_loot,p->turn,wide);
        if (roof>=0) return roof;
    }
    if (abs(a)<=5 && between(b,9,16)) {
        prepare(c,top,y,stone);
        c[y-1]=(uint8_t)stone;
        if (abs(a)==5 || b==9 || b==16) { c[y]=(uint8_t)stone; return y; }
        if (a==0) { c[y]=WATER; return y; }
        c[y]=FARM_BASE+7;
        c[y+1]=(uint8_t)(abs(a)<=2 ? WHEAT_BASE+7 : a<0 ? POTATO_BASE+7 : CARROT_BASE+7);
        return y+1;
    }
    if (road_at(a,b)) {
        /* Feather the ends toward solid terrain; interiors stay accessible. */
        if (abs(a)>=18 || b<=-20) {
            int ground=top;
            while (ground>0 && ground>y-9 && (cell_type(c[ground])==LEAF || c[ground]==TALL_GRASS || c[ground]==WOOD ||
                c[ground]==ACACIA_LOG)) --ground;
            int step=abs(a)>=18 ? abs(a)-17 : 2;
            int offset=ground-y;
            if (offset>step) offset=step;
            if (offset<-step) offset=-step;
            y+=offset;
        }
        prepare(c,top,y,stone);
        c[y]=(uint8_t)(desert ? SANDSTONE : GRAVEL);
        if (a==2 && (b==5 || b==-5)) { c[y+1]=TORCH; return y+1; }
        return y;
    }
    return -1;
}

void village_generate_chunk(const VillagePlan *p,int cx,int cz,uint8_t *cells,uint8_t *tops,int smith_loot,TerrainWide *wide) {
    int x0=cx*CHUNK_SIZE-p->x,z0=cz*CHUNK_SIZE-p->z;
    if (x0>VILLAGE_RADIUS || x0+CHUNK_SIZE-1< -VILLAGE_RADIUS || z0>VILLAGE_RADIUS || z0+CHUNK_SIZE-1< -VILLAGE_RADIUS) return;
    for (int x=0;x<CHUNK_SIZE;++x) for (int z=0;z<CHUNK_SIZE;++z) {
        int a,b,col=x*CHUNK_SIZE+z;
        rotate((4-p->turn)&3u,x0+x,z0+z,&a,&b);
        int top=village_column(p,cells,col,tops[col],a,b,smith_loot,wide);
        if (top>=0) tops[col]=(uint8_t)top;
    }
}
