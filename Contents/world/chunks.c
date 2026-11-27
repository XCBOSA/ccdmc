#include "internal.h"
#include "lava.h"
#include "../game/villagers.h"
#include "rails.h"
#include "barriers.h"
#include "villages.h"
#include "redstone.h"
#include "light.h"
#include "terrain_cover.h"
#include "stream_priority.h"
#include "../game/brewing.h"
#include "../game/gravity.h"
#include "../game/hoppers.h"
#include "../game/droppers.h"
#include "../game/blast.h"
#include "../game/fire.h"
#include "../game/weather.h"
#include "../game/leaf_decay.h"
#include "../game/chests.h"
#include "../game/drops.h"
#include <stdlib.h>
#include <string.h>

/* A bounded cache holds generated chunks. In flight, favor the immediate
   neighborhood and forward corridor over distant LRU entries. Sparse edits
   live separately for both cache eviction and disk snapshots. */
enum { BUCKETS = 257, CELLS = CHUNK_SIZE * CHUNK_SIZE * WY, RESERVED_CHUNKS = 12 };
typedef struct {
    int used, x, z, next;
    unsigned long long age;
    unsigned char *cells;
    ChunkHigh *high;
    uint8_t tops[CHUNK_SIZE * CHUNK_SIZE];
    uint8_t biomes[CHUNK_SIZE * CHUNK_SIZE];
    int16_t sky_tops[CHUNK_COLUMNS];
    TerrainColumnCover cover[CHUNK_COLUMNS];
    uint16_t emitters[LIGHT_BANDS];
    unsigned emitter_mask;
    TerrainDevices devices;
    int spawner_index[2],mine_cart_index; /* Dungeon, mineshaft. -1 if absent. */
    unsigned webs;
} Chunk;
typedef struct Edit {
    int index;
    uint16_t type;
    struct Edit *next;
} Edit;
enum { UTILITY_CISTERN, UTILITY_SORTER, UTILITY_TYPES };
typedef struct {
    Edit **cells; /* Sorted by cell index; Edit objects live until world reset. */
    unsigned count,capacity,cursor;
} UtilityIndex;
typedef struct WorldEditGroup {
    int x, z;
    unsigned long long revision,light_revision,saved_revision;
    Edit *first;
    struct WorldEditGroup *next;
    uint16_t *farm;
    unsigned farm_count,farm_capacity,farm_cursor,circuit_count,plates,object_plates,mesh_details,wide_count,pistons,tripwires,edit_count;
    UtilityIndex utility[UTILITY_TYPES];
} ChunkEdits;
static Chunk chunks[CHUNK_CACHE];
static unsigned char reserved[RESERVED_CHUNKS][CELLS];
static int heads[BUCKETS];
static ChunkEdits *edits[DIMENSION_COUNT][BUCKETS];
static Dimension dimension_value = OVERWORLD;
static unsigned long long clock_tick, edit_version;
static int last = -1;
static uint64_t seed_value;
static int generation_value = GENERATOR_LEGACY;
static WorldProfile profile_value;
static int structures_disabled_value;
static FlatLayout flat_value;
static int interactive;
static int cache_anchor_valid,cache_x,cache_z,cache_hx,cache_hz;
static int cache_sampling;
static ChunkCacheSample cache_sample;
static unsigned pressure_counts[DIMENSION_COUNT],tripwire_counts[DIMENSION_COUNT];
static unsigned wooden_buttons[DIMENSION_COUNT];
static unsigned piston_counts[DIMENSION_COUNT];
static unsigned utility_counts[DIMENSION_COUNT][UTILITY_TYPES];
static int pressure_center_x,pressure_center_z,pressure_radius,pressure_found,pressure_valid;
static unsigned awaiting_terrain_devices;
static TerrainKey terrain_key(void) {
    return (TerrainKey){seed_value,generation_value,dimension_value,profile_value,structures_disabled_value,flat_value};
}

unsigned long long world_edit_version(void) { return edit_version; }
uint64_t world_seed(void) { return seed_value; }
int world_generation(void) { return generation_value; }
WorldProfile world_profile(void) { return profile_value; }
const FlatLayout *world_flat_layout(void) { return &flat_value; }
int world_structures_disabled(void) { return structures_disabled_value; }
int world_profile_valid(int profile) { return (unsigned)profile<WORLD_PROFILE_COUNT; }
const char *world_profile_name(WorldProfile profile) {
    static const char *const names[]={"默认","超平坦","巨型生物群系","放大化"};
    return world_profile_valid(profile) ? names[profile] : names[WORLD_STANDARD];
}
Dimension world_dimension(void) { return dimension_value; }
int world_sea_level(void) {
    return dimension_value == NETHER ? 31 : dimension_value == END ? 0 :
        profile_value==WORLD_SUPERFLAT ? flat_effective(&flat_value)->height-1 :
        generation_value == GENERATOR_LEGACY ? 26 : 63;
}
void world_begin(uint64_t seed) {
    world_begin_generated(seed, GENERATOR_LEGACY);
}
int world_begin_generated(uint64_t seed, int generation) {
    return world_begin_profile(seed,generation,WORLD_STANDARD);
}
int world_begin_profile(uint64_t seed,int generation,WorldProfile profile) {
    return world_begin_options(seed,generation,profile,0);
}
int world_begin_options(uint64_t seed,int generation,WorldProfile profile,int structures_disabled) {
    return world_begin_layout(seed,generation,profile,structures_disabled,NULL);
}
int world_begin_layout(uint64_t seed,int generation,WorldProfile profile,int structures_disabled,const FlatLayout *layout) {
    if (!generation_valid(generation) || !world_profile_valid(profile) ||
        (profile!=WORLD_STANDARD && generation<GENERATOR_MUSHROOM_ISLAND) ||
        (layout && profile!=WORLD_SUPERFLAT) || !flat_valid(layout) ||
        (structures_disabled!=0 && structures_disabled!=1)) return 0;
    seed_value = seed;
    generation_value = generation;
    profile_value=profile;
    structures_disabled_value=structures_disabled;
    flat_value=layout ? *layout : (FlatLayout){0};
    world_reset();
    return 1;
}

unsigned hash_for_seed(uint64_t seed,int x,int z) {
    /* Keep every legacy seed byte-for-byte; wide seeds mix all 64 bits
       with the coordinates instead of aliasing them through a 32-bit seed. */
    if (seed>UINT32_MAX) {
        uint64_t a=seed+(uint64_t)(uint32_t)x*UINT64_C(0x9e3779b97f4a7c15)
            +(uint64_t)(uint32_t)z*UINT64_C(0xd1b54a32d192ed03);
        a=(a^(a>>30))*UINT64_C(0xbf58476d1ce4e5b9);
        a=(a^(a>>27))*UINT64_C(0x94d049bb133111eb);
        return (uint32_t)(a^(a>>31)^(a>>32));
    }
    uint32_t a = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u + (12345u ^ (uint32_t)seed);
    a = (a ^ (a >> 13)) * 1274126177u;
    return a ^ (a >> 16);
}
unsigned hash2(int x,int z) { return hash_for_seed(seed_value,x,z); }
uint32_t world_seed_mix32(void) { return seed_value>UINT32_MAX ? hash2(11,97) : (uint32_t)seed_value; }

