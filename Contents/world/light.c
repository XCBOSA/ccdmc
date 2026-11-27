#include "light.h"
#include "internal.h"
#include "terrain_cover.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* The highest source travels at most 14 cells. The 3x3 chunk halo
   therefore includes every possible path to the central chunk plus
   its one-cell face samples, even along a diagonal or across a seam. */
enum { EDGE=3*CHUNK_SIZE, VOLUME=EDGE*EDGE*WY, OUTPUT=LIGHT_VIEW_EDGE*LIGHT_VIEW_EDGE*WY, CACHE=18 };
enum { IDLE, QUEUED, SOLVING, COMPLETE };
typedef struct { int x,z; unsigned long long epoch,revision[9]; } Key;
typedef struct {
    Key key;
    uint8_t cells[9][CHUNK_CELLS];
    ChunkHigh owned_high[9], view_high[9];
    const ChunkHigh *high[9];
    unsigned bands[9];
    uint8_t level[OUTPUT];
} Job;
typedef struct { Key key; uint8_t *level; unsigned long long use; } Result;
typedef struct { uint8_t levels[VOLUME]; int previous[VOLUME],next[VOLUME]; } Scratch;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static pthread_t worker;
static int running,stopping,status,success;
static unsigned long long epoch=1,used;
static Result cache[CACHE];
static Job job;
static Scratch *scratch;

int light_emission(int raw) {
    if (lava_flow_cell(raw)) return 15;
    if (ender_chest_cell(raw)) return 7;
    if (air_station_cell(raw)) return raw==AIR_STATION_BASE ? 0 : 6;
    switch (raw) {
    case AQUIFER_WINCH: case AQUIFER_DOCK: case AQUIFER_BRINE: case AQUIFER_VENT: return 10;
    case AQUIFER_LIGHT: return 14;
    case LAMP_ON: case GLOWSTONE: case LAVA: case FIRE_CELL: return 15;
    case TORCH: return 14;
    case PRESSURE_HATCH: case PRESSURE_LIFT: case PRESSURE_KILN: return 13;
    case PRESSURE_CLOSED: case PRESSURE_OPEN: return 11;
    case RESONANCE_MARKER: return 11;
    case SEEP_BED: return 4;
    case SEEP_TILE: return 7;
    case SEEP_TILE_BRIGHT: return 12;
    case BREWING_STAND: return 1;
    default: return raw>=TORCH_POWER_BASE+TORCH_POWER_ON && raw<TORCH_POWER_END ? 7 : 0;
    }
}
int light_opacity(int raw) {
    int type=cell_type(raw);
    if (type==WATER || type==ICE) return 3;
    if (type==GLASS || type==LEAF || type==AIR_STATION || type==MONSTER_SPAWNER || chest_shape_type(type) || type==FARMLAND ||
        type==ENCHANTING_TABLE || type==ANVIL || type==HOPPER || !solid(type)) return 1;
    return 16;
}
unsigned light_summarize(const uint8_t *cells,const uint8_t *tops,int16_t *sky_tops,
                         uint16_t *emitters,TerrainColumnCover *cover) {
    memset(emitters,0,LIGHT_BANDS*sizeof *emitters);
    for (int i=0;i<CHUNK_COLUMNS;++i) {
        int y=tops[i];
        const uint8_t *col=cells+i*WY;
        for (int word=0;word<TERRAIN_COVER_WORDS;++word) {
            int start=word*64,end=y+1;
            if (end>start+64) end=start+64;
            uint64_t dry=0,seal=0;
            for (int j=start;j<end;++j) {
                const unsigned raw=col[j];
                const int wet=water_strength((int)raw)!=0;
                dry|=(uint64_t)(raw!=AIR && !wet)<<(j-start);
                seal|=(uint64_t)(wet || terrain_full_cover[raw])<<(j-start);
                /* No generating byte below the first luminous block emits. */
                if (raw>=GLOWSTONE && light_emission((int)raw)) ++emitters[j/LIGHT_BAND_HEIGHT];
            }
            cover[i].dry[word]=dry;
            cover[i].seal[word]=seal;
        }
        while (y>=0 && light_opacity(col[y])==1) --y;
        sky_tops[i]=(int16_t)y;
    }
    unsigned mask=0;
    for (int i=0;i<LIGHT_BANDS;++i) if (emitters[i]) mask|=1u<<i;
    return mask;
}
static int voxel(int x,int y,int z) { return (x*EDGE+z)*WY+y; }
static int sample(const Job *j,int x,int y,int z) {
    int chunk=(x/CHUNK_SIZE)*3+z/CHUNK_SIZE,index=((x%CHUNK_SIZE)*CHUNK_SIZE+z%CHUNK_SIZE)*WY+y;
    return chunk_snapshot_cell(j->cells[chunk],j->high[chunk],index);
}
/* Intrusive strength buckets: a pending voxel may be promoted without
   duplicates, even when several bright sources overlap a dim one. */
