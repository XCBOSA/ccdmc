#include "internal.h"
#include "aquifer_layout.h"
#include "pressure_layout.h"
#include "dungeons.h"
#include "mineshafts.h"
#include "spillways.h"
#include "mushrooms.h"
#include "rails.h"
#include "villages.h"
#include "build_blocks.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* One immutable snapshot per invocation. Terrain helpers never read the live
   world selection while a background generate is in flight. */
#ifdef __cplusplus
static thread_local const TerrainKey *terrain_snapshot;
#else
static _Thread_local const TerrainKey *terrain_snapshot;
#endif
static uint64_t terrain_seed(void) { return terrain_snapshot ? terrain_snapshot->seed : world_seed(); }
static int terrain_generation(void) { return terrain_snapshot ? terrain_snapshot->generation : world_generation(); }
int terrain_build_generation(void) { return terrain_generation(); }
static Dimension terrain_dimension(void) { return terrain_snapshot ? terrain_snapshot->dimension : world_dimension(); }
static WorldProfile terrain_profile(void) { return terrain_snapshot ? terrain_snapshot->profile : world_profile(); }
static int terrain_structures_disabled(void) {
    return terrain_snapshot ? terrain_snapshot->structures_disabled : world_structures_disabled();
}
static const FlatLayout *terrain_flat(void) {
    return flat_effective(terrain_snapshot ? &terrain_snapshot->flat : world_flat_layout());
}
static unsigned terrain_hash(int x,int z) { return hash_for_seed(terrain_seed(),x,z); }
#define hash2 terrain_hash
#define world_seed terrain_seed
#define world_generation terrain_generation
#define world_dimension terrain_dimension
#define world_profile terrain_profile
/* Use the request snapshot, even while another world is active on the UI thread. */
static int terrain_uses_aquifer(void) { return terrain_generation()>=GENERATOR_AQUIFER; }

/* The same seed and absolute coordinates always yield the same chunk. */
static float smooth(float t) { return t * t * (3.0f - 2.0f * t); }
static float lerp(float a, float b, float t) { return a + (b - a) * t; }
static float sample2(int x, int z) { return (hash2(x, z) & 65535u) / 65535.0f; }

static float noise2(float x, float z) {
    int ix = (int)floorf(x), iz = (int)floorf(z);
    float u = smooth(x - ix), v = smooth(z - iz);
    return lerp(lerp(sample2(ix, iz), sample2(ix + 1, iz), u),
                lerp(sample2(ix, iz + 1), sample2(ix + 1, iz + 1), u), v);
}

static float sample3_with_base(unsigned base,int y) {
    unsigned h = base ^ ((unsigned)y * 2246822519u);
    h = (h ^ (h >> 15)) * 3266489917u;
    return ((h ^ (h >> 16)) & 65535u) / 65535.0f;
}
static float sample3(int x, int y, int z) { return sample3_with_base(hash2(x,z),y); }

static float noise3(float x, float y, float z) {
    int ix = (int)floorf(x), iy = (int)floorf(y), iz = (int)floorf(z);
    float u = smooth(x - ix), v = smooth(y - iy), w = smooth(z - iz);
    float a = lerp(lerp(sample3(ix, iy, iz), sample3(ix + 1, iy, iz), u),
                   lerp(sample3(ix, iy + 1, iz), sample3(ix + 1, iy + 1, iz), u), v);
    float b = lerp(lerp(sample3(ix, iy, iz + 1), sample3(ix + 1, iy, iz + 1), u),
                   lerp(sample3(ix, iy + 1, iz + 1), sample3(ix + 1, iy + 1, iz + 1), u), v);
    return lerp(a, b, w);
}

/* Most Y samples share a lattice slab with the previous block. Cache the
   four horizontal hashes and the two interpolated X rows on each Y plane.
   Keep the original X -> Y -> Z interpolation order (not X -> Z -> Y). */
typedef struct {
    unsigned base[4];
    float u,w,lower[2],upper[2];
    int iy,ready;
} VerticalNoise;
static VerticalNoise vertical_noise(float x,float z) {
    int ix=(int)floorf(x),iz=(int)floorf(z);
    return (VerticalNoise){{hash2(ix,iz),hash2(ix+1,iz),hash2(ix,iz+1),hash2(ix+1,iz+1)},
        smooth(x-ix),smooth(z-iz),{0.f,0.f},{0.f,0.f},0,0};
}
static void vertical_plane(const VerticalNoise *n,int y,float dest[2]) {
    dest[0]=lerp(sample3_with_base(n->base[0],y),sample3_with_base(n->base[1],y),n->u);
    dest[1]=lerp(sample3_with_base(n->base[2],y),sample3_with_base(n->base[3],y),n->u);
}
static float vertical_sample(VerticalNoise *n,float y) {
    int iy=(int)floorf(y);
    if (!n->ready || iy!=n->iy) {
        if (n->ready && iy==n->iy+1) {
            n->lower[0]=n->upper[0];
            n->lower[1]=n->upper[1];
        } else vertical_plane(n,iy,n->lower);
        vertical_plane(n,iy+1,n->upper);
        n->iy=iy;
        n->ready=1;
    }
    float v=smooth(y-iy);
    return lerp(lerp(n->lower[0],n->upper[0],v),lerp(n->lower[1],n->upper[1],v),n->w);
}

static float ramp(float a, float b, float x) {
    float t = (x - a) / (b - a);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return smooth(t);
}

/* Low plains blend into ridged mountains and flat-topped highlands.
   Two independently winding bands carve riverbeds and dry canyons. */
static int legacy_height(int x, int z) {
    float biome = noise2(x * .0028f + 63, z * .0028f - 91);
    float rough = noise2(x * .014f, z * .014f);
    float detail = noise2(x * .055f + 12, z * .055f - 5);
    float mountain = ramp(.50f, .69f, biome);
    float plateau = ramp(.52f, .70f, noise2(x * .0035f - 170, z * .0035f + 54)) * (1 - mountain);
    float ridge = 1 - fabsf(2 * noise2(x * .011f + 91, z * .011f + 37) - 1);
    float h = 30 + 5 * rough + 2 * detail;
    h += mountain * (13 + 27 * ridge * ridge);
    h += plateau * 19;
    float river = fabsf(noise2(x * .007f + 430, z * .007f - 60) - .5f);
    float river_mask = 1 - ramp(.018f, .055f, river);
    h = lerp(h, 23 + 2 * detail, river_mask);
    float canyon = fabsf(noise2(x * .005f - 230, z * .005f + 580) - .5f);
    float canyon_mask = (1 - ramp(.012f, .036f, canyon)) * (1 - river_mask);
    h -= canyon_mask * (11 + 8 * plateau);
    if (h < 6) h = 6;
    if (h > LEGACY_WY - 9) h = LEGACY_WY - 9;
    return (int)h;
}

static int column_index(int x, int z) { return x * CHUNK_SIZE + z; }
static int index3(int x, int y, int z) { return column_index(x, z) * WY + y; }

static void generate_legacy(int cx, int cz, uint8_t *cells, uint8_t *tops, uint8_t *biomes) {
    const int ox = cx * CHUNK_SIZE, oz = cz * CHUNK_SIZE, sea = 26;
    for (int x = 0; x < CHUNK_SIZE; x++) for (int z = 0; z < CHUNK_SIZE; z++) {
        int wx = ox + x, wz = oz + z, h = legacy_height(wx, wz);
        VerticalNoise chambers=vertical_noise(wx*.055f,wz*.055f);
        VerticalNoise tunnels=vertical_noise(wx*.095f+83,wz*.095f+43);
        for (int y = 0; y <= h; y++) {
            int type = STONE;
            if (y == 0) type = BEDROCK;
            else if (y == h) type = h <= sea + 1 ?
                (hash2(wx / 4, wz / 4) % 5 == 0 ? CLAY : SAND) : h >= 65 ? SNOW : GRASS;
            else if (y >= h - 3) type = h <= sea + 1 ? SAND : DIRT;
            else if (h <= sea + 1 && y >= h - 6) type = SANDSTONE;
            else {
                float cave = vertical_sample(&chambers,y*.075f);
                if (y > 3 && y < h - 4 &&
                    (cave > .67f || (fabsf(cave - .5f) < .045f &&
                        fabsf(vertical_sample(&tunnels,y*.09f-29)-.5f)<.09f))) {
                    type = AIR;
                } else {
                    unsigned ore = hash2(wx + y * 113, wz - y * 71);
                    if (y < 15 && ore % 211 == 0) type = DIAMOND_ORE;
                    else if (y < 25 && ore % 137 == 0) type = GOLD_ORE;
                    else if (y < 38 && ore % 53 < 2) type = IRON_ORE;
                    else if (ore % 31 < 2) type = COAL_ORE;
                    else if (y < 48 && ore % 97 == 0) type = GRAVEL;
                    else if (y < 40 && cave > .58f && ore % 7 == 0) type = MOSS_STONE;
                }
            }
            cells[index3(x, y, z)] = (unsigned char)type;
        }
        for (int y = h + 1; y <= sea; y++) cells[index3(x, y, z)] = WATER;
        tops[column_index(x, z)] = (uint8_t)(h > sea ? h : sea);
        biomes[column_index(x, z)] = BIOME_LEGACY;
    }
    /* Check nearby tree roots too: crowns must agree on both sides of a seam. */
    for (int wx = ox - 2; wx < ox + CHUNK_SIZE + 2; wx++)
        for (int wz = oz - 2; wz < oz + CHUNK_SIZE + 2; wz++) {
            if (hash2(wx, wz) % 115 || (abs(wx) < 5 && abs(wz) < 5)) continue;
            int h = legacy_height(wx, wz);
            if (h <= sea + 1 || h >= 65 || h + 6 >= LEGACY_WY) continue;
            for (int dx = -2; dx <= 2; dx++) for (int dz = -2; dz <= 2; dz++) {
                int x = wx + dx - ox, z = wz + dz - oz;
                if (x < 0 || x >= CHUNK_SIZE || z < 0 || z >= CHUNK_SIZE) continue;
                for (int dy = 1; dy <= 6; dy++) {
                    int idx = index3(x, h + dy, z);
                    if (dx == 0 && dz == 0 && dy <= 4) cells[idx] = WOOD;
                    else if (dy >= 3 && abs(dx) + abs(dz) + (dy == 6 ? 2 : 0) <= 3 && cells[idx] == AIR)
                        cells[idx] = LEAF;
                    if (cells[idx] && h + dy > tops[column_index(x, z)])
                        tops[column_index(x, z)] = (uint8_t)(h + dy);
                }
            }
        }
}

typedef struct { Biome biome; int height; } ClimateColumn;
enum { SEA = 63, CROWN_BORDER = 4, EXTENT = CHUNK_SIZE + CROWN_BORDER * 2 };

typedef struct { int row,pillar,x,z; } CoastColumn;
static int joint_div(int n,int period) { return n/period-(n%period<0); }
static CoastColumn cooling_column(int x,int z) {
    /* Stagger rows by half a column width. Weathering accentuates the
       joints, without making every shoreline a regular wall. */
    const int row=joint_div(z,5),offset=(row%2!=0)*3;
    const int shifted=x+offset,pillar=joint_div(shifted,6);
    return (CoastColumn){row,pillar,shifted-pillar*6,z-row*5};
}
static int joint_rind(CoastColumn c) {
    return c.x>1 && c.x<5 && c.z>0 && c.z<4;
}

/* Independent continent, heat and moisture fields keep biomes contiguous.
   Ridges and river cuts blend before assigning the surface biome. */
