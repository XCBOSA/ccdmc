#include "weather.h"
#include "actors.h"
#include "blast.h"
#include "fire.h"
#include "cistern.h"
#include "../world/light.h"
#include <math.h>

enum { WEATHER_MINIMUM=12000, WEATHER_MAXIMUM=180000, COVER_PERIOD=8,
       LIGHTNING_MINIMUM=160, LIGHTNING_MAXIMUM=1200 };
static WeatherSave weather={.random=1,.remaining=WEATHER_MINIMUM};
static LightningView bolt;
static unsigned fade,frame;

static unsigned next(void) {
    uint32_t r=weather.random;
    r^=r<<13; r^=r>>17; r^=r<<5;
    return weather.random=r;
}
static unsigned between(unsigned low,unsigned high) { return low+next()%(high-low+1); }
static void change(WeatherKind w) {
    weather.kind=(uint8_t)w;
    weather.remaining=between(WEATHER_MINIMUM,w==WEATHER_CLEAR ? WEATHER_MAXIMUM : 24000);
    weather.strike=w==WEATHER_THUNDER ? (uint16_t)between(LIGHTNING_MINIMUM,LIGHTNING_MAXIMUM) : 0;
    weather.cover=w==WEATHER_CLEAR ? 0 : COVER_PERIOD;
}
void weather_reset(uint32_t seed) {
    weather=(WeatherSave){.random=seed^0xa48a6623u};
    if (!weather.random) weather.random=1u;
    change(WEATHER_CLEAR);
    bolt=(LightningView){{0,0,0},0}; fade=frame=0;
}
WeatherSave weather_record(void) { return weather; }
int weather_restore(WeatherSave s) {
    if (!s.random || !s.remaining || s.kind>WEATHER_THUNDER ||
        s.remaining>(s.kind==WEATHER_CLEAR ? WEATHER_MAXIMUM : 24000) ||
        s.strike>LIGHTNING_MAXIMUM || ((s.kind==WEATHER_THUNDER)!=(s.strike!=0)) ||
        s.cover>COVER_PERIOD || ((s.kind!=WEATHER_CLEAR)!=(s.cover!=0))) return 0;
    weather=s;
    fade=s.kind!=WEATHER_CLEAR ? WEATHER_FADE_TICKS : 0;
    bolt=(LightningView){{0,0,0},0}; frame=0;
    return 1;
}
WeatherKind weather_kind(void) {
    return world_dimension()==OVERWORLD ? (WeatherKind)weather.kind : WEATHER_CLEAR;
}
float weather_intensity(void) {
    return world_dimension()==OVERWORLD ? fade/(float)WEATHER_FADE_TICKS : 0.f;
}
unsigned weather_frame(void) { return frame; }
LightningView weather_lightning(void) {
    return world_dimension()==OVERWORLD ? bolt : (LightningView){{0,0,0},0};
}
void weather_clear_at_dawn(void) {
    if (weather_kind()!=WEATHER_CLEAR) change(WEATHER_CLEAR);
    bolt.ticks=0;
}
Precipitation weather_climate(Biome biome,int y) {
    switch (biome) {
        case BIOME_MESA: case BIOME_SAVANNA: case BIOME_DESERT: case BIOME_NETHER: case BIOME_END:
            return PRECIP_NONE;
        case BIOME_FROZEN_RIVER: case BIOME_COLD_TAIGA: case BIOME_ICE_PLAINS: case BIOME_ICE_SPIKES:
            return PRECIP_SNOW;
        case BIOME_MOUNTAINS:
            return y>=96 ? PRECIP_SNOW : PRECIP_RAIN;
        default:
            return PRECIP_RAIN;
    }
}
Precipitation weather_column(int x,int z,int *ground) {
    if (world_dimension()!=OVERWORLD || (!fade && weather.kind==WEATHER_CLEAR)) return PRECIP_NONE;
    int y;
    Biome biome;
    if (!world_precipitation_surface(x,z,&y,&biome)) return PRECIP_NONE;
    if (ground) *ground=y;
    return weather_climate(biome,y+1);
}
int weather_wets(int x,int y,int z) {
    if (weather_kind()==WEATHER_CLEAR) return 0;
    int ground;
    return weather_column(x,z,&ground)==PRECIP_RAIN && y>ground;
}
int weather_strike(int x,int z) {
    int ground;
    if (weather_kind()!=WEATHER_THUNDER || weather_column(x,z,&ground)!=PRECIP_RAIN ||
        ground<0 || ground+1>=WY) return 0;
    bolt=(LightningView){{x+.5f,ground+1.f,z+.5f},WEATHER_FLASH_TICKS};
    if (block(x,ground,z)==TNT_BLOCK) blast_prime(x,ground,z);
    else fire_ignite(x,ground+1,z);
    actors_lightning(bolt.point);
    if (fabsf(px-bolt.point.x)<3.f && fabsf(pz-bolt.point.z)<3.f &&
        py>=bolt.point.y-1.f && py<bolt.point.y+6.f &&
        weather_wets((int)floorf(px),(int)floorf(py+1.f),(int)floorf(pz))) {
        player_take_damage(DAMAGE_DIRECT_FIRE,5);
        player_catch_fire(160);
    }
    return 1;
}
static int snow_layer(int x,int y,int z) {
    int raw,ground;
    if (y<1 || y>=WY || !world_resident_cell(x,y,z,&raw) || raw!=AIR ||
        !world_resident_cell(x,y-1,z,&ground)) return 0;
    int type=cell_type(ground);
    if (!solid(type) || type==ICE || type==PACKED_ICE || type==GLASS ||
        type==FARMLAND || type==HOPPER || type==ANVIL || type==ENCHANTING_TABLE) return 0;
    int light=block_light_at_resident(x,y,z);
    return light>=0 && light<10 && set_block(x,y,z,SNOW_LAYER);
}
static void cold_cover(int x,int z) {
    int y;
    if (weather_column(x,z,&y)!=PRECIP_SNOW || y<0 || y+1>=WY) return;
    int raw=world_cell(x,y,z);
    if (block(x,y+1,z)!=AIR) return;
    if (raw==WATER) {
        int light=block_light_at_resident(x,y,z);
        if (light>=0 && light<10) set_block(x,y,z,ICE);
    } else snow_layer(x,y+1,z);
}
void weather_step(int x,int z,unsigned ticks) {
    if (world_dimension()!=OVERWORLD) return;
    while (ticks--) {
        ++frame;
        if (bolt.ticks) --bolt.ticks;
        if (!--weather.remaining) {
            WeatherKind kind=weather.kind==WEATHER_CLEAR ? (next()%4u==0 ? WEATHER_THUNDER : WEATHER_RAIN) : WEATHER_CLEAR;
            change(kind);
        }
        if (weather.kind==WEATHER_CLEAR) { if (fade) --fade; continue; }
        if (fade<WEATHER_FADE_TICKS) ++fade;
        cistern_collect_rain(x,z,weather.remaining);
        if (!--weather.cover) {
            weather.cover=COVER_PERIOD;
            int dx=(int)between(0,48)-24, dz=(int)between(0,48)-24;
            cold_cover(x+dx,z+dz);
        }
        if (weather.strike && !--weather.strike) {
            weather.strike=(uint16_t)between(LIGHTNING_MINIMUM,LIGHTNING_MAXIMUM);
            for (int j=0;j<4;++j) {
                int dx=(int)between(0,56)-28, dz=(int)between(0,56)-28;
                if (dx*dx+dz*dz>=64 && weather_strike(x+dx,z+dz)) break;
            }
        }
    }
}