static int bucket(int x, int z) { return (int)(hash2(x, z) % BUCKETS); }
static ChunkEdits *find_edits(int cx, int cz, int b) {
    for (ChunkEdits *group = edits[dimension_value][b]; group; group = group->next)
        if (group->x == cx && group->z == cz) return group;
    return NULL;
}
unsigned long long chunk_revision(int cx, int cz) {
    ChunkEdits *group = find_edits(cx, cz, bucket(cx, cz));
    return group ? group->revision : 0;
}
unsigned long long world_light_revision(int cx,int cz) {
    ChunkEdits *group=find_edits(cx,cz,bucket(cx,cz));
    return group ? group->light_revision : 0;
}
static int chunk_coord(int v) {
    int q = v / CHUNK_SIZE, r = v % CHUNK_SIZE;
    return q - (r < 0);
}
int world_circuit_near(int x,int z) {
    /* A torch may read a strongly powered block three cells away. */
    for (int cx=chunk_coord(x-3);cx<=chunk_coord(x+3);++cx)
        for (int cz=chunk_coord(z-3);cz<=chunk_coord(z+3);++cz) {
            const ChunkEdits *g=find_edits(cx,cz,bucket(cx,cz));
            if (g && g->circuit_count) return 1;
        }
    return 0;
}
void world_visit_piston_near(int x,int z,WorldRecord visit,void *ctx) {
    if (!piston_counts[dimension_value] || !visit) return;
    const int reach=PISTON_PUSH_LIMIT+1;
    for (int a=chunk_coord(x-reach);a<=chunk_coord(x+reach);++a)
        for (int b=chunk_coord(z-reach);b<=chunk_coord(z+reach);++b) {
            const ChunkEdits *g=find_edits(a,b,bucket(a,b));
            if (!g || !g->pistons) continue;
            unsigned remaining=g->pistons;
            for (const Edit *e=g->first;e && remaining;e=e->next) if (piston_base_cell(e->type)) {
                --remaining;
                int col=e->index/WY;
                if (!visit(ctx,a*CHUNK_SIZE+col/CHUNK_SIZE,e->index%WY,
                    b*CHUNK_SIZE+col%CHUNK_SIZE,e->type)) return;
            }
        }
}
int world_has_pistons(void) { return piston_counts[dimension_value]!=0; }
void world_touch_circuit_mesh(int x,int z) {
    int cx=chunk_coord(x),cz=chunk_coord(z);
    ChunkEdits *group=find_edits(cx,cz,bucket(cx,cz));
    if (group) group->revision=++edit_version;
}
int world_contact_sensors_near(int cx,int cz,int radius) {
    if (!pressure_counts[dimension_value] && !tripwire_counts[dimension_value]) return 0;
    if (pressure_valid && pressure_center_x==cx && pressure_center_z==cz && pressure_radius==radius)
        return pressure_found;
    pressure_valid=1; pressure_center_x=cx;pressure_center_z=cz;pressure_radius=radius;
    pressure_found=0;
    for (int a=cx-radius;a<=cx+radius;++a) for (int b=cz-radius;b<=cz+radius;++b) {
        const ChunkEdits *g=find_edits(a,b,bucket(a,b));
        if (g && g->object_plates) pressure_found|=SENSOR_OBJECT_PLATES;
        if (g && g->plates) pressure_found|=SENSOR_PLATES;
        if (g && g->tripwires) pressure_found|=SENSOR_TRIPWIRE;
        if ((pressure_found&(SENSOR_TRIPWIRE|SENSOR_OBJECT_PLATES))==(SENSOR_TRIPWIRE|SENSOR_OBJECT_PLATES) ||
            ((pressure_found&SENSOR_OBJECT_PLATES) && !tripwire_counts[dimension_value]) ||
            ((pressure_found&SENSOR_TRIPWIRE) && !pressure_counts[dimension_value])) return pressure_found;
    }
    return pressure_found;
}
int world_has_wooden_buttons(void) { return wooden_buttons[dimension_value]!=0; }
static int local_coord(int v) {
    int r = v % CHUNK_SIZE;
    return r < 0 ? r + CHUNK_SIZE : r;
}
static int cell_index(int x, int y, int z) {
    return (local_coord(x) * CHUNK_SIZE + local_coord(z)) * WY + y;
}
static int cell_section(int index) { return (index%WY)/CELL_SECTION_HEIGHT; }
static int prepare_wide(ChunkHigh **high,int index) {
    if (!*high && !(*high=(ChunkHigh *)calloc(1,sizeof **high))) return 0;
    uint8_t **plane=&(*high)->planes[cell_section(index)];
    return *plane || (*plane=(uint8_t *)calloc(CELL_SECTION_SIZE,1))!=NULL;
}
static int read_cached(const Chunk *c,int index) { return chunk_snapshot_cell(c->cells,c->high,index); }
static void free_wide(ChunkHigh *high) {
    if (!high) return;
    for (int i=0;i<CELL_SECTIONS;++i) free(high->planes[i]);
    free(high);
}

static void write_cached(Chunk *c, int index, int cell) {
    const int column = index / WY, y = index % WY;
    uint8_t *top = &c->tops[column];
    int old=read_cached(c,index);
    if (old==COBWEB) --c->webs;
    if (cell==COBWEB) ++c->webs;
    int band=y/LIGHT_BAND_HEIGHT;
    unsigned was=light_emission(old)!=0,is=light_emission(cell)!=0;
    if (is && !was) { ++c->emitters[band]; c->emitter_mask|=1u<<band; }
    else if (was && !is && !--c->emitters[band]) c->emitter_mask&=~(1u<<band);
    c->cells[index] = (uint8_t)cell;
    terrain_set_cover(&c->cover[column],y,cell);
    if (c->high && c->high->planes[cell_section(index)])
        c->high->planes[cell_section(index)][column*CELL_SECTION_HEIGHT+y%CELL_SECTION_HEIGHT]=(uint8_t)(cell>>8);
    if (cell != AIR && y > *top) *top = (uint8_t)y;
    else if (cell == AIR && y == *top) {
        int top_y=y;
        while (top_y>0 && read_cached(c,column*WY+top_y)==AIR) --top_y;
        *top=(uint8_t)top_y;
    }
    int16_t *sky=&c->sky_tops[column];
    if (light_opacity(cell)>1) {
        if (y>*sky) *sky=(int16_t)y;
    } else if (y==*sky) {
        /* Compare to the edited height, not the new geometry top.
           The tallest remaining cell can itself be transparent. */
        int sky_y=y-1;
        if (sky_y>*top) sky_y=*top;
        while (sky_y>=0 && light_opacity(read_cached(c,column*WY+sky_y))==1) --sky_y;
        *sky=(int16_t)sky_y;
    }
}

static void clear_cache(int release) {
    terrain_worker_reset(terrain_key());
    cache_anchor_valid=0;
    light_reset();
    pressure_valid=0;
    awaiting_terrain_devices=0;
    for (int i = 0; i < BUCKETS; i++) heads[i] = -1;
    for (int i = 0; i < CHUNK_CACHE; i++) {
        free_wide(chunks[i].high);
        chunks[i].high=NULL;
        if (i < RESERVED_CHUNKS) chunks[i].cells = reserved[i];
        else if (release) { free(chunks[i].cells); chunks[i].cells = NULL; }
        chunks[i].used = 0;
    }
    last = -1;
    clock_tick = 0;
    ++edit_version;
}

int world_select_dimension(Dimension dimension) {
    if ((unsigned)dimension >= DIMENSION_COUNT) return 0;
    if (dimension_value != dimension) {
        chest_access_end();
        dimension_value = dimension;
        clear_cache(0);
    }
    return 1;
}