static ClimateColumn climate(int x, int z) {
    WorldProfile profile=world_profile();
    if (profile==WORLD_SUPERFLAT) {
        const FlatLayout *flat=terrain_flat();
        return (ClimateColumn){(Biome)flat->biome,flat->height-1+flat_spawn_platform(flat,x,z)};
    }
    /* Grow the coarse climate, coastline and river pattern without
       enlarging fine hills, cave spacing, trees, or individual structures. */
    float domain=profile==WORLD_LARGE_BIOMES ? .25f : 1.f;
    float bx=x*domain,bz=z*domain;
    float continent = .76f * noise2(bx * .0025f + 17, bz * .0025f - 53) +
        .24f * noise2(bx * .0061f - 427, bz * .0061f + 98);
    float heat = .82f * noise2(bx * .0031f + 93, bz * .0031f + 161) +
        .18f * noise2(bx * .010f + 78, bz * .010f + 20);
    float wet = .85f * noise2(bx * .0035f - 383, bz * .0035f + 61) +
        .15f * noise2(bx * .0094f + 71, bz * .0094f - 246);
    float variant = noise2(bx * .0064f + 102, bz * .0064f + 207);
    float fine = noise2(x * .049f + 37, z * .049f - 29);
    float rolling = noise2(x * .013f - 128, z * .013f - 218);
    float ridge = 1 - fabsf(2 * noise2(x * .0085f - 124, z * .0085f + 372) - 1);
    float range = ramp(.53f, .80f, continent) * ramp(.57f, .80f, variant);
    float inland = ramp(.38f, .60f, continent);
    float river_band = fabsf(noise2(bx * .0063f + 525, bz * .0063f - 303) - .5f);
    float cut = (1 - ramp(.014f, .061f, river_band)) * ramp(.39f, .50f, continent);
    float marsh = ramp(.63f, .93f, wet) * (1 - ramp(.46f, .62f, continent));
    float h = SEA - 24 + 31 * ramp(.16f, .48f, continent) + 18 * inland * rolling +
        8 * inland * (fine - .5f) + range * (13 + 37 * ridge);
    h = lerp(h, SEA + 1 + 3 * rolling, marsh);
    h = lerp(h, SEA - 5 + 2 * fine, cut);
    Biome b;

    if (continent < .34f && variant > .79f && heat > .38f && heat < .68f) {
        if (terrain_generation()<GENERATOR_JOINT_COAST) {
            b = BIOME_MUSHROOM;
            h = SEA + 2 + (variant - .79f) * 51 + 4 * fine;
        } else {
            b=BIOME_JOINT_COAST;
            const CoastColumn c=cooling_column(x,z);
            const unsigned rock=hash2(c.pillar^0x4d81,c.row^0x20e5);
            const float shelf=ramp(.79f,.94f,variant);
            const int groove=(c.z==0 || (c.x==0 && c.z!=4)) ? 2+(int)(rock&1u) : 0;
            const int stair=(int)(shelf*(8+(int)((rock>>8)%6u)));
            h=SEA+1+stair-groove;
        }
    } else if (h < SEA - 1) {
        if (continent > .39f && cut > .45f) b = heat < .29f ? BIOME_FROZEN_RIVER : BIOME_RIVER;
        else b = h < SEA - 16 ? BIOME_DEEP_OCEAN : BIOME_OCEAN;
    } else if (h < SEA + 3 && continent < .50f && marsh < .35f) {
        b = range > .07f || rolling > .73f ? BIOME_STONE_SHORE : BIOME_BEACH;
    } else if (heat < .30f) {
        if (wet > .51f) b = BIOME_COLD_TAIGA;
        else b = variant > .61f ? BIOME_ICE_SPIKES : BIOME_ICE_PLAINS;
    } else if (range > .34f) {
        b = BIOME_MOUNTAINS;
    } else if (heat > .66f && wet < .42f) {
        if (variant > .64f) {
            b = BIOME_MESA;
            float peak = 97 + 21 * rolling + 7 * fine;
            h = lerp(h, floorf(peak / 4) * 4, ramp(.64f, .78f, variant) * (1 - cut));
        } else b = wet < .27f ? BIOME_DESERT : BIOME_SAVANNA;
    } else if (heat > .59f && wet > .63f) {
        b = BIOME_JUNGLE;
    } else if (wet > .64f && continent < .52f) {
        b = BIOME_SWAMP;
    } else if (heat < .44f && wet > .41f) {
        b = variant > .56f ? BIOME_MEGA_TAIGA : BIOME_TAIGA;
    } else if (wet > .49f) {
        b = variant > .64f ? BIOME_ROOFED : variant < .36f ? BIOME_BIRCH : BIOME_FOREST;
    } else b = BIOME_PLAINS;

    if (profile==WORLD_AMPLIFIED) {
        /* Raise inland relief continuously; oceans, shorelines and the
           river floor remain where the normal generator put them. */
        float relief=fmaxf(0.f,h-(SEA+3));
        h+=relief*(1.18f+.90f*ridge)*ramp(.43f,.63f,continent)*(1.f-cut);
    }
    return (ClimateColumn){b, (int)fmaxf(5, fminf(WY - 26, h))};
}

static int nether_floor(int x, int z) {
    float large = noise2(x * .023f + 809, z * .023f - 91);
    float detail = noise2(x * .077f - 880, z * .077f + 48);
    return (int)(25 + 42 * large + 8 * (detail - .5f));
}
static int end_floor(int x, int z) {
    if (x < -117 || x > 117 || z < -117 || z > 117) return 0;
    float radius = sqrtf((float)(x*x + z*z));
    float edge = 83 + 22 * noise2(x * .033f + 17, z * .033f - 50);
    if (radius > edge) return 0;
    return (int)(54 + 12 * (noise2(x * .049f + 615, z * .049f + 394) - .5f)
        + 6 * (1 - radius / edge));
}

int terrain_height(int x, int z) {
    if (world_dimension() == NETHER)
        return terrain_generation()>=GENERATOR_PRESSURE_WORKS ? PRESSURE_FLOOR : nether_floor(x,z);
    if (world_dimension() == END) return terrain_uses_aquifer() ? AQUIFER_FLOOR : end_floor(x,z);
    return world_generation() == GENERATOR_LEGACY ? legacy_height(x, z) : climate(x, z).height;
}
Biome terrain_biome(int x, int z) {
    if (world_dimension() == NETHER) return BIOME_NETHER;
    if (world_dimension() == END) return BIOME_END;
    return world_generation() == GENERATOR_LEGACY ? BIOME_LEGACY : climate(x, z).biome;
}

/* Cache region planning per worker. Never share a mutable cache with the
   foreground terrain queries, and key it by the immutable generator input. */
typedef struct {
    VillagePlan plan;
    uint64_t seed;
    int rx,rz,generation,status,flat_height,flat_biome;
    WorldProfile profile;
} VillageCache;
enum { VILLAGE_PLANS=64, VILLAGE_MARGIN=40, VILLAGE_ATTEMPTS=4 };
#ifdef __cplusplus
static thread_local VillageCache village_cache[VILLAGE_PLANS];
#else
static _Thread_local VillageCache village_cache[VILLAGE_PLANS];
#endif

static int village_region(int coord) {
    return coord>=0 ? coord/VILLAGE_REGION_EDGE : (coord+1)/VILLAGE_REGION_EDGE-1;
}
static int dry_village_region(int y,Biome b) {
    return y>=SEA+2 && y<=118 && b!=BIOME_DEEP_OCEAN && b!=BIOME_OCEAN &&
        b!=BIOME_RIVER && b!=BIOME_FROZEN_RIVER && b!=BIOME_SWAMP && b!=BIOME_JOINT_COAST;
}
static int make_village_plan(int rx,int rz,VillagePlan *out) {
    const int span=VILLAGE_REGION_EDGE-2*VILLAGE_MARGIN;
    for (int i=0;i<VILLAGE_ATTEMPTS;++i) {
        unsigned r=hash2(rx*19+i*11+3871,rz*23+i*17-1997);
        int x=rx*VILLAGE_REGION_EDGE+VILLAGE_MARGIN+(int)((r&65535u)%(unsigned)span);
        int z=rz*VILLAGE_REGION_EDGE+VILLAGE_MARGIN+(int)((r>>16)%(unsigned)span);
        if (abs(x)>WORLD_SAVE_LIMIT-VILLAGE_RADIUS || abs(z)>WORLD_SAVE_LIMIT-VILLAGE_RADIUS) continue;
        ClimateColumn center=climate(x,z);
        int y=center.height;
        Biome b=center.biome;
        if (world_profile()==WORLD_SUPERFLAT) {
            *out=(VillagePlan){x,z,y,b,(r>>6)&3u};
            return 1;
        }
        if ((b!=BIOME_PLAINS && b!=BIOME_DESERT && b!=BIOME_SAVANNA) || !dry_village_region(y,b)) continue;
        int adequate=1;
        for (int a=-18;a<=18 && adequate;a+=18) for (int c=-18;c<=18;c+=18) {
            if (!a && !c) continue;
            ClimateColumn edge=climate(x+a,z+c);
            if (abs(edge.height-y)>8 || !dry_village_region(edge.height,edge.biome)) { adequate=0; break; }
        }
        if (!adequate) continue;
        *out=(VillagePlan){x,z,y,b,(r>>6)&3u};
        return 1;
    }
    return 0;
}
int village_plan_for(int x,int z,VillagePlan *out) {
    int gen=world_generation();
    if (!out || terrain_structures_disabled() || gen<GENERATOR_VILLAGES || world_dimension()!=OVERWORLD ||
        x< -WORLD_SAVE_LIMIT || x>WORLD_SAVE_LIMIT || z< -WORLD_SAVE_LIMIT || z>WORLD_SAVE_LIMIT) return 0;
    uint64_t seed=world_seed();
    WorldProfile profile=world_profile();
    int flat_height=0,flat_biome=0;
    if (profile==WORLD_SUPERFLAT) {
        const FlatLayout *flat=terrain_flat();
        flat_height=flat->height; flat_biome=flat->biome;
        if (!(flat->features&FLAT_VILLAGE) || flat_height<4 || flat_height>WY-10 ||
            !solid(flat->blocks[flat_height-1]) ||
            (flat_biome!=BIOME_PLAINS && flat_biome!=BIOME_DESERT && flat_biome!=BIOME_SAVANNA)) return 0;
    }
    int rx=village_region(x),rz=village_region(z);
    VillageCache *p=&village_cache[hash2(rx,rz)%VILLAGE_PLANS];
    if (!p->status || p->seed!=seed || p->rx!=rx || p->rz!=rz ||
        p->generation!=gen || p->profile!=profile ||
        p->flat_height!=flat_height || p->flat_biome!=flat_biome) {
        int valid=make_village_plan(rx,rz,&p->plan);
        p->status=valid ? 1 : -1;
        p->seed=seed; p->rx=rx; p->rz=rz; p->generation=gen;
        p->profile=profile;
        p->flat_height=flat_height; p->flat_biome=flat_biome;
    }
    if (p->status<0) return 0;
    *out=p->plan;
    return 1;
}
enum { CANOPY_REGION=192, CANOPY_MARGIN=24, CANOPY_PLANS=32 };
typedef struct { RainGardenPlan plan; uint64_t seed; int rx,rz,generation,status; WorldProfile profile; } CanopyCache;
static _Thread_local CanopyCache canopy_cache[CANOPY_PLANS];
static int canopy_region(int p) { return p/CANOPY_REGION-(p%CANOPY_REGION<0); }
static int make_canopy_plan(int rx,int rz,RainGardenPlan *plan) {
    enum { RANGE=CANOPY_REGION-2*CANOPY_MARGIN };
    for (int attempt=0;attempt<5;++attempt) {
        unsigned pick=hash2(rx^(0x2ab8+attempt*0x47),rz^0x4a54);
        int x=rx*CANOPY_REGION+CANOPY_MARGIN+(int)(pick%RANGE);
        int z=rz*CANOPY_REGION+CANOPY_MARGIN+(int)((pick>>12)%RANGE);
        if (abs(x)>WORLD_SAVE_LIMIT-CANOPY_MARGIN || abs(z)>WORLD_SAVE_LIMIT-CANOPY_MARGIN) continue;
        ClimateColumn center=climate(x,z);
        if (center.biome!=BIOME_JUNGLE || center.height<SEA+2 || center.height>WY-16) continue;
        int low=center.height,high=center.height;
        for (int dx=-7;dx<=7;dx+=7) for (int dz=-7;dz<=7;dz+=7) {
            ClimateColumn sample=climate(x+dx,z+dz);
            if (sample.height<low) low=sample.height;
            if (sample.height>high) high=sample.height;
        }
        if (high-low>5 || low<SEA+1) continue;
        VillagePlan village;
        if (village_plan_for(x,z,&village) &&
            abs(x-village.x)<VILLAGE_RADIUS+8 && abs(z-village.z)<VILLAGE_RADIUS+8) continue;
        *plan=(RainGardenPlan){x,(high+low)/2+1,z,(pick>>24)&3};
        return 1;
    }
    return 0;
}
static int canopy_site_plan_for(int x,int z,RainGardenPlan *out) {
    if (!out || terrain_structures_disabled() || world_dimension()!=OVERWORLD ||
        world_generation()<GENERATOR_TEMPLES || world_profile()==WORLD_SUPERFLAT ||
        x< -WORLD_SAVE_LIMIT || x>WORLD_SAVE_LIMIT || z< -WORLD_SAVE_LIMIT || z>WORLD_SAVE_LIMIT) return 0;
    int rx=canopy_region(x),rz=canopy_region(z);
    uint64_t seed=world_seed();
    CanopyCache *entry=&canopy_cache[hash2(rx^0x4a54,rz)%CANOPY_PLANS];
    int generation=world_generation();
    WorldProfile profile=world_profile();
    if (!entry->status || entry->seed!=seed || entry->rx!=rx || entry->rz!=rz ||
        entry->generation!=generation || entry->profile!=profile) {
        entry->status=make_canopy_plan(rx,rz,&entry->plan) ? 1 : -1;
        entry->seed=seed; entry->rx=rx; entry->rz=rz; entry->generation=generation;
        entry->profile=profile;
    }
    if (entry->status<0) return 0;
    *out=entry->plan;
    return 1;
}
int temple_plan_for(int x,int z,TemplePlan *out) {
    RainGardenPlan site;
    if (!out || world_generation()>=GENERATOR_RAIN_GARDEN || !canopy_site_plan_for(x,z,&site)) return 0;
    *out=(TemplePlan){site.x,site.y,site.z,site.rotation};
    return 1;
}
int rain_garden_plan_for(int x,int z,RainGardenPlan *out) {
    return world_generation()>=GENERATOR_RAIN_GARDEN && canopy_site_plan_for(x,z,out);
}

