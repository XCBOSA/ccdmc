#include "internal.h"
#include "lava.h"
#include "light.h"
#include "redstone.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Queue only cells near edits or moving water. Generated lakes are sources,
   but do not schedule work merely because their chunk was loaded. */
enum { WATER_BUCKETS = 1021, WATER_WAIT_BUCKETS=257, WATER_BUDGET = 512 };
typedef struct Pending {
    int x, y, z;
    int chunk_x,chunk_z;
    struct Pending *next,*same,*save_next,*save_prev;
} Pending;
typedef struct {
    Pending *buckets[WATER_BUCKETS],*waiting[WATER_WAIT_BUCKETS];
    Pending *head,*tail,*save_head,*save_tail;
    unsigned asleep;
} WaterBank;
static WaterBank water[DIMENSION_COUNT];
static const int dx[] = {1, -1, 0, 0}, dz[] = {0, 0, 1, -1};

static float resident_surface(int x,int y,int z,float fx,float fz) {
    int cells[3][3]={{0}},above[3][3]={{0}};
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) {
        (void)world_resident_cell(x+i-1,y,z+j-1,&cells[i][j]);
        if (water_strength(cells[i][j]))
            (void)world_resident_cell(x+i-1,y+1,z+j-1,&above[i][j]);
    }
    float h[2][2];
    for (int i=0;i<2;++i) for (int j=0;j<2;++j) {
        const int c[]={cells[i][j],cells[i+1][j],cells[i][j+1],cells[i+1][j+1]};
        const int a[]={above[i][j],above[i+1][j],above[i][j+1],above[i+1][j+1]};
        h[i][j]=water_corner(c,a).height;
    }
    /* Interpolate the same triangles as the renderer's center fan. On a
       planar patch this also agrees with either two-triangle diagonal. */
    const float ox=fx-.5f,oz=fz-.5f;
    const float mid=(h[0][0]+h[0][1]+h[1][0]+h[1][1])*.25f;
    if (fabsf(ox)>=fabsf(oz)) {
        const float *edge=h[ox>=0.f];
        return y+mid+fabsf(ox)*(edge[0]+edge[1]-2.f*mid)+oz*(edge[1]-edge[0]);
    }
    int j=oz>=0.f;
    return y+mid+fabsf(oz)*(h[0][j]+h[1][j]-2.f*mid)+ox*(h[1][j]-h[0][j]);
}

WaterView water_view_sample(float x,float y,float z) {
    if (!(x>=-WORLD_SAVE_LIMIT && x<=WORLD_SAVE_LIMIT && z>=-WORLD_SAVE_LIMIT && z<=WORLD_SAVE_LIMIT &&
          y>=0.f && y<WY)) return (WaterView){0};
    int bx=(int)floorf(x),by=(int)floorf(y),bz=(int)floorf(z),cell,above=AIR;
    if (!world_resident_cell(bx,by,bz,&cell) || !water_strength(cell)) return (WaterView){0};
    (void)world_resident_cell(bx,by+1,bz,&above);
    const int limit=by+24;
    while (water_strength(above) && by<limit) {
        ++by; above=AIR;
        (void)world_resident_cell(bx,by+1,bz,&above);
    }
    float surface=water_strength(above) ? by+1.f : resident_surface(bx,by,bz,x-bx,z-bz);
    if (y>=surface) return (WaterView){0};
    return (WaterView){surface,1,!water_strength(above) && light_has_open_sky_resident(bx,by+1,bz)};
}