void world_reset(void) {
    chest_access_end();
    water_reset();
    lava_reset();
    gravity_reset();
    redstone_reset();
    hoppers_reset();
    droppers_reset();
    blasts_reset();
    fires_reset();
    leaves_reset();
    weather_reset(world_seed_mix32());
    villagers_reset();
    dimension_value = OVERWORLD;
    memset(pressure_counts,0,sizeof pressure_counts);
    memset(tripwire_counts,0,sizeof tripwire_counts);
    memset(wooden_buttons,0,sizeof wooden_buttons);
    memset(piston_counts,0,sizeof piston_counts);
    memset(utility_counts,0,sizeof utility_counts);
    for (int d = 0; d < DIMENSION_COUNT; ++d) for (int i = 0; i < BUCKETS; i++) {
        ChunkEdits *group = edits[d][i];
        while (group) {
            ChunkEdits *next_group = group->next;
            Edit *e = group->first;
            while (e) { Edit *next = e->next; free(e); e = next; }
            free(group->farm);
            for (int k=0;k<UTILITY_TYPES;++k) free(group->utility[k].cells);
            free(group);
            group = next_group;
        }
        edits[d][i] = NULL;
    }
    clear_cache(1);
}

static Chunk *peek_chunk(int cx,int cz) {
    if (last >= 0 && chunks[last].used && chunks[last].x == cx && chunks[last].z == cz) {
        return &chunks[last];
    }
    int b = bucket(cx, cz);
    for (int i = heads[b]; i >= 0; i = chunks[i].next) {
        if (chunks[i].x == cx && chunks[i].z == cz) return &chunks[i];
    }
    return NULL;
}
static Chunk *find_chunk(int cx,int cz) {
    Chunk *c=peek_chunk(cx,cz);
    if (c) { c->age=++clock_tick; last=(int)(c-chunks); }
    return c;
}
int world_pressure_cell(int x,int y,int z) {
    if ((unsigned)y>=WY) return AIR;
    int cx=chunk_coord(x),cz=chunk_coord(z);
    const ChunkEdits *g=find_edits(cx,cz,bucket(cx,cz));
    if (!g || !g->plates) return AIR;
    Chunk *c=find_chunk(cx,cz);
    if (c) return read_cached(c,cell_index(x,y,z));
    int idx=cell_index(x,y,z);
    for (const Edit *e=g->first;e;e=e->next) if (e->index==idx) return e->type;
    return AIR;
}
static void detach_chunk(Chunk *c) {
    int index=(int)(c-chunks);
    if (c->used) {
        if (c->devices.count) --awaiting_terrain_devices;
        int *link=&heads[bucket(c->x,c->z)];
        while (*link>=0 && *link!=index) link=&chunks[*link].next;
        if (*link==index) *link=c->next;
    }
    c->used=0;
    if (last==index) last=-1;
    free_wide(c->high);
    c->high=NULL;
}
enum { CACHE_OTHER, CACHE_AHEAD, CACHE_NEAR };
static int cache_location(const Chunk *c) {
    if (!cache_anchor_valid) return CACHE_OTHER;
    /* Bound the differences before projecting into the heading. */
    enum { REACH=FLIGHT_PREFETCH_DEPTH+1+FLIGHT_PREFETCH_DEPTH/3 };
    int64_t x=(int64_t)c->x-cache_x,z=(int64_t)c->z-cache_z;
    if (x < -REACH || x > REACH || z < -REACH || z > REACH) return CACHE_OTHER;
    int dx=(int)x,dz=(int)z;
    if (stream_ring(dx,dz)<=2) return CACHE_NEAR;
    if ((cache_hx || cache_hz) && flight_stream_tier(dx,dz,cache_hx,cache_hz,0)<FLIGHT_REST_TIER)
        return CACHE_AHEAD;
    return CACHE_OTHER;
}
static Chunk *install_chunk(int cx,int cz,const TerrainResult *result) {
    int b=bucket(cx,cz);
    ChunkEdits *group=find_edits(cx,cz,b);
    ChunkHigh *wide=NULL;
    if (!result) result=terrain_prepare_immediate(terrain_key(),cx,cz);
    for (unsigned i=0;i<result->wide.count;++i) if (!prepare_wide(&wide,result->wide.cell[i].index)) {
        free_wide(wide);
        return NULL;
    }
    if (group && group->wide_count)
        for (const Edit *e=group->first;e;e=e->next) if (e->type>255 && !prepare_wide(&wide,e->index)) {
            free_wide(wide);
            return NULL;
        }
    int victim = 0;
    /* Slots are filled in order. On allocation failure keep the plain LRU,
       so even the small reserved cache can service a distant mesh halo. */
    const int retain=chunks[CHUNK_CACHE-1].used && (cache_hx || cache_hz) && cache_anchor_valid;
    int victim_rank=retain ? cache_location(&chunks[0]) : CACHE_OTHER;
    for (int i = 0; i < CHUNK_CACHE; i++) {
        if (!chunks[i].used) {
            if (!chunks[i].cells) chunks[i].cells = (unsigned char *)malloc(CELLS);
            if (chunks[i].cells) victim = i;
            /* Allocation failure simply reuses the least-recently used slot.
               Reserved slots cover the mesher's chunk and its light halo. */
            break;
        }
        if (victim_rank==CACHE_OTHER && chunks[i].age>=chunks[victim].age) continue;
        int rank=retain ? cache_location(&chunks[i]) : CACHE_OTHER;
        if (rank<victim_rank || (rank==victim_rank && chunks[i].age<chunks[victim].age)) {
            victim=i;
            victim_rank=rank;
        }
    }
    Chunk *c = &chunks[victim];
    if (cache_sampling) {
        ++cache_sample.installs;
        cache_sample.generated_reused+=(unsigned)result->reused;
        if (c->used) {
            ++cache_sample.evictions;
            int region=cache_location(c);
            if (region==CACHE_NEAR) ++cache_sample.near_evictions;
            if (region==CACHE_AHEAD) ++cache_sample.ahead_evictions;
        }
    }
    detach_chunk(c);
    c->high=wide;
    c->used = 1; c->x = cx; c->z = cz; c->age = ++clock_tick;
    c->next = heads[b]; heads[b] = victim; last = victim;
    memcpy(c->cells,result->cells,sizeof result->cells);
    memcpy(c->tops,result->tops,sizeof c->tops);
    memcpy(c->biomes,result->biomes,sizeof c->biomes);
    memcpy(c->sky_tops,result->sky_tops,sizeof c->sky_tops);
    memcpy(c->cover,result->cover,sizeof c->cover);
    memcpy(c->emitters,result->emitters,sizeof c->emitters);
    c->emitter_mask=result->emitter_mask;
    c->devices=result->devices;
    if (c->devices.count) ++awaiting_terrain_devices;
    c->spawner_index[0]=c->spawner_index[1]=c->mine_cart_index=-1;
    c->webs=0;
    for (unsigned i=0;i<result->wide.count;++i) {
        int index=result->wide.cell[i].index,raw=result->wide.cell[i].cell;
        write_cached(c,index,raw);
        if (dungeon_spawner_cell(raw))
            c->spawner_index[raw==MINE_CAVE_SPIDER_CELL]=index;
        if (mine_cart_track(raw)) c->mine_cart_index=index;
    }
    if (group)
        for (Edit *e = group->first; e; e = e->next) write_cached(c, e->index, e->type);
    water_chunk_installed(cx,cz);
    return c;
}
static int visit_natural_features(int cx,int cz,int radius,int cart,WorldRecord visit,void *ctx) {
    if (!visit || dimension_value!=OVERWORLD || generation_value<(cart ? GENERATOR_MINESHAFTS : GENERATOR_DUNGEONS) ||
        radius<0 || radius>2) return 0;
    for (int dx=-radius;dx<=radius;++dx) for (int dz=-radius;dz<=radius;++dz) {
        const Chunk *c=peek_chunk(cx+dx,cz+dz);
        if (!c) continue;
        for (int i=0;i<(cart ? 1 : 2);++i) {
            int idx=cart ? c->mine_cart_index : c->spawner_index[i];
            if (idx<0) continue;
            int raw=read_cached(c,idx);
            if ((cart ? mine_cart_track(raw) : dungeon_spawner_cell(raw)) &&
                !visit(ctx,c->x*CHUNK_SIZE+idx/WY/CHUNK_SIZE,idx%WY,c->z*CHUNK_SIZE+idx/WY%CHUNK_SIZE,raw))
                return 0;
        }
    }
    return 1;
}
int world_visit_dungeon_spawners(int cx,int cz,int radius,WorldRecord visit,void *ctx) {
    return visit_natural_features(cx,cz,radius,0,visit,ctx);
}
int world_visit_mine_carts(int cx,int cz,int radius,WorldRecord visit,void *ctx) {
    return visit_natural_features(cx,cz,radius,1,visit,ctx);
}
static Chunk *get_chunk(int cx,int cz,int urgent) {
    Chunk *c=find_chunk(cx,cz);
    if (c) return c;
    if (interactive && terrain_worker_running()) {
        terrain_worker_request(cx,cz,urgent);
        return NULL;
    }
    return install_chunk(cx,cz,NULL);
}
int world_stream_start(void) {
    terrain_worker_reset(terrain_key());
    int light=light_start();
    return terrain_worker_start() && light;
}
void world_stream_stop(void) {
    terrain_worker_stop(); light_stop(); interactive=cache_anchor_valid=cache_sampling=0;
}
int world_set_interactive(int active) {
    int previous=interactive; interactive=active!=0;
    if (!interactive) cache_anchor_valid=0;
    return previous;
}
int world_streaming_active(void) { return interactive && terrain_worker_running(); }
int world_chunk_ready(int x,int z) {
    return !world_streaming_active() || get_chunk(chunk_coord(x),chunk_coord(z),1)!=NULL;
}
void world_stream_pump_directional(int x,int z,int hx,int hz) {
    if (!interactive || !terrain_worker_running()) return;
    int cx=chunk_coord(x),cz=chunk_coord(z);
    hx=(hx>0)-(hx<0); hz=(hz>0)-(hz<0);
    cache_anchor_valid=1; cache_x=cx; cache_z=cz; cache_hx=hx; cache_hz=hz;
    terrain_worker_center(cx,cz,hx,hz);
    /* Never wait for a generation or publish more than a few buffers per frame. */
    for (int i=0;i<4;++i) {
        const TerrainResult *delivered=terrain_worker_take();
        if (!delivered) break;
        if (!find_chunk(delivered->x,delivered->z)) install_chunk(delivered->x,delivered->z,delivered);
    }
    /* Seed two rings ahead of movement, without asking the generator on this thread. */
    for (int r=0;r<=2;++r) for (int dx=-r;dx<=r;++dx) for (int dz=-r;dz<=r;++dz) {
        if (abs(dx)!=r && abs(dz)!=r) continue;
        (void)get_chunk(cx+dx,cz+dz,r<=1);
    }
    if (hx || hz) {
        /* Keep ahead of the two mesh workers even while both are busy.
           Submit one row per pump rather than a burst on every frame. */
        static unsigned prefetch_row;
        int depth=3+(int)(prefetch_row++%(FLIGHT_PREFETCH_DEPTH-1));
        for (int cross=-1;cross<=1;++cross) {
            int fx=cx+hx*depth+hz*cross,fz=cz+hz*depth-hx*cross;
            if (!peek_chunk(fx,fz)) terrain_worker_request(fx,fz,0);
        }
    }
}
void world_stream_pump(int x,int z) { world_stream_pump_directional(x,z,0,0); }
void world_begin_cache_sample(void) {
    cache_sample=(ChunkCacheSample){0,0,0,0,0};
    cache_sampling=1;
}
ChunkCacheSample world_finish_cache_sample(void) {
    cache_sampling=0;
    return cache_sample;
}