enum { ARID_SITE_MARGIN=24, ARID_SITE_CACHE=32 };
typedef struct {
    WindcutPlan plan;
    uint64_t seed;
    int rx,rz,profile,generation,status;
} AridSiteCache;
static _Thread_local AridSiteCache arid_site_cache[ARID_SITE_CACHE];

static int arid_site_near_village(int x,int z) {
    for (int dx=-PYRAMID_RADIUS;dx<=PYRAMID_RADIUS;dx+=2*PYRAMID_RADIUS)
        for (int dz=-PYRAMID_RADIUS;dz<=PYRAMID_RADIUS;dz+=2*PYRAMID_RADIUS) {
            VillagePlan village;
            if (village_plan_for(x+dx,z+dz,&village) &&
                abs(x-village.x)<=VILLAGE_RADIUS+PYRAMID_RADIUS &&
                abs(z-village.z)<=VILLAGE_RADIUS+PYRAMID_RADIUS) return 1;
        }
    return 0;
}
static int make_arid_site_plan(int rx,int rz,int flat,int flat_floor,WindcutPlan *out) {
    enum { SPAN=ARID_SITE_REGION-2*ARID_SITE_MARGIN };
    for (int i=0;i<5;++i) {
        unsigned pick=hash2(rx^(0x4461+i*79),rz^0x5079);
        int x=rx*ARID_SITE_REGION+ARID_SITE_MARGIN+(int)(pick%SPAN);
        int z=rz*ARID_SITE_REGION+ARID_SITE_MARGIN+(int)((pick>>10)%SPAN);
        if (abs(x)>WORLD_SAVE_LIMIT-ARID_SITE_MARGIN || abs(z)>WORLD_SAVE_LIMIT-ARID_SITE_MARGIN) continue;
        int y=flat_floor;
        if (!flat) {
            ClimateColumn center=climate(x,z);
            if (center.biome!=BIOME_DESERT || center.height<SEA+2 || center.height>WY-14) continue;
            int low=center.height,high=center.height,sandy=1;
            for (int dx=-PYRAMID_RADIUS;dx<=PYRAMID_RADIUS;dx+=2*PYRAMID_RADIUS)
                for (int dz=-PYRAMID_RADIUS;dz<=PYRAMID_RADIUS;dz+=2*PYRAMID_RADIUS) {
                    ClimateColumn corner=climate(x+dx,z+dz);
                    sandy&=corner.biome==BIOME_DESERT && corner.height>=SEA+1;
                    if (corner.height<low) low=corner.height;
                    if (corner.height>high) high=corner.height;
                }
            if (!sandy || high-low>4) continue;
            y=(low+high)/2+1;
        }
        int sx,sz;
        if (nearest_stronghold(x,z,&sx,&sz) && abs(x-sx)<48 && abs(z-sz)<22) continue;
        if (arid_site_near_village(x,z)) continue;
        RainGardenPlan jungle;
        if (!flat && canopy_site_plan_for(x,z,&jungle) &&
            abs(x-jungle.x)<33 && abs(z-jungle.z)<33) continue;
        *out=(WindcutPlan){x,y,z,(pick>>27)&3u};
        return 1;
    }
    return 0;
}
static int arid_site_plan_for(int x,int z,WindcutPlan *out) {
    if (!out || terrain_structures_disabled() || world_dimension()!=OVERWORLD ||
        world_generation()<GENERATOR_DESERT_PYRAMIDS || x< -WORLD_SAVE_LIMIT || x>WORLD_SAVE_LIMIT ||
        z< -WORLD_SAVE_LIMIT || z>WORLD_SAVE_LIMIT) return 0;
    int rx=x/ARID_SITE_REGION-(x%ARID_SITE_REGION<0),rz=z/ARID_SITE_REGION-(z%ARID_SITE_REGION<0);
    if (world_profile()==WORLD_SUPERFLAT) {
        const FlatLayout *flat=terrain_flat();
        if (!(flat->features&FLAT_BIOME_STRUCTURES) || flat->biome!=BIOME_DESERT ||
            flat->height<20 || flat->height>WY-12 ||
            (flat->blocks[flat->height-1]!=SAND && flat->blocks[flat->height-1]!=RED_SAND)) return 0;
        for (int y=flat->height-15;y<flat->height;++y) if (!solid(flat->blocks[y])) return 0;
        return make_arid_site_plan(rx,rz,1,flat->height,out);
    }
    AridSiteCache *cache=&arid_site_cache[hash2(rx^0x50c1,rz)%ARID_SITE_CACHE];
    if (!cache->status || cache->seed!=world_seed() || cache->rx!=rx || cache->rz!=rz ||
        cache->profile!=(int)world_profile() || cache->generation!=world_generation()) {
        cache->seed=world_seed(); cache->rx=rx; cache->rz=rz; cache->profile=(int)world_profile();
        cache->generation=world_generation();
        cache->status=make_arid_site_plan(rx,rz,0,0,&cache->plan) ? 1 : -1;
    }
    if (cache->status<0) return 0;
    *out=cache->plan;
    return 1;
}
int desert_pyramid_plan_for(int x,int z,DesertPyramidPlan *out) {
    WindcutPlan site;
    if (!out || world_generation()>=GENERATOR_WINDCUT || !arid_site_plan_for(x,z,&site)) return 0;
    *out=(DesertPyramidPlan){site.x,site.y,site.z,site.rotation};
    return 1;
}
int windcut_plan_for(int x,int z,WindcutPlan *out) {
    return world_generation()>=GENERATOR_WINDCUT && arid_site_plan_for(x,z,out);
}

static int arid_site_overlaps(WindcutPlan site,int cx,int cz) {
    int x=cx*CHUNK_SIZE,z=cz*CHUNK_SIZE;
    return site.x+WINDCUT_RADIUS>=x && site.x-WINDCUT_RADIUS<x+CHUNK_SIZE &&
        site.z+WINDCUT_RADIUS>=z && site.z-WINDCUT_RADIUS<z+CHUNK_SIZE;
}

const char *biome_name(Biome biome) {
    if (biome==BIOME_END && terrain_uses_aquifer()) return "深层含水带";
    if (biome==BIOME_NETHER && terrain_generation()>=GENERATOR_PRESSURE_WORKS) return "压力岩廊";
    static const char *const names[] = {
        "旧版地形", "深海", "海洋", "河流", "冻河", "沙滩", "石岸", "平原", "森林",
        "白桦林", "黑森林", "针叶林", "冷针叶林", "大型针叶林", "沼泽", "沙漠",
        "热带草原", "平顶山", "丛林", "峭壁", "冰原", "冰刺", "湿菌群岛", "旧热蚀层", "旧高空岩层", "柱岩海岸"
    };
    return (unsigned)biome < sizeof names / sizeof *names ? names[biome] : names[0];
}

static int climate_surface(Biome b, int x, int h, int z) {
    switch (b) {
    case BIOME_MESA: return h >= SEA + 24 ? TERRACOTTA : RED_SAND;
    case BIOME_DESERT: case BIOME_BEACH: case BIOME_OCEAN: return SAND;
    case BIOME_RIVER: case BIOME_FROZEN_RIVER: case BIOME_SWAMP:
        return h < SEA ? (hash2(x, z) % 5 ? SAND : CLAY) : GRASS;
    case BIOME_DEEP_OCEAN: return GRAVEL;
    case BIOME_STONE_SHORE: case BIOME_JOINT_COAST: return STONE;
    case BIOME_ICE_PLAINS: case BIOME_ICE_SPIKES: case BIOME_COLD_TAIGA: return SNOW;
    case BIOME_MEGA_TAIGA: return (hash2(x / 2, z / 2) % 4) ? PODZOL : GRASS;
    case BIOME_MUSHROOM: return MYCELIUM;
    case BIOME_MOUNTAINS: return h > 110 ? SNOW : (h > SEA + 22 && hash2(x, z) % 5 ? STONE : GRASS);
    default: return GRASS;
    }
}
static int climate_cell(int x,int y,int z,ClimateColumn c,int surface,
                        VerticalNoise *chambers,VerticalNoise *tunnels) {
    if (!y) return BEDROCK;
    if (y == c.height) return surface;
    if (y >= c.height - 3) {
        if (surface == SAND || surface == RED_SAND || surface == GRAVEL || surface == TERRACOTTA)
            return surface;
        return surface == STONE ? STONE : DIRT;
    }
    if (c.biome == BIOME_MESA && y > SEA + 4)
        return (y % 6 < 2) ? RED_SAND : TERRACOTTA;
    if (surface == SAND && y >= c.height - 6) return SANDSTONE;
    if (y > 3 && y < c.height - 4) {
        float chamber = chambers ? vertical_sample(chambers,y*.074f) : noise3(x*.051f,y*.074f,z*.051f);
        if (chamber > .68f || (fabsf(chamber - .5f) < .037f &&
            fabsf((tunnels ? vertical_sample(tunnels,y*.10f-79) :
                noise3(x*.094f+64,y*.10f-79,z*.094f+11))-.5f)<.085f))
            return AIR;
    }
    unsigned ore = hash2(x + 113 * y, z - 71 * y);
    if (y < 16 && ore % 204 == 0) return DIAMOND_ORE;
    if ((y < 34 || (c.biome == BIOME_MESA && y < 78)) && ore % 131 == 0) return GOLD_ORE;
    if (y < SEA + 2 && ore % 49 < 2) return IRON_ORE;
    if (ore % 36 < 2) return COAL_ORE;
    if (ore % 96 == 0) return GRAVEL;
    return STONE;
}

