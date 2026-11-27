#ifndef VOXEL_WORLD_INTERNAL_H
#define VOXEL_WORLD_INTERNAL_H

#include "flat.h"
#include "temples.h"
#include "desert_pyramids.h"
#include "windcut.h"
#include "rain_garden.h"

/* GPU meshes outlive CPU chunks. Keep this cache modest for the mobile runtime. */
enum { CHUNK_CACHE = 128, CHUNK_CELLS=CHUNK_SIZE*CHUNK_SIZE*WY, CHUNK_COLUMNS=CHUNK_SIZE*CHUNK_SIZE };
enum { LIGHT_BAND_HEIGHT=16, LIGHT_BANDS=WY/LIGHT_BAND_HEIGHT };
typedef struct {
    uint64_t seed;
    int generation;
    Dimension dimension;
    WorldProfile profile;
    int structures_disabled;
    FlatLayout flat;
} TerrainKey;
#define TERRAIN_KEY(seed_,generation_,dimension_,profile_,disabled_) \
    (TerrainKey){(seed_),(generation_),(dimension_),(profile_),(disabled_),{0}}
/* Keep the historical overlay budget for old worlds. A new chunk can add
   at most one roof block per column, plus walkways and a tower ladder. */
enum { TERRAIN_WIDE_LIMIT=128, TERRAIN_WIDE_ARCHITECTURE_LIMIT=512 };
typedef struct { uint16_t index,cell; } TerrainWideCell;
typedef struct TerrainWide { unsigned count; TerrainWideCell cell[TERRAIN_WIDE_ARCHITECTURE_LIMIT]; } TerrainWide;
int terrain_build_generation(void);
static inline unsigned terrain_wide_budget(void) {
    return terrain_build_generation()>=GENERATOR_ARCHITECTURE ? TERRAIN_WIDE_ARCHITECTURE_LIMIT : TERRAIN_WIDE_LIMIT;
}
void terrain_build_shape(uint8_t *cells,int index,int raw,TerrainWide *wide);
enum { TERRAIN_DEVICE_LIMIT=25 };
/* Devices are staged separately from geometry, then authored near the
   player on the game thread so existing circuit and save indexes own them. */
typedef struct { unsigned count; TerrainWideCell cell[TERRAIN_DEVICE_LIMIT]; } TerrainDevices;
typedef struct {
    int x,z,reused;
    uint8_t cells[CHUNK_CELLS],tops[CHUNK_COLUMNS],biomes[CHUNK_COLUMNS];
    int16_t sky_tops[CHUNK_COLUMNS];
    TerrainColumnCover cover[CHUNK_COLUMNS];
    uint16_t emitters[LIGHT_BANDS];
    unsigned emitter_mask;
    TerrainWide wide;
    TerrainDevices devices;
} TerrainResult;
unsigned hash_for_seed(uint64_t seed,int x,int z);
void terrain_generate(TerrainKey key,int cx,int cz,uint8_t *cells,uint8_t *tops,uint8_t *biomes);
unsigned light_summarize(const uint8_t *cells,const uint8_t *tops,int16_t *sky_tops,
                         uint16_t *emitters,TerrainColumnCover *cover);
void terrain_prepare(TerrainKey key,int cx,int cz,TerrainResult *out);
/* Worker-owned; epoch changes drop the old seed/dimension/profile. */
void terrain_history_prepare(TerrainKey key,unsigned epoch,int x,int z,TerrainResult *out);
void terrain_history_select_epoch(unsigned epoch);
void terrain_history_clear(void);
void temple_generate_chunk(TemplePlan plan,uint64_t seed,int cx,int cz,uint8_t *cells,uint8_t *tops,
    TerrainWide *wide,TerrainDevices *devices);
void desert_pyramid_generate_chunk(DesertPyramidPlan plan,int cx,int cz,uint8_t *cells,uint8_t *tops,
    TerrainWide *wide,TerrainDevices *devices);
void windcut_generate_chunk(WindcutPlan plan,uint64_t seed,int cx,int cz,uint8_t *cells,
    uint8_t *tops,TerrainWide *wide,TerrainDevices *devices);
void rain_garden_generate_chunk(RainGardenPlan plan,uint64_t seed,int cx,int cz,uint8_t *cells,
    uint8_t *tops,TerrainWide *wide,TerrainDevices *devices);
unsigned long long world_light_revision(int cx,int cz);
int terrain_worker_start(void);
void terrain_worker_stop(void);
int terrain_worker_running(void);
void terrain_worker_reset(TerrainKey key);
void terrain_worker_center(int x,int z,int hx,int hz);
void terrain_worker_request(int x,int z,int urgent);
/* Game-thread-only scratch. Consume before another successful take,
   immediate generation, or worker restart. Never pass it to a mesh worker. */
const TerrainResult *terrain_worker_take(void);
const TerrainResult *terrain_prepare_immediate(TerrainKey key,int x,int z);
int world_write_cell(int x, int y, int z, int cell);
int world_circuit_near(int x,int z);
void world_activate_terrain_devices(int cx,int cz,int radius);
/* Visit authored piston bases close enough for a changed line to reach them. */
void world_visit_piston_near(int x,int z,WorldRecord visit,void *ctx);
int world_has_pistons(void);
void world_touch_circuit_mesh(int x,int z);
/* OR of 1 = a plate and 2 = a plate that accepts nonliving entities. */
int world_contact_sensors_near(int cx,int cz,int radius);
int world_pressure_cell(int x,int y,int z);
int world_has_wooden_buttons(void);
int world_streaming_active(void);
void water_reset(void);
void water_notify_edit(int x, int y, int z, int before, int after);
void water_chunk_installed(int cx,int cz);

#endif