int inworld(int x, int y, int z) {
    (void)x; (void)z;
    return y >= 0 && y < WY;
}
int world_cell(int x, int y, int z) {
    if (y < 0) return dimension_value == END ? AIR : BEDROCK;
    if (y >= WY) return AIR;
    Chunk *c=get_chunk(chunk_coord(x),chunk_coord(z),1);
    return c ? read_cached(c,cell_index(x,y,z)) : BEDROCK;
}
int world_resident_cell(int x,int y,int z,int *cell) {
    if (!cell) return 0;
    if (y<0 || y>=WY) {
        *cell=y<0 && dimension_value!=END ? BEDROCK : AIR;
        return 1;
    }
    Chunk *c=peek_chunk(chunk_coord(x),chunk_coord(z));
    if (!c) return 0;
    *cell=read_cached(c,cell_index(x,y,z));
    return 1;
}
int world_resident_web_column(int x,int z,int y0,int y1) {
    const Chunk *c=peek_chunk(chunk_coord(x),chunk_coord(z));
    if (!c || !c->webs) return 0;
    if (y0<0) y0=0;
    if (y1>WY) y1=WY;
    int col=(local_coord(x)*CHUNK_SIZE+local_coord(z))*WY;
    for (int y=y0;y<y1;++y) if (read_cached(c,col+y)==COBWEB) return 1;
    return 0;
}
int block(int x, int y, int z) { return cell_type(world_cell(x, y, z)); }
int world_resident_top(int x,int z,int *out) {
    const Chunk *c=out ? peek_chunk(chunk_coord(x),chunk_coord(z)) : NULL;
    if (!c) return 0;
    *out=c->tops[local_coord(x)*CHUNK_SIZE+local_coord(z)];
    return 1;
}
int world_column_top(int x, int z) {
    Chunk *c=get_chunk(chunk_coord(x),chunk_coord(z),1);
    return c ? c->tops[local_coord(x)*CHUNK_SIZE+local_coord(z)] : WY-1;
}
int world_precipitation_surface(int x,int z,int *top,Biome *biome) {
    Chunk *c=find_chunk(chunk_coord(x),chunk_coord(z));
    if (!c) return 0;
    int column=local_coord(x)*CHUNK_SIZE+local_coord(z);
    int y=c->tops[column];
    for (;y>=0;--y) {
        int t=cell_type(read_cached(c,column*WY+y));
        /* A glass or leaf roof stops precipitation even if it transmits light.
           Flat decorations and plants do not provide cover. */
        if (solid(t) || build_partial(t) || t==WATER || t==LAVA || t==BED || t==SNOW_LAYER) break;
    }
    if (top) *top=y;
    if (biome) *biome=(Biome)c->biomes[column];
    return 1;
}
int light_has_open_sky(int x,int y,int z) {
    if (dimension_value!=OVERWORLD) return 0;
    Chunk *c=get_chunk(chunk_coord(x),chunk_coord(z),1);
    return c && y>c->sky_tops[local_coord(x)*CHUNK_SIZE+local_coord(z)];
}
int light_has_open_sky_resident(int x,int y,int z) {
    if (dimension_value!=OVERWORLD) return 0;
    const Chunk *c=peek_chunk(chunk_coord(x),chunk_coord(z));
    return c && y>c->sky_tops[local_coord(x)*CHUNK_SIZE+local_coord(z)];
}
int solid(int type) {
    return type != AIR && type != WATER && type != LAVA && type != NETHER_PORTAL_X &&
        type != NETHER_PORTAL_Z && type!=BED &&
        !rail_type(type) && !crop_type(type) && !barrier_type(type) && !build_partial(type) && type!=TALL_GRASS && type!=TORCH &&
        type!=FILTER_MAT && type!=BREWING_STAND && type!=SUGAR_CANE && type!=REDSTONE_WIRE && type!=LEVER &&
        type!=REPEATER && type!=REDSTONE_TORCH && type!=COMPARATOR && type!=PRESSURE_PLATE && type!=BUTTON &&
        type!=TRIPWIRE && type!=TRIPWIRE_HOOK && type!=AIR_OUTLET && type!=COBWEB &&
        type!=FIRE_CELL && type!=SNOW_LAYER && type!=SAPLING && type!=PUMPKIN_STEM && type!=POPPY &&
        type!=DANDELION && type!=COCOA && type!=LILY_PAD && !small_mushroom(type);
}