static unsigned water_bucket(int x, int y, int z) {
    return (hash2(x, z) ^ ((unsigned)y * 2654435761u)) % WATER_BUCKETS;
}
static unsigned wait_bucket(int cx,int cz) { return hash2(cx,cz)%WATER_WAIT_BUCKETS; }
static int chunk_of(int coordinate) { return coordinate/CHUNK_SIZE-(coordinate%CHUNK_SIZE<0); }
static void schedule(WaterBank *w,Pending *p) {
    p->next=NULL;
    if (w->tail) w->tail->next=p;
    else w->head=p;
    w->tail=p;
}
static void track(WaterBank *w,Pending *p) {
    unsigned h=water_bucket(p->x,p->y,p->z);
    p->same=w->buckets[h];
    w->buckets[h]=p;
    p->save_prev=w->save_tail;
    p->save_next=NULL;
    if (w->save_tail) w->save_tail->save_next=p;
    else w->save_head=p;
    w->save_tail=p;
}
static void detach(WaterBank *w,Pending *p) {
    Pending **link=&w->buckets[water_bucket(p->x,p->y,p->z)];
    while (*link!=p) link=&(*link)->same;
    *link=p->same;
    if (p->save_prev) p->save_prev->save_next=p->save_next;
    else w->save_head=p->save_next;
    if (p->save_next) p->save_next->save_prev=p->save_prev;
    else w->save_tail=p->save_prev;
}
static void park(WaterBank *w,Pending *p,int missing_x,int missing_z) {
    p->chunk_x=chunk_of(missing_x); p->chunk_z=chunk_of(missing_z);
    const unsigned h=wait_bucket(p->chunk_x,p->chunk_z);
    /* One known missing dependency is enough. If another is still absent
       when this chunk installs, the cell waits for that dependency next. */
    p->next=w->waiting[h]; w->waiting[h]=p; ++w->asleep;
}
void water_chunk_installed(int cx,int cz) {
    WaterBank *w=&water[world_dimension()];
    if (!w->asleep) return;
    Pending **link=&w->waiting[wait_bucket(cx,cz)];
    Pending *awake=NULL;
    while (*link) {
        Pending *p=*link;
        if (p->chunk_x!=cx || p->chunk_z!=cz) { link=&p->next; continue; }
        *link=p->next;
        p->next=awake; awake=p; --w->asleep;
    }
    /* Restore first-parked order among jobs with the same dependency. */
    while (awake) {
        Pending *p=awake;
        awake=p->next;
        schedule(w,p);
    }
}

static int enqueue_mode(int x,int y,int z,int defer_cold) {
    if (!inworld(x, y, z)) return 1;
    WaterBank *w = &water[world_dimension()];
    unsigned h = water_bucket(x, y, z);
    for (Pending *p = w->buckets[h]; p; p = p->same)
        if (p->x == x && p->y == y && p->z == z) return 1;
    Pending *p = (Pending *)malloc(sizeof *p);
    if (!p) return 0;
    *p=(Pending){x,y,z,0,0,NULL,NULL,NULL,NULL};
    track(w,p);
    int raw;
    if (defer_cold && !world_resident_cell(x,y,z,&raw)) park(w,p,x,z);
    else schedule(w,p);
    return 1;
}

static int enqueue(int x,int y,int z) { return enqueue_mode(x,y,z,world_streaming_active()); }
int water_restore_pending(int x,int y,int z) {
    /* The UI starts the worker before opening the save but enables
       interactive streaming afterwards. Classify cold records on decode
       so the first local edit never queues behind a far-away backlog. */
    return enqueue_mode(x,y,z,terrain_worker_running());
}

int water_visit_pending(WorldRecord visit, void *ctx) {
    return water_visit_dimension_pending(world_dimension(), visit, ctx);
}
int water_visit_dimension_pending(Dimension dimension, WorldRecord visit, void *ctx) {
    if ((unsigned)dimension >= DIMENSION_COUNT || !visit) return 0;
    /* Parked work is still pending work. The file format intentionally does
       not encode cache state: the next session resolves its own residency. */
    for (const Pending *p=water[dimension].save_head;p;p=p->save_next)
        if (!visit(ctx, p->x, p->y, p->z, 0)) return 0;
    return 1;
}

static void enqueue_sides(int x, int y, int z) {
    for (int i = 0; i < 4; i++) enqueue(x + dx[i], y, z + dz[i]);
}