enum { DUNGEON_PLANS=64, DUNGEON_MARGIN=7, DUNGEON_CHOICES=7 };
typedef struct {
    DungeonPlan plan;
    uint64_t seed;
    int rx,rz,profile,gallery,status;
} DungeonCache;
#ifdef __cplusplus
static thread_local DungeonCache dungeon_cache[DUNGEON_PLANS];
#else
static _Thread_local DungeonCache dungeon_cache[DUNGEON_PLANS];
#endif
static void dungeon_edge(DungeonPlan plan,int side,int along,int *dx,int *dz) {
    *dx=side<2 ? (side ? plan.radius_x : -plan.radius_x) : along;
    *dz=side<2 ? along : (side==3 ? plan.radius_z : -plan.radius_z);
}
static int dungeon_cave_cell(int x,int y,int z,ClimateColumn c) {
    if (y>c.height) return y<=SEA ? WATER : AIR;
    return climate_cell(x,y,z,c,climate_surface(c.biome,x,c.height,z),NULL,NULL);
}
enum { SPILLWAY_CACHED=64 };
typedef struct { SpillwayPlan plan; uint64_t seed; int rx,rz,status; } SpillwayCache;
#ifdef __cplusplus
static thread_local SpillwayCache spillway_cache[SPILLWAY_CACHED];
#else
static _Thread_local SpillwayCache spillway_cache[SPILLWAY_CACHED];
#endif
static int spillway_has_cover(SpillwayPlan p) {
    /* A dry cave mouth alone is not enough: a short, steep ocean cliff
       can expose the upstream end. Check the full footprint once per plan. */
    for (int u=SPILLWAY_BEGIN;u<=SPILLWAY_END;++u) for (int v=SPILLWAY_NEAR;v<=SPILLWAY_FAR;++v) {
        const BlockEdit q=spillway_cell(p,u,v,0,0);
        if (climate(q.x,q.z).height<p.floor+SPILLWAY_ROOF+3) return 0;
    }
    return 1;
}
static int find_spillway(int rx,int rz,SpillwayPlan *out) {
    for (int i=0;i<8;++i) {
        const unsigned r=hash2(rx^(0x18a7+i*239),rz^0x9751);
        SpillwayPlan p={rx*SPILLWAY_REGION+24+(int)(r&63u),
            rz*SPILLWAY_REGION+24+(int)((r>>6)&63u),32+(int)((r>>12)&7u),(int)((r>>15)&1u),0};
        if (abs(p.x)>WORLD_SAVE_LIMIT-12 || abs(p.z)>WORLD_SAVE_LIMIT-12) continue;
        const int first=-3+(int)((r>>16)%3u);
        for (int v=first;v<=-1;++v) {
            const BlockEdit a=spillway_cell(p,SPILLWAY_END+1,v,0,0);
            const ClimateColumn c=climate(a.x,a.z);
            if (c.height < p.floor+12 || dungeon_cave_cell(a.x,p.floor+1,a.z,c)!=AIR ||
                dungeon_cave_cell(a.x,p.floor+2,a.z,c)!=AIR) continue;
            const BlockEdit b=spillway_cell(p,SPILLWAY_END+1,v+1,0,0);
            const ClimateColumn d=climate(b.x,b.z);
            if (dungeon_cave_cell(b.x,p.floor+1,b.z,d)!=AIR ||
                dungeon_cave_cell(b.x,p.floor+2,b.z,d)!=AIR) continue;
            if (!spillway_has_cover(p)) break;
            p.mouth=v;
            *out=p;
            return 1;
        }
    }
    return 0;
}
int spillway_plan_for(int x,int z,SpillwayPlan *out) {
    if (!out || terrain_generation()<GENERATOR_SPILLWAYS || terrain_structures_disabled() ||
        world_dimension()!=OVERWORLD || world_profile()==WORLD_SUPERFLAT ||
        x< -WORLD_SAVE_LIMIT || x>WORLD_SAVE_LIMIT || z< -WORLD_SAVE_LIMIT || z>WORLD_SAVE_LIMIT) return 0;
    const int rx=x/SPILLWAY_REGION-(x%SPILLWAY_REGION<0),rz=z/SPILLWAY_REGION-(z%SPILLWAY_REGION<0);
    if (hash2(rx^0x7873,rz^0x3217)%4u) return 0;
    SpillwayCache *e=&spillway_cache[hash2(rx^0x767c,rz)%SPILLWAY_CACHED];
    if (!e->status || e->seed!=world_seed() || e->rx!=rx || e->rz!=rz) {
        e->seed=world_seed(); e->rx=rx; e->rz=rz;
        e->status=find_spillway(rx,rz,&e->plan) ? 1 : -1;
    }
    if (e->status<0) return 0;
    *out=e->plan;
    return 1;
}
static int find_dungeon_openings(DungeonPlan *plan) {
    int count=0;
    for (int side=0;side<4;++side) {
        int radius=side<2 ? plan->radius_z : plan->radius_x;
        for (int along=1-radius;along<radius;++along) {
            int dx,dz;
            dungeon_edge(*plan,side,along,&dx,&dz);
            dx+=side==0 ? -1 : side==1 ? 1 : 0;
            dz+=side==2 ? -1 : side==3 ? 1 : 0;
            int x=plan->x+dx,z=plan->z+dz;
            ClimateColumn c=climate(x,z);
            if (dungeon_cave_cell(x,plan->floor+1,z,c)==AIR &&
                dungeon_cave_cell(x,plan->floor+2,z,c)==AIR) {
                plan->entrances|=UINT32_C(1)<<(side*DUNGEON_CHOICES+along+3);
                if (++count>5) return 0;
            }
        }
    }
    return count!=0;
}
static int find_dungeon(int rx,int rz,int gallery,DungeonPlan *out) {
    enum { SPAN=DUNGEON_REGION-2*DUNGEON_MARGIN };
    for (int i=0;i<5;++i) {
        unsigned r=hash2(rx^(0x174b+i*497),rz^0x17d2);
        int x=rx*DUNGEON_REGION+DUNGEON_MARGIN+(int)(r%SPAN);
        int z=rz*DUNGEON_REGION+DUNGEON_MARGIN+(int)((r>>9)%SPAN);
        int depth=13+(int)((r>>18)%12);
        /* The rock roof stays strictly below the old rail gallery floor. */
        if (gallery && depth+DUNGEON_ROOF>=28) continue;
        unsigned mob=(r>>26)&3u;
        DungeonPlan p={x,depth,z,3+(int)((r>>24)&1u),3+(int)((r>>25)&1u),
            mob<2 ? DUNGEON_ZOMBIE_CELL : mob==2 ? DUNGEON_SKELETON_CELL : DUNGEON_SPIDER_CELL,
            1+(int)((r>>28)&1u),0};
        if (abs(x)>WORLD_SAVE_LIMIT-6 || abs(z)>WORLD_SAVE_LIMIT-6 || !find_dungeon_openings(&p))
            continue;
        int enclosed=1;
        for (int dx=-p.radius_x;dx<=p.radius_x && enclosed;++dx)
            for (int dz=-p.radius_z;dz<=p.radius_z && enclosed;++dz) {
                ClimateColumn c=climate(x+dx,z+dz);
                enclosed=c.height>=depth+DUNGEON_ROOF+4 &&
                    solid(dungeon_cave_cell(x+dx,depth,z+dz,c)) &&
                    solid(dungeon_cave_cell(x+dx,depth+DUNGEON_ROOF,z+dz,c));
            }
        if (!enclosed) continue;
        *out=p;
        return 1;
    }
    return 0;
}
static int underground_site(int x,int z,int gallery,DungeonPlan *out) {
    if (!out || world_dimension()!=OVERWORLD || world_generation()<GENERATOR_DUNGEONS ||
        world_profile()==WORLD_SUPERFLAT || x< -WORLD_SAVE_LIMIT || x>WORLD_SAVE_LIMIT ||
        z< -WORLD_SAVE_LIMIT || z>WORLD_SAVE_LIMIT) return 0;
    int rx=x/DUNGEON_REGION-(x%DUNGEON_REGION<0),rz=z/DUNGEON_REGION-(z%DUNGEON_REGION<0);
    DungeonCache *e=&dungeon_cache[hash2(rx^0x723,rz)%DUNGEON_PLANS];
    if (!e->status || e->rx!=rx || e->rz!=rz || e->seed!=world_seed() || e->profile!=(int)world_profile() ||
        e->gallery!=gallery) {
        e->rx=rx; e->rz=rz; e->seed=world_seed(); e->profile=world_profile(); e->gallery=gallery;
        e->status=find_dungeon(rx,rz,gallery,&e->plan) ? 1 : -1;
    }
    if (e->status<0) return 0;
    *out=e->plan;
    return 1;
}
int dungeon_plan_for(int x,int z,DungeonPlan *out) {
    return world_generation()<GENERATOR_STRATA_GALLERIES && underground_site(x,z,0,out);
}
int strata_gallery_plan_for(int x,int z,DungeonPlan *out) {
    return world_generation()>=GENERATOR_STRATA_GALLERIES && !terrain_structures_disabled() &&
        underground_site(x,z,1,out);
}
void strata_gallery_sample(DungeonPlan plan,int upper,int *x,int *y,int *z) {
    *x=plan.x+(upper ? 1 : -1);
    *y=plan.floor+(upper ? 2 : 1);
    *z=plan.z-1;
}
static int dungeon_entrance(DungeonPlan plan,int dx,int dz) {
    int side=dx==-plan.radius_x ? 0 : dx==plan.radius_x ? 1 :
        dz==-plan.radius_z ? 2 : dz==plan.radius_z ? 3 : -1;
    if (side<0) return 0;
    int along=side<2 ? dz : dx;
    return along>=-3 && along<=3 && ((plan.entrances>>(side*DUNGEON_CHOICES+along+3))&1u);
}
static void generate_dungeon(int cx,int cz,uint8_t *cells,TerrainWide *wide) {
    DungeonPlan plan;
    if (!dungeon_plan_for(cx*CHUNK_SIZE,cz*CHUNK_SIZE,&plan)) return;
    for (int dx=-plan.radius_x;dx<=plan.radius_x;++dx) for (int dz=-plan.radius_z;dz<=plan.radius_z;++dz) {
        int x=plan.x+dx-cx*CHUNK_SIZE,z=plan.z+dz-cz*CHUNK_SIZE;
        if ((unsigned)x>=CHUNK_SIZE || (unsigned)z>=CHUNK_SIZE) continue;
        int border=abs(dx)==plan.radius_x || abs(dz)==plan.radius_z;
        int door=border && dungeon_entrance(plan,dx,dz);
        int index=column_index(x,z)*WY+plan.floor;
        unsigned stone=hash2(plan.x+dx,plan.z+dz);
        cells[index]=(uint8_t)((stone&3u) ? MOSS_STONE : COBBLESTONE);
        for (int y=1;y<=DUNGEON_ROOF;++y)
            cells[index+y]=(uint8_t)((y==DUNGEON_ROOF || (border && (!door || y>2))) ? COBBLESTONE : AIR);
        int marker=dx==0 && dz==0 ? plan.spawner :
            ((dx==plan.radius_x-1 && dz==plan.radius_z-1) ||
             (plan.chest_count==2 && dx==1-plan.radius_x && dz==1-plan.radius_z)) ? DUNGEON_CHEST_CELL : 0;
        if (marker==DUNGEON_CHEST_CELL) cells[index+1]=CHEST;
        if (marker && wide && wide->count<terrain_wide_budget())
            wide->cell[wide->count++]=(TerrainWideCell){(uint16_t)(index+1),(uint16_t)marker};
    }
}
static void resource_cell(TerrainWide *wide,int index,int raw);
static void generate_strata_gallery(int cx,int cz,uint8_t *cells,TerrainWide *wide) {
    DungeonPlan plan;
    const int ox=cx*CHUNK_SIZE,oz=cz*CHUNK_SIZE;
    if (!strata_gallery_plan_for(ox,oz,&plan) || plan.x+plan.radius_x<ox || plan.x-plan.radius_x>=ox+CHUNK_SIZE ||
        plan.z+plan.radius_z<oz || plan.z-plan.radius_z>=oz+CHUNK_SIZE) return;
    int sx[2],sy[2],sz[2];
    for (int side=0;side<2;++side) strata_gallery_sample(plan,side,&sx[side],&sy[side],&sz[side]);
    for (int dx=-plan.radius_x;dx<=plan.radius_x;++dx) for (int dz=-plan.radius_z;dz<=plan.radius_z;++dz) {
        const int wx=plan.x+dx,wz=plan.z+dz,x=wx-ox,z=wz-oz;
        if ((unsigned)x>=CHUNK_SIZE || (unsigned)z>=CHUNK_SIZE) continue;
        const int outer=abs(dx)==plan.radius_x || abs(dz)==plan.radius_z;
        const int door=outer && dungeon_entrance(plan,dx,dz);
        if (outer && !door && abs(dx)+abs(dz)>plan.radius_x+plan.radius_z-2) continue;
        /* A sloping shelf, interrupted on the walking line and at every cave mouth. */
        const int ledge=dx>0 && dx<plan.radius_x-1 && dz<0 && dz>1-plan.radius_z;
        const int low_roof=ledge || (!door && abs(dx)+abs(dz)>plan.radius_x+1);
        const int index=column_index(x,z)*WY+plan.floor;
        for (int dy=0;dy<=DUNGEON_ROOF;++dy) {
            const int roof=dy>=DUNGEON_ROOF-low_roof;
            const int wall=outer && (!door || dy>2);
            const int fill=dy<=ledge || roof || wall;
            cells[index+dy]=(uint8_t)(fill ? STONE : AIR);
            /* Short streaks rather than coating the chamber with ore. */
            const int band=dx>=0 ? 2+(dx>2) : 1+(dz<0);
            if (wide && fill && wall && !roof && dy==band && (dz&1)!=0)
                resource_cell(wide,index+dy,dx>=0 ? LIMESTONE_ROCK : SLATE_ROCK);
        }
        for (int side=0;side<2;++side) if (wx==sx[side] && wz==sz[side]) {
            cells[index+sy[side]-plan.floor]=STONE;
            if (wide) resource_cell(wide,index+sy[side]-plan.floor,
                side==0 ? STRATA_SAMPLE_SHALE : STRATA_SAMPLE_LIME);
        }
    }
}