static ChunkView view_of(Chunk *chunk) {
    return chunk ? (ChunkView){chunk->cells,chunk->tops,chunk->biomes,chunk->sky_tops,chunk->emitter_mask,chunk->high,chunk->cover} :
        (ChunkView){NULL,NULL,NULL,NULL,0,NULL,NULL};
}
ChunkView chunk_view(int cx,int cz) { return view_of(get_chunk(cx,cz,0)); }
ChunkView chunk_cached_view(int cx,int cz) { return view_of(find_chunk(cx,cz)); }
ChunkView chunk_peek_view(int cx,int cz) { return view_of(peek_chunk(cx,cz)); }

static int mesh_detail(int raw) {
    return wire_cell(raw) || repeater_cell(raw) || comparator_cell(raw) || pressure_cell(raw) || button_cell(raw) ||
        tripwire_hook_cell(raw) ||
        cell_type(raw)==POWERED_RAIL || detector_track(raw) || activator_track(raw);
}
static int utility_kind(int cell) {
    return cistern_cell(cell) ? UTILITY_CISTERN : cell==HYDRAULIC_SORTER ? UTILITY_SORTER : -1;
}
static int reserve_utility_index(UtilityIndex *index) {
    if (index->count<index->capacity) return 1;
    if (index->capacity>=CELLS) return 0;
    unsigned next=index->capacity ? index->capacity*2u : 4u;
    if (next>CELLS) next=CELLS;
    Edit **grown=(Edit **)realloc(index->cells,next*sizeof *grown);
    if (!grown) return 0;
    index->cells=grown;
    index->capacity=next;
    return 1;
}
static unsigned utility_position(const UtilityIndex *utility,int index) {
    unsigned lo=0,hi=utility->count;
    while (lo<hi) {
        unsigned mid=lo+(hi-lo)/2u;
        if (utility->cells[mid]->index<index) lo=mid+1u;
        else hi=mid;
    }
    return lo;
}
static int remember_edit(int cx, int cz, int idx, int cell,int before) {
    int b = bucket(cx, cz);
    ChunkEdits *group = find_edits(cx, cz, b);
    if (!group) {
        group = (ChunkEdits *)malloc(sizeof *group);
        if (!group) return 0;
        *group=(ChunkEdits){.x=cx,.z=cz,.next=edits[dimension_value][b]};
        edits[dimension_value][b] = group;
    }
    Edit *e = group->first;
    while (e && e->index != idx) e = e->next;
    int was=e && is_farm_cell(e->type),is=is_farm_cell(cell);
    const int utility_before=e ? utility_kind(e->type) : -1,utility_now=utility_kind(cell);
    if (utility_now>=0 && utility_now!=utility_before && !reserve_utility_index(&group->utility[utility_now])) return 0;
    int piston_before=e && piston_base_cell(e->type),piston_now=piston_base_cell(cell);
    int circuit_before=e && (circuit_cell(e->type)||air_relay_cell(e->type)||cistern_probe_cell(e->type)||detector_track(e->type)||e->type==TRAPPED_CHEST_CELL||gate_cell(e->type)||door_cell(e->type)||trapdoor_cell(e->type)||lamp_cell(e->type)||ejector_cell(e->type)||
        e->type==TNT_BLOCK||piston_before);
    int circuit_now=circuit_cell(cell)||air_relay_cell(cell)||cistern_probe_cell(cell)||detector_track(cell)||cell==TRAPPED_CHEST_CELL||gate_cell(cell)||door_cell(cell)||trapdoor_cell(cell)||lamp_cell(cell)||ejector_cell(cell)||cell==TNT_BLOCK||piston_now;
    int visual_before=e && mesh_detail(e->type),visual_now=mesh_detail(cell);
    int plate_before=e && pressure_cell(e->type),plate_now=pressure_cell(cell);
    int trip_before=e && e->type==TRIPWIRE_CELL,trip_now=cell==TRIPWIRE_CELL;
    int object_before=plate_before && e->type!=PRESSURE_STONE,object_now=plate_now && cell!=PRESSURE_STONE;
    int wide_before=e && e->type>255;
    int wood_before=e && e->type>=BUTTON_WOOD && e->type<BUTTON_END;
    int wood_now=cell>=BUTTON_WOOD && cell<BUTTON_END;
    if (is && !was && group->farm_count==group->farm_capacity) {
        unsigned next=group->farm_capacity ? group->farm_capacity*2u : 16u;
        /* Indices are uint16_t in a fixed-size chunk. */
        if (next>CELLS) return 0;
        uint16_t *grown=(uint16_t *)realloc(group->farm,next*sizeof *grown);
        if (!grown) return 0;
        group->farm=grown; group->farm_capacity=next;
    }
    if (!e) {
        e = (Edit *)malloc(sizeof *e);
        if (!e) return 0;
        e->index = idx;
        e->next = group->first;
        group->first = e;
        ++group->edit_count;
    }
    e->type = (uint16_t)cell;
    if (utility_before!=utility_now) {
        if (utility_before>=0) {
            UtilityIndex *u=&group->utility[utility_before];
            unsigned i=utility_position(u,idx);
            --u->count;
            memmove(u->cells+i,u->cells+i+1,(u->count-i)*sizeof *u->cells);
            if (i<u->cursor) --u->cursor;
            --utility_counts[dimension_value][utility_before];
        }
        if (utility_now>=0) {
            UtilityIndex *u=&group->utility[utility_now];
            unsigned i=utility_position(u,idx);
            memmove(u->cells+i+1,u->cells+i,(u->count-i)*sizeof *u->cells);
            u->cells[i]=e;
            ++u->count;
            ++utility_counts[dimension_value][utility_now];
        }
    }
    if (piston_before!=piston_now) {
        if (piston_now) { ++group->pistons; ++piston_counts[dimension_value]; }
        else { --group->pistons; --piston_counts[dimension_value]; }
    }
    if (visual_before!=visual_now) {
        if (visual_now) ++group->mesh_details; else --group->mesh_details;
    }
    if (wide_before!=(cell>255)) {
        if (wide_before) --group->wide_count; else ++group->wide_count;
    }
    if (wood_before!=wood_now) {
        if (wood_now) ++wooden_buttons[dimension_value]; else --wooden_buttons[dimension_value];
    }
    if (circuit_before!=circuit_now) {
        if (circuit_now) ++group->circuit_count;
        else --group->circuit_count;
    }
    if (plate_before!=plate_now) {
        if (plate_now) { ++group->plates; ++pressure_counts[dimension_value]; }
        else { --group->plates; --pressure_counts[dimension_value]; }
        pressure_valid=0;
    }
    if (trip_before!=trip_now) {
        if (trip_now) { ++group->tripwires; ++tripwire_counts[dimension_value]; }
        else { --group->tripwires; --tripwire_counts[dimension_value]; }
        pressure_valid=0;
    }
    if (object_before!=object_now) {
        if (object_now) ++group->object_plates; else --group->object_plates;
        pressure_valid=0;
    }
    if (is && !was) group->farm[group->farm_count++]=(uint16_t)idx;
    else if (was && !is) {
        for (unsigned i=0;i<group->farm_count;++i) if (group->farm[i]==idx) {
            group->farm[i]=group->farm[--group->farm_count];
            if (i<group->farm_cursor) group->farm_cursor=i;
            break;
        }
    }
    group->saved_revision=group->revision=++edit_version;
    if (before<0 || light_emission(before)!=light_emission(cell) || light_opacity(before)!=light_opacity(cell))
        group->light_revision=group->revision;
    return 1;
}

