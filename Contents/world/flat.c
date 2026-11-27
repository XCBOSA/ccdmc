#include "flat.h"
#include <string.h>

const FlatPreset flat_presets[FLAT_PRESETS]={
    {"营地平原","bedrock,3*stone,4*loam,turf | meadow | settlements"},
    {"深层采掘场","bedrock,124*stone,5*gravel,5*loam,turf | upland | archives,tunnels,flora"},
    {"浅湾航道","bedrock,7*stone,6*clay,6*sand,32*water | coast"},
    {"河谷农场","bedrock,42*stone,7*loam,turf | meadow | settlements,flora,pools"},
    {"寒地试验站","bedrock,37*stone,4*clay,3*loam,turf,2*snow | tundra | settlements"},
    {"架空工地","bedrock,7*air,6*rubble | meadow"},
    {"层积旱原","bedrock,6*stone,36*sandstone,6*sand | dryland | settlements,outposts,tunnels"},
    {"陶土工场","bedrock,11*stone,12*clay,8*ceramic | dryland"}
};
/* An unset layout is a compact internal default. A serialized layout stores
   the material bytes, never the spelling of its input program. */
static const FlatLayout base_layout={4,BIOME_PLAINS,FLAT_VILLAGE,{BEDROCK,DIRT,DIRT,GRASS}};
typedef struct { const char *name; int value; } LayerWord;
static const LayerWord blocks[]={
    {"air",AIR},{"stone",STONE},{"turf",GRASS},{"loam",DIRT},{"humus",PODZOL},{"rubble",COBBLESTONE},
    {"timber",PLANKS},{"bedrock",BEDROCK},{"water",WATER},{"lava",LAVA},{"sand",SAND},{"oxide-sand",RED_SAND},
    {"gravel",GRAVEL},{"gold-vein",GOLD_ORE},{"iron-vein",IRON_ORE},{"coal-seam",COAL_ORE},
    {"oak",WOOD},{"spruce",SPRUCE_LOG},{"birch",BIRCH_LOG},{"tropical-wood",JUNGLE_LOG},
    {"sandstone",SANDSTONE},{"wool",WOOL},{"mossy-stone",MOSS_STONE},{"volcanic-glass",OBSIDIAN},
    {"diamond-vein",DIAMOND_ORE},{"conductive-vein",REDSTONE_ORE},{"snow",SNOW},{"ice",ICE},{"clay",CLAY},
    {"scoria",NETHERRACK},{"ash-sand",ASH_SAND},{"luminous-aggregate",GLOWSTONE},{"masonry",STONE_BRICKS},
    {"fungal-soil",MYCELIUM},{"firebrick",NETHER_BRICKS},{"pale-rock",STONE},
    {"quartz-vein",NETHER_QUARTZ_ORE},
    {"ceramic",TERRACOTTA},{"acacia",ACACIA_LOG},{"dark-oak",DARK_OAK_LOG},{"glacier-ice",PACKED_ICE}
};
static const LayerWord biomes[]={
    {"ocean",BIOME_OCEAN},{"meadow",BIOME_PLAINS},{"dryland",BIOME_DESERT},{"upland",BIOME_MOUNTAINS},
    {"forest",BIOME_FOREST},{"pinewoods",BIOME_TAIGA},{"marsh",BIOME_SWAMP},{"river",BIOME_RIVER},
    {"ice-river",BIOME_FROZEN_RIVER},{"tundra",BIOME_ICE_PLAINS},{"fungal-grove",BIOME_MUSHROOM},
    {"coast",BIOME_BEACH},{"rainforest",BIOME_JUNGLE},{"deep-water",BIOME_DEEP_OCEAN},
    {"cliff-coast",BIOME_STONE_SHORE},{"birchwood",BIOME_BIRCH},{"canopy",BIOME_ROOFED},
    {"snow-pine",BIOME_COLD_TAIGA},{"old-growth",BIOME_MEGA_TAIGA},{"savanna",BIOME_SAVANNA},
    {"badlands",BIOME_MESA},{"ice-spires",BIOME_ICE_SPIKES}
};
static const LayerWord features[]={
    {"settlements",FLAT_VILLAGE},{"flora",FLAT_DECORATION},{"archives",FLAT_STRONGHOLD},
    {"outposts",FLAT_BIOME_STRUCTURES},{"tunnels",FLAT_MINESHAFT},{"burrows",FLAT_DUNGEON},
    {"pools",FLAT_LAKE},{"hot-pools",FLAT_LAVA_LAKE}
};
#define LENGTH(a) (sizeof(a)/sizeof((a)[0]))