static void decorate(uint8_t *cells, uint8_t *tops, int x, int y, int z, int type, int replace_leaf) {
    if ((unsigned)x >= CHUNK_SIZE || (unsigned)z >= CHUNK_SIZE || y <= 0 || y >= WY) return;
    int i = index3(x, y, z);
    if (cells[i] != AIR && !(replace_leaf && cell_type(cells[i]) == LEAF)) return;
    cells[i] = (uint8_t)type;
    int col = column_index(x, z);
    if (y > tops[col]) tops[col] = (uint8_t)y;
}

static void climate_feature(uint8_t *cells, uint8_t *tops, int x, int z, ClimateColumn info,
                            unsigned key,int typed_leaves) {
    int h = info.height, density = 0, trunk = WOOD, tall = 5 + (int)(key >> 19) % 3, broad = 2;
    int tip = 0, thick = 0;
    if (h < SEA || h > WY - 26) return;
    if (world_generation()>=GENERATOR_MUSHROOM_ISLAND &&
        (info.biome==BIOME_MUSHROOM || info.biome==BIOME_ROOFED)) {
        if (key%(info.biome==BIOME_MUSHROOM ? 38u : 211u)==0) {
            int red=(key>>9)&1u,height=4+(int)((key>>13)%3u);
            for (int y=0;y<=height;++y) for (int dx=-MUSHROOM_RADIUS;dx<=MUSHROOM_RADIUS;++dx)
                for (int dz=-MUSHROOM_RADIUS;dz<=MUSHROOM_RADIUS;++dz) {
                    int part=huge_mushroom_part(red,height,dx,y,dz);
                    if (part!=AIR) decorate(cells,tops,x+dx,h+1+y,z+dz,
                        part==BROWN_MUSHROOM_BLOCK ? BROWN_MUSHROOM_TERRAIN_BYTE : part,part==MUSHROOM_STEM);
                }
            return;
        }
        if (info.biome==BIOME_MUSHROOM) return;
    }
    switch (info.biome) {
    case BIOME_FOREST: density = 24; break;
    case BIOME_BIRCH: density = 24; trunk = BIRCH_LOG; ++tall; break;
    case BIOME_ROOFED: density = 16; trunk = DARK_OAK_LOG; broad = 3; thick = 1; break;
    case BIOME_TAIGA: case BIOME_COLD_TAIGA: density = 23; trunk = SPRUCE_LOG; tall += 3; break;
    case BIOME_MEGA_TAIGA: density = 25; trunk = SPRUCE_LOG; tall += 9; thick = 1; broad = 3; break;
    case BIOME_JUNGLE: density = 17; trunk = JUNGLE_LOG; tall += 6; broad = 3; thick = key % 4 == 0; break;
    case BIOME_SAVANNA: density = 58; trunk = ACACIA_LOG; broad = 3; tip = key & 1 ? 1 : -1; break;
    case BIOME_SWAMP: density = 40; broad = 3; --tall; break;
    case BIOME_PLAINS: density = 179; break;
    case BIOME_MOUNTAINS: if (h < 95) { density = 94; trunk = SPRUCE_LOG; tall += 2; } break;
    case BIOME_MUSHROOM: density = 38; broad = 2; trunk = MUSHROOM_STEM; break;
    case BIOME_ICE_SPIKES:
        if (key % 84 == 0) {
            int spike = 6 + (int)((key >> 9) % 15);
            for (int y = 1; y <= spike; ++y) {
                int r = y > spike / 2 ? 0 : y > 2 ? 1 : 2;
                for (int a = -r; a <= r; ++a) for (int b = -r; b <= r; ++b)
                    if (abs(a) + abs(b) <= r) decorate(cells, tops, x + a, h + y, z + b, PACKED_ICE, 0);
            }
        }
        return;
    default: return;
    }
    if (!density || key % (unsigned)density) return;
    int upright = trunk != ACACIA_LOG ? tall : tall - 2;
    for (int y = 1; y <= tall; y++) {
        int lean = y > upright ? tip : 0;
        for (int a = 0; a <= thick; a++) for (int b = 0; b <= thick; b++)
            decorate(cells, tops, x + lean + a, h + y, z + b, trunk, 1);
        if (y == upright && tip) decorate(cells, tops, x + tip, h + y, z, trunk, 1);
    }
    int start = trunk == SPRUCE_LOG ? tall - (thick ? 8 : 5) : trunk == ACACIA_LOG ? tall - 1 : tall - 2;
    int cap = trunk == MUSHROOM_STEM ? RED_MUSHROOM_BLOCK :
        typed_leaves ? natural_leaf(tree_log_species(trunk)) : LEAF;
    for (int y = start; y <= tall + 1; y++) {
        if (cap == RED_MUSHROOM_BLOCK && y < tall) continue;
        int r = y > tall ? 1 : broad;
        if (trunk == SPRUCE_LOG && y != tall + 1) {
            r = y > tall - 2 ? 1 : 1 + ((tall - y) / 2) % broad;
        }
        for (int a = -r; a <= r + thick; ++a) for (int b = -r; b <= r + thick; ++b) {
            if (a == -r && abs(b) >= r && trunk != MUSHROOM_STEM) continue;
            decorate(cells, tops, x + tip + a, h + y, z + b, cap, 0);
        }
    }
}

static void generate_climate(int cx, int cz, uint8_t *cells, uint8_t *tops, uint8_t *biomes) {
    int ox = cx * CHUNK_SIZE, oz = cz * CHUNK_SIZE;
    int typed_leaves=world_generation()>=GENERATOR_FORESTRY;
    ClimateColumn region[EXTENT][EXTENT];
    for (int i = 0; i < EXTENT; ++i) for (int j = 0; j < EXTENT; ++j)
        region[i][j] = climate(ox + i - CROWN_BORDER, oz + j - CROWN_BORDER);
    for (int x = 0; x < CHUNK_SIZE; ++x) for (int z = 0; z < CHUNK_SIZE; ++z) {
        ClimateColumn c = region[x + CROWN_BORDER][z + CROWN_BORDER];
        int i = column_index(x, z), h = c.height;
        uint8_t *dst = cells + i * WY;
        int wx=ox+x,wz=oz+z,surface=climate_surface(c.biome,wx,h,wz);
        VerticalNoise chambers=vertical_noise(wx*.051f,wz*.051f);
        VerticalNoise tunnels=vertical_noise(wx*.094f+64,wz*.094f+11);
        for (int y = 0; y <= h; ++y)
            dst[y] = (uint8_t)climate_cell(wx,y,wz,c,surface,&chambers,&tunnels);
        for (int y = h + 1; y <= SEA; ++y)
            dst[y] = (uint8_t)(y == SEA && c.biome == BIOME_FROZEN_RIVER ? ICE : WATER);
        biomes[i] = (uint8_t)c.biome;
        tops[i] = (uint8_t)(h > SEA ? h : SEA);
    }
    for (int i = 0; i < EXTENT; ++i) for (int j = 0; j < EXTENT; ++j) {
        int x = i - CROWN_BORDER, z = j - CROWN_BORDER;
        if (abs(ox + x) < 5 && abs(oz + z) < 5) continue;
        climate_feature(cells, tops, x, z, region[i][j], hash2(ox + x, oz + z),typed_leaves);
    }
    if (world_generation()>=GENERATOR_FARM) for (int x=0;x<CHUNK_SIZE;++x) for (int z=0;z<CHUNK_SIZE;++z) {
        int col=column_index(x,z),y=tops[col];
        if (y+1<WY && cells[col*WY+y]==GRASS && hash2(ox+x+371,oz+z-719)%11u==0) {
            cells[col*WY+y+1]=TALL_GRASS;
            tops[col]=(uint8_t)(y+1);
        }
    }
    if (world_generation()>=GENERATOR_BOOKS) for (int x=0;x<CHUNK_SIZE;++x) for (int z=0;z<CHUNK_SIZE;++z) {
        int col=column_index(x,z),i=x+CROWN_BORDER,j=z+CROWN_BORDER;
        unsigned r=hash2(ox+x+781,oz+z-118);
        const uint8_t *at=cells+col*WY;
        if (r%19u || region[i][j].height!=SEA || (at[SEA]!=GRASS && at[SEA]!=SAND) || at[SEA+1]!=AIR) continue;
        const ClimateColumn around[]={region[i-1][j],region[i+1][j],region[i][j-1],region[i][j+1]};
        int wet=0;
        for (unsigned n=0;n<4;++n) wet|=around[n].height<SEA && around[n].biome!=BIOME_FROZEN_RIVER;
        if (wet) {
            int stems=2+(int)((r>>6)&1u);
            for (int h=1;h<=stems && at[SEA+h]==AIR;++h) {
                cells[col*WY+SEA+h]=SUGAR_CANE;
                if (tops[col]<SEA+h) tops[col]=(uint8_t)(SEA+h);
            }
        }
    }
}

static void stronghold(int index, int *x, int *z) {
    static const int dir[][2] = {{0,-1},{1,1},{-1,1}};
    int k = (int)(world_seed() % 4);
    int a = dir[index][0], b = dir[index][1];
    while (k-- > 0) { int swap = a; a = -b; b = swap; }
    unsigned r = hash2(778, 191 + index);
    *x = (a * (690 + (int)(r & 127)) / (a && b ? 2 : 1)) + (int)((r >> 16) & 127) - 64;
    *z = (b * (690 + (int)((r >> 7) & 127)) / (a && b ? 2 : 1)) + (int)((r >> 23) & 127) - 64;
}

static int flat_has_stronghold(void) {
    const FlatLayout *flat=terrain_flat();
    if (!(flat->features&FLAT_STRONGHOLD) || flat->height<STRONGHOLD_FLOOR+10) return 0;
    for (int i=STRONGHOLD_FLOOR;i<flat->height;++i) if (!solid(flat->blocks[i])) return 0;
    return 1;
}

int nearest_stronghold(int x, int z, int *center_x, int *center_z) {
    if (!center_x || !center_z || terrain_structures_disabled() || world_dimension()!=OVERWORLD ||
        world_generation()<GENERATOR_ADVENTURE || terrain_uses_aquifer() ||
        (world_profile()==WORLD_SUPERFLAT && !flat_has_stronghold()) ||
        x< -WORLD_SAVE_LIMIT || x>WORLD_SAVE_LIMIT || z< -WORLD_SAVE_LIMIT || z>WORLD_SAVE_LIMIT) return 0;
    int64_t best = INT64_MAX;
    for (int i = 0; i < 3; ++i) {
        int sx, sz;
        stronghold(i, &sx, &sz);
        int64_t dx = (int64_t)x - sx, dz = (int64_t)z - sz;
        int64_t distance = dx*dx + dz*dz;
        if (distance < best) { best = distance; *center_x = sx; *center_z = sz; }
    }
    return 1;
}

/* The last byte is only a logical anvil ID in the live world, never a
   generated terrain byte. A later intersecting structure can replace this
   marker normally; stale geometry cannot reappear when overlays install. */
