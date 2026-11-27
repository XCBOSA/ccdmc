#include "navigation.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAP_RADIUS=32, MAP_BUDGET=160, FULL_SWEEP=(MAP_RADIUS*2+1)*(MAP_RADIUS*2+1) };
enum { MAP_UNSEEN,MAP_GRASS,MAP_FOLIAGE,MAP_SOIL,MAP_STONE,MAP_SAND,MAP_WATER,MAP_SNOW,
    MAP_WOOD,MAP_NETHER,MAP_LAVA,MAP_RED,MAP_CLAY,MAP_END,MAP_ICE,MAP_GRAY,MAP_BLACK,
    MAP_WHITE,MAP_GOLD,MAP_PURPLE,MAP_CYAN,MAP_ORANGE,MAP_PALETTE };
static const uint32_t palette[MAP_PALETTE]={
    0xe5cb97,0x7eb044,0x4a7a27,0xa18164,0x88878a,0xe1ce94,0x496ebd,0xf2f5f0,
    0xa17d50,0x7f4143,0xf57a20,0xc33c34,0xa5a8ba,0xe3e2a0,0xa1bde0,0x62636c,
    0x302a32,0xe8e3d4,0xe2ba55,0x9784aa,0x65b4bb,0xc6774a
};
typedef struct { int x,z; Dimension dimension; unsigned scale,explored; uint64_t revision; uint8_t cells[MAP_PIXELS]; } Map;
typedef struct { int8_t x,z; } Offset;
static Map *maps[MAP_LIMIT];
static unsigned used,cursor,last_id;
static uint64_t revision;
static float elapsed,spin;
static Offset sweep[FULL_SWEEP];
static unsigned sweep_count;

static int down_div(int a,int b) { return a/b-(a%b<0); }
static Map *lookup(unsigned id) { return id && id<=used ? maps[id-1] : NULL; }
unsigned map_count(void) { return used; }
int map_view(unsigned id,MapView *out) {
    const Map *m=lookup(id);
    if (!m) return 0;
    if (out) *out=(MapView){m->x,m->z,m->dimension,m->scale,m->cells,m->explored,m->revision};
    return 1;
}
void navigation_reset(void) {
    for (unsigned i=0;i<used;++i) { free(maps[i]); maps[i]=NULL; }
    used=cursor=last_id=0; elapsed=spin=0;
    ++revision;
}
static unsigned create(int x,int z,Dimension dim,unsigned scale,const uint8_t *pixels) {
    if (used>=MAP_LIMIT || (unsigned)dim>=DIMENSION_COUNT || scale>MAP_MAX_SCALE ||
        x< -WORLD_SAVE_LIMIT || x>WORLD_SAVE_LIMIT || z< -WORLD_SAVE_LIMIT || z>WORLD_SAVE_LIMIT ||
        x%MAP_SIDE || z%MAP_SIDE) return 0;
    Map *m=(Map *)calloc(1,sizeof *m);
    if (!m) return 0;
    if (pixels) {
        for (int i=0;i<MAP_PIXELS;++i) {
            if ((pixels[i]>0 && pixels[i]<4) || pixels[i]>=MAP_PALETTE*4) { free(m); return 0; }
            m->explored+=pixels[i]!=0;
        }
        memcpy(m->cells,pixels,sizeof m->cells);
    }
    m->x=x; m->z=z; m->dimension=dim; m->scale=scale; m->revision=++revision;
    maps[used++]=m;
    return used;
}
int map_restore(int x,int z,Dimension dim,unsigned scale,const uint8_t colors[MAP_PIXELS]) {
    return colors && create(x,z,dim,scale,colors);
}
int map_can_zoom(unsigned id) {
    const Map *m=lookup(id);
    return used<MAP_LIMIT && m && m->scale<MAP_MAX_SCALE;
}
unsigned map_zoom(unsigned id) {
    const Map *m=lookup(id);
    return map_can_zoom(id) ? create(m->x,m->z,m->dimension,m->scale+1,NULL) : 0;
}
void map_rollback(unsigned id) {
    if (id && id==used) { free(maps[--used]); maps[used]=NULL; ++revision; }
}
static int map_center(float v) {
    int cell=down_div((int)floorf(v)+MAP_SIDE/2,MAP_SIDE)*MAP_SIDE;
    int max=WORLD_SAVE_LIMIT/MAP_SIDE*MAP_SIDE;
    return cell < -max ? -max : cell>max ? max : cell;
}
const char *map_activate(void) {
    if ((unsigned)selected_slot>=HOTBAR_SIZE) return NULL;
    ItemStack *held=&inventory[selected_slot];
    if (held->type!=EMPTY_MAP || !held->count) return NULL;
    unsigned id=create(map_center(px),map_center(pz),world_dimension(),0,NULL);
    if (!id) return "此世界的地图已达上限，或内存不足";
    ItemStack drawn={FILLED_MAP,1,0,0,id};
    /* A lone map is replaced in place even in creative mode, so the
       player sees it immediately; creative can always pick a new blank. */
    if (held->count==1) *held=drawn;
    else if (!inventory_fill_container(selected_slot,drawn)) {
        map_rollback(id);
        return "背包没有位置放新地图";
    }
    return held->type==FILLED_MAP ? "已绘制地图 · 手持时自动探索" : "已绘制地图 · 请从背包选中新地图";
}

