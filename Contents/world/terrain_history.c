#include "internal.h"
#include <stdlib.h>
#include <string.h>

/* This worker-local history stores the pristine generator output, never
   resident chunks or authored edits. Fast worlds can still bypass it. */
enum { HISTORY_SETS=128, HISTORY_WAYS=4, HISTORY_LIMIT=4*1024*1024 };
typedef struct {
    int x,z;
    unsigned length,cell_bytes,wide_count,device_count;
    uint64_t use;
    uint8_t *bytes;
} HistoryEntry;
static HistoryEntry history[HISTORY_SETS][HISTORY_WAYS];
static const uint8_t empty_column[WY]={0};
static unsigned epoch,bytes_used;
static uint64_t tick;

static unsigned set_for(int x,int z) {
    unsigned hash=(unsigned)x*UINT32_C(0x9e3779b1) ^ (unsigned)z*UINT32_C(0x85ebca77);
    return (hash^(hash>>16))&(HISTORY_SETS-1);
}
static void drop(HistoryEntry *e) {
    if (!e->bytes) return;
    free(e->bytes);
    e->bytes=NULL;
    bytes_used-=e->length;
}
void terrain_history_clear(void) {
    for (int i=0;i<HISTORY_SETS;++i)
        for (int j=0;j<HISTORY_WAYS;++j) drop(&history[i][j]);
    epoch=0;
    tick=0;
}
static HistoryEntry *oldest(void) {
    HistoryEntry *best=NULL;
    for (int i=0;i<HISTORY_SETS;++i) for (int j=0;j<HISTORY_WAYS;++j) {
        HistoryEntry *e=&history[i][j];
        if (e->bytes && (!best || e->use<best->use)) best=e;
    }
    return best;
}
static int restore(TerrainResult *out,const HistoryEntry *e) {
    const uint8_t *bytes=e->bytes;
    memcpy(out->tops,bytes,sizeof out->tops); bytes+=sizeof out->tops;
    memcpy(out->biomes,bytes,sizeof out->biomes); bytes+=sizeof out->biomes;
    out->wide.count=e->wide_count;
    size_t wide=e->wide_count*sizeof out->wide.cell[0];
    memcpy(out->wide.cell,bytes,wide); bytes+=wide;
    out->devices.count=e->device_count;
    size_t devices=e->device_count*sizeof out->devices.cell[0];
    memcpy(out->devices.cell,bytes,devices); bytes+=devices;
    size_t copied=0;
    for (unsigned i=0;i<CHUNK_COLUMNS;++i) {
        size_t n=1u+out->tops[i];
        if (n>WY || n>e->cell_bytes-copied) return 0;
        uint8_t *column=out->cells+i*WY;
        memcpy(column,bytes+copied,n);
        memset(column+n,AIR,WY-n);
        copied+=n;
    }
    if (copied!=e->cell_bytes) return 0;
    out->emitter_mask=light_summarize(out->cells,out->tops,out->sky_tops,out->emitters,out->cover);
    out->x=e->x; out->z=e->z;
    out->reused=1;
    return 1;
}
static void remember(HistoryEntry *target,const TerrainResult *out) {
    /* Skip the empty end of each column, but never assume a future
       generator variant maintains its top correctly. Cache only exact data. */
    _Static_assert(AIR==0,"zero bytes in the prepared generator stand for air");
    unsigned raw=0;
    for (unsigned i=0;i<CHUNK_COLUMNS;++i) {
        size_t n=1u+out->tops[i];
        if (n>WY || memcmp(out->cells+i*WY+n,empty_column,WY-n)) return;
        raw+=(unsigned)n;
    }
    unsigned wide=out->wide.count*sizeof out->wide.cell[0],devices=out->devices.count*sizeof out->devices.cell[0];
    unsigned length=raw+sizeof out->tops+sizeof out->biomes+wide+devices;
    drop(target);
    while (length>HISTORY_LIMIT-bytes_used) {
        HistoryEntry *victim=oldest();
        if (!victim) return;
        drop(victim);
    }
    uint8_t *bytes=(uint8_t *)malloc(length);
    if (!bytes) return;
    *target=(HistoryEntry){out->x,out->z,length,raw,out->wide.count,out->devices.count,++tick,bytes};
    bytes_used+=length;
    memcpy(bytes,out->tops,sizeof out->tops); bytes+=sizeof out->tops;
    memcpy(bytes,out->biomes,sizeof out->biomes); bytes+=sizeof out->biomes;
    memcpy(bytes,out->wide.cell,wide); bytes+=wide;
    memcpy(bytes,out->devices.cell,devices); bytes+=devices;
    for (unsigned i=0;i<CHUNK_COLUMNS;++i) {
        size_t n=1u+out->tops[i];
        memcpy(bytes,out->cells+i*WY,n);
        bytes+=n;
    }
}
void terrain_history_select_epoch(unsigned wanted_epoch) {
    if (epoch!=wanted_epoch) {
        terrain_history_clear();
        epoch=wanted_epoch;
    }
}
void terrain_history_prepare(TerrainKey key,unsigned wanted_epoch,int x,int z,TerrainResult *out) {
    terrain_history_select_epoch(wanted_epoch);
    /* Simple flat layouts generate with one memcpy per column and do not
       justify maintaining another large set of packed columns. */
    if (key.dimension==OVERWORLD && key.profile==WORLD_SUPERFLAT) {
        terrain_prepare(key,x,z,out);
        return;
    }
    HistoryEntry *set=history[set_for(x,z)],*target=&set[0];
    for (int i=0;i<HISTORY_WAYS;++i) {
        HistoryEntry *e=&set[i];
        if (e->bytes && e->x==x && e->z==z) {
            if (restore(out,e)) { e->use=++tick; return; }
            target=e;
            break;
        }
        if (!e->bytes || (target->bytes && e->use<target->use)) target=e;
    }
    terrain_prepare(key,x,z,out);
    remember(target,out);
}