int world_farm_batch(int cx,int cz,BlockEdit *out,int limit) {
    if (!out || limit<=0) return 0;
    ChunkEdits *g=find_edits(cx,cz,bucket(cx,cz));
    if (!g || !g->farm_count) return 0;
    int n=(unsigned)limit<g->farm_count ? limit : (int)g->farm_count;
    Chunk *c=peek_chunk(cx,cz);
    if (!c) return 0;
    for (int i=0;i<n;++i) {
        if (g->farm_cursor>=g->farm_count) g->farm_cursor=0;
        unsigned idx=g->farm[g->farm_cursor++],col=idx/WY;
        out[i]=(BlockEdit){cx*CHUNK_SIZE+(int)(col/CHUNK_SIZE),(int)(idx%WY),
            cz*CHUNK_SIZE+(int)(col%CHUNK_SIZE),read_cached(c,(int)idx)};
    }
    return n;
}

int world_write_cell(int x, int y, int z, int cell) {
    if (!inworld(x,y,z) || !valid_cell(cell)) return 0;
    int cx = chunk_coord(x), cz = chunk_coord(z), idx = cell_index(x, y, z);
    Chunk *c=get_chunk(cx,cz,1);
    if (!c) return 0;
    int before=read_cached(c,idx);
    if (before==cell) return 1;
    if (cell>255 && !prepare_wide(&c->high,idx)) return 0;
    if (!remember_edit(cx, cz, idx, cell,before)) return 0;
    write_cached(c, idx, cell);
    return 1;
}
static int utility_batch(int kind,int cx,int cz,BlockEdit *out,int limit) {
    if (!out || limit<=0) return 0;
    ChunkEdits *g=find_edits(cx,cz,bucket(cx,cz));
    if (!g || !g->utility[kind].count) return 0;
    UtilityIndex *u=&g->utility[kind];
    const Chunk *c=peek_chunk(cx,cz);
    if (!c) return 0;
    int n=(unsigned)limit<u->count ? limit : (int)u->count;
    for (int i=0;i<n;++i) {
        if (u->cursor>=u->count) u->cursor=0;
        unsigned idx=(unsigned)u->cells[u->cursor++]->index,col=idx/WY;
        out[i]=(BlockEdit){cx*CHUNK_SIZE+(int)(col/CHUNK_SIZE),(int)(idx%WY),
            cz*CHUNK_SIZE+(int)(col%CHUNK_SIZE),read_cached(c,(int)idx)};
    }
    return n;
}
int world_has_cisterns(void) { return utility_counts[dimension_value][UTILITY_CISTERN]!=0; }
int world_cistern_batch(int cx,int cz,BlockEdit *out,int limit) { return utility_batch(UTILITY_CISTERN,cx,cz,out,limit); }
int world_has_sorters(void) { return utility_counts[dimension_value][UTILITY_SORTER]!=0; }
int world_sorter_batch(int cx,int cz,BlockEdit *out,int limit) { return utility_batch(UTILITY_SORTER,cx,cz,out,limit); }
int world_set_heat_bench_level(int x,int y,int z,int expected,int next) {
    int cell;
    if (!inworld(x,y,z) || !heat_bench_cell(expected) || !heat_bench_cell(next) ||
        !world_resident_cell(x,y,z,&cell) || cell!=expected) return 0;
    /* The thermal charge changes appearance only, not support, flow,
       circuit connectivity or light transmission. */
    return world_write_cell(x,y,z,next);
}
int world_set_cistern_level(int x,int y,int z,int expected,int next) {
    if (!inworld(x,y,z) || !cistern_cell(expected) || !cistern_cell(next)) return 0;
    int cx=chunk_coord(x),cz=chunk_coord(z),idx=cell_index(x,y,z);
    ChunkEdits *g=find_edits(cx,cz,bucket(cx,cz));
    Chunk *c=peek_chunk(cx,cz);
    if (!g || !c || read_cached(c,idx)!=expected) return 0;
    UtilityIndex *u=&g->utility[UTILITY_CISTERN];
    unsigned i=utility_position(u,idx);
    if (i==u->count || u->cells[i]->index!=idx || u->cells[i]->type!=expected) return 0;
    /* No placement/destruction callbacks: only the fill amount changed. */
    if (next!=expected) {
        u->cells[i]->type=(uint16_t)next;
        write_cached(c,idx,next);
        g->saved_revision=g->revision=++edit_version;
        redstone_cistern_changed(x,y,z,expected,next);
    }
    return 1;
}
int world_claim_mine_cart(int x,int y,int z) {
    int raw;
    return world_resident_cell(x,y,z,&raw) && mine_cart_track(raw) &&
        world_write_cell(x,y,z,TRACK_BASE+raw-MINE_CART_TRACK_BASE);
}
void world_activate_terrain_devices(int cx,int cz,int radius) {
    if (!awaiting_terrain_devices) return;
    for (int a=cx-radius;a<=cx+radius;++a) for (int b=cz-radius;b<=cz+radius;++b) {
        Chunk *c=peek_chunk(a,b);
        if (!c || !c->devices.count) continue;
        unsigned pending=0;
        for (unsigned i=0;i<c->devices.count;++i) {
            TerrainWideCell stage=c->devices.cell[i];
            int col=stage.index/WY,y=stage.index%WY;
            int x=a*CHUNK_SIZE+col/CHUNK_SIZE,z=b*CHUNK_SIZE+col%CHUNK_SIZE;
            /* A saved change, including destruction, always wins. Never
               reconstruct a dispenser inventory from terrain on reload. */
            if (world_has_edit(x,y,z)) continue;
            int dispenser=dispenser_cell(stage.cell);
            if (dispenser && !dropper_seed_temple(x,y,z)) {
                c->devices.cell[pending++]=stage;
                continue;
            }
            if (!world_write_cell(x,y,z,stage.cell)) {
                if (dispenser) dropper_forget(x,y,z);
                c->devices.cell[pending++]=stage;
                continue;
            }
            redstone_activate_terrain_device(x,y,z,stage.cell);
        }
        c->devices.count=pending;
        if (!pending && !--awaiting_terrain_devices) return;
    }
}
int world_set_farm_stage(int x,int y,int z,int cell) {
    return is_farm_cell(cell) && cell_type(world_cell(x,y,z))==cell_type(cell) &&
        world_write_cell(x,y,z,cell);
}

int world_restore_edit(int x, int y, int z, int cell) {
    if (!inworld(x, y, z) || !valid_cell(cell)) return 0;
    int cx = chunk_coord(x), cz = chunk_coord(z), index = cell_index(x, y, z);
    Chunk *c=find_chunk(cx,cz);
    if (c && cell>255 && !prepare_wide(&c->high,index)) return 0;
    if (!remember_edit(cx, cz, index, cell,-1)) return 0;
    /* Spawn selection may already have generated this chunk during load.
       Update it in place without creating new chunks or queuing fluid work. */
    if (c) write_cached(c,index,cell);
    return 1;
}

int world_visit_edits(WorldRecord visit, void *ctx) {
    return world_visit_dimension_edits(dimension_value, visit, ctx);
}

int world_visit_chunk_mesh_details(int cx,int cz,WorldRecord visit,void *ctx) {
    const ChunkEdits *g=find_edits(cx,cz,bucket(cx,cz));
    if (!visit) return 0;
    if (!g || !g->mesh_details) return 1;
    for (const Edit *e=g->first;e;e=e->next) {
        if (!mesh_detail(e->type)) continue;
        int col=e->index/WY;
        if (!visit(ctx,cx*CHUNK_SIZE+col/CHUNK_SIZE,e->index%WY,
            cz*CHUNK_SIZE+col%CHUNK_SIZE,e->type)) return 0;
    }
    return 1;
}

