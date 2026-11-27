#include "cistern.h"
#include "sound.h"
#include "weather.h"
#include <limits.h>

static int chunk_of(int coordinate) {
    return coordinate/CHUNK_SIZE-(coordinate%CHUNK_SIZE<0);
}
static int residue(int chunk) {
    int r=chunk%3;
    return r<0 ? r+3 : r;
}

void cistern_collect_rain(int x,int z,unsigned wet_ticks_left) {
    if (!world_has_cisterns() || weather_kind()==WEATHER_CLEAR) return;
    enum { SPACING=12 };
    const unsigned phase=wet_ticks_left%CISTERN_RAIN_PERIOD;
    if (phase%SPACING || phase/SPACING>=9) return;
    /* Color the 3×3 neighborhood by absolute chunk coordinates. One
       chunk and at most one bounded batch per slot, independent of how
       much ground has been explored. Use the saved weather clock so a
       restart or moving the neighborhood cannot repeat a time slot. */
    int cx=chunk_of(x),cz=chunk_of(z);
    const int ax=(int)(phase/SPACING/3u),az=(int)(phase/SPACING%3u);
    cx+=residue(cx-1)==ax ? -1 : residue(cx)==ax ? 0 : 1;
    cz+=residue(cz-1)==az ? -1 : residue(cz)==az ? 0 : 1;
    if (cx<INT_MIN/CHUNK_SIZE || cx>INT_MAX/CHUNK_SIZE ||
        cz<INT_MIN/CHUNK_SIZE || cz>INT_MAX/CHUNK_SIZE) return;
    BlockEdit batch[CISTERN_RAIN_BATCH];
    int n=world_cistern_batch(cx,cz,batch,CISTERN_RAIN_BATCH);
    for (int i=0;i<n;++i) {
        const BlockEdit e=batch[i];
        if (!cistern_cell(e.cell) || e.cell>=CISTERN_FULL) continue;
        int top;
        if (weather_column(e.x,e.z,&top)==PRECIP_RAIN && top==e.y)
            (void)world_set_cistern_level(e.x,e.y,e.z,e.cell,e.cell+1);
    }
}

const char *cistern_use(Hit hit) {
    int raw;
    if (!hit.hit || (unsigned)selected_slot>=HOTBAR_SIZE ||
        !world_resident_cell(hit.x,hit.y,hit.z,&raw) || !cistern_cell(raw)) return NULL;
    ItemStack *held=&inventory[selected_slot];
    int stored=raw-CISTERN_EMPTY;
    if (!held->count) {
        static const char *const levels[]={"集雨罐 · 空 · 露天降雨时蓄水",
            "集雨罐 · 1/3 口","集雨罐 · 2/3 口","集雨罐 · 3/3 口"};
        if (!stored && world_dimension()==OVERWORLD) {
            int top;
            Biome biome;
            if (world_precipitation_surface(hit.x,hit.z,&top,&biome)) {
                if (top>hit.y) return "集雨罐 · 空 · 顶部被遮住";
                if (weather_climate(biome,hit.y+1)!=PRECIP_RAIN) return "集雨罐 · 空 · 此地没有降雨";
            }
        }
        return levels[stored];
    }
    if (held->count!=1 || !field_flask_item(held->type)) return NULL;
    int space=FIELD_FLASK_FULL-held->type;
    if (!space) return "水囊已满 · 集雨罐未扣水";
    if (!stored) return "集雨罐没有储水";
    int taken=space<stored ? space : stored;
    if (!world_set_cistern_level(hit.x,hit.y,hit.z,raw,raw-taken)) return "暂时无法从集雨罐取水";
    held->type=(uint16_t)(held->type+taken);
    sound_play(SOUND_SPLASH,.32f,1,(Vec3){hit.x+.5f,hit.y+.7f,hit.z+.5f});
    static const char *const filled[]={"","已从集雨罐装入 1 口",
        "已从集雨罐装入 2 口","已从集雨罐装入 3 口"};
    return filled[taken];
}
