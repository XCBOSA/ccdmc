#include "field_supplies.h"
#include "armor.h"
#include "beds.h"
#include "sound.h"
#include "weather.h"
#include "../world/light.h"
#include <math.h>

static FieldSave supplies={FIELD_WATER_MAX,0,0};
static unsigned until_weather;
static int last_dimension=-1,exposed,cached_sun;

int field_enabled(void) {
    return player_rules.mode==SURVIVAL && game_effective_difficulty()!=PEACEFUL;
}
int field_in_sun(void) { return field_enabled() && exposed; }
FieldSave field_record(void) { return supplies; }
int field_restore(FieldSave saved) {
    if (saved.water>FIELD_WATER_MAX || saved.heat>FIELD_HEAT_MAX || saved.drain>=FIELD_DRAIN_UNIT ||
        (!saved.water && saved.drain)) return 0;
    supplies=saved;
    until_weather=0;
    last_dimension=-1;
    exposed=cached_sun=0;
    return 1;
}
void field_reset(void) { (void)field_restore((FieldSave){FIELD_WATER_MAX,0,0}); }
float field_stride_scale(void) {
    if (!field_enabled() || supplies.water>=FIELD_WATER_LOW) return 1.f;
    return .80f+supplies.water*.01f;
}
int field_allows_recovery(void) { return !field_enabled() || supplies.water>=FIELD_WATER_DRY; }

void field_apply_exposure(unsigned ticks,int moved,int immersed,int sun,int hat) {
    if (!field_enabled() || !player_rules.health || bed_sleeping()) return;
    if (ticks>20) ticks=20;
    exposed=sun && !immersed;
    while (ticks--) {
        int heat=supplies.heat;
        heat+=exposed ? (hat ? 1 : 2) : immersed ? -4 : -2;
        supplies.heat=(uint8_t)(heat<0 ? 0 : heat>FIELD_HEAT_MAX ? FIELD_HEAT_MAX : heat);
        unsigned cost=immersed ? (moved ? 3u : 0u) : moved ? 1u : 0u;
        if (!immersed && supplies.heat>=24) cost+=hat ? 1u : 2u;
        if (!immersed && supplies.heat>=60) cost+=hat ? 1u : 2u;
        if (!supplies.water) continue;
        supplies.drain=(uint16_t)(supplies.drain+cost);
        if (supplies.drain>=FIELD_DRAIN_UNIT) {
            supplies.drain=(uint16_t)(supplies.drain-FIELD_DRAIN_UNIT);
            if (!--supplies.water) supplies.drain=0;
        }
    }
}

void field_advance(unsigned ticks,int moved,int immersed) {
    if (!ticks || !field_enabled() || !player_rules.health || bed_sleeping()) return;
    int dimension=world_dimension();
    /* Terrain climate and skylight are sampled once per game second. Do
       not load a chunk just to calculate a player status indicator. */
    if (last_dimension!=dimension || ticks>=until_weather) {
        last_dimension=dimension;
        until_weather=20;
        const int x=(int)floorf(px),y=(int)floorf(py+1.55f),z=(int)floorf(pz);
        const unsigned time=player_rules.day_tick;
        int hot_time=time>=1000 && time<11500;
        Biome b=dimension==OVERWORLD && hot_time && weather_kind()==WEATHER_CLEAR ?
            terrain_biome(x,z) : BIOME_COUNT;
        cached_sun=(b==BIOME_DESERT || b==BIOME_MESA || b==BIOME_SAVANNA) &&
            (y>=WY || light_has_open_sky_resident(x,y,z));
    } else until_weather-=ticks;
    field_apply_exposure(ticks,moved,immersed,cached_sun,armor_worn(ARMOR_HEAD).type==LEATHER_HELMET);
}

const char *field_use_water(ItemStack *held,int aimed_at_source) {
    if (!held || held->count!=1) return NULL;
    int type=held->type;
    if (!field_flask_item(type) && type!=WATER_BOTTLE) return NULL;
    if (type!=WATER_BOTTLE && aimed_at_source) {
        if (type==FIELD_FLASK_FULL) return "水囊已满 · 移开准星后轻点饮用";
        held->type=FIELD_FLASK_FULL;
        sound_play(SOUND_SPLASH,.30f,1,(Vec3){px,py+1.f,pz});
        return "水囊装满 · 还可饮用 3 次";
    }
    if (!field_enabled()) return "和平或创造模式不需要饮水";
    if (type==FIELD_FLASK_EMPTY) return "空水囊 · 对准静止水源、集雨罐或滤苔床装水";
    if (supplies.water>=FIELD_WATER_MAX) return "水分已满，未消耗";
    unsigned after=supplies.water+(type==WATER_BOTTLE ? 14u : 26u);
    supplies.water=(uint8_t)(after>FIELD_WATER_MAX ? FIELD_WATER_MAX : after);
    if (type==WATER_BOTTLE) {
        *held=(ItemStack){GLASS_BOTTLE,1,0,0,0};
        return "已喝水 · 玻璃瓶可重复使用";
    }
    --held->type;
    return held->type==FIELD_FLASK_TWO ? "已饮用 · 水囊还剩 2 口" :
        held->type==FIELD_FLASK_ONE ? "已饮用 · 水囊还剩 1 口" : "已饮用 · 水囊已空";
}