int world_visit_edit_group(const WorldEditGroup *g,WorldRecord visit,void *ctx) {
    if (!g || !visit) return 0;
    for (const Edit *e=g->first;e;e=e->next) {
        int col=e->index/WY;
        if (!visit(ctx,g->x*CHUNK_SIZE+col/CHUNK_SIZE,e->index%WY,
            g->z*CHUNK_SIZE+col%CHUNK_SIZE,e->type)) return 0;
    }
    return 1;
}
int world_visit_dimension_edits(Dimension dimension, WorldRecord visit, void *ctx) {
    if ((unsigned)dimension>=DIMENSION_COUNT || !visit) return 0;
    for (int i=0;i<BUCKETS;++i) for (const ChunkEdits *g=edits[dimension][i];g;g=g->next)
        if (!world_visit_edit_group(g,visit,ctx)) return 0;
    return 1;
}
int world_visit_edit_groups(Dimension dimension,WorldEditGroupVisit visit,void *ctx) {
    if ((unsigned)dimension>=DIMENSION_COUNT || !visit) return 0;
    for (int i=0;i<BUCKETS;++i) for (const ChunkEdits *g=edits[dimension][i];g;g=g->next)
        if (g->edit_count && !visit(ctx,g,g->x,g->z,g->saved_revision,g->edit_count,g->wide_count)) return 0;
    return 1;
}

int world_recorded_cell(Dimension d,int x,int y,int z) {
    if ((unsigned)d>=DIMENSION_COUNT || !inworld(x,y,z)) return -1;
    int cx = chunk_coord(x), cz = chunk_coord(z);
    int i = cell_index(x, y, z);
    for (ChunkEdits *g=edits[d][bucket(cx,cz)];g;g=g->next) if (g->x==cx && g->z==cz)
        for (Edit *e=g->first;e;e=e->next) if (i==e->index) return e->type;
    return -1;
}
int world_has_edit(int x,int y,int z) { return world_recorded_cell(dimension_value,x,y,z)>=0; }

enum { EDIT_GROUP_LIMIT=1024 };
static int commit_edits(const BlockEdit *group,int count,const uint16_t *before) {
    for (int i=0;i<count;++i) {
        BlockEdit e=group[i];
        if (!gravity_prepare_edit(e.x,e.y,e.z,before[i],e.cell) ||
            !leaves_prepare_edit(e.x,e.y,e.z,before[i],e.cell)) {
            leaves_finish_edits();
            return 0;
        }
    }
    for (int i = 0; i < count; ++i) {
        BlockEdit e = group[i];
        if (!world_write_cell(e.x, e.y, e.z, e.cell)) {
            while (i-- > 0) {
                e=group[i];
                /* Every successful write has an edit and enough farm
                   capacity when unwound in reverse order. Don't allocate
                   cache planes during rollback; retire a conflicting slot. */
                if (e.cell==before[i]) continue;
                int cx=chunk_coord(e.x),cz=chunk_coord(e.z),idx=cell_index(e.x,e.y,e.z);
                (void)remember_edit(cx,cz,idx,before[i],e.cell);
                Chunk *c=find_chunk(cx,cz);
                if (!c) continue;
                if (before[i]>255 && (!c->high || !c->high->planes[cell_section(idx)])) detach_chunk(c);
                else write_cached(c,idx,before[i]);
            }
            leaves_finish_edits();
            return 0;
        }
    }
    for (int i=0;i<count;++i) {
        BlockEdit e=group[i];
        leaves_commit_edit(e.x,e.y,e.z,before[i],e.cell);
    }
    leaves_finish_edits();
    for (int i = 0; i < count; ++i) {
        BlockEdit e = group[i];
        if (before[i] != e.cell) {
            if ((door_cell(before[i]) && !door_upper(before[i])) || (door_cell(e.cell) && !door_upper(e.cell)))
                villagers_door_changed(e.x,e.y,e.z);
            water_notify_edit(e.x, e.y, e.z, before[i], e.cell);
            lava_notify_edit(e.x,e.y,e.z,before[i],e.cell);
            rail_after_edit(e.x,e.y,e.z,before[i],e.cell);
            barrier_after_edit(e.x,e.y,e.z,before[i],e.cell);
            redstone_after_edit(e.x,e.y,e.z,before[i],e.cell);
            fire_after_edit(e.x,e.y,e.z,before[i],e.cell);
            if (before[i]==BREWING_STAND && e.cell!=BREWING_STAND && block(e.x,e.y,e.z)!=BREWING_STAND)
                brew_forget(e.x,e.y,e.z);
            if (hopper_cell(before[i]) && !hopper_cell(e.cell) && block(e.x,e.y,e.z)!=HOPPER)
                hopper_forget(e.x,e.y,e.z);
            if (ejector_cell(before[i]) && !ejector_cell(e.cell) && !ejector_type(block(e.x,e.y,e.z)))
                dropper_forget(e.x,e.y,e.z);
        }
    }
    return 1;
}
static int edit_at(const BlockEdit *edits,int count,int x,int y,int z) {
    for (int i=count;i>0;--i) if (edits[i-1].x==x && edits[i-1].y==y && edits[i-1].z==z) return i-1;
    return -1;
}
static int stage_attachment_removal(BlockEdit *all,int *count,int x,int y,int z) {
    if (edit_at(all,*count,x,y,z)>=0) return 1;
    if (*count==EDIT_GROUP_LIMIT) return 0;
    all[(*count)++]=(BlockEdit){x,y,z,AIR};
    return 1;
}
static int final_in_column(const BlockEdit *edits,int count,const Chunk *c,int x,int y,int z) {
    if (y<0 || y>=WY) return AIR;
    int i=edit_at(edits,count,x,y,z);
    return i<0 ? read_cached(c,cell_index(x,y,z)) : edits[i].cell;
}
/* At a streaming seam, an edit (even AIR) supersedes the natural attachment. */
static int wall_neighbor(int x,int y,int z,int d) {
    int nx=x+rail_dx(d),nz=z+rail_dz(d),raw;
    if (!world_resident_cell(nx,y,nz,&raw)) {
        raw=world_recorded_cell(world_dimension(),nx,y,nz);
        if (raw<0) raw=village_natural_ladder(nx,y,nz);
    }
    return wall_attachment_dir(raw)==((d+2)&3);
}
static int has_wall_neighbor(int x,int y,int z) {
    for (int d=0;d<4;++d) if (wall_neighbor(x,y,z,d)) return 1;
    return 0;
}
int world_set_blocks(const BlockEdit *group,int count) {
    if (!group || count<0 || count>EDIT_GROUP_LIMIT) return 0;
    BlockEdit paired[EDIT_GROUP_LIMIT];
    uint16_t before[EDIT_GROUP_LIMIT];
    int total=count,expanded=0;
    for (int i=0;i<count;++i) {
        BlockEdit e=group[i];
        if (!inworld(e.x,e.y,e.z) || !valid_cell(e.cell)) return 0;
        Chunk *c=get_chunk(chunk_coord(e.x),chunk_coord(e.z),1);
        if (!c) return 0;
        int original=read_cached(c,cell_index(e.x,e.y,e.z));
        before[i]=(uint16_t)original;
        if (door_cell(original) && !door_cell(e.cell)) {
            int mate_y=e.y+(door_upper(original) ? -1 : 1);
            if (inworld(e.x,mate_y,e.z) && read_cached(c,cell_index(e.x,mate_y,e.z))==door_peer(original)) {
                if (!expanded) { memcpy(paired,group,(size_t)count*sizeof *group); expanded=1; }
                if (!stage_attachment_removal(paired,&total,e.x,mate_y,e.z)) return 0;
            }
        } else if (e.y<WY-2 && door_support(original) && !door_support(e.cell)) {
            int raw=read_cached(c,cell_index(e.x,e.y+1,e.z));
            if (door_cell(raw) && !door_upper(raw)) {
                if (!expanded) { memcpy(paired,group,(size_t)count*sizeof *group); expanded=1; }
                if (!stage_attachment_removal(paired,&total,e.x,e.y+1,e.z) ||
                    !stage_attachment_removal(paired,&total,e.x,e.y+2,e.z)) return 0;
            }
        }
        if (original==WATER && e.cell!=WATER && e.y+1<WY &&
            read_cached(c,cell_index(e.x,e.y+1,e.z))==LILY_PAD && edit_at(group,count,e.x,e.y,e.z)==i) {
            if (!expanded) { memcpy(paired,group,(size_t)count*sizeof *group); expanded=1; }
            if (!stage_attachment_removal(paired,&total,e.x,e.y+1,e.z)) return 0;
        }
        if (wall_attachment_support(original) && !wall_attachment_support(e.cell))
            for (int d=0;d<4;++d) if (wall_neighbor(e.x,e.y,e.z,d)) {
                if (!expanded) { memcpy(paired,group,(size_t)count*sizeof *group); expanded=1; }
                if (!stage_attachment_removal(paired,&total,e.x+rail_dx(d),e.y,e.z+rail_dz(d))) return 0;
            }
    }
    if (expanded) group=paired;
    for (int i=0;i<total;++i) if (i>=count || door_cell(group[i].cell) || group[i].cell==LILY_PAD ||
        wall_attachment_dir(group[i].cell)>=0) {
        BlockEdit e=group[i];
        Chunk *c=get_chunk(chunk_coord(e.x),chunk_coord(e.z),1);
        if (!c) return 0;
        if (i>=count) before[i]=(uint16_t)read_cached(c,cell_index(e.x,e.y,e.z));
        else if (e.cell==LILY_PAD) {
            if (final_in_column(group,total,c,e.x,e.y-1,e.z)!=WATER) return 0;
        } else if (wall_attachment_dir(e.cell)>=0) {
            int d=wall_attachment_dir(e.cell),x=e.x+rail_dx(d),z=e.z+rail_dz(d);
            Chunk *wall=get_chunk(chunk_coord(x),chunk_coord(z),1);
            if (!wall || !wall_attachment_support(final_in_column(group,total,wall,x,e.y,z))) return 0;
        } else {
            int lower=e.y-(door_upper(e.cell)!=0),peer=e.y+(door_upper(e.cell) ? -1 : 1);
            if (lower<1 || lower>=WY-1 || final_in_column(group,total,c,e.x,peer,e.z)!=door_peer(e.cell) ||
                !door_support(final_in_column(group,total,c,e.x,lower-1,e.z))) return 0;
        }
    }
    if (!commit_edits(group,total,before)) return 0;
    /* Gameplay drops for direct mining and boats are handled by their callers. */
    for (int i=0;i<total;++i) if (before[i]==LILY_PAD && (i>=count || water_strength(group[i].cell))) {
        BlockEdit e=group[i];
        (void)drop_loose_item((Vec3){e.x+.5f,e.y+.12f,e.z+.5f},(ItemStack){LILY_PAD_ITEM,1,0,0,0});
    }
    if (player_rules.mode==SURVIVAL) for (int i=count;i<total;++i) if (wall_attachment_dir(before[i])>=0) {
        const BlockEdit e=group[i];
        uint16_t item=ladder_cell(before[i]) ? LADDER_ITEM : WOOD_TRAPDOOR_ITEM;
        (void)drop_loose_item((Vec3){e.x+.5f,e.y+.3f,e.z+.5f},(ItemStack){item,1,0,0,0});
    }
    return 1;
}

