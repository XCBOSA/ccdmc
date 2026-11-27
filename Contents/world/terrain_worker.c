#include "internal.h"
#include "stream_priority.h"
#include <pthread.h>
#include <stdlib.h>

enum { REQUESTS=48, READY=6 };
typedef struct { int x,z,urgent; } Request;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static pthread_t thread;
static int running,stopping,center_x,center_z,request_count,ready_count,ready_head;
static int heading_x,heading_z;
static unsigned epoch,busy_epoch;
static Request pending[REQUESTS],busy;
static TerrainResult buffers[READY+2];
/* Slots, draft and receipt are always disjoint. The lock transfers ownership,
   never copies the generated chunk. Only the game thread touches receipt. */
static TerrainResult *ready[READY],*draft=&buffers[READY],*receipt=&buffers[READY+1];
static TerrainKey key;

static int priority(Request r) {
    int x=r.x-center_x,z=r.z-center_z,ring=stream_ring(x,z);
    if (!(heading_x || heading_z)) return ring*4+(r.urgent ? 0 : 2);
    return flight_stream_tier(x,z,heading_x,heading_z,1)*64+ring*2+!r.urgent;
}
static void *prepare(void *arg) {
    (void)arg;
    unsigned history_epoch=0;
    pthread_mutex_lock(&lock);
    while (!stopping) {
        if (history_epoch!=epoch) {
            /* A reset can leave the worker idle on the world list, so
               release the previous world's history even without a job. */
            history_epoch=epoch;
            pthread_mutex_unlock(&lock);
            terrain_history_select_epoch(history_epoch);
            pthread_mutex_lock(&lock);
            continue;
        }
        if (!request_count || ready_count==READY) { pthread_cond_wait(&changed,&lock); continue; }
        int best=0;
        for (int i=1;i<request_count;++i) if (priority(pending[i])<priority(pending[best])) best=i;
        busy=pending[best]; pending[best]=pending[--request_count];
        busy_epoch=epoch;
        const unsigned request_epoch=busy_epoch;
        TerrainKey snapshot=key;
        int x=busy.x,z=busy.z;
        pthread_mutex_unlock(&lock);
        terrain_history_prepare(snapshot,request_epoch,x,z,draft);
        pthread_mutex_lock(&lock);
        if (!stopping && epoch==busy_epoch) {
            TerrainResult **slot=&ready[(ready_head+ready_count)%READY];
            TerrainResult *spare=*slot;
            *slot=draft;
            draft=spare;
            ++ready_count;
        }
        busy_epoch=0;
    }
    pthread_mutex_unlock(&lock);
    terrain_history_clear();
    return NULL;
}
int terrain_worker_running(void) { return running; }
int terrain_worker_start(void) {
    if (running) return 1;
    for (int i=0;i<READY;++i) ready[i]=&buffers[i];
    draft=&buffers[READY];
    receipt=&buffers[READY+1];
    ready_head=ready_count=0;
    stopping=0;
    running=pthread_create(&thread,NULL,prepare,NULL)==0;
    return running;
}
void terrain_worker_stop(void) {
    if (!running) return;
    pthread_mutex_lock(&lock);
    stopping=1;
    pthread_cond_signal(&changed);
    pthread_mutex_unlock(&lock);
    pthread_join(thread,NULL);
    running=0;
}
void terrain_worker_reset(TerrainKey next) {
    if (running) pthread_mutex_lock(&lock);
    if (!++epoch) ++epoch; /* zero is the idle marker */
    key=next;
    request_count=ready_head=ready_count=0;
    heading_x=heading_z=0;
    if (running) {
        pthread_cond_signal(&changed);
        pthread_mutex_unlock(&lock);
    }
}
void terrain_worker_center(int x,int z,int hx,int hz) {
    if (!running || pthread_mutex_trylock(&lock)!=0) return;
    heading_x=hx;
    heading_z=hz;
    if (x!=center_x || z!=center_z) {
        center_x=x; center_z=z;
        for (int i=0;i<request_count;) {
            if (stream_ring(pending[i].x-x,pending[i].z-z)>WORLD_VIEW_CHUNKS+3) pending[i]=pending[--request_count];
            else ++i;
        }
    }
    pthread_mutex_unlock(&lock);
}
void terrain_worker_request(int x,int z,int urgent) {
    if (!running || pthread_mutex_trylock(&lock)!=0) return;
    if (busy_epoch==epoch && busy.x==x && busy.z==z) goto finished;
    for (int i=0;i<ready_count;++i) {
        const TerrainResult *r=ready[(ready_head+i)%READY];
        if (r->x==x && r->z==z) goto finished;
    }
    {
        int worst=0;
        for (int i=0;i<request_count;++i) {
            if (pending[i].x==x && pending[i].z==z) {
                if (urgent) pending[i].urgent=1;
                goto finished;
            }
            if (priority(pending[i])>priority(pending[worst])) worst=i;
        }
        Request r={x,z,urgent};
        if (request_count<REQUESTS) pending[request_count++]=r;
        else if (priority(r)<priority(pending[worst])) pending[worst]=r;
        pthread_cond_signal(&changed);
    }
finished:
    pthread_mutex_unlock(&lock);
}
const TerrainResult *terrain_worker_take(void) {
    if (!running || pthread_mutex_trylock(&lock)!=0) return NULL;
    const TerrainResult *out=NULL;
    if (ready_count) {
        TerrainResult **slot=&ready[ready_head];
        TerrainResult *filled=*slot;
        *slot=receipt;
        out=receipt=filled;
        ready_head=(ready_head+1)%READY;
        --ready_count;
        pthread_cond_signal(&changed);
    }
    pthread_mutex_unlock(&lock);
    return out;
}

const TerrainResult *terrain_prepare_immediate(TerrainKey key,int x,int z) {
    terrain_prepare(key,x,z,receipt);
    return receipt;
}