enum { TERRAIN_SHAPE_BYTE=255 };
static int build_fallback(int raw) {
    int material=build_material(raw);
    return ladder_cell(raw) ? AIR : (unsigned)material<BUILD_MATERIALS ?
        build_material_block(material) : AIR;
}
static void terrain_mark_wide(uint8_t *cells,int index,int raw,int fallback,TerrainWide *wide) {
    if (!wide) { cells[index]=(uint8_t)(fallback>255 ? PLANKS : fallback); return; }
    for (unsigned i=0;i<wide->count;++i) if (wide->cell[i].index==(unsigned)index) {
        wide->cell[i].cell=(uint16_t)raw;
        cells[index]=TERRAIN_SHAPE_BYTE;
        return;
    }
    if (wide->count>=terrain_wide_budget()) return;
    cells[index]=TERRAIN_SHAPE_BYTE;
    wide->cell[wide->count++]=(TerrainWideCell){(uint16_t)index,(uint16_t)raw};
}
void terrain_build_shape(uint8_t *cells,int index,int raw,TerrainWide *wide) {
    if (!cells || (unsigned)index>=CHUNK_CELLS || !(build_partial(raw) || ladder_cell(raw))) return;
    terrain_mark_wide(cells,index,raw,build_fallback(raw),wide);
}
static void finish_architecture(uint8_t *cells,TerrainWide *wide) {
    unsigned keep=0;
    for (unsigned i=0;i<wide->count;++i) {
        TerrainWideCell it=wide->cell[i];
        if (build_partial(it.cell) || ladder_cell(it.cell)) {
            if (cells[it.index]!=TERRAIN_SHAPE_BYTE) continue;
            int fallback=build_fallback(it.cell);
            cells[it.index]=(uint8_t)(fallback>255 ? PLANKS : fallback);
        }
        wide->cell[keep++]=it;
    }
    wide->count=keep;
}
static void resource_cell(TerrainWide *wide,int index,int raw);
static void joint_coast_veneer(int cx,int cz,TerrainResult *out) {
    /* One collectible layer, max 256 sparse overrides. Natural submerged
       rock and scenery under other terrain features remain unchanged. */
    int any=0;
    for (int i=0;i<CHUNK_COLUMNS && !any;++i) any=out->biomes[i]==BIOME_JOINT_COAST;
    if (!any) return;
    uint8_t occupied[CHUNK_COLUMNS]={0};
    for (unsigned i=0;i<out->wide.count;++i) {
        const unsigned index=out->wide.cell[i].index,col=index/WY;
        const int below=(int)out->tops[col]-(int)(index%WY);
        if (below==0 || below==1) occupied[col]|=(uint8_t)(1u<<below);
    }
    for (int x=0;x<CHUNK_SIZE;++x) for (int z=0;z<CHUNK_SIZE;++z) {
        const int col=column_index(x,z);
        if (out->biomes[col]!=BIOME_JOINT_COAST || out->tops[col]<SEA ||
            out->cells[col*WY+out->tops[col]]!=STONE) continue;
        const CoastColumn c=cooling_column(cx*CHUNK_SIZE+x,cz*CHUNK_SIZE+z);
        const int rind=joint_rind(c),index=col*WY+out->tops[col]-rind;
        if (out->wide.count==terrain_wide_budget()) return;
        if (!(occupied[col]&(1u<<rind)) && out->cells[index]==STONE &&
            (!rind || !(occupied[col]&1u)))
            out->wide.cell[out->wide.count++]=(TerrainWideCell){(uint16_t)index,JOINT_BASALT};
    }
}
static void generate_survey_galleries(int cx, int cz, uint8_t *cells, uint8_t *tops,TerrainWide *wide) {
    for (int j = 0; j < 3; ++j) {
        int sx, sz;
        stronghold(j, &sx, &sz);
        int start_x = cx * CHUNK_SIZE - sx, start_z = cz * CHUNK_SIZE - sz;
        if (start_x > 7 || start_x + 15 < -35 || start_z > 6 || start_z + 15 < -6) continue;
        for (int x = 0; x < CHUNK_SIZE; ++x) for (int z = 0; z < CHUNK_SIZE; ++z) {
            int a = start_x + x, b = start_z + z, lo, hi, wall;
            if (a >= -7 && a <= 7 && abs(b) <= 6) {
                lo = STRONGHOLD_FLOOR; hi = lo + 6;
                wall = (a == 7 || abs(b) == 6 || (a == -7 && abs(b) > 1));
            } else if (a >= -35 && a < -7 && abs(b) <= 2) {
                lo = STRONGHOLD_FLOOR + (-a - 7);
                hi = lo + 5;
                wall = abs(b) == 2;
            } else continue;
            int col = column_index(x, z);
            uint8_t *dst = cells + col * WY;
            for (int y = lo; y <= hi; ++y)
                dst[y] = (uint8_t)(wall || y == lo || (y == hi && a != -35) ? STONE_BRICKS : AIR);
            /* Keep the old door, staircase and storage coordinates for saved
               projects. Two exposed benches replace the ring in the room. */
            if (a>=4 && a<=6 && abs(b)>=2 && abs(b)<=4 && !(a==5 && b==-4)) {
                int sample=b<0 ? SLATE_ROCK : LIMESTONE_ROCK;
                dst[lo+1]=STONE;
                if (wide) resource_cell(wide,col*WY+lo+1,sample);
                if (a==6) {
                    dst[lo+2]=STONE;
                    if (wide) resource_cell(wide,col*WY+lo+2,sample);
                }
            }
            if (a==3 && abs(b)==4) dst[lo+1]=TORCH;
            if (world_generation()>=GENERATOR_EQUINE && a==5 && b==-4)
                dst[STRONGHOLD_FLOOR+1]=SADDLE_CHEST;
            if (world_generation()>=GENERATOR_ARCHITECTURE && !wall) {
                if (a< -7) terrain_build_shape(cells,col*WY+lo,build_stair_raw(BUILD_STONE_BRICK,WEST,0),wide);
                else if (a== -3 && abs(b)<=1)
                    terrain_build_shape(cells,col*WY+lo+1,build_stair_raw(BUILD_STONE_BRICK,EAST,0),wide);
            }
            /* The outer end of the stairs is open to the surface. */
            if (a == -35 && abs(b) <= 1) {
                while (hi < tops[col]) dst[++hi] = AIR;
            }
            if (hi > tops[col]) tops[col] = (uint8_t)hi;
            while (tops[col] && dst[tops[col]] == AIR) --tops[col];
        }
    }
}

enum { FORTRESS_REGION = 128 };
typedef struct { int x, z, y, turn; } Fortress;
typedef enum { OUTSIDE, DECK, HALL, ROOM, STAIR } FortressArea;
typedef struct { FortressArea area; int y, wall, support; } FortressColumn;

static int fortress_region(int coord) {
    return coord / FORTRESS_REGION - (coord % FORTRESS_REGION < 0);
}
static Fortress fortress_at(int x, int z) {
    int cx = fortress_region(x), cz = fortress_region(z);
    unsigned r = hash2(cx + 173174, cz - 71563);
    return (Fortress){cx*FORTRESS_REGION+60+(int)(r&7), cz*FORTRESS_REGION+60+(int)((r>>3)&7),
        59+(int)((r>>6)&7), (int)((r>>9)&1)};
}
static FortressColumn fortress_column(int x, int z) {
    Fortress f = fortress_at(x,z);
    int a = (f.turn ? z-f.z : x-f.x), b = (f.turn ? x-f.x : z-f.z);
    int u = abs(a), v = abs(b);
    FortressColumn c = {OUTSIDE, f.y, 0, 0};
    if (u <= 7 && v <= 6) {
        c.area = ROOM;
        c.wall = (u == 7 && v > 1) || (v == 6 && u > 1);
        c.support = u == 7 && v == 6;
    } else if (u <= 5 && v >= 15 && v <= 24) {
        c.area = ROOM;
        c.wall = u == 5 || ((v == 15 || v == 24) && u > 1);
        c.support = u == 5 && (v == 15 || v == 24);
    } else if (u <= 2 && v < 25) {
        c.area = HALL;
        c.wall = u == 2 && v % 6 != 0;
        c.support = u == 2 && v % 8 == 0;
    } else if (v <= 2 && u <= 34) {
        c.area = DECK; c.wall = v == 2; c.support = v == 2 && u % 8 == 0;
    } else if ((v <= 1 && u <= 46) || (u <= 1 && v <= 36)) {
        int along_a = v <= 1;
        int edge = along_a ? 46 : 36, delta = (along_a ? u : v) - (edge-12);
        int sign = (along_a ? a : b) < 0 ? -1 : 1;
        int dx = (f.turn == along_a) ? 0 : sign*edge;
        int dz = (f.turn == along_a) ? sign*edge : 0;
        int dest = nether_floor(f.x+dx,f.z+dz);
        if (dest < 33) dest = 33;
        if (dest < f.y-12) dest = f.y-12;
        if (dest > f.y+12) dest = f.y+12;
        c.area = STAIR;
        c.y += (dest-f.y)*delta/12;
    }
    return c;
}
int nether_fortress_floor(int x, int z) {
    if (terrain_structures_disabled() || world_generation() < GENERATOR_FORTRESS) return -1;
    FortressColumn c = fortress_column(x,z);
    return c.area == ROOM && !c.wall ? c.y : -1;
}

static void generate_nether(int cx, int cz, uint8_t *cells, uint8_t *tops, uint8_t *biomes,TerrainWide *wide) {
    int fortresses=!terrain_structures_disabled() && world_generation()>=GENERATOR_FORTRESS;
    for (int x = 0; x < CHUNK_SIZE; ++x) for (int z = 0; z < CHUNK_SIZE; ++z) {
        int wx = cx * CHUNK_SIZE + x, wz = cz * CHUNK_SIZE + z;
        int floor = nether_floor(wx,wz), col = column_index(x,z);
        int roof = 91 + (int)(23 * noise2(wx*.029f - 37, wz*.029f + 309));
        if (noise2(wx*.039f - 41, wz*.039f + 302) > .82f) floor = roof;
        uint8_t *dst = cells + col * WY;
        int porous = floor < 43 && noise2(wx*.046f + 291, wz*.046f - 73) > .61f;
        for (int y = 0; y <= floor; ++y) {
            unsigned ore = hash2(wx + y*31, wz - y*121);
            dst[y] = !y ? BEDROCK : y == floor && porous ? ASH_SAND :
                ore % 43 < 2 ? NETHER_QUARTZ_ORE : NETHERRACK;
        }
        for (int y = floor + 1; y <= 31; ++y) dst[y] = LAVA;
        for (int y = roof; y <= 127; ++y) dst[y] = (uint8_t)(y == 127 ? BEDROCK : NETHERRACK);
        if (floor < roof - 5 && hash2(wx/2+257,wz/2-177) % 21 == 0)
            dst[roof - 1] = GLOWSTONE;
        if (fortresses) {
            FortressColumn c = fortress_column(wx,wz);
            if (c.area != OUTSIDE) {
                int base = c.support ? 32 : c.y-1;
                for (int y = base; y <= c.y; ++y) dst[y] = NETHER_BRICKS;
                int covered = c.area == HALL || c.area == ROOM;
                for (int y = 1; y <= 5; ++y)
                    dst[c.y+y] = (uint8_t)((c.wall && (covered || y == 1)) ||
                        (covered && y == 5) ? NETHER_BRICKS : AIR);
                if (wide && world_generation()>=GENERATOR_ARCHITECTURE && !c.wall && c.area!=ROOM)
                  for (int d=0;d<4;++d) {
                    FortressColumn lower=fortress_column(wx+rail_dx(d),wz+rail_dz(d));
                    if (lower.area==OUTSIDE || lower.wall || lower.y!=c.y-1 ||
                        (c.area!=STAIR && lower.area!=STAIR)) continue;
                    terrain_build_shape(cells,col*WY+c.y,build_stair_raw(BUILD_NETHER_BRICK,(d+2)&3,0),wide);
                    break;
                }
                if (world_generation()>=GENERATOR_EQUINE && c.area==ROOM && !c.wall) {
                    Fortress f=fortress_at(wx,wz);
                    if (wx==f.x && wz==f.z) dst[c.y+1]=SADDLE_CHEST;
                    int a=f.turn ? wz-f.z : wx-f.x,b=f.turn ? wx-f.x : wz-f.z;
                    if (world_generation()>=GENERATOR_FILTER_ARCHIVE && a>=3 && a<=5 && b>=3 && b<=4) {
                        dst[c.y]=ASH_SAND;
                        dst[c.y+1]=FILTER_MAT_END-1;
                    }
                }
            }
        }
        tops[col] = 127;
        biomes[col] = BIOME_NETHER;
    }
}

static void generate_archived_region(int cx,int cz,uint8_t *cells,uint8_t *tops,uint8_t *biomes) {
    /* The bank is not a destination. Keep a neutral foundation under old
       construction without recreating the encounter's structures. */
    for (int x=0;x<CHUNK_SIZE;++x) for (int z=0;z<CHUNK_SIZE;++z) {
        int wx=cx*CHUNK_SIZE+x,wz=cz*CHUNK_SIZE+z,col=column_index(x,z);
        int h=end_floor(wx,wz), top=h;
        uint8_t *dst=cells+col*WY;
        biomes[col]=BIOME_END;
        if (h) {
            int thickness=9+(int)(21*noise2(wx*.052f+19,wz*.052f-713));
            for (int y=h-thickness;y<=h;++y) dst[y]=STONE;
        }
        if (wx>=98 && wx<=102 && wz>=-2 && wz<=2) {
            dst[48]=STONE;
            for (int y=49;y<=52;++y) dst[y]=AIR;
            if (top<48) top=48;
            while (top && dst[top]==AIR) --top;
        }
        if (abs(wx)<=3 && abs(wz)<=3) {
            for (int y=h;y<=68;++y) dst[y]=STONE;
            if (top<68) top=68;
        }
        tops[col]=(uint8_t)top;
    }
}