int set_block(int x, int y, int z, int type) {
    if (!inworld(x,y,z) || !((type>=0 && type<BASE_BLOCK_COUNT) || fence_type(type) || brewing_block(type) ||
        book_block(type) || type==EMERALD_ORE || type==EMERALD_BLOCK || type==IRON_BLOCK || type==ANVIL || type==REDSTONE_LAMP || type==TNT_BLOCK ||
        type==SNOW_LAYER || type==PUMPKIN || type==POPPY || type==LAPIS_ORE || type==LAPIS_BLOCK ||
        type==CACTUS || type==DANDELION || type==TRAPPED_CHEST || type==ENDER_CHEST ||
        type==AIR_STATION || type==GOLD_BLOCK || type==DIAMOND_BLOCK || type==COBWEB ||
        small_mushroom(type) || type==BROWN_MUSHROOM_BLOCK || type==LILY_PAD ||
        stained_glass_cell(type) || stained_clay_cell(type) || pane_cell(type) || build_full(type) ||
        geology_block(type) || aquifer_cell(type) || pressure_works_cell(type) ||
        type==RESONANCE_MARKER || type==AIR_PIPE || seep_cell(type) || settling_cell(type) || cistern_cell(type) ||
        type==HYDRAULIC_SORTER || type==HEAT_BENCH_EMPTY || type==FALL_SIEVE || coast_stone(type))) return 0;
    if (type>=BED && type<=CROP_POTATO) return 0; /* Logical, never raw metadata. */
    if (type==ANVIL) type=ANVIL_BASE;
    if (type==REDSTONE_LAMP) type=LAMP_OFF;
    if (type==EMERALD_ORE) type=EMERALD_ORE_CELL;
    if (type==EMERALD_BLOCK) type=EMERALD_BLOCK_CELL;
    if (type==PUMPKIN) type=PUMPKIN_CELL;
    if (type==POPPY) type=POPPY_CELL;
    if (type==LAPIS_ORE) type=LAPIS_ORE_CELL;
    if (type==LAPIS_BLOCK) type=LAPIS_BLOCK_CELL;
    if (type==DANDELION) type=DANDELION_CELL;
    if (type==CACTUS) type=CACTUS_BASE;
    if (type==TRAPPED_CHEST) type=TRAPPED_CHEST_CELL;
    if (type==ENDER_CHEST) type=ENDER_CHEST_BASE;
    if (type==AIR_STATION) type=AIR_STATION_BASE;
    if (type==GOLD_BLOCK) type=GOLD_BLOCK_CELL;
    if (type==DIAMOND_BLOCK) type=DIAMOND_BLOCK_CELL;
    if (!world_chunk_ready(x,z)) return 0;
    int before = world_cell(x, y, z);
    if (before == type) return 1;
    if (!valid_cell(type)) return 0;
    if (type==LILY_PAD || (before==WATER && world_cell(x,y+1,z)==LILY_PAD) ||
        door_cell(before) || (door_support(before) && !door_support(type) && door_cell(world_cell(x,y+1,z))) ||
        (wall_attachment_support(before) && !wall_attachment_support(type) && has_wall_neighbor(x,y,z))) {
        BlockEdit single={x,y,z,type};
        return world_set_blocks(&single,1);
    }
    if (rail_type(type) && !full_top_surface(world_cell(x,y-1,z))) return 0;
    if (!gravity_prepare_edit(x,y,z,before,type)) return 0;
    if (!leaves_prepare_edit(x,y,z,before,type) || !world_write_cell(x, y, z, type)) {
        leaves_finish_edits();
        return 0;
    }
    leaves_commit_edit(x,y,z,before,type);
    leaves_finish_edits();
    water_notify_edit(x, y, z, before, type);
    lava_notify_edit(x,y,z,before,type);
    rail_after_edit(x,y,z,before,type);
    barrier_after_edit(x,y,z,before,type);
    redstone_after_edit(x,y,z,before,type);
    fire_after_edit(x,y,z,before,type);
    if (before==BREWING_STAND) brew_forget(x,y,z);
    if (hopper_cell(before)) hopper_forget(x,y,z);
    if (ejector_cell(before)) dropper_forget(x,y,z);
    return 1;
}