static void queue(Scratch *s,int heads[16],int k,int value) {
    int old=s->levels[k];
    if (old>=value) return;
    if (old) {
        int a=s->previous[k], b=s->next[k];
        if (a<0) heads[old]=b; else s->next[a]=b;
        if (b>=0) s->previous[b]=a;
    }
    s->levels[k]=(uint8_t)value;
    s->previous[k]=-1;
    s->next[k]=heads[value];
    if (heads[value]>=0) s->previous[heads[value]]=k;
    heads[value]=k;
}
static int solve(Job *j) {
    if (!scratch) scratch=(Scratch *)malloc(sizeof *scratch);
    Scratch *s=scratch;
    if (!s) return 0;
    memset(s->levels,0,sizeof s->levels);
    int heads[16];
    uint8_t power[256],cost[256];
    for (int i=0;i<16;++i) heads[i]=-1;
    for (int i=0;i<256;++i) {
        power[i]=(uint8_t)light_emission(i);
        cost[i]=(uint8_t)light_opacity(i);
    }
    for (int chunk=0;chunk<9;++chunk) if (j->bands[chunk]) {
        int ox=(chunk/3)*CHUNK_SIZE,oz=(chunk%3)*CHUNK_SIZE;
        for (int band=0;band<LIGHT_BANDS;++band) if (j->bands[chunk]&(1u<<band))
            for (int x=0;x<CHUNK_SIZE;++x) for (int z=0;z<CHUNK_SIZE;++z) {
                int col=(x*CHUNK_SIZE+z)*WY;
                for (int y=band*LIGHT_BAND_HEIGHT;y<(band+1)*LIGHT_BAND_HEIGHT;++y) {
                    int cell=chunk_snapshot_cell(j->cells[chunk],j->high[chunk],col+y);
                    int emission=cell<256 ? power[cell] : light_emission(cell);
                    if (emission) queue(s,heads,voxel(ox+x,y,oz+z),emission);
                }
            }
    }
    static const int delta[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for (int l=15;l>1;--l) while (heads[l]>=0) {
        int k=heads[l],y=k%WY,z=(k/WY)%EDGE,x=k/(WY*EDGE);
        heads[l]=s->next[k];
        if (heads[l]>=0) s->previous[heads[l]]=-1;
        for (int i=0;i<6;++i) {
            int a=x+delta[i][0],b=y+delta[i][1],c=z+delta[i][2];
            if ((unsigned)a>=EDGE || (unsigned)b>=WY || (unsigned)c>=EDGE) continue;
            int cell=sample(j,a,b,c);
            int value=l-(cell<256 ? cost[cell] : light_opacity(cell));
            if (value>0) queue(s,heads,voxel(a,b,c),value);
        }
    }
    for (int x=0;x<LIGHT_VIEW_EDGE;++x) for (int z=0;z<LIGHT_VIEW_EDGE;++z)
        memcpy(&j->level[(x*LIGHT_VIEW_EDGE+z)*WY],&s->levels[voxel(CHUNK_SIZE-1+x,0,CHUNK_SIZE-1+z)],WY);
    return 1;
}
static void *work(void *arg) {
    (void)arg;
    pthread_mutex_lock(&lock);
    while (!stopping) {
        if (status!=QUEUED) { pthread_cond_wait(&changed,&lock); continue; }
        status=SOLVING;
        pthread_mutex_unlock(&lock);
        int ok=solve(&job);
        pthread_mutex_lock(&lock);
        success=ok;
        status=COMPLETE;
    }
    pthread_mutex_unlock(&lock);
    return NULL;
}
int light_start(void) {
    if (running) return 1;
    stopping=0; status=IDLE;
    running=pthread_create(&worker,NULL,work,NULL)==0;
    return running;
}
void light_reset(void) { ++epoch; }
void light_stop(void) {
    if (running) {
        pthread_mutex_lock(&lock);
        stopping=1;
        pthread_cond_signal(&changed);
        pthread_mutex_unlock(&lock);
        pthread_join(worker,NULL);
    }
    running=0;
    free(scratch); scratch=NULL;
    for (int i=0;i<9;++i) for (int s=0;s<CELL_SECTIONS;++s) {
        free(job.owned_high[i].planes[s]);
        job.owned_high[i].planes[s]=NULL;
    }
    for (int i=0;i<CACHE;++i) { free(cache[i].level); cache[i].level=NULL; }
    light_reset();
}
static int equal_key(const Key *a,const Key *b) {
    return a->x==b->x && a->z==b->z && a->epoch==b->epoch &&
        !memcmp(a->revision,b->revision,sizeof a->revision);
}
static const uint8_t *remember(void) {
    int slot=0;
    for (int i=0;i<CACHE;++i) {
        if (!cache[i].level || cache[i].key.epoch!=epoch) { slot=i; break; }
        if (cache[i].use<cache[slot].use) slot=i;
    }
    Result *r=&cache[slot];
    if (!r->level) r->level=(uint8_t *)malloc(OUTPUT);
    if (!r->level) return NULL;
    memcpy(r->level,job.level,OUTPUT);
    r->key=job.key;
    r->use=++used;
    return r->level;
}
static LightView get_view(int cx,int cz,int query_height,int resident_only) {
    if (running && pthread_mutex_trylock(&lock)==0) {
        if (status==COMPLETE) {
            if (success && job.key.epoch==epoch) (void)remember();
            status=IDLE;
        }
        pthread_mutex_unlock(&lock);
    }
    Key key={cx,cz,epoch,{0}};
    for (int i=0;i<9;++i) key.revision[i]=world_light_revision(cx+i/3-1,cz+i%3-1);
    for (int i=0;i<CACHE;++i) if (cache[i].level && equal_key(&cache[i].key,&key)) {
        cache[i].use=++used;
        return (LightView){cache[i].level,1};
    }
    ChunkView chunks[9];
    unsigned mask=0;
    int waiting=0;
    for (int i=0;i<9;++i) {
        chunks[i]=resident_only ? chunk_cached_view(cx+i/3-1,cz+i%3-1) : chunk_view(cx+i/3-1,cz+i%3-1);
        waiting|=chunks[i].cells==NULL;
        mask|=chunks[i].emitter_mask;
    }
    if (waiting) return (LightView){NULL,0};
    /* Height-only gameplay queries often sit far above generated lava. */
    if (query_height>=0) {
        int lo=query_height>14 ? (query_height-14)/LIGHT_BAND_HEIGHT : 0;
        int hi=query_height<WY-14 ? (query_height+14)/LIGHT_BAND_HEIGHT : LIGHT_BANDS-1;
        mask &= ((1u<<(hi+1))-1) & ~((1u<<lo)-1);
    }
    if (!mask) return (LightView){NULL,1};
    if (running) {
        if (pthread_mutex_trylock(&lock)!=0) return (LightView){NULL,0};
        if (status!=IDLE) { pthread_mutex_unlock(&lock); return (LightView){NULL,0}; }
    }
    job.key=key;
    for (int i=0;i<9;++i) {
        job.bands[i]=chunks[i].emitter_mask;
        memcpy(job.cells[i],chunks[i].cells,CHUNK_CELLS);
        job.high[i]=NULL;
        if (!chunks[i].high) continue;
        job.high[i]=&job.view_high[i];
        for (int s=0;s<CELL_SECTIONS;++s) {
            job.view_high[i].planes[s]=NULL;
            const uint8_t *source=chunks[i].high->planes[s];
            if (!source) continue;
            uint8_t **owned=&job.owned_high[i].planes[s];
            if (!*owned) *owned=(uint8_t *)malloc(CELL_SECTION_SIZE);
            if (!*owned) {
                if (running) pthread_mutex_unlock(&lock);
                return (LightView){NULL,0};
            }
            memcpy(*owned,source,CELL_SECTION_SIZE);
            job.view_high[i].planes[s]=*owned;
        }
    }
    if (running) {
        status=QUEUED;
        pthread_cond_signal(&changed);
        pthread_mutex_unlock(&lock);
        return (LightView){NULL,0};
    }
    const uint8_t *p=solve(&job) ? remember() : NULL;
    return (LightView){p,p!=NULL};
}
LightView block_light_view(int cx,int cz) { return get_view(cx,cz,-1,0); }
const uint8_t *block_light_previous(int cx,int cz) {
    const Result *best=NULL;
    for (int i=0;i<CACHE;++i) {
        const Result *r=&cache[i];
        if (r->level && r->key.epoch==epoch && r->key.x==cx && r->key.z==cz && (!best || r->use>best->use)) best=r;
    }
    return best ? best->level : NULL;
}
int block_light_level(LightView view,int x,int y,int z) {
    if (!view.levels || x < -1 || x>CHUNK_SIZE || (unsigned)y>=WY || z < -1 || z>CHUNK_SIZE) return 0;
    return view.levels[((x+1)*LIGHT_VIEW_EDGE+z+1)*WY+y];
}
static int light_at(int x,int y,int z,int resident_only) {
    if ((unsigned)y>=WY || x < -WORLD_SAVE_LIMIT || x>WORLD_SAVE_LIMIT ||
        z < -WORLD_SAVE_LIMIT || z>WORLD_SAVE_LIMIT) return 0;
    int cx=x/CHUNK_SIZE-(x%CHUNK_SIZE<0),cz=z/CHUNK_SIZE-(z%CHUNK_SIZE<0);
    LightView v=get_view(cx,cz,y,resident_only);
    return v.ready ? block_light_level(v,x-cx*CHUNK_SIZE,y,z-cz*CHUNK_SIZE) : -1;
}
int block_light_at(int x,int y,int z) { return light_at(x,y,z,0); }
int block_light_at_resident(int x,int y,int z) { return light_at(x,y,z,1); }