static int supports_water(int cell) {
    return cell == WATER || build_partial(cell) || solid(cell_type(cell));
}

void water_notify_edit(int x, int y, int z, int before, int after) {
    enqueue(x, y, z);
    enqueue(x, y + 1, z);
    enqueue(x, y - 1, z);
    enqueue_sides(x, y, z);
    /* Changing support changes where the cell above can send water. */
    if (supports_water(before) != supports_water(after)) enqueue_sides(x, y + 1, z);
}

void water_reset(void) {
    for (int d = 0; d < DIMENSION_COUNT; ++d) {
        WaterBank *w = &water[d];
        while (w->save_head) {
            Pending *p = w->save_head;
            w->save_head=p->save_next;
            free(p);
        }
        memset(w,0,sizeof *w);
    }
}

static void update_cell(WaterBank *w,Pending *p,int streaming) {
    const int x=p->x,y=p->y,z=p->z;
    int before;
    if (streaming && !world_resident_cell(x,y,z,&before)) {
        park(w,p,x,z);
        return;
    }
    if (!streaming) before=world_cell(x,y,z);
    if (before!=AIR && !lava_strength(before) && before!=FIRE_CELL && before!=SNOW_LAYER && before!=LILY_PAD && !circuit_cell(before) &&
        (before==WATER || !water_strength(before))) {
        detach(w,p); free(p); return;
    }
    int side[4];
    if (streaming) for (int i=0;i<4;++i) {
        if (!world_resident_cell(x+dx[i],y,z+dz[i],&side[i])) {
            park(w,p,x+dx[i],z+dz[i]);
            return;
        }
    }
    /* Untrack before a transaction can notify the same cell again. The
       non-streaming path keeps synchronous behaviour for tools and tests. */
    detach(w,p); free(p);
    if (lava_strength(before)) {
        int product=lava_quenched_block(before);
        if (product==AIR) return;
        int contact = water_strength(world_cell(x, y + 1, z));
        for (int i = 0; i < 4 && !contact; ++i)
            contact = water_strength(streaming ? side[i] : world_cell(x+dx[i],y,z+dz[i]));
        if (contact && !set_block(x,y,z,product)) enqueue(x,y,z);
        return;
    }
    /* Downward flow retains its strength. Horizontal flow loses one level
       per cell and only leaves a cell whose bottom is supported. */
    int strength = water_strength(world_cell(x, y + 1, z));
    for (int i = 0; i < 4 && strength < WATER_LEVELS - 1; i++) {
        int a = x + dx[i], b = z + dz[i];
        int from_side = water_strength(streaming ? side[i] : world_cell(a,y,b)) - 1;
        if (from_side > strength && supports_water(world_cell(a, y - 1, b)))
            strength = from_side;
    }
    int after = strength ? FLOW_BASE + strength - 1 : AIR;
    if (circuit_cell(before)) {
        if (strength && !redstone_replace(x,y,z,after)) enqueue(x,y,z);
        return;
    }
    if (before==FIRE_CELL || before==SNOW_LAYER || before==LILY_PAD) {
        BlockEdit flooded={x,y,z,after};
        if (strength && !world_set_blocks(&flooded,1)) enqueue(x,y,z);
        return;
    }
    if (after != before && world_write_cell(x, y, z, after)) {
        enqueue(x, y - 1, z);
        enqueue_sides(x, y, z);
    }
}

void water_step(void) {
    /* Newly queued cells wait until the next tick, so a waterfall actually
       progresses through space instead of appearing all in one frame. */
    WaterBank *w = &water[world_dimension()];
    Pending *tail = w->tail;
    const int streaming=world_streaming_active();
    for (int i = 0; i < WATER_BUDGET && w->head; i++) {
        Pending *p = w->head;
        int end = p == tail;
        w->head = p->next;
        if (!w->head) w->tail = NULL;
        update_cell(w,p,streaming);
        if (end) break;
    }
}