static int tint(int raw) {
    int type=cell_type(raw);
    if (type==AIR || type==GLASS || type==GLASS_PANE || type==IRON_BARS ||
        (!solid(type) && type!=WATER && type!=LAVA && type!=SNOW_LAYER)) return MAP_UNSEEN;
    if (stained_glass_cell(raw)) return MAP_UNSEEN;
    if (is_log(type) || type==PLANKS || type==BOOKSHELF || type==CRAFTING_TABLE ||
        type==MUSHROOM_STEM || type==BROWN_MUSHROOM_BLOCK) return MAP_WOOD;
    if (fence_type(type)) return MAP_WOOD;
    switch (type) {
    case GRASS: return MAP_GRASS;
    case LEAF: case CACTUS: return MAP_FOLIAGE;
    case DIRT: case PODZOL: case FARMLAND: case ASH_SAND: return MAP_SOIL;
    case RED_SAND: case TERRACOTTA: return MAP_ORANGE;
    case SAND: case SANDSTONE: return MAP_SAND;
    case WATER: return MAP_WATER;
    case LAVA: return MAP_LAVA;
    case SNOW: case SNOW_LAYER: return MAP_SNOW;
    case ICE: case PACKED_ICE: return MAP_ICE;
    case CLAY: return MAP_CLAY;
    case MYCELIUM: return MAP_PURPLE;
    case LAPIS_BLOCK: case DIAMOND_ORE: return MAP_CYAN;
    case RED_MUSHROOM_BLOCK: case REDSTONE_BLOCK: case TNT_BLOCK: return MAP_RED;
    case NETHER_BRICKS: case NETHERRACK: return MAP_NETHER;
    case OBSIDIAN: case BEDROCK: case ENDER_CHEST: return MAP_BLACK;
    case GLOWSTONE: case GOLD_ORE: return MAP_GOLD;
    case WOOL: return MAP_WHITE;
    case GRAVEL: case COBBLESTONE: return MAP_GRAY;
    default: return MAP_STONE;
    }
}
uint32_t map_rgb(uint8_t color) {
    unsigned p=color>>2;
    if (!p || p>=MAP_PALETTE) return palette[0];
    static const unsigned shade[]={177,220,255,137};
    uint32_t c=palette[p],f=shade[color&3u];
    return (((c>>16)*f/255u)<<16)|((((c>>8)&255u)*f/255u)<<8)|((c&255u)*f/255u);
}
static int column(int x,int z,int *height) {
    int top,raw;
    if (!world_resident_top(x,z,&top)) return -1;
    for (int y=top;y>=0;--y) {
        if (!world_resident_cell(x,y,z,&raw)) return -1;
        int color=tint(raw);
        if (color) { if (height) *height=y; return color; }
    }
    return MAP_UNSEEN;
}
static uint8_t survey(const Map *m,int u,int v) {
    int span=1<<m->scale;
    int x=m->x+(u-MAP_SIDE/2)*span,z=m->z+(v-MAP_SIDE/2)*span;
    if (x< -WORLD_SAVE_LIMIT || x>=WORLD_SAVE_LIMIT || z< -WORLD_SAVE_LIMIT || z>=WORLD_SAVE_LIMIT) return 0;
    if (m->dimension==NETHER) {
        int unreferenced;
        if (!world_resident_top(x,z,&unreferenced)) return 0;
        unsigned pattern=hash2(x*3,z*3);
        return (uint8_t)(MAP_NETHER*4+(pattern%7u==0 ? 1 : 0));
    }
    int y=0,north=0;
    int mid=span/2;
    int color=column(x+mid,z+mid,&y);
    if (color<=0) return 0;
    /* At wider scales take a five-column vote instead of visiting up
       to 256 blocks for each pixel. Missing samples don't invent land. */
    if (span>=4) {
        int votes[MAP_PALETTE]={0}; votes[color]=1;
        for (int a=0;a<span;a+=span-1) for (int b=0;b<span;b+=span-1) {
            int c=column(x+a,z+b,NULL);
            if (c>0) ++votes[c];
        }
        for (int c=1;c<MAP_PALETTE;++c) if (votes[c]>votes[color]) color=c;
    }
    int before=column(x+mid,z+mid-span,&north);
    int shade=before>0 ? (y>north ? 2 : y<north ? 0 : 1) : 1;
    if (color==MAP_WATER) {
        int raw,depth=0;
        while (depth<9 && world_resident_cell(x+mid,y-depth,z+mid,&raw) && cell_type(raw)==WATER) ++depth;
        shade=depth>=8 ? 0 : depth>=4 ? 1 : 2;
    }
    return (uint8_t)(color*4+shade);
}
static void prepare_sweep(void) {
    if (sweep_count) return;
    for (int r=0;r<=MAP_RADIUS;++r) for (int z=-r;z<=r;++z) for (int x=-r;x<=r;++x)
        if ((abs(x)==r || abs(z)==r) && x*x+z*z<=MAP_RADIUS*MAP_RADIUS)
            sweep[sweep_count++]=(Offset){(int8_t)x,(int8_t)z};
}
static int pos(float v,int center,unsigned scale) {
    return down_div((int)floorf(v)-center,1<<scale)+MAP_SIDE/2;
}
void navigation_step(float dt) {
    if (!(dt>0.f && dt<=.1f)) return;
    spin+=dt;
    if (spin>=32.f) spin-=32.f;
    unsigned id=(unsigned)selected_slot<HOTBAR_SIZE ? map_item_id(inventory[selected_slot]) : 0;
    Map *m=lookup(id);
    if (!m || world_dimension()!=m->dimension) { last_id=0; return; }
    if (id!=last_id) { cursor=0; elapsed=.05f; last_id=id; }
    elapsed+=dt;
    if (elapsed<.05f) return;
    elapsed=fminf(.05f,elapsed-.05f);
    int x=pos(px,m->x,m->scale),z=pos(pz,m->z,m->scale);
    if (x< -MAP_RADIUS || x>=MAP_SIDE+MAP_RADIUS || z< -MAP_RADIUS || z>=MAP_SIDE+MAP_RADIUS) return;
    prepare_sweep();
    unsigned changed=0;
    for (int j=0;j<MAP_BUDGET;++j) {
        Offset s=sweep[cursor++];
        if (cursor==sweep_count) cursor=0;
        int u=x+s.x,v=z+s.z;
        if ((unsigned)u>=MAP_SIDE || (unsigned)v>=MAP_SIDE) continue;
        uint8_t color=survey(m,u,v),*at=&m->cells[v*MAP_SIDE+u];
        if (!color || *at==color) continue;
        m->explored+=*at==0; *at=color; changed=1;
    }
    if (changed) m->revision=++revision;
}
static int angle_step(float radians,int n) {
    return ((int)floorf(remainderf(radians,6.283185307f)*(n/6.283185307f)+.5f)+n)%n;
}
MapMarker map_marker(MapView m) {
    if (m.dimension!=world_dimension() || m.scale>MAP_MAX_SCALE) return (MapMarker){0};
    int x=pos(px,m.x,m.scale),z=pos(pz,m.z,m.scale);
    int inside=(unsigned)x<MAP_SIDE && (unsigned)z<MAP_SIDE;
    if (x< -MAP_SIDE || x>=2*MAP_SIDE || z< -MAP_SIDE || z>=2*MAP_SIDE) return (MapMarker){0};
    if (x<1) x=1;
    if (x>=MAP_SIDE-1) x=MAP_SIDE-2;
    if (z<1) z=1;
    if (z>=MAP_SIDE-1) z=MAP_SIDE-2;
    int heading=m.dimension==NETHER ? ((int)(spin*18.f))&15 : angle_step(yaw,16);
    return (MapMarker){x,z,heading,inside ? 1 : 2};
}
int navigation_compass_step(void) {
    if (world_dimension()!=OVERWORLD) return ((int)(spin*18.f))&7;
    Vec3 spawn=player_world_spawn();
    if (fabsf(spawn.x-px)+fabsf(spawn.z-pz)<1.f) return -1;
    return angle_step(atan2f(spawn.x-px,pz-spawn.z)-yaw,8);
}
int navigation_clock_minutes(void) {
    if (world_dimension()!=OVERWORLD) return -1;
    return (int)(((player_rules.day_tick+6000u)%DAY_TICKS)*1440u/DAY_TICKS);
}
void navigation_status(char *out,size_t n) {
    if (!out || !n) return;
    out[0]=0;
    if ((unsigned)selected_slot>=HOTBAR_SIZE) return;
    ItemStack held=inventory[selected_slot];
    if (!held.count) return;
    if (held.type==COMPASS_ITEM) {
        static const char *const arrow[]={"↑","↗","→","↘","↓","↙","←","↖"};
        int d=navigation_compass_step();
        if (world_dimension()!=OVERWORLD) snprintf(out,n,"指南针 %s · 此维度不可靠",arrow[d]);
        else if (d<0) snprintf(out,n,"指南针 · 已到世界出生点");
        else snprintf(out,n,"指南针 %s 世界出生点",arrow[d]);
    } else if (held.type==CLOCK_ITEM) {
        int clock=navigation_clock_minutes();
        if (clock<0) snprintf(out,n,"时钟 · 此维度日夜不明");
        else snprintf(out,n,"时钟 · %02d:%02d",clock/60,clock%60);
    } else if (held.type==EMPTY_MAP) snprintf(out,n,"空地图 · 轻点绘制");
    else if (held.type==FILLED_MAP) {
        MapView m;
        unsigned id=map_item_id(held);
        if (map_view(id,&m)) {
            if (m.dimension==world_dimension()) snprintf(out,n,"地图 #%u · 1:%u",id,1u<<m.scale);
            else snprintf(out,n,"地图 #%u · 此维度不绘制",id);
        }
    }
}