static void generate_aquifer(int cx,int cz,uint8_t *cells,uint8_t *tops,uint8_t *biomes,TerrainWide *wide) {
    /* The site is enclosed rock, with two independent corridors and a dry
       returning dock. Vary the roof and rock bands, never the service points. */
    for (int x=0;x<CHUNK_SIZE;++x) for (int z=0;z<CHUNK_SIZE;++z) {
        int wx=cx*CHUNK_SIZE+x,wz=cz*CHUNK_SIZE+z,col=column_index(x,z);
        int hub=wx>=-6 && wx<=6 && wz>=-5 && wz<=5;
        int wet=wx>=-21 && wx< -6 && wz>=-3 && wz<=3;
        int hot=wx>6 && wx<=21 && wz>=-3 && wz<=3;
        int floor=AQUIFER_FLOOR;
        if (wet) floor=wx>=-10 ? AQUIFER_FLOOR+wx+6 : 50;
        int roof=AQUIFER_ROOF+(int)(hash2(wx*3+172,wz*3-888)%3u);
        uint8_t *base=cells+col*WY;
        memset(base,STONE,AQUIFER_ROOF+16);
        base[0]=BEDROCK;
        base[AQUIFER_ROOF+15]=BEDROCK;
        tops[col]=AQUIFER_ROOF+15;
        biomes[col]=BIOME_END;
        if (!hub && !wet && !hot) continue;
        memset(base+floor+1,AIR,(size_t)(roof-floor-1));
        if (wet && floor<=52) {
            base[floor]=CLAY;
            for (int y=floor+1;y<=53;++y) base[y]=WATER;
        } else if (hot && (wz==-3 || wz==3)) base[floor]=TERRACOTTA;
        if (terrain_generation()>=GENERATOR_SEEP_GARDENS && wx==SEEP_GARDEN_X) {
            if (wz==SEEP_GARDEN_Z) {
                terrain_mark_wide(cells,col*WY+SEEP_GARDEN_Y,SEEP_BED,STONE,wide);
                base[SEEP_GARDEN_Y]=STONE;
            } else if (wz==SEEP_GARDEN_Z-1) base[SEEP_GARDEN_Y]=STONE;
        }
        if ((hub && (wx==-4 || wx==4) && (wz==-4 || wz==4)) ||
            (hot && (wx==9 || wx==18) && wz==2)) base[floor+1]=TORCH;
        if (hot && abs(wx-AQUIFER_SEAL_X)+abs(wz)==1)
            base[AQUIFER_SEAL_Y]=TERRACOTTA;
        if (hot && wx==AQUIFER_SEAL_X && wz==0) base[AQUIFER_SEAL_Y]=AIR;
        int item=wx==0 && wz==PRESSURE_AQUIFER_HATCH_Z && terrain_generation()>=GENERATOR_PRESSURE_WORKS ?
            PRESSURE_HATCH : wx==0 && wz==AQUIFER_DOCK_Z ? AQUIFER_DOCK : wx==AQUIFER_BRINE_X && wz==0 ?
            AQUIFER_BRINE : wx==AQUIFER_VENT_X && wz==0 ? AQUIFER_VENT : AIR;
        if (item!=AIR) {
            int y=item==AQUIFER_BRINE ? AQUIFER_BRINE_Y : floor+1;
            int at=col*WY+y;
            terrain_mark_wide(cells,at,item,STONE,wide);
            base[y]=STONE;
        } else if (wet && (wx==-11 || wx==-18) && wz==3) {
            int at=col*WY+54;
            terrain_mark_wide(cells,at,AQUIFER_LIGHT,STONE,wide);
            base[54]=STONE;
        }
    }
}

static void generate_pressure_works(int cx,int cz,uint8_t *cells,uint8_t *tops,uint8_t *biomes,TerrainWide *wide) {
    for (int x=0;x<CHUNK_SIZE;++x) for (int z=0;z<CHUNK_SIZE;++z) {
        const int wx=cx*CHUNK_SIZE+x,wz=cz*CHUNK_SIZE+z,col=column_index(x,z),side=abs(wx);
        uint8_t *base=cells+col*WY;
        const int drill=side<=3 && wz>=-19 && wz<=5;
        const int manifold=side<=14 && wz>=-8 && wz<=-4;
        const int chamber=side<=7 && wz>=-19 && wz<=-13;
        const int alcove=side>=9 && side<=15 && wz>=-11 && wz<=-3;
        const int cap=PRESSURE_FLOOR+16;
        for (int y=0;y<=cap;++y)
            base[y]=y==0 || y==cap ? BEDROCK :
                ((y+side/3)%8==0 ? TERRACOTTA : STONE);
        tops[col]=(uint8_t)cap;
        biomes[col]=BIOME_NETHER;
        if (!drill && !manifold && !chamber && !alcove) continue;
        const int y=PRESSURE_FLOOR+1;
        int height=chamber ? 10 : alcove ? 7 : 5;
        if (chamber && side>=6) height-=3;
        memset(base+y,AIR,(size_t)height);
        base[y-1]=(manifold && wz==PRESSURE_VALVE_Z) || (drill && !wx) ? TERRACOTTA :
            (alcove && ((wx+wz)&3)==0 ? CLAY : STONE);
        if (terrain_generation()>=GENERATOR_SHELL_HABITATS && alcove && side>=11 && wz<=-9) {
            terrain_mark_wide(cells,col*WY+y-1,LIMESTONE_ROCK,STONE,wide);
            base[y-1]=STONE;
        }
        const int fixture=wx==0 && wz==PRESSURE_LIFT_Z ? PRESSURE_LIFT :
            pressure_valve_site(wx,y,wz) ? PRESSURE_CLOSED :
            wx==0 && wz==PRESSURE_KILN_Z ? PRESSURE_KILN : AIR;
        if (fixture) {
            terrain_mark_wide(cells,col*WY+y,fixture,STONE,wide);
            base[y]=STONE;
        } else if ((side==3 && (wz==1 || wz==-11)) ||
                   (side==6 && wz==-16) || (side==13 && wz==-9)) {
            terrain_mark_wide(cells,col*WY+y+2,AQUIFER_LIGHT,STONE,wide);
            base[y+2]=STONE;
        }
    }
}