static int number(const char **cursor) {
    const char *p=*cursor;
    if (*p<'0' || *p>'9') return -1;
    int n=0;
    do {
        n=n*10+(*p++-'0');
        if (n>4095) return -1;
    } while (*p>='0' && *p<='9');
    *cursor=p;
    return n;
}
static void skip_spaces(const char **cursor) {
    while (**cursor==' ' || **cursor=='\t') ++*cursor;
}
static int word(const char **cursor,const LayerWord *table,size_t count) {
    skip_spaces(cursor);
    const char *start=*cursor,*p=start;
    while ((*p>='a' && *p<='z') || *p=='-') ++p;
    size_t length=(size_t)(p-start);
    if (!length) return -1;
    *cursor=p;
    skip_spaces(cursor);
    for (size_t i=0;i<count;++i)
        if (strlen(table[i].name)==length && !memcmp(start,table[i].name,length)) return table[i].value;
    return -1;
}
const char *flat_parse(const char *code,FlatLayout *out) {
    if (!out) return "无法读取地层。";
    if (!code || !*code) { *out=(FlatLayout){0}; return NULL; }
    size_t bytes=0;
    while (bytes<=FLAT_CODE_MAX && code[bytes]) ++bytes;
    if (bytes>FLAT_CODE_MAX) return "地层代码太长。";
    const char *p=code;
    FlatLayout result={0};
    result.biome=BIOME_PLAINS;
    for (;;) {
        skip_spaces(&p);
        int count=1;
        if (*p>='0' && *p<='9') {
            count=number(&p);
            skip_spaces(&p);
            if (*p!='*') return "厚度后需要 *，例如 4*loam。";
            ++p;
        }
        if (count<1 || count>FLAT_MAX_HEIGHT-result.height)
            return "每层至少一格，总高度最多 253 格。";
        int block=word(&p,blocks,LENGTH(blocks));
        if (block<0) return "无法识别材料。请从下到上填写，例如 bedrock,4*loam,turf。";
        memset(result.blocks+result.height,block,(size_t)count);
        result.height=(uint16_t)(result.height+count);
        if (*p!=',') break;
        ++p;
    }
    if (*p=='|') {
        ++p;
        int biome=word(&p,biomes,LENGTH(biomes));
        if (biome<0) return "无法识别地貌，例如 meadow、coast 或 dryland。";
        result.biome=(uint8_t)biome;
    }
    if (*p=='|') {
        ++p;
        for (;;) {
            int flag=word(&p,features,LENGTH(features));
            if (flag<0 || (result.features&flag)) return "未知或重复的生成特性。";
            result.features|=(uint8_t)flag;
            if (*p!=',') break;
            ++p;
        }
    }
    if (*p) return "地层代码中有多余字符，请检查分隔符。";
    while (result.height && result.blocks[result.height-1]==AIR) --result.height;
    if (!result.height) return "至少需要一层非空气方块。";
    *out=result;
    return NULL;
}
const FlatLayout *flat_effective(const FlatLayout *layout) {
    return layout && layout->height ? layout : &base_layout;
}
int flat_valid(const FlatLayout *layout) {
    if (!layout || !layout->height) return !layout || (!layout->biome && !layout->features);
    if (layout->height>FLAT_MAX_HEIGHT || layout->blocks[layout->height-1]==AIR) return 0;
    int valid=0;
    for (unsigned i=0;i<LENGTH(biomes);++i) if (layout->biome==biomes[i].value) valid=1;
    if (!valid) return 0;
    for (unsigned j=0;j<layout->height;++j) {
        /* Saved custom layouts record bytes, not the removed material keyword. */
        if (layout->blocks[j]==REDSTONE_BLOCK) continue;
        valid=0;
        for (unsigned i=0;i<LENGTH(blocks);++i) if (layout->blocks[j]==blocks[i].value) { valid=1; break; }
        if (!valid) return 0;
    }
    return 1;
}
int flat_equal(const FlatLayout *a,const FlatLayout *b) {
    a=flat_effective(a); b=flat_effective(b);
    return a->height==b->height && a->biome==b->biome && a->features==b->features &&
        !memcmp(a->blocks,b->blocks,a->height);
}
int flat_spawn_platform(const FlatLayout *layout,int x,int z) {
    if (x < -4 || x > 4 || z < -4 || z > 4) return 0;
    layout=flat_effective(layout);
    int top=layout->blocks[layout->height-1];
    return top==WATER || top==LAVA;
}
const char *flat_name(const FlatLayout *layout) {
    if (!layout || !layout->height) return "基础平地";
    for (unsigned i=0;i<FLAT_PRESETS;++i) {
        FlatLayout preset;
        if (!flat_parse(flat_presets[i].code,&preset) && flat_equal(&preset,layout)) return flat_presets[i].name;
    }
    return "自定义地层";
}
