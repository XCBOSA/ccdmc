#ifndef VOXEL_WEATHER_H
#define VOXEL_WEATHER_H

#include "game.h"

typedef enum { WEATHER_CLEAR, WEATHER_RAIN, WEATHER_THUNDER } WeatherKind;
typedef enum { PRECIP_NONE, PRECIP_RAIN, PRECIP_SNOW } Precipitation;
enum { WEATHER_FILE_BYTES=13, WEATHER_FADE_TICKS=80, WEATHER_FLASH_TICKS=4 };
typedef struct {
    uint32_t random, remaining;
    uint16_t strike, cover;
    uint8_t kind;
} WeatherSave;
typedef struct { Vec3 point; unsigned ticks; } LightningView;

void weather_reset(uint32_t seed);
WeatherSave weather_record(void);
int weather_restore(WeatherSave saved);
WeatherKind weather_kind(void);
float weather_intensity(void);
unsigned weather_frame(void);
LightningView weather_lightning(void);
void weather_step(int x,int z,unsigned ticks);
void weather_clear_at_dawn(void);
Precipitation weather_climate(Biome biome,int y);
Precipitation weather_column(int x,int z,int *ground);
int weather_wets(int x,int y,int z);
int weather_strike(int x,int z);

#endif