static void wild_pumpkins(int cx,int cz,const uint8_t *cells,const uint8_t *tops,TerrainWide *wide) {
    enum { PATCH=24 };
    _Static_assert(TERRAIN_WIDE_LIMIT>=4*13,"A chunk intersects at most four diamond-shaped patches");
    for (int x=0;x<CHUNK_SIZE;++x) for (int z=0;z<CHUNK_SIZE;++z) {
        int wx=cx*CHUNK_SIZE+x,wz=cz*CHUNK_SIZE+z,c=column_index(x,z),y=tops[c];
        if (cells[c*WY+y]!=GRASS || y+1>=WY) continue;
        int gx=wx/PATCH-(wx%PATCH<0),gz=wz/PATCH-(wz%PATCH<0);
        unsigned seed=hash2(gx+17240,gz-2141);
        if (seed%5u) continue;
        int a=wx-gx*PATCH-3-(int)((seed>>12)%18u),b=wz-gz*PATCH-3-(int)((seed>>20)%18u);
        if (abs(a)+abs(b)>2 || ((a||b) && hash2(wx+719,wz-311)%3u)) continue;
        unsigned face=hash2(wx+17,wz-29)&3u;
        if (wide->count<terrain_wide_budget())
            wide->cell[wide->count++]=(TerrainWideCell){(uint16_t)(c*WY+y+1),(uint16_t)(PUMPKIN_FACE_BASE+face)};
    }
}
static int wide_used(const TerrainWide *wide,int index) {
    for (unsigned i=0;i<wide->count;++i) if (wide->cell[i].index==index) return 1;
    return 0;
}
static void resource_cell(TerrainWide *wide,int index,int raw) {
    if (wide->count<terrain_wide_budget() && !wide_used(wide,index))
        wide->cell[wide->count++]=(TerrainWideCell){(uint16_t)index,(uint16_t)raw};
}
static void swamp_wetlands(int cx,int cz,uint8_t *cells,const uint8_t *tops,const uint8_t *biomes,TerrainWide *wide) {
    /* The older swamp shape is mostly above sea level. Give newly generated
       swamps shallow, level pools, without pruning trees or changing banks.
       The elevation constraint uses absolute neighbours across chunk seams. */
    for (int col=0;col<CHUNK_SIZE*CHUNK_SIZE;++col) {
        if (biomes[col]!=BIOME_SWAMP || tops[col]<SEA || tops[col]>SEA+9) continue;
        int x=cx*CHUNK_SIZE+col/CHUNK_SIZE,z=cz*CHUNK_SIZE+col%CHUNK_SIZE;
        int y=tops[col],at=col*WY+y;
        if (cells[at+1]!=AIR || (wide && (wide_used(wide,at)||wide_used(wide,at+1)))) continue;
        if (cells[at]==GRASS && noise2(x*.075f+94.f,z*.075f-121.f)>.59f &&
            climate(x-1,z).height>=y && climate(x+1,z).height>=y &&
            climate(x,z-1).height>=y && climate(x,z+1).height>=y) cells[at]=WATER;
        if (wide && cells[at]==WATER && hash2(x^0x4c7931,z^0x53706d)%7u==0)
            resource_cell(wide,at+1,LILY_PAD);
    }
}
static void wild_mushrooms(int cx,int cz,const uint8_t *cells,const uint8_t *tops,
                           const uint8_t *biomes,TerrainWide *wide) {
    for (int i=0;i<7;++i) {
        unsigned roll=hash2(cx*7+i-6721,cz+8449);
        int col=(int)(roll&255u),y=tops[col]+1;
        Biome b=(Biome)biomes[col];
        if (world_generation()>=GENERATOR_MUSHROOM_ISLAND) {
            /* Look beneath the canopy, not just on its top surface. */
            int x=cx*CHUNK_SIZE+col/CHUNK_SIZE,z=cz*CHUNK_SIZE+col%CHUNK_SIZE;
            y=climate(x,z).height+1;
            if (y<1 || y>=WY || cells[col*WY+y]!=AIR) continue;
            int soil=cells[col*WY+y-1],shade=tops[col]>y;
            if (soil==MYCELIUM || soil==PODZOL ||
                (shade && soil==GRASS && (b==BIOME_SWAMP || b==BIOME_ROOFED ||
                    ((b==BIOME_FOREST || b==BIOME_BIRCH || b==BIOME_TAIGA) && (roll>>23)%4u==0))))
                resource_cell(wide,col*WY+y,(roll>>18)&1u ? RED_MUSHROOM : BROWN_MUSHROOM);
            continue;
        }
        if (y<1 || y>=WY || cells[col*WY+y]!=AIR) continue;
        int ground=cells[col*WY+y-1];
        if (ground==MYCELIUM || ground==PODZOL ||
            (ground==GRASS && (b==BIOME_ROOFED || b==BIOME_SWAMP ||
            ((b==BIOME_TAIGA || b==BIOME_FOREST || b==BIOME_BIRCH) && (roll>>18)%5u==0u))))
            resource_cell(wide,col*WY+y,BROWN_MUSHROOM);
    }
}
static void wild_dyes(int cx,int cz,const uint8_t *cells,const uint8_t *tops,
    const uint8_t *biomes,TerrainWide *wide,int surface) {
    _Static_assert(TERRAIN_WIDE_LIMIT>=52+21+8+18+5,"Combined independent resource overlay budget");
    static const int around[][3]={{0,0,0},{1,0,0},{-1,0,0},{0,0,1},{0,0,-1},{0,1,0},{0,-1,0}};
    for (int i=0;i<3;++i) {
        unsigned r=hash2(cx*3+i+18289,cz-8151);
        int x=1+(int)(r%14),z=1+(int)((r>>8)%14);
        int y=4+(int)((r>>16)%13)+(int)((r>>24)%13);
        for (int k=0;k<7;++k) {
            if (k && !(r&(1u<<(k+5)))) continue;
            int idx=column_index(x+around[k][0],z+around[k][2])*WY+y+around[k][1];
            if (cells[idx]==STONE) resource_cell(wide,idx,LAPIS_ORE_CELL);
        }
    }
    if (!surface) return;
    for (int i=0;i<8;++i) {
        unsigned r=hash2(cx*8+i-4121,cz+2513);
        int col=(int)(r&255u),y=tops[col]+1;
        if (y<1 || y>=WY || cells[col*WY+y-1]!=GRASS || cells[col*WY+y]!=AIR || (r>>16)%3u==0) continue;
        if (biomes[col]!=BIOME_MUSHROOM) resource_cell(wide,col*WY+y,(r>>20)%5u==0 ? POPPY_CELL : DANDELION_CELL);
    }
    for (int i=0;i<6;++i) {
        unsigned r=hash2(cx*6+i+29281,cz-348);
        int x=1+(int)(r%14),z=1+(int)((r>>8)%14),col=column_index(x,z),y=tops[col]+1;
        if (y<1 || y+2>=WY || (biomes[col]!=BIOME_DESERT && biomes[col]!=BIOME_MESA) ||
            (cells[col*WY+y-1]!=SAND && cells[col*WY+y-1]!=RED_SAND)) continue;
        int height=1+(int)((r>>16)%3);
        for (int j=0;j<height;++j) {
            int idx=col*WY+y+j,open=cells[idx]==AIR && !wide_used(wide,idx);
            for (int d=0;d<4 && open;++d) {
                int peer=column_index(x+rail_dx(d),z+rail_dz(d))*WY+y+j;
                open=cells[peer]==AIR && !wide_used(wide,peer);
            }
            if (!open) break;
            resource_cell(wide,idx,CACTUS_BASE);
        }
    }
    int pods=0;
    for (int x=1;x<CHUNK_SIZE-1 && pods<5;++x) for (int z=1;z<CHUNK_SIZE-1 && pods<5;++z) {
        int col=column_index(x,z);
        if (biomes[col]!=BIOME_JUNGLE) continue;
        for (int y=tops[col];y>62 && y>tops[col]-18 && pods<5;--y) if (cells[col*WY+y]==JUNGLE_LOG) {
            unsigned r=hash2(cx*CHUNK_SIZE+x+y*43,cz*CHUNK_SIZE+z-y*17);
            if (r%9u) continue;
            int dir=(int)((r>>12)&3u),peer=column_index(x+rail_dx(dir),z+rail_dz(dir));
            int idx=peer*WY+y;
            if (cells[idx]!=AIR || wide_used(wide,idx)) continue;
            resource_cell(wide,idx,COCOA_BASE+8+((dir+2)&3));
            ++pods;
        }
    }
}
static void terrain_chunk(int cx,int cz,uint8_t *cells,uint8_t *tops,uint8_t *biomes,TerrainWide *wide,TerrainDevices *devices) {
    memset(cells, 0, CHUNK_SIZE * CHUNK_SIZE * WY);
    if (wide) wide->count=0;
    if (devices) devices->count=0;
    if (world_dimension() == NETHER) {
        if (terrain_generation()>=GENERATOR_PRESSURE_WORKS) generate_pressure_works(cx,cz,cells,tops,biomes,wide);
        else generate_nether(cx,cz,cells,tops,biomes,wide);
        return;
    }
    if (world_dimension() == END) {
        if (terrain_uses_aquifer()) generate_aquifer(cx,cz,cells,tops,biomes,wide);
        else generate_archived_region(cx,cz,cells,tops,biomes);
        return;
    }
    const int flat=world_profile()==WORLD_SUPERFLAT;
    if (flat) {
        const FlatLayout *layout=terrain_flat();
        const int x0=cx*CHUNK_SIZE,z0=cz*CHUNK_SIZE;
        int near_spawn=x0<=4 && x0+CHUNK_SIZE-1>=-4 && z0<=4 && z0+CHUNK_SIZE-1>=-4;
        for (int col=0;col<CHUNK_SIZE*CHUNK_SIZE;++col) {
            uint8_t *p=cells+col*WY;
            memcpy(p,layout->blocks,layout->height);
            int platform=near_spawn && flat_spawn_platform(layout,x0+col/CHUNK_SIZE,z0+col%CHUNK_SIZE);
            if (platform) p[layout->height]=COBBLESTONE;
            tops[col]=(uint8_t)(layout->height-1+platform);
            biomes[col]=layout->biome;
        }
        if (!terrain_uses_aquifer() && !terrain_structures_disabled() && flat_has_stronghold())
            generate_survey_galleries(cx,cz,cells,tops,wide);
    } else if (world_generation() == GENERATOR_LEGACY) generate_legacy(cx, cz, cells, tops, biomes);
    else generate_climate(cx, cz, cells, tops, biomes);
    if (!flat && world_generation() >= GENERATOR_ADVENTURE) {
        for (int x = 0; x < CHUNK_SIZE; ++x) for (int z = 0; z < CHUNK_SIZE; ++z) {
            int wx = cx*CHUNK_SIZE + x, wz = cz*CHUNK_SIZE + z;
            uint8_t *col = cells + column_index(x,z) * WY;
            if (noise2(wx*.035f + 666,wz*.035f - 829) > .67f)
                for (int y = 1; y <= 11; ++y) if (col[y] == AIR) col[y] = LAVA;
        }
        if (!terrain_uses_aquifer() && !terrain_structures_disabled()) generate_survey_galleries(cx,cz,cells,tops,wide);
    }
    if (world_generation()>=GENERATOR_DUNGEONS && !flat) {
        if (world_generation()>=GENERATOR_STRATA_GALLERIES) generate_strata_gallery(cx,cz,cells,wide);
        else generate_dungeon(cx,cz,cells,wide);
    }
    MineshaftPlan mine;
    if (mineshaft_plan(*terrain_snapshot,cx*CHUNK_SIZE,cz*CHUNK_SIZE,&mine))
        mineshaft_generate_chunk(&mine,cx,cz,cells,wide);
    SpillwayPlan spillway;
    if (spillway_plan_for(cx*CHUNK_SIZE,cz*CHUNK_SIZE,&spillway)) {
        const BlockEdit lo=spillway_cell(spillway,SPILLWAY_BEGIN,SPILLWAY_NEAR,0,0),
            hi=spillway_cell(spillway,SPILLWAY_END,SPILLWAY_FAR,0,0);
        if (lo.x<(cx+1)*CHUNK_SIZE && hi.x>=cx*CHUNK_SIZE &&
            lo.z<(cz+1)*CHUNK_SIZE && hi.z>=cz*CHUNK_SIZE)
            spillway_generate_chunk(spillway,cx,cz,cells,wide);
    }
    if (!flat && world_generation() >= GENERATOR_RAILWAYS) {
        for (int x=0;x<CHUNK_SIZE;++x) for (int z=0;z<CHUNK_SIZE;++z) {
            int wx=cx*CHUNK_SIZE+x,wz=cz*CHUNK_SIZE+z;
            uint8_t *column=cells+column_index(x,z)*WY;
            for (int y=2;y<=16;++y)
                if (column[y]==STONE && hash2((int)floorf((wx+y*.25f)*.5f),(int)floorf(wz*.5f)+y/2)%137<3)
                    column[y]=REDSTONE_ORE;
        }
    }
    int treasure=world_generation()>=GENERATOR_TREASURE;
    if (treasure && !flat) {
        /* Classic single-cell attempts, independent of ore/structure RNG
           and of the order in which neighbouring chunks become resident. */
        unsigned key=hash2(cx^0x4f7265,cz^0x456d);
        unsigned tries=3+key%6;
        for (unsigned i=0;i<tries;++i) {
            unsigned roll=hash2((int)(key>>8)^(int)(i*0x63du),cz+cx);
            int column=(int)(roll&255u),y=4+(int)((roll>>8)%28u);
            if (biomes[column]==BIOME_MOUNTAINS && cells[column*WY+y]==STONE)
                cells[column*WY+y]=EMERALD_ORE_CELL;
        }
    }
    VillagePlan village;
    WindcutPlan arid;
    const int arid_here=arid_site_plan_for(cx*CHUNK_SIZE,cz*CHUNK_SIZE,&arid) && arid_site_overlaps(arid,cx,cz);
    const int windcut_here=arid_here && world_generation()>=GENERATOR_WINDCUT;
    int decorate_flat=flat && (terrain_flat()->features&FLAT_DECORATION);
    if (!village_plan_for(cx*CHUNK_SIZE,cz*CHUNK_SIZE,&village)) {
        if (!flat || decorate_flat) {
            if (wide && world_generation()>=GENERATOR_GOLEMS) wild_pumpkins(cx,cz,cells,tops,wide);
            if (wide && world_generation()>=GENERATOR_DYES) wild_dyes(cx,cz,cells,tops,biomes,wide,1);
            if (wide && world_generation()>=GENERATOR_FUNGI) wild_mushrooms(cx,cz,cells,tops,biomes,wide);
        }
    } else {
        village_generate_chunk(&village,cx,cz,cells,tops,treasure,wide);
        if (wide && world_generation()>=GENERATOR_DOORS) {
            _Static_assert(TERRAIN_WIDE_LIMIT>=2*VILLAGE_HOMES,"Village door list must fit");
            for (int i=0;i<VILLAGE_HOMES;++i) {
                int x,z,dir;
                if (!village_home_door(&village,i,&x,&z,&dir)) continue;
                x-=cx*CHUNK_SIZE; z-=cz*CHUNK_SIZE;
                if ((unsigned)x>=CHUNK_SIZE || (unsigned)z>=CHUNK_SIZE) continue;
                int idx=(x*CHUNK_SIZE+z)*WY+village.floor+1;
                int lower=DOOR_BASE+dir;
                if (wide->count+2>terrain_wide_budget()) break;
                wide->cell[wide->count++]=(TerrainWideCell){(uint16_t)idx,(uint16_t)lower};
                wide->cell[wide->count++]=(TerrainWideCell){(uint16_t)(idx+1),(uint16_t)door_peer(lower)};
            }
            if ((!flat || decorate_flat) && world_generation()>=GENERATOR_DYES)
                wild_dyes(cx,cz,cells,tops,biomes,wide,0);
        }
    }
    if (windcut_here)
        windcut_generate_chunk(arid,world_seed(),cx,cz,cells,tops,wide,devices);
    else if (arid_here)
        desert_pyramid_generate_chunk((DesertPyramidPlan){arid.x,arid.y,arid.z,arid.rotation},
            cx,cz,cells,tops,wide,devices);
    if (flat) return;
    const int x0=cx*CHUNK_SIZE,z0=cz*CHUNK_SIZE;
    RainGardenPlan canopy;
    if (canopy_site_plan_for(x0,z0,&canopy) &&
        canopy.x+RAIN_GARDEN_RADIUS>=x0 && canopy.x-RAIN_GARDEN_RADIUS<x0+CHUNK_SIZE &&
        canopy.z+RAIN_GARDEN_RADIUS>=z0 && canopy.z-RAIN_GARDEN_RADIUS<z0+CHUNK_SIZE) {
        if (world_generation()>=GENERATOR_RAIN_GARDEN)
            rain_garden_generate_chunk(canopy,world_seed(),cx,cz,cells,tops,wide,devices);
        else
            temple_generate_chunk((TemplePlan){canopy.x,canopy.y,canopy.z,canopy.rotation},
                world_seed(),cx,cz,cells,tops,wide,devices);
    }
    if (world_generation()>=GENERATOR_WETLANDS) swamp_wetlands(cx,cz,cells,tops,biomes,wide);
}
void terrain_generate(TerrainKey key,int cx,int cz,uint8_t *cells,uint8_t *tops,uint8_t *biomes) {
    const TerrainKey *previous=terrain_snapshot;
    terrain_snapshot=&key;
    terrain_chunk(cx,cz,cells,tops,biomes,NULL,NULL);
    terrain_snapshot=previous;
}
void terrain_prepare(TerrainKey key,int cx,int cz,TerrainResult *out) {
    const TerrainKey *previous=terrain_snapshot;
    terrain_snapshot=&key;
    out->x=cx; out->z=cz; out->reused=0;
    terrain_chunk(cx,cz,out->cells,out->tops,out->biomes,&out->wide,&out->devices);
    if (key.generation>=GENERATOR_JOINT_COAST && key.dimension==OVERWORLD && key.profile!=WORLD_SUPERFLAT)
        joint_coast_veneer(cx,cz,out);
    if (key.generation>=GENERATOR_GEOLOGY && key.dimension==OVERWORLD && key.profile!=WORLD_SUPERFLAT) {
        /* Deterministic thin layers, separate from other ore and structure RNG.
           Work is capped per chunk and never looks up neighbouring chunks. */
        for (int i=0;i<6;++i) {
            unsigned sample=hash2(cx*29+i*7+941,cz*31-2368);
            int x=1+(int)(sample%14u),z=1+(int)((sample>>9)%14u);
            int col=column_index(x,z),rock=i<3 ? SLATE_ROCK : LIMESTONE_ROCK;
            int y=i<3 ? 13+(int)((sample>>18)%20u) :
                (int)out->tops[col]-7-(int)((sample>>18)%7u);
            if (y<6 || y>=WY) continue;
            for (int dx=-1;dx<=1;++dx) for (int dz=-1;dz<=1;++dz) {
                int idx=column_index(x+dx,z+dz)*WY+y;
                if (out->cells[idx]==STONE && !wide_used(&out->wide,idx))
                    resource_cell(&out->wide,idx,rock);
            }
        }
    }
    if (key.generation>=GENERATOR_ARCHITECTURE) finish_architecture(out->cells,&out->wide);
    terrain_snapshot=previous;
    out->emitter_mask=light_summarize(out->cells,out->tops,out->sky_tops,out->emitters,out->cover);
}
