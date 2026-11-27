#include "renderer.h"
#include "../world/barriers.h"
#include "../world/build_blocks.h"
#include "../world/space.h"
#include "../world/redstone.h"
#include "../world/light.h"
#include "../world/terrain_cover.h"
#include "../world/stream_priority.h"
#include "../game/actors.h"
#include "../game/villagers.h"
#include "../game/combat.h"
#include "../game/button_arrows.h"
#include "../game/hoppers.h"
#include "../game/boats.h"
#include "../game/minecarts.h"
#include "../game/horses.h"
#include "../game/fishing.h"
#include "../game/chests.h"
#include "../game/tripwire.h"
#include "../game/brewing.h"
#include "../game/armor.h"
#include "../game/drops.h"
#include "../game/experience.h"
#include "../game/gravity.h"
#include "../game/blast.h"
#include "../game/fireworks.h"
#include "../game/weather.h"
#include "../game/pistons.h"
#include "../game/game.h"
#include "../game/item_frames.h"
#include "../game/item_icons.h"
#include "../game/navigation.h"
#include "../save/save.h"
#include <ccd_sdl.h>
#include <float.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { float p[3]; Uint8 c[4]; float uv[2]; } Vertex;
enum { MESH_TRANSFER_PAGE=1024*1024, MESH_DRAW_PAGE=32766 };
/* CCD's MSL path snapshots one draw into vertex bytes plus 32-bit indices.
   Keep its temporary packet small without reducing geometry or view width. */
_Static_assert(MESH_DRAW_PAGE%3==0 && 32+MESH_DRAW_PAGE*(sizeof(Vertex)+4)<1024*1024,
    "Mesh draw batches must contain whole triangles and bounded staging");
static SDL_Window *win;
static SDL_GPUDevice *dev;
static int surface_created_w,surface_created_h;
static int view_w=START_W,view_h=START_H,hd_w=2*START_W,hd_h=2*START_H;
static SDL_GPUTexture *depth, *atlas;
static SDL_GPUSampler *atlasSampler;
static SDL_GPUGraphicsPipeline *pipe,*pipe_lit;
static SDL_GPUGraphicsPipeline *pipe_world,*pipe_unshadowed,*pipe_shadow,*pipe_reflection,*pipe_sky,*pipe_mirror_sky,*pipe_present;
static SDL_GPUGraphicsPipeline *pipe_underwater,*pipe_water_below;
static SDL_GPUGraphicsPipeline *pipe_water,*pipe_water_flat,*pipe_basic_moon;
static SDL_GPUSampler *smoothSampler,*shadowSampler;
static SDL_GPUTexture *shadowMap,*skyColor,*mirrorColor,*mirrorDepth;
static Uint32 effectW,effectH,mirrorW,mirrorH;
enum { SHADOW_SIZE=1024, SHADOW_CHUNKS=7, REFLECTION_CHUNKS=5 };
static const float shadow_radius=80.f,shadow_depth_range=210.f;
enum { OCEAN_LEVEL=63 };
static char renderer_error[256];
static float atlas_column_u[ATLAS_UV_WIDTH];
static int atlas_coordinates_ready;
/* One mesh per visible chunk. A ring keeps the overlapping meshes when the
   player moves, and revisions limit edits to the affected chunk and seams. */
enum { RENDER_CHUNKS = WORLD_VIEW_CHUNKS, MESH_SPAN = 2 * RENDER_CHUNKS + 1 };
enum { VIEW_DIRECTIONS=8, EDIT_RINGS=2, VIEW_RING_BIAS=2 };
typedef struct { int8_t x,z; } ChunkOffset;
static const ChunkOffset view_directions[VIEW_DIRECTIONS]={
    {0,-1},{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1}
};
static ChunkOffset view_orders[2][VIEW_DIRECTIONS][MESH_SPAN*MESH_SPAN];
static unsigned view_order_ready[2];
enum { MESH_REUSED, MESH_QUEUED, MESH_WAITING };
enum { MESH_IDLE, MESH_PENDING, MESH_BAKING, MESH_READY, MESH_WORKERS=2 };
enum { MESH_COLUMNS=CHUNK_SIZE*CHUNK_SIZE, MESH_CELLS=MESH_COLUMNS*WY,
       MESH_LIGHT_CELLS=LIGHT_VIEW_EDGE*LIGHT_VIEW_EDGE*WY,
       MESH_HALO=3, MESH_EDGE=CHUNK_SIZE+2*MESH_HALO };
static int meshX, meshZ, terrain_pending = 1, prefer_initial_mesh=1, edit_hint;
/* Give a ready exact-light update a turn even if the player never stops. */
enum { MESH_FIRST, MESH_GEOMETRY, MESH_ONLY_LIGHT, MESH_WORK_CLASSES, LIGHT_TURN_AFTER=6 };
static unsigned geometry_since_light;
static unsigned long long meshWorldVersion, mesh_epoch;
typedef struct {
    SDL_GPUBuffer *buffer;
    Uint32 capacity, count;
    unsigned long long revision[9];
    int x, z, valid, highest, relight;
    float opaque_bottom,opaque_top; /* Exact vertical extent of uploaded opaque geometry. */
    uint16_t water_bottom,water_top; /* Undisplaced water lies in this cell interval. */
} Mesh;
static Mesh terrain_mesh[MESH_SPAN][MESH_SPAN], drop_mesh, actor_mesh, horse_mesh, fishing_mesh, lead_mesh;
static Mesh frame_mesh,frame_maps;
static SDL_GPUTexture *frame_map_atlas;
static unsigned frame_tiles,frame_uploaded;
static int frame_x,frame_z;
static unsigned char frame_maps_needed[MAP_LIMIT+1];
static uint64_t frame_map_uploaded[MAP_LIMIT+1];
static Mesh water_mesh[MESH_SPAN][MESH_SPAN];
static float water_planes[MESH_SPAN][MESH_SPAN];
/* The oldest snapshot that may show a completed player edit. Ring slots
   carry coordinates so jumping across the world cannot inherit a fence. */
typedef struct { int x,z; unsigned long long floor; } MeshEditFence;
static MeshEditFence edit_fences[MESH_SPAN][MESH_SPAN];
static Mesh xp_mesh;
static Mesh falling_mesh;
static Mesh weather_mesh;
static int weather_x,weather_y,weather_z;
static unsigned weather_phase;
static unsigned weather_geometry(int cx,int cy,int cz,unsigned phase);
static int weather_mesh_selftest(void);
static int terrain_clip_selftest(void);
static int water_visibility_selftest(void);
#if __has_include("ccdmc_shadow_receivers_test.h")
static int shadow_receivers_selftest(void);
#endif
static unsigned falling_uploaded,blast_uploaded,fireworks_uploaded;
static float falling_fraction;
static int falling_view_x,falling_view_z;
static Dimension falling_view_dimension;
static unsigned xp_uploaded;
static int xp_mesh_x,xp_mesh_z;
static unsigned lead_version,lead_actors,lead_horses;
static Vec3 lead_hand;
static unsigned uploaded_actors, uploaded_combat, uploaded_boats, uploaded_carts, uploaded_horses;
static int send_mesh(Mesh *mesh);
static SDL_GPUTransferBuffer *transfer;
static Uint32 transferCapacity, depthW, depthH;
#ifdef __cplusplus
#define MESH_TLS thread_local
#else
#define MESH_TLS _Thread_local
#endif
/* Geometry helpers are shared by the main-thread actor renderer and the
   CPU-only terrain workers, with one builder per thread. */
static MESH_TLS Uint32 vertexCount, vertexCapacity;
static MESH_TLS Vertex *vertices;
static MESH_TLS int meshOOM;
static MESH_TLS Vertex *water_vertices;
static MESH_TLS Uint32 water_count,water_capacity;
static MESH_TLS unsigned brightness=255;
static MESH_TLS int vertex_light_encoding;

static inline unsigned mesh_rgb_light(void) { return vertex_light_encoding ? 255u : brightness; }
static inline Uint8 mesh_alpha(void) { return (Uint8)(vertex_light_encoding ? brightness : 255u); }

typedef struct {
    unsigned long long epoch,revision[9];
    int cx,cz,has_light,has_high,highest,failed,relight,urgent,work;
    Dimension dimension;
    uint8_t cells[MESH_EDGE*MESH_EDGE*WY],tops[MESH_COLUMNS],biomes[MESH_COLUMNS];
    uint8_t high[MESH_EDGE*MESH_EDGE*WY];
    int16_t sky[MESH_EDGE*MESH_EDGE];
    TerrainColumnCover cover[MESH_EDGE*MESH_EDGE];
    uint8_t light[MESH_LIGHT_CELLS],circuit[MESH_CELLS];
    /* Borrowed from the worker until the main thread uploads or discards it. */
    const Vertex *data;
    Uint32 count;
    const Vertex *water;
    Uint32 waterCount;
    float waterPlane,opaque_bottom,opaque_top;
    uint16_t water_bottom,water_top;
} TerrainMeshJob;
typedef struct {
    TerrainMeshJob *job;
    pthread_t thread;
    int state, cancel;
    unsigned long long sequence;
} MeshWorker;
static TerrainMeshJob mesh_job,mesh_aux;
static MeshWorker mesh_workers[MESH_WORKERS]={{.job=&mesh_job},{.job=&mesh_aux}};
static pthread_mutex_t mesh_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t mesh_changed=PTHREAD_COND_INITIALIZER;
static int mesh_running,mesh_stop;
static unsigned long long mesh_sequence;
/* Only recorded when the player explicitly starts the frame sampler.
   The generation keeps jobs from an earlier measurement out of a new one. */
static unsigned mesh_sample_generation,mesh_sample_bakes,mesh_sample_active,mesh_sample_published;
static unsigned mesh_sample_front_slots,mesh_sample_front_ready,mesh_sample_front_drawn;
static unsigned mesh_sample_work[MESH_WORK_CLASSES];
static Uint64 mesh_sample_work_ns[MESH_WORK_CLASSES];
static Uint64 mesh_sample_bake_ns;
enum { CAPTURE_REFLECTION, CAPTURE_ABOVE_WATER, CAPTURE_CLASSES };
typedef struct {
    Uint64 vertices, skipped_vertices;
    unsigned passes, chunks, skipped_chunks;
} CaptureSample;
static CaptureSample mesh_sample_capture[CAPTURE_CLASSES];
enum { CULL_WORLD, CULL_SHADOW, CULL_MIRROR, CULL_SNELL, CULL_CLASSES };
typedef struct { uint64_t submitted, avoided; unsigned draws,skipped; } CullSample;
static CullSample mesh_sample_cull[CULL_CLASSES];
static unsigned shadow_sample_passes,shadow_sample_inactive;
static unsigned shadow_sample_no_receiver;
static Uint64 shadow_sample_no_receiver_vertices;
static unsigned entity_sample_visible,entity_sample_culled,entity_sample_uploads;
static Uint64 entity_sample_vertices;
static CullSample mesh_sample_water;
static unsigned water_sample_frames,water_sample_dry,water_sample_unused_capture;

void renderer_begin_mesh_sample(void) {
    pthread_mutex_lock(&mesh_lock);
    if (!++mesh_sample_generation) ++mesh_sample_generation;
    mesh_sample_active=1;
    mesh_sample_bakes=mesh_sample_published=0;
    mesh_sample_bake_ns=0;
    memset(mesh_sample_work,0,sizeof mesh_sample_work);
    memset(mesh_sample_work_ns,0,sizeof mesh_sample_work_ns);
    mesh_sample_front_slots=mesh_sample_front_ready=mesh_sample_front_drawn=0;
    memset(mesh_sample_capture,0,sizeof mesh_sample_capture);
    memset(mesh_sample_cull,0,sizeof mesh_sample_cull);
    shadow_sample_passes=shadow_sample_inactive=0;
    shadow_sample_no_receiver=0;
    shadow_sample_no_receiver_vertices=0;
    entity_sample_visible=entity_sample_culled=entity_sample_uploads=0;
    entity_sample_vertices=0;
    mesh_sample_water=(CullSample){0};
    water_sample_frames=water_sample_dry=water_sample_unused_capture=0;
    pthread_mutex_unlock(&mesh_lock);
}
void renderer_finish_mesh_sample(int report) {
    pthread_mutex_lock(&mesh_lock);
    const unsigned bakes=mesh_sample_bakes;
    const Uint64 baked_ns=mesh_sample_bake_ns;
    unsigned work[MESH_WORK_CLASSES];
    Uint64 work_ns[MESH_WORK_CLASSES];
    memcpy(work,mesh_sample_work,sizeof work);
    memcpy(work_ns,mesh_sample_work_ns,sizeof work_ns);
    mesh_sample_active=0;
    pthread_mutex_unlock(&mesh_lock);
    if (report) printf("MESH SAMPLE bakes=%u published=%u worker mean=%.2f ms\n",bakes,mesh_sample_published,
        bakes ? (double)baked_ns/(1000000.0*bakes) : 0.0);
    if (report) printf("MESH WORK first=%u geometry=%u relight=%u relight worker=%.2f ms\n",
        work[MESH_FIRST],work[MESH_GEOMETRY],work[MESH_ONLY_LIGHT],(double)work_ns[MESH_ONLY_LIGHT]/1000000.);
    if (report && mesh_sample_front_slots) printf("FRONT SAMPLE slots=%u resident=%u meshed=%u\n",
        mesh_sample_front_slots,mesh_sample_front_ready,mesh_sample_front_drawn);
    const char *const names[]={"reflection","above-water"};
    if (report) for (int i=0;i<CAPTURE_CLASSES;++i) {
        const CaptureSample s=mesh_sample_capture[i];
        if (s.passes) printf("CAPTURE SAMPLE %s frames=%u chunks=%u skipped=%u vertices=%llu skippedVertices=%llu\n",
            names[i],s.passes,s.chunks,s.skipped_chunks,(unsigned long long)s.vertices,
            (unsigned long long)s.skipped_vertices);
    }
    if (report && (shadow_sample_passes || shadow_sample_inactive))
        printf("SHADOW SAMPLE active=%u skippedInactive=%u\n",shadow_sample_passes,shadow_sample_inactive);
    if (report && shadow_sample_no_receiver)
        printf("SHADOW RECEIVERS skipped=%u skippedVertices=%llu\n",shadow_sample_no_receiver,
            (unsigned long long)shadow_sample_no_receiver_vertices);
    if (report && (entity_sample_visible || entity_sample_culled))
        printf("ENTITY SAMPLE visible=%u culled=%u uploads=%u uploadedVertices=%llu\n",
            entity_sample_visible,entity_sample_culled,entity_sample_uploads,
            (unsigned long long)entity_sample_vertices);
    if (report && water_sample_frames)
        printf("WATER VISIBILITY frames=%u noSurface=%u skippedCapture=%u draws=%u skippedDraws=%u vertices=%llu skippedVertices=%llu\n",
            water_sample_frames,water_sample_dry,water_sample_unused_capture,mesh_sample_water.draws,mesh_sample_water.skipped,
            (unsigned long long)mesh_sample_water.submitted,(unsigned long long)mesh_sample_water.avoided);
    const char *const cull_names[]={"world","shadow","reflection","above-water"};
    if (report) for (int i=0;i<CULL_CLASSES;++i) {
        const CullSample s=mesh_sample_cull[i];
        if (s.draws || s.skipped)
            printf("CULL SAMPLE %s draws=%u skippedHeight=%u vertices=%llu skippedVerticesHeight=%llu\n",
                cull_names[i],s.draws,s.skipped,(unsigned long long)s.submitted,(unsigned long long)s.avoided);
    }
}

static Vec3 scale(Vec3 a, float k) { return (Vec3){a.x * k, a.y * k, a.z * k}; }
static float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 cross(Vec3 a, Vec3 b) {
    return (Vec3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static Vec3 unit(Vec3 a) {
    float n = sqrtf(dot(a, a));
    return n > 0 ? scale(a, 1 / n) : a;
}

/* Quantize the horizontal heading; pitch does not change which ground
   the player is heading towards. Never convert an angle to an index. */
static unsigned view_octant(float angle) {
    const float x=sinf(angle),z=-cosf(angle),a=fabsf(x),b=fabsf(z),split=.414213562f;
    if (a<=b*split) return z>0 ? 4u : 0u;
    if (b<=a*split) return x>0 ? 2u : 6u;
    if (x>0) return z>0 ? 3u : 1u;
    return z>0 ? 5u : 7u;
}
static int mesh_ring(int x,int z) {
    return stream_ring(x,z);
}
static int view_tier(int x,int z,int ring,ChunkOffset heading) {
    if (ring<=EDIT_RINGS) return ring;
    const int along=x*heading.x+z*heading.z,lateral=abs(x*heading.z-z*heading.x);
    return ring+(along<0 ? VIEW_RING_BIAS : lateral>along ? 1 : 0);
}
static int ahead_of_flight(void) {
    return player_rules.flying && fabsf(pitch)<.96f;
}
void renderer_stream_world(int x,int z) {
    if (!ahead_of_flight()) { world_stream_pump(x,z); return; }
    const ChunkOffset f=view_directions[view_octant(yaw)];
    world_stream_pump_directional(x,z,f.x,f.z);
}
static int stream_view_tier(int x,int z,int ring,ChunkOffset heading,int flying) {
    return flying ? flight_stream_tier(x,z,heading.x,heading.z,0) : view_tier(x,z,ring,heading);
}
static const ChunkOffset *view_order(unsigned direction,int flying) {
    ChunkOffset *order=view_orders[flying][direction];
    const unsigned bit=1u<<direction;
    if (view_order_ready[flying]&bit) return order;
    /* Stable bucket order, cached per heading and movement mode. The
       flight cone only reorders work; both modes schedule every chunk. */
    enum { TIERS=RENDER_CHUNKS+FLIGHT_REST_TIER+1 };
    unsigned sizes[TIERS]={0},index[TIERS];
    const ChunkOffset heading=view_directions[direction];
    for (int x=-RENDER_CHUNKS;x<=RENDER_CHUNKS;++x)
        for (int z=-RENDER_CHUNKS;z<=RENDER_CHUNKS;++z)
            ++sizes[stream_view_tier(x,z,mesh_ring(x,z),heading,flying)];
    index[0]=0;
    for (int i=1;i<TIERS;++i) index[i]=index[i-1]+sizes[i-1];
    for (int ring=0;ring<=RENDER_CHUNKS;++ring)
        for (int x=-ring;x<=ring;++x) for (int z=-ring;z<=ring;++z) {
            if (mesh_ring(x,z)!=ring) continue;
            order[index[stream_view_tier(x,z,ring,heading,flying)]++]=(ChunkOffset){(int8_t)x,(int8_t)z};
        }
    view_order_ready[flying]|=bit;
    return order;
}

static float camera_scale(void) { return 1.f/tanf(.95f*.5f); }
enum { ACTOR_MESH_RADIUS=4, HORSE_MESH_RADIUS=3, VEHICLE_MESH_RADIUS=2 };
_Static_assert(ACTOR_CAPACITY<=64 && HORSE_CAPACITY<=32 && BOAT_CAPACITY<=32 && CART_CAPACITY<=32,
    "Entity visibility masks must cover all allocated records");
typedef struct {
    Vec3 eye,forward,right,up;
    float tan_y,tan_x,normal_y,normal_x,roundoff;
} EntityView;

static EntityView entity_view(Vec3 eye,Vec3 f,float aspect) {
    Vec3 right=unit(cross(f,(Vec3){0,1,0}));
    float ty=1.f/camera_scale(),tx=aspect*ty;
    /* The GPU multiplies world coordinates by a float matrix; leave extra
       room when world coordinates are large enough to lose precision. */
    float roundoff=.03f+8.f*FLT_EPSILON*(fabsf(eye.x)+fabsf(eye.y)+fabsf(eye.z)+512.f);
    return (EntityView){eye,f,right,cross(right,f),ty,tx,hypotf(1.f,ty),hypotf(1.f,tx),roundoff};
}
static int entity_in_view(const EntityView *view,Vec3 point,float radius) {
    Vec3 delta={point.x-view->eye.x,point.y-view->eye.y,point.z-view->eye.z};
    float d=dot(view->forward,delta),r=radius+view->roundoff;
    /* Only reject a sphere wholly outside a plane. In particular a body
       whose origin is past the edge may still have visible limbs or gear. */
    if (d < -r || fabsf(dot(view->up,delta))-view->tan_y*d > r*view->normal_y) return 0;
    return view->tan_x<=0.f || fabsf(dot(view->right,delta))-view->tan_x*d<=r*view->normal_x;
}
static EntityView current_entity_view(void) {
    float aspect=0.f;
    const char *driver=SDL_GetCurrentVideoDriver();
    if (driver && !strcmp(driver,"ccd") && surface_created_w>0 && surface_created_h>0) {
        /* CCD returns the dimensions of its allocated drawable. Use the
           widest of the existing, requested and fallback aspects so a
           resize between upload and draw cannot hide a model at the edge. */
        aspect=fmaxf((float)surface_created_w/surface_created_h,
            fmaxf((float)view_w/view_h,(float)hd_w/hd_h));
    }
    return entity_view((Vec3){px,py+1.55f,pz},forward(),aspect);
}
static int visible_entity(const EntityView *view,Vec3 point,float radius) {
    int visible=entity_in_view(view,point,radius);
    if (mesh_sample_active) {
        entity_sample_visible+=(unsigned)visible;
        entity_sample_culled+=(unsigned)!visible;
    }
    return visible;
}
int renderer_project(Vec3 point,int width,int height,float *x,float *y) {
    if (!x || !y || width<=0 || height<=0) return 0;
    Vec3 f=forward(),s=unit(cross(f,(Vec3){0,1,0})),u=cross(s,f);
    Vec3 relative={point.x-px,point.y-(py+1.55f),point.z-pz};
    float depth=dot(f,relative);
    if (!(depth>.05f)) return 0;
    float scale=.5f*height*camera_scale()/depth;
    *x=width*.5f+dot(s,relative)*scale;
    *y=height*.5f-dot(u,relative)*scale;
    return isfinite(*x) && isfinite(*y);
}
static void camera_matrix(float out[16],float aspect,Vec3 eye,Vec3 f,Vec3 s,Vec3 u,float zoom) {
    float view[16] = {s.x,u.x,-f.x,0, s.y,u.y,-f.y,0, s.z,u.z,-f.z,0,
                      -dot(s,eye),-dot(u,eye),dot(f,eye),1};
    float k = camera_scale()*zoom, near = .05f;
    float far = (RENDER_CHUNKS + 1) * CHUNK_SIZE * 1.75f;
    float pr[16] = {k/aspect,0,0,0, 0,k,0,0, 0,0,far/(near-far),-1,
                    0,0,near*far/(near-far),0};
    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            out[c * 4 + r] = 0;
            for (int j = 0; j < 4; j++) out[c * 4 + r] += pr[j * 4 + r] * view[c * 4 + j];
        }
    }
}

static int make_atlas(void) {
    Uint32 bytes=ATLAS_WIDTH*ATLAS_HEIGHT*4u;
    SDL_GPUTextureCreateInfo info = {
        .type = SDL_GPU_TEXTURETYPE_2D, .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER, .width = ATLAS_WIDTH, .height = ATLAS_HEIGHT,
        .layer_count_or_depth = 1, .num_levels = 1, .sample_count = SDL_GPU_SAMPLECOUNT_1
    };
    atlas = SDL_CreateGPUTexture(dev, &info);
    SDL_GPUSamplerCreateInfo si = {
        .min_filter = SDL_GPU_FILTER_LINEAR, .mag_filter = SDL_GPU_FILTER_LINEAR,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
    };
    atlasSampler = SDL_CreateGPUSampler(dev, &si);
    SDL_GPUTransferBufferCreateInfo ti = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = bytes};
    SDL_GPUTransferBuffer *tb = SDL_CreateGPUTransferBuffer(dev, &ti);
    if (!atlas || !atlasSampler || !tb) {
        if (tb) SDL_ReleaseGPUTransferBuffer(dev, tb);
        return 0;
    }
    void *p = SDL_MapGPUTransferBuffer(dev, tb, false);
    if (!p) { SDL_ReleaseGPUTransferBuffer(dev, tb); return 0; }
    int filled=material_atlas_build((Uint8 *)p,bytes);
    SDL_UnmapGPUTransferBuffer(dev, tb);
    if (!filled) { SDL_ReleaseGPUTransferBuffer(dev,tb); return 0; }
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd) { SDL_ReleaseGPUTransferBuffer(dev, tb); return 0; }
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    if (!cp) { SDL_CancelGPUCommandBuffer(cmd); SDL_ReleaseGPUTransferBuffer(dev, tb); return 0; }
    SDL_GPUTextureTransferInfo src={.transfer_buffer=tb,.pixels_per_row=ATLAS_WIDTH,.rows_per_layer=ATLAS_HEIGHT};
    SDL_GPUTextureRegion dst={.texture=atlas,.w=ATLAS_WIDTH,.h=ATLAS_HEIGHT,.d=1};
    SDL_UploadToGPUTexture(cp,&src,&dst,false);
    SDL_EndGPUCopyPass(cp);
    int ok = SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(dev, tb);
    return ok;
}

/* Cube corners and neighbor offsets, in matching face order. */
static const float corners[6][4][3] = {
    {{0,0,0},{0,0,1},{0,1,1},{0,1,0}}, {{1,0,1},{1,0,0},{1,1,0},{1,1,1}},
    {{0,1,1},{1,1,1},{1,1,0},{0,1,0}}, {{0,0,0},{1,0,0},{1,0,1},{0,0,1}},
    {{1,0,0},{0,0,0},{0,1,0},{1,1,0}}, {{0,0,1},{1,0,1},{1,1,1},{0,1,1}}
};
static const int dirs[6][3] = {{-1,0,0},{1,0,0},{0,1,0},{0,-1,0},{0,0,-1},{0,0,1}};

static Uint32 capacity_for(Uint32 count) {
    Uint32 capacity = 256;
    while (capacity < count) capacity *= 2;
    return capacity;
}

static Vertex *reserve_face(void) {
    if (meshOOM) return NULL;
    if (vertexCount + 6 > vertexCapacity) {
        Uint32 capacity = capacity_for(vertexCount + 6);
        Vertex *next = (Vertex *)realloc(vertices, capacity * sizeof *next);
        if (!next) { meshOOM = 1; return NULL; }
        vertices = next;
        vertexCapacity = capacity;
    }
    Vertex *base = &vertices[vertexCount];
    vertexCount += 6;
    return base;
}

/* Initialize on the game thread before spawning mesh workers. With -Ofast
   a float reciprocal for the non-power-of-two width can perturb one ULP.
   Use an exact half-texel numerator and round from double once. */
static void init_atlas_coordinates(void) {
    if (atlas_coordinates_ready) return;
    for (int x=0;x<ATLAS_UV_WIDTH;++x)
        atlas_column_u[x]=(float)((double)(2*x+1)/(2*ATLAS_UV_WIDTH));
    atlas_coordinates_ready=1;
}
static inline float atlas_u(int tile,int texel) {
    return atlas_column_u[tile*TILE_EDGE+texel];
}
static inline int face_tile(int type,int side,float x,float z,float size) {
    int tile=tile_for_block(type,side);
    if (size==1.f && tile<=3 && tile!=1) tile=tile_variant(tile,(int)x,(int)z);
    return tile;
}

static void face(float x, float y, float z, float size, int type, int side, float lo, float hi,
                 unsigned tint) {
    Vertex *base = reserve_face();
    if (!base) return;
    int tile=face_tile(type,side,x,z,size);
    float u0=atlas_u(tile,0),u1=atlas_u(tile,TILE_EDGE-1);
    const float v0=.5f/TILE_EDGE,v1=(.5f+(TILE_EDGE-1))/TILE_EDGE;
    unsigned shade=(side==2 ? 255 : side==3 ? 145 : side<2 ? 202 : 225)*mesh_rgb_light()/255;
    Uint8 red=(Uint8)(shade*((tint>>16)&255)/255),green=(Uint8)(shade*((tint>>8)&255)/255);
    Uint8 blue=(Uint8)(shade*(tint&255)/255);
    /* Triangles still use 0,1,2,0,2,3. Compute shared vertices only once
       and hoist face-invariant shading and atlas arithmetic out of the loop. */
    for (int k=0;k<4;++k) {
        Vertex *v=&base[k==3 ? 5 : k];
        const float *corner=corners[side][k];
        v->p[0]=x+size*corner[0];
        v->p[1]=y+size*(corner[1] ? hi : lo);
        v->p[2]=z+size*corner[2];
        v->c[0]=red; v->c[1]=green; v->c[2]=blue; v->c[3]=mesh_alpha();
        v->uv[0]=k==1 || k==2 ? u1 : u0;
        v->uv[1]=k<2 ? v1 : v0;
    }
    base[3]=base[0];
    base[4]=base[2];
}

/* Render surfaces of the shared 2x2x2 occupancy grid. Merge rectangular
   patches, omit internal faces, and preserve block-local atlas coordinates. */
static void build_face(Vec3 origin,float scale,int material,int side,int plane,
        int u,int v,int wide,int high) {
    Vertex *base=reserve_face();
    if (!base) return;
    const int axis=side/2,a=(axis+1)%3,b=(axis+2)%3;
    const float *first=corners[side][0],*second=corners[side][1],*last=corners[side][3];
    int ua=first[a]!=second[a] ? a : b,va=ua==a ? b : a;
    int tile=tile_for_block(material,side);
    float u0=atlas_u(tile,0),du=atlas_u(tile,TILE_EDGE-1)-u0;
    unsigned shade=(side==2 ? 255 : side==3 ? 145 : side<2 ? 202 : 225)*mesh_rgb_light()/255;
    const float at[]={origin.x,origin.y,origin.z};
    for (int k=0;k<4;++k) {
        const float *c=corners[side][k];
        float xyz[3];
        xyz[axis]=plane*.5f; xyz[a]=(u+c[a]*wide)*.5f; xyz[b]=(v+c[b]*high)*.5f;
        Vertex *q=&base[k==3 ? 5 : k];
        for (int i=0;i<3;++i) q->p[i]=at[i]+xyz[i]*scale;
        q->uv[0]=u0+du*(second[ua] ? xyz[ua] : 1.f-xyz[ua]);
        q->uv[1]=(.5f+(TILE_EDGE-1)*(last[va] ? 1.f-xyz[va] : xyz[va]))/TILE_EDGE;
        q->c[0]=q->c[1]=q->c[2]=(Uint8)shade;
        q->c[3]=mesh_alpha();
    }
    base[3]=base[0]; base[4]=base[2];
}
static unsigned unit_bit(int a,int c,int u,int v) {
    int p[3]; p[a]=c; p[(a+1)%3]=u; p[(a+2)%3]=v;
    return 1u<<(p[0]+2*p[2]+4*p[1]);
}
static void shape_mesh(Vec3 at,float size,int material,unsigned mask,const unsigned other[6]) {
    for (int f=0;f<6;++f) {
        const int axis=f/2,d=dirs[f][axis];
        for (int k=0;k<2;++k) {
            const int plane=k+(d>0),outside=k+d;
            unsigned faces=0;
            unsigned next=(unsigned)outside<2 ? mask : other ? other[f] : 0;
            for (int v=0;v<2;++v) for (int u=0;u<2;++u)
                if ((mask&unit_bit(axis,k,u,v)) && !(next&unit_bit(axis,(outside+2)%2,u,v)))
                    faces|=1u<<(u+2*v);
            while (faces) {
                int bit=__builtin_ctz(faces),u=bit&1,v=bit>>1,w=1,h=1;
                if (!u && (faces&(1u<<(bit+1)))) w=2;
                unsigned row=((1u<<w)-1u)<<bit;
                if (!v && (faces&(row<<2))==(row<<2)) h=2;
                faces&=~(row|(h==2 ? row<<2 : 0));
                build_face(at,size,material,f,plane,u,v,w,h);
            }
        }
    }
}

/* Reference builder used only by the native mesh parity test. */
static void reference_face(float x, float y, float z, float size, int type, int side, float lo, float hi,
                           unsigned tint) {
    Vertex *base=reserve_face();
    if (!base) return;
    static const int order[6] = {0,1,2,0,2,3};
    static const float uu[4] = {0,1,1,0}, vv[4] = {1,1,0,0};
    const volatile float atlas_width=ATLAS_UV_WIDTH; /* Force the reference division. */
    int tile = face_tile(type,side,x,z,size);
    unsigned shade = (side == 2 ? 255 : side == 3 ? 145 : side < 2 ? 202 : 225)*mesh_rgb_light()/255;
    for (int i = 0; i < 6; i++) {
        Vertex *v = &base[i];
        int k = order[i];
        v->p[0] = x + size * corners[side][k][0];
        v->p[1] = y + size * (corners[side][k][1] ? hi : lo);
        v->p[2] = z + size * corners[side][k][2];
        v->c[0] = shade * ((tint >> 16) & 255) / 255;
        v->c[1] = shade * ((tint >> 8) & 255) / 255;
        v->c[2] = shade * (tint & 255) / 255;
        v->c[3] = mesh_alpha();
        v->uv[0] = (tile*TILE_EDGE+.5f+(TILE_EDGE-1)*uu[k])/atlas_width;
        v->uv[1] = (.5f + (TILE_EDGE - 1) * vv[k]) / TILE_EDGE;
    }
}

typedef struct { Vec3 world; float c, s; } Pose;
static void part(Pose pose, Vec3 lo, Vec3 size, unsigned tint) {
    static const int order[6] = {0,1,2,0,2,3};
    for (int side = 0; side < 6; ++side) {
        Vertex *base = reserve_face();
        if (!base) return;
        unsigned shade = (side == 2 ? 255 : side == 3 ? 153 : side < 2 ? 204 : 231)*mesh_rgb_light()/255;
        for (int i = 0; i < 6; ++i) {
            const float *corner = corners[side][order[i]];
            float x = lo.x+size.x*corner[0], z = lo.z+size.z*corner[2];
            Vertex *v = &base[i];
            v->p[0] = pose.world.x+x*pose.c-z*pose.s;
            v->p[1] = pose.world.y+lo.y+size.y*corner[1];
            v->p[2] = pose.world.z+x*pose.s+z*pose.c;
            v->c[0] = shade*((tint>>16)&255)/255;
            v->c[1] = shade*((tint>>8)&255)/255;
            v->c[2] = shade*(tint&255)/255; v->c[3] = mesh_alpha();
            v->uv[0] = atlas_u(56,0);
            v->uv[1] = .5f/TILE_EDGE;
        }
    }
}

static void draw_piston(int x,int y,int z,int raw) {
    BlockBox parts[PISTON_PARTS];
    int count=piston_boxes(x,y,z,raw,parts);
    static const int corner[6]={0,1,2,0,2,3},u[4]={0,1,1,0},v[4]={1,1,0,0};
    for (int i=0;i<count;++i) {
        BlockBox b=parts[i];
        Uint32 first=vertexCount;
        part((Pose){{b.x0,b.y0,b.z0},1,0},(Vec3){0,0,0},
            (Vec3){b.x1-b.x0,b.y1-b.y0,b.z1-b.z0},0xffffff);
        if (vertexCount-first!=36) return; /* Mesh OOM is handled by the caller. */
        for (int face_index=0;face_index<6;++face_index) {
            int tile=i==1 ? 76 : tile_for_block(raw,face_index);
            for (int k=0;k<6;++k) {
                Vertex *vertex=&vertices[first+face_index*6+k];
                vertex->uv[0]=atlas_u(tile,(TILE_EDGE-1)*u[corner[k]]);
                vertex->uv[1]=(.5f+(TILE_EDGE-1)*v[corner[k]])/TILE_EDGE;
            }
        }
    }
}

static void draw_chest(int x,int y,int z,int type,ChestJoint joint) {
    Pose p={{(float)x,(float)y,(float)z},1,0};
    float x0=joint==CHEST_WEST ? 0.f : .064f,x1=joint==CHEST_EAST ? 1.f : .936f;
    float z0=joint==CHEST_NORTH ? 0.f : .064f,z1=joint==CHEST_SOUTH ? 1.f : .936f;
    unsigned wood=type==SADDLE_CHEST ? 0xa06636 : 0xa9713c;
    unsigned brass=type==TRAPPED_CHEST ? 0xd85443 : 0xd8be79;
    part(p,(Vec3){x0,.02f,z0},(Vec3){x1-x0,.57f,z1-z0},wood);
    part(p,(Vec3){x0,.575f,z0},(Vec3){x1-x0,.05f,z1-z0},0x684329);
    part(p,(Vec3){x0,.625f,z0},(Vec3){x1-x0,.25f,z1-z0},0xbe884a);
    if (joint==CHEST_ALONE || joint==CHEST_EAST) {
        float latch=joint==CHEST_EAST ? .93f : .43f;
        part(p,(Vec3){latch,.51f,.036f},(Vec3){.14f,.22f,.051f},brass);
    } else if (joint==CHEST_SOUTH) {
        part(p,(Vec3){.915f,.51f,.93f},(Vec3){.052f,.22f,.14f},brass);
    }
}
static void draw_ender_chest(int x,int y,int z,int raw) {
    static const float c[]={1,0,-1,0},s[]={0,1,0,-1};
    unsigned dir=(unsigned)(raw-ENDER_CHEST_BASE);
    if (dir>=4) return;
    Pose p={{x+.5f,(float)y,z+.5f},c[dir],s[dir]};
    part(p,(Vec3){-.41f,.04f,-.40f},(Vec3){.82f,.71f,.80f},0x96724e);
    part(p,(Vec3){-.40f,.76f,-.39f},(Vec3){.80f,.115f,.78f},0xc39b67);
    for (int k=0;k<2;++k) {
        float left=k ? .2375f : -.3875f;
        part(p,(Vec3){left,0,-.4375f},(Vec3){.15f,.81f,.875f},0x53626a);
        part(p,(Vec3){left-.004f,.585f,-.469f},(Vec3){.158f,.094f,.045f},0x8b9da3);
    }
    part(p,(Vec3){-.19f,.30f,-.409f},(Vec3){.38f,.15f,.033f},0x344e59);
    for (int k=0;k<3;++k)
        part(p,(Vec3){-.146f+.11f*k,.342f,-.42f},(Vec3){.071f,.064f,.02f},k==0 ? 0x85d0d5 : 0xbeeee5);
}
static void draw_air_outlet(int x,int y,int z,int raw) {
    BlockBox b=air_outlet_box(x,y,z,raw);
    int turn=raw>=AIR_OUTLET_WALL ? 4*(raw-AIR_OUTLET_WALL) : raw-AIR_OUTLET_BASE;
    float angle=turn*.3926990817f;
    Pose p={{(b.x0+b.x1)*.5f,b.y0,(b.z0+b.z1)*.5f},cosf(angle),sinf(angle)};
    part(p,(Vec3){-.232f,0,-.235f},(Vec3){.464f,.12f,.47f},0x386b69);
    part(p,(Vec3){-.161f,.115f,-.160f},(Vec3){.322f,.215f,.320f},0xb48955);
    part(p,(Vec3){-.225f,.305f,-.219f},(Vec3){.450f,.087f,.438f},0x88cbc4);
    for (int bar=-1;bar<=1;++bar)
        part(p,(Vec3){bar*.105f-.018f,.394f,-.135f},(Vec3){.036f,.034f,.270f},0x2e5557);
}
static void draw_bed(int x,int y,int z,int raw) {
    static const float c[]={1,0,-1,0},s[]={0,1,0,-1};
    int type=raw-BED_BASE,dir=type&3,head=(type&4)!=0;
    Pose p={{x+.5f,(float)y,z+.5f},c[dir],s[dir]};
    float front=head ? -.46f : -.5f,back=head ? .5f : .46f;
    part(p,(Vec3){-.46f,.17f,front},(Vec3){.92f,.13f,back-front},0x957050);
    part(p,(Vec3){-.46f,.30f,front},(Vec3){.92f,.20f,back-front},0xaf303a);
    float end=head ? -.39f : .31f;
    for (int side=-1;side<=1;side+=2)
        part(p,(Vec3){side*.34f-.056f,0,end},(Vec3){.112f,.19f,.115f},0x694729);
    if (head) part(p,(Vec3){-.36f,.498f,-.40f},(Vec3){.72f,.0645f,.31f},0xfff1dc);
    else part(p,(Vec3){-.44f,.501f,-.49f},(Vec3){.88f,.05f,.92f},0xcb3943);
}

static void draw_barrier(int x,int y,int z,int raw,const int adjacent[4]) {
    Pose p={{(float)x,(float)y,(float)z},1,0};
    BlockBox boxes[BARRIER_PARTS];
    int n=barrier_boxes(raw,fence_connections(raw,adjacent),BARRIER_VISUAL,boxes);
    for (int i=0;i<n;++i) {
        BlockBox b=boxes[i];
        unsigned tint=door_cell(raw) ? (door_bits(raw)&DOOR_IRON ?
            (door_upper(raw)||!i ? 0xadb4bf : i==4 ? 0x595b61 : 0x7d858e) :
            (door_upper(raw)||!i ? 0xae7744 : i==4 ? 0xc5a46a : 0x79522d)) :
            trapdoor_cell(raw) ? (i<2 ? 0xaa733b : 0xb98048) :
            ladder_cell(raw) ? (i<2 ? 0xa6733f : 0xcf9a53) :
            raw==NETHER_FENCE ? (i ? 0x66382d : 0x763f34) :
            raw==WOOD_FENCE ? (i ? 0xb58047 : 0xa36d3b) : (i<2 ? 0x835333 : 0xbc864f);
        part(p,(Vec3){b.x0,b.y0,b.z0},(Vec3){b.x1-b.x0,b.y1-b.y0,b.z1-b.z0},tint);
    }
}

static Vec3 local_point(Pose p, Vec3 a) {
    return (Vec3){p.world.x+a.x*p.c-a.z*p.s,p.world.y+a.y,p.world.z+a.x*p.s+a.z*p.c};
}

static void flat_quad(const Vec3 points[4], unsigned tint) {
    static const int order[6]={0,1,2,0,2,3};
    Vertex *base=reserve_face();
    if (!base) return;
    unsigned light=mesh_rgb_light();
    Uint8 red=(Uint8)(((tint>>16)&255u)*light/255),
        green=(Uint8)(((tint>>8)&255u)*light/255),blue=(Uint8)((tint&255u)*light/255);
    for (int i=0;i<6;++i) {
        Vec3 p=points[order[i]];
        base[i]=(Vertex){{p.x,p.y,p.z},
            {red,green,blue,mesh_alpha()},
            {atlas_u(56,0),.5f/TILE_EDGE}};
    }
}

static Vec3 glass_point(Vec3 cube,float size,int side,float u,float v) {
    const float (*f)[3]=corners[side];
    return (Vec3){cube.x+size*(f[0][0]+u*(f[1][0]-f[0][0])+v*(f[3][0]-f[0][0])),
                  cube.y+size*(f[0][1]+u*(f[1][1]-f[0][1])+v*(f[3][1]-f[0][1])),
                  cube.z+size*(f[0][2]+u*(f[1][2]-f[0][2])+v*(f[3][2]-f[0][2]))};
}
static unsigned glass_highlight(unsigned tint) {
    unsigned out=0;
    for (unsigned shift=0;shift<=16;shift+=8)
        out|=((((tint>>shift)&255u)*3+255u)/4u)<<shift;
    return out;
}
static void glass_face(Vec3 cube,float size,int side,int cell) {
    int item=stained_glass_item(cell);
    unsigned color=item==AIR ? (side==2 ? 0xd3f9f6 : 0x9ccbd2) : item_def(item)->color;
    unsigned highlight=item==AIR ? 0xc6e5e6 : glass_highlight(color);
    /* Existing CCD triangle profile is opaque. Leave actual holes so
       terrain and entities behind the glass still pass the depth test. */
    const float strips[][4]={{0,0,1,.035f},{0,.965f,1,1},{0,.035f,.035f,.965f},
        {.965f,.035f,1,.965f},{.08f,.71f,.19f,.735f},{.08f,.76f,.24f,.788f}};
    for (unsigned i=0;i<sizeof strips/sizeof *strips;++i) {
        const float *q=strips[i];
        Vec3 points[]={glass_point(cube,size,side,q[0],q[1]),glass_point(cube,size,side,q[2],q[1]),
            glass_point(cube,size,side,q[2],q[3]),glass_point(cube,size,side,q[0],q[3])};
        flat_quad(points,i<4 ? color : highlight);
    }
}
static void air_gauge_quad(Vec3 o,float size,int side,float depth,
        float u0,float v0,float u1,float v1,unsigned tint) {
    float inset=size*((side<2 ? .08f : .065f)-depth);
    o.x-=dirs[side][0]*inset;
    o.z-=dirs[side][2]*inset;
    Vec3 points[]={glass_point(o,size,side,u0,v0),glass_point(o,size,side,u1,v0),
        glass_point(o,size,side,u1,v1),glass_point(o,size,side,u0,v1)};
    flat_quad(points,tint);
}
static void draw_air_station(Vec3 o,float size,int pressure) {
    Pose p={o,1,0};
    part(p,(Vec3){.035f*size,.02f*size,.032f*size},(Vec3){.93f*size,.16f*size,.936f*size},0x354746);
    part(p,(Vec3){.08f*size,.18f*size,.065f*size},(Vec3){.84f*size,.70f*size,.87f*size},0x458b82);
    part(p,(Vec3){.15f*size,.86f*size,.13f*size},(Vec3){.70f*size,.10f*size,.74f*size},0xb58854);
    /* Placement has no facing, so the pressure is readable from any approach.
       The strips are quads rather than little boxes: 32 faces across all sides. */
    const int sides[]={0,1,4,5};
    for (unsigned s=0;s<sizeof sides/sizeof sides[0];++s) {
        int side=sides[s];
        air_gauge_quad(o,size,side,.008f,.19f,.44f,.81f,.74f,0xb88e59);
        air_gauge_quad(o,size,side,.015f,.21f,.46f,.79f,.72f,0x192b2e);
        for (int i=0;i<AIR_STATION_MAX;++i) {
            float u=.245f+.089f*i;
            air_gauge_quad(o,size,side,.023f,u,.51f,u+.065f,.67f,
                i<pressure ? 0xa5f0cd : 0x3d5452);
        }
    }
    part(p,(Vec3){.45f*size,.95f*size,.40f*size},(Vec3){.12f*size,.04f*size,.20f*size},0x244548);
}
static void draw_air_pipe(Vec3 o,float size) {
    Pose p={o,1,0};
    part(p,(Vec3){0,0,0},(Vec3){size,size,size},0x426260);
    for (int end=0;end<2;++end) {
        float a=end ? size : -.002f*size;
        part(p,(Vec3){a,.225f*size,.225f*size},(Vec3){.002f*size,.55f*size,.55f*size},0xb78b5e);
        part(p,(Vec3){.225f*size,a,.225f*size},(Vec3){.55f*size,.002f*size,.55f*size},0xb78b5e);
        part(p,(Vec3){.225f*size,.225f*size,a},(Vec3){.55f*size,.55f*size,.002f*size},0xb78b5e);
    }
}
static void draw_dungeon_spawner(Vec3 origin,int raw) {
    Pose p={origin,1,0};
    const unsigned frame=0x35434d,bar=0x516777;
    for (int edge=0;edge<2;++edge) {
        float y=edge*.92f;
        part(p,(Vec3){0,y,0},(Vec3){1,.08f,.08f},frame);
        part(p,(Vec3){0,y,.92f},(Vec3){1,.08f,.08f},frame);
        part(p,(Vec3){0,y,.08f},(Vec3){.08f,.08f,.84f},frame);
        part(p,(Vec3){.92f,y,.08f},(Vec3){.08f,.08f,.84f},frame);
        for (int i=0;i<3;++i) {
            float q=.22f+.25f*i,s=.02f+edge*.90f;
            part(p,(Vec3){q,.08f,s},(Vec3){.060f,.84f,.060f},bar);
            part(p,(Vec3){s,.08f,q},(Vec3){.060f,.84f,.060f},bar);
        }
    }
    for (int a=0;a<2;++a) for (int b=0;b<2;++b)
        part(p,(Vec3){.92f*a,.08f,.92f*b},(Vec3){.08f,.84f,.08f},frame);
    /* Recognizable miniature species, with transparent gaps in the cage. */
    if (raw==DUNGEON_SPIDER_CELL || raw==MINE_CAVE_SPIDER_CELL) {
        int cave=raw==MINE_CAVE_SPIDER_CELL;
        part(p,(Vec3){.34f,.42f,.40f},(Vec3){.32f,.20f,.28f},cave ? 0x2e6570 : 0x503d42);
        part(p,(Vec3){.40f,.43f,.31f},(Vec3){.20f,.13f,.09f},cave ? 0x294c58 : 0x6e4344);
        for (int i=0;i<4;++i)
            part(p,(Vec3){.23f,.43f,.39f+i*.063f},(Vec3){.54f,.035f,.032f},cave ? 0x284750 : 0x4b3437);
    } else if (raw==DUNGEON_ZOMBIE_CELL) {
        /* Preserve the old save's cage ID, not its former figurine. */
        part(p,(Vec3){.35f,.38f,.36f},(Vec3){.30f,.30f,.28f},0x514c3e);
        part(p,(Vec3){.37f,.62f,.38f},(Vec3){.26f,.11f,.23f},0x827e58);
        part(p,(Vec3){.39f,.54f,.345f},(Vec3){.22f,.035f,.025f},0xdea457);
        part(p,(Vec3){.395f,.25f,.42f},(Vec3){.071f,.16f,.13f},0x5e5845);
        part(p,(Vec3){.535f,.25f,.42f},(Vec3){.071f,.16f,.13f},0x5e5845);
    } else {
        part(p,(Vec3){.40f,.60f,.40f},(Vec3){.20f,.20f,.20f},0xcbcfbe);
        part(p,(Vec3){.41f,.36f,.43f},(Vec3){.18f,.24f,.14f},0xbfbdae);
        part(p,(Vec3){.41f,.23f,.44f},(Vec3){.067f,.13f,.12f},0x737c9b);
        part(p,(Vec3){.523f,.23f,.44f},(Vec3){.067f,.13f,.12f},0x737c9b);
    }
}
static int dungeon_mesh_selftest(void) {
    unsigned old=brightness;
    brightness=255;
    int failed=0;
    const int cages[]={DUNGEON_ZOMBIE_CELL,DUNGEON_SKELETON_CELL,DUNGEON_SPIDER_CELL,MINE_CAVE_SPIDER_CELL};
    for (unsigned i=0;i<sizeof cages/sizeof *cages;++i) {
        vertexCount=meshOOM=0;
        draw_dungeon_spawner((Vec3){0,0,0},cages[i]);
        failed|=meshOOM || vertexCount<24*36 || vertexCount%6;
        for (unsigned base=0;base+5<vertexCount;base+=6) {
            float low[3]={1,1,1},high[3]={0,0,0};
            for (unsigned j=0;j<6;++j) for (int axis=0;axis<3;++axis) {
                float v=vertices[base+j].p[axis];
                low[axis]=fminf(low[axis],v);
                high[axis]=fmaxf(high[axis],v);
                failed|=v<-.00001f || v>1.00001f;
            }
            /* Each exterior wall must leave holes. No filled face. */
            for (int axis=0;axis<3;++axis) if (high[axis]-low[axis]<.00001f &&
                (low[axis]<.00001f || high[axis]>.99999f))
                failed|=(high[(axis+1)%3]-low[(axis+1)%3])*(high[(axis+2)%3]-low[(axis+2)%3])>=.9f;
        }
    }
    brightness=old;
    free(vertices); vertices=NULL; vertexCount=vertexCapacity=0;
    printf("MESH TEST four spawner cages and open walls failures=%d\n",failed!=0);
    return failed!=0;
}
static void pane_rect(Pose pose,BlockBox b,float size,int side,float u0,float v0,float u1,float v1,unsigned tint) {
    const float uv[][2]={{u0,v0},{u1,v0},{u1,v1},{u0,v1}};
    const float (*f)[3]=corners[side];
    Vec3 p[4];
    for (int i=0;i<4;++i) {
        float q[3];
        for (int axis=0;axis<3;++axis)
            q[axis]=f[0][axis]+uv[i][0]*(f[1][axis]-f[0][axis])+uv[i][1]*(f[3][axis]-f[0][axis]);
        p[i]=local_point(pose,(Vec3){size*(b.x0+(b.x1-b.x0)*q[0]),
            size*(b.y0+(b.y1-b.y0)*q[1]),size*(b.z0+(b.z1-b.z0)*q[2])});
    }
    flat_quad(p,tint);
}
static void draw_pane(Pose pose,float size,int raw,unsigned parts,unsigned links,unsigned above,unsigned below) {
    static const int facing[]={WEST,EAST,-1,-1,NORTH,SOUTH};
    int bars=raw==IRON_BARS_CELL;
    unsigned color=item_def(pane_item(raw))->color;
    unsigned highlight=glass_highlight(color);
    for (int i=0;i<=PANE_CENTER;++i) if (parts&(1u<<i)) {
        BlockBox b=pane_part(i);
        for (int side=0;side<6;++side) {
            int d=facing[side];
            int covered_above=(above&(1u<<i))!=0,covered_below=(below&(1u<<i))!=0;
            if ((side==2 && covered_above) || (side==3 && covered_below)) continue;
            if (d>=0 && (i==PANE_CENTER ? (parts&(1u<<d))!=0 :
                ((i+2)&3)==d || (i==d && (links&(1u<<d))))) continue;
            unsigned across=d>=0 ? ((1u<<((d+1)&3))|(1u<<((d+3)&3))) : 0;
            int join=i==PANE_CENTER && (parts&across);
            if (d<0 || i==d || (i==PANE_CENTER && !join)) {
                pane_rect(pose,b,size,side,0,0,1,1,side==2 ? highlight : color);
            } else if (bars) {
                pane_rect(pose,b,size,side,0,.06f,1,.14f,color);
                pane_rect(pose,b,size,side,0,.86f,1,.94f,highlight);
                for (int post=0;post<(join ? 1 : 2);++post) {
                    float u=join ? .5f : .24f+post*.52f,w=join ? .22f : .065f;
                    pane_rect(pose,b,size,side,u-w,.14f,u+w,.86f,color);
                }
            } else {
                /* Outline shading uses actual open geometry; do not put an
                   opaque quad in transparent cells or at connected ends. */
                if (!covered_above) pane_rect(pose,b,size,side,0,.965f,1,1,highlight);
                if (!covered_below) pane_rect(pose,b,size,side,0,0,1,.035f,color);
                if (!join) pane_rect(pose,b,size,side,.35f,.70f,.63f,.727f,highlight);
            }
        }
    }
}
static void draw_filter_mat(int x,int y,int z,int raw) {
    Pose p={{(float)x,(float)y,(float)z},1,0};
    const unsigned fiber=raw==FILTER_MAT_SEEDED ? 0x7f8d5e : 0x468c75;
    const unsigned water=0x78c9cb;
    part(p,(Vec3){.07f,.005f,.07f},(Vec3){.86f,.055f,.86f},0x426054);
    part(p,(Vec3){.13f,.062f,.13f},(Vec3){.74f,.041f,.74f},fiber);
    for (int i=0;i<4;++i) {
        float xo=.18f+i*.16f;
        part(p,(Vec3){xo,.107f,.14f},(Vec3){.035f,.018f,.72f},i&1 ? 0x9cb17a : 0x64845f);
    }
    if (raw>=FILTER_MAT_ONE)
        part(p,(Vec3){.28f,.110f,.38f},(Vec3){.16f,.035f,.24f},water);
    if (raw==FILTER_MAT_TWO)
        part(p,(Vec3){.59f,.110f,.38f},(Vec3){.16f,.035f,.24f},water);
}
static void draw_brewing_stand(int x,int y,int z) {
    Pose p={{x+.50f,(float)y,z+.50f},1,0};
    part(p,(Vec3){-.39f,0,-.33f},(Vec3){.78f,.14f,.66f},0x435553);
    part(p,(Vec3){-.34f,.14f,-.285f},(Vec3){.68f,.30f,.57f},0x96a69b);
    part(p,(Vec3){-.28f,.44f,-.07f},(Vec3){.56f,.28f,.34f},0x556d62);
    part(p,(Vec3){-.245f,.72f,-.04f},(Vec3){.49f,.155f,.28f},0x8eb4a4);
    const unsigned lanes[]={0xa6c778,0xcb9b64,0x7aafb3};
    for (int i=0;i<3;++i) {
        float x=-.24f+.24f*i;
        part(p,(Vec3){x-.058f,.32f,-.316f},(Vec3){.116f,.076f,.038f},lanes[i]);
        part(p,(Vec3){x-.058f,.32f,.278f},(Vec3){.116f,.076f,.038f},lanes[i]);
        part(p,(Vec3){x-.052f,.44f,-.23f},(Vec3){.104f,.055f,.13f},0x616f67);
    }
}
static void wire_patch(float x,float y,float z,float cx,float cz,unsigned color) {
    flat_quad((Vec3[]){{x,y,z},{cx,y,z},{cx,y,cz},{x,y,cz}},color);
}
static void draw_wire(int x,int y,int z,int raw,WireShape shape) {
    unsigned strength=(unsigned)(raw-WIRE_BASE);
    unsigned color=((83u+strength*11u)<<16)|((56u+strength*9u)<<8)|(28u+strength*4u);
    float cx=x+.5f,cz=z+.5f,base=y+.024f,r=.10f;
    wire_patch(cx-r,base,cz-r,cx+r,cz+r,color);
    for (int d=0;d<4;++d) if (shape.mask&(1<<d)) {
        float dx=(float)rail_dx(d),dz=(float)rail_dz(d);
        float ex=cx+dx*.499f,ez=cz+dz*.499f;
        float wx=-dz*r,wz=dx*r;
        flat_quad((Vec3[]){{cx+wx,base,cz+wz},{ex+wx,base,ez+wz},
            {ex-wx,base,ez-wz},{cx-wx,base,cz-wz}},color);
        if (shape.raised&(1<<d))
            flat_quad((Vec3[]){{ex+wx,base,ez+wz},{ex-wx,base,ez-wz},
                {ex-wx,base+1.f,ez-wz},{ex+wx,base+1.f,ez+wz}},color);
    }
}
static void draw_lever(int x,int y,int z,int raw) {
    BlockBox bits[4];
    int n=redstone_lever_boxes(raw,bits);
    Pose p={{(float)x,(float)y,(float)z},1,0};
    int active=(raw-LEVER_BASE)>=LEVER_ON;
    for (int i=0;i<n;++i) {
        BlockBox b=bits[i];
        unsigned tint=i==0 ? 0x787981 : i==3 && active ? 0xe3654a : 0xaa7848;
        part(p,(Vec3){b.x0,b.y0,b.z0},(Vec3){b.x1-b.x0,b.y1-b.y0,b.z1-b.z0},tint);
    }
}
static void draw_redstone_torch(int x,int y,int z,int raw) {
    BlockBox bits[4];
    int n=redstone_torch_boxes(raw,bits);
    Pose p={{(float)x,(float)y,(float)z},1,0};
    int on=raw>=TORCH_POWER_BASE+TORCH_POWER_ON;
    for (int i=0;i<n;++i) {
        BlockBox b=bits[i];
        unsigned tint=i+1==n ? (on ? 0xf9b85b : 0x70553a) : 0x6f6660;
        part(p,(Vec3){b.x0,b.y0,b.z0},(Vec3){b.x1-b.x0,b.y1-b.y0,b.z1-b.z0},tint);
    }
}
static void draw_repeater(int x,int y,int z,int raw,int locked) {
    static const float c[]={1,0,-1,0},s[]={0,1,0,-1};
    int facing=(raw-REPEATER_BASE)&3,delay=((raw-REPEATER_BASE)>>2)&3;
    int on=(raw-REPEATER_BASE)&REPEATER_ON;
    Pose p={{x+.5f,(float)y,z+.5f},c[facing],s[facing]};
    part(p,(Vec3){-.46f,0,-.47f},(Vec3){.92f,.11f,.94f},0x93949d);
    part(p,(Vec3){-.058f,.112f,-.31f},(Vec3){.116f,.013f,.60f},on ? 0xf4b14d : 0x6b5137);
    float rear=-.045f+.114f*delay;
    if (locked)
        part(p,(Vec3){-.30f,.13f,rear-.065f},(Vec3){.60f,.10f,.13f},0x656877);
    else {
        part(p,(Vec3){-.050f,.116f,rear-.048f},(Vec3){.10f,.17f,.096f},0xa7734e);
        part(p,(Vec3){-.071f,.259f,rear-.069f},(Vec3){.142f,.075f,.138f},on ? 0xd98d3e : 0x73543a);
    }
    part(p,(Vec3){-.050f,.116f,-.365f},(Vec3){.10f,.15f,.10f},0x9a613d);
    part(p,(Vec3){-.068f,.239f,-.383f},(Vec3){.136f,.08f,.136f},on ? 0xffce6d : 0x73543a);
}
static void draw_comparator(int x,int y,int z,int raw,int power) {
    static const float c[]={1,0,-1,0},s[]={0,1,0,-1};
    int facing=(raw-COMPARATOR_BASE)&3,sub=(raw-COMPARATOR_BASE)&COMPARATOR_SUBTRACT;
    Pose p={{x+.5f,(float)y,z+.5f},c[facing],s[facing]};
    part(p,(Vec3){-.46f,0,-.47f},(Vec3){.92f,.11f,.94f},0x9b98a0);
    part(p,(Vec3){-.068f,.112f,-.33f},(Vec3){.136f,.018f,.66f},0x772e35);
    const float xs[]={-.25f,.25f,0.f},zs[]={.25f,.25f,-.29f};
    for (int i=0;i<3;++i) {
        float h=i==2 && !sub ? .14f : .17f;
        unsigned tip=i==2 ? (sub ? 0xff6950 : 0x71373a) :
            power ? (0x7d2428u+((unsigned)power*8u<<16)) : 0x71373a;
        part(p,(Vec3){xs[i]-.048f,.11f,zs[i]-.048f},(Vec3){.096f,h,.096f},0x9b6339);
        part(p,(Vec3){xs[i]-.067f,.11f+h,zs[i]-.067f},(Vec3){.134f,.063f,.134f},tip);
    }
}
static void draw_pressure_plate(int x,int y,int z,int raw,int powered) {
    const ItemDef *def=item_def(redstone_plate_item(raw));
    float height=powered ? .03125f : .0625f;
    Pose p={{(float)x,(float)y,(float)z},1,0};
    unsigned top=def ? def->color : 0x8b8d92;
    part(p,(Vec3){.0625f,0,.0625f},(Vec3){.875f,height-.008f,.875f},0x50484a);
    part(p,(Vec3){.09375f,height-.008f,.09375f},(Vec3){.8125f,.008f,.8125f},top);
}
static void draw_button(int x,int y,int z,int raw,int powered) {
    BlockBox b;
    if (!redstone_button_box(raw,powered,&b)) return;
    Pose p={{(float)x,(float)y,(float)z},1,0};
    unsigned color=raw>=BUTTON_WOOD ? 0xb88450 : 0x999ba6;
    part(p,(Vec3){b.x0,b.y0,b.z0},(Vec3){b.x1-b.x0,b.y1-b.y0,b.z1-b.z0},color);
}
static void draw_tripwire(int x,int y,int z,int raw,int ground,unsigned links,int powered) {
    BlockBox boxes[TRIPWIRE_PARTS];
    int n=tripwire_boxes(raw,ground,links,powered,boxes);
    Pose p={{(float)x,(float)y,(float)z},1,0};
    for (int i=0;i<n;++i) {
        BlockBox b=boxes[i];
        unsigned color=!tripwire_hook_cell(raw) || i==3 ? 0xc0b9a5 : i==0 ? 0xa07449 :
            powered ? 0xe34b40 : 0xaebac5;
        part(p,(Vec3){b.x0,b.y0,b.z0},(Vec3){b.x1-b.x0,b.y1-b.y0,b.z1-b.z0},color);
    }
}
static void draw_hopper(int x,int y,int z,int raw) {
    BlockBox boxes[HOPPER_PARTS];
    int n=hopper_boxes(0,0,0,raw,boxes);
    Pose p={{(float)x,(float)y,(float)z},1,0};
    for (int i=0;i<n;++i) {
        BlockBox b=boxes[i];
        part(p,(Vec3){b.x0,b.y0,b.z0},(Vec3){b.x1-b.x0,b.y1-b.y0,b.z1-b.z0},
            i==0 ? 0x414650 : i==5 ? 0x535965 : 0x777e89);
    }
}
static void draw_anvil(int x,float y,int z,int raw) {
    BlockBox boxes[ANVIL_PARTS];
    int count=anvil_boxes(0,0,0,raw,boxes);
    Pose p={{(float)x,y,(float)z},1,0};
    const unsigned metals[]={0x51545e,0x4b4147,0x453338};
    int wear=(raw-ANVIL_BASE)/2;
    for (int i=0;i<count;++i) {
        BlockBox b=boxes[i];
        part(p,(Vec3){b.x0,b.y0,b.z0},(Vec3){b.x1-b.x0,b.y1-b.y0,b.z1-b.z0},
            i==1 ? 0x3b4049 : metals[wear]);
    }
}
static void draw_calibration_table(int x,int y,int z) {
    Pose p={{x+.5f,(float)y,z+.5f},1,0};
    part(p,(Vec3){-.49f,0,-.49f},(Vec3){.98f,.16f,.98f},0x4d565b);
    part(p,(Vec3){-.40f,.16f,-.40f},(Vec3){.8f,.29f,.8f},0x62574c);
    part(p,(Vec3){-.49f,.45f,-.49f},(Vec3){.98f,.11f,.98f},0x998a72);
    part(p,(Vec3){-.38f,.56f,-.28f},(Vec3){.76f,.05f,.56f},0x495657);
    for (int i=0;i<2;++i) {
        float x=-.32f+i*.49f;
        part(p,(Vec3){x,.61f,-.23f},(Vec3){.15f,.125f,.46f},0xad7647);
        part(p,(Vec3){x+.016f,.735f,-.17f},(Vec3){.118f,.015f,.34f},0xcdb678);
    }
    part(p,(Vec3){-.26f,.26f,.40f},(Vec3){.52f,.07f,.02f},0xa17c55);
}
static void draw_cane(int x,int y,int z) {
    Pose p={{x+.5f,(float)y,z+.5f},1,0};
    for (int i=0;i<3;++i) {
        float ax=-.22f+.19f*i,az=i==1 ? -.12f : .11f;
        part(p,(Vec3){ax,0,az},(Vec3){.065f,1,.066f},i&1 ? 0xa2b054 : 0x779a46);
        part(p,(Vec3){ax-.006f,.58f,az-.005f},(Vec3){.078f,.043f,.078f},0x516d3a);
    }
}

static void draw_plant(int x,int y,int z,int raw) {
    float height=plant_height(raw);
    int stage=(raw-WHEAT_BASE)&7;
    float radius=raw==TALL_GRASS ? .36f : .17f+.026f*stage;
    unsigned leaf=raw==TALL_GRASS ? 0x55933b : raw>=CARROT_BASE && raw<POTATO_BASE ? 0x539e36 : 0x79a244;
    unsigned stalk=raw==WHEAT_BASE+7 ? 0xbd883f : leaf;
    unsigned tip=raw==WHEAT_BASE+7 ? 0xf0c267 : raw==POTATO_BASE+7 ? 0xb0ca6a : 0x90be52;
    float cx=x+.5f,cz=z+.5f;
    for (int plane=0;plane<2;++plane) {
        float dx=radius,dz=plane ? -radius : radius;
        for (int n=-1;n<=1;++n) {
            float offset=.19f*n,base=y-.06f+(raw==TALL_GRASS ? .06f : 0.f);
            float a=cx-offset*dz,b=cz+offset*dx;
            float h=height*(n ? .73f : 1.f),s=n*.20f;
            flat_quad((Vec3[]){{a-dx*.08f,base,b-dz*.08f},{a+dx*.08f,base,b+dz*.08f},
                {a+dx*(s+.12f),base+h,b+dz*(s+.12f)},{a+dx*(s-.12f),base+h,b+dz*(s-.12f)}},stalk);
            flat_quad((Vec3[]){{a+dx*(s-.27f),base+h*.60f,b+dz*(s-.27f)},
                {a+dx*(s+.27f),base+h*.60f,b+dz*(s+.27f)},
                {a+dx*(s+.14f),base+h,b+dz*(s+.14f)},{a+dx*(s-.14f),base+h,b+dz*(s-.14f)}},tip);
        }
    }
}

static void draw_cobweb(int x,int y,int z) {
    /* Two crossed planes made of thin opaque threads. Holes really remain
       open; the world mesh does not need transparent depth sorting. */
    for (int plane=0;plane<2;++plane) {
        float start=z+(plane ? .98f : .02f), slope=plane ? -1.f : 1.f;
        for (int i=1;i<=3;++i) {
            float h=i*.24f, t=i*.24f, w=.019f;
            flat_quad((Vec3[]){{x+.02f,y+h-w,start},{x+.98f,y+h-w,start+slope*.96f},
                {x+.98f,y+h+w,start+slope*.96f},{x+.02f,y+h+w,start}},0xd8dfdf);
            flat_quad((Vec3[]){{x+t-w,y+.02f,start+slope*(t-w-.02f)},
                {x+t+w,y+.02f,start+slope*(t+w-.02f)},
                {x+t+w,y+.94f,start+slope*(t+w-.02f)},
                {x+t-w,y+.94f,start+slope*(t-w-.02f)}},0xc9d1d3);
        }
    }
}

static void draw_sapling(int x,int y,int z,int raw) {
    const ItemDef *item=item_def(OAK_SAPLING+sapling_species(raw));
    float h=plant_height(raw);
    unsigned green=item ? item->color : 0x689f39u;
    Pose p={{x+.5f,(float)y,z+.5f},1,0};
    part(p,(Vec3){-.035f,0,-.035f},(Vec3){.07f,h*.93f,.07f},0x8d6542);
    for (int axis=0;axis<2;++axis) {
        float a=axis ? .23f : .15f,b=axis ? .15f : .23f;
        part(p,(Vec3){-a,h*(.24f+.24f*axis),-b},(Vec3){a*2,.11f,b*2},green);
    }
    part(p,(Vec3){-.092f,h-.165f,-.092f},(Vec3){.184f,.165f,.184f},green);
}

static void draw_flower(int x,int y,int z,int yellow) {
    Pose p={{x+.5f,(float)y,z+.5f},1,0};
    float top=yellow ? .37f : .56f;
    part(p,(Vec3){-.021f,0,-.021f},(Vec3){.042f,top+.06f,.042f},0x5d8937);
    part(p,(Vec3){-.157f,.23f,-.033f},(Vec3){.15f,.044f,.066f},0x609944);
    for (int j=-1;j<=1;j+=2) {
        part(p,(Vec3){j*.084f-.071f,top,-.066f},(Vec3){.142f,.095f,.132f},yellow ? 0xead030 : 0xd8332c);
        part(p,(Vec3){-.062f,top+.002f,j*.085f-.068f},(Vec3){.124f,.094f,.136f},yellow ? 0xffe551 : 0xe3513e);
    }
    part(p,(Vec3){-.038f,top+.078f,-.038f},(Vec3){.076f,.036f,.076f},yellow ? 0x9b691d : 0x664018);
}

static void mushroom_model(Pose p,Vec3 at,float s,int red) {
    part(p,(Vec3){at.x-.07f*s,at.y,at.z-.07f*s},(Vec3){.14f*s,.29f*s,.14f*s},0xdbcead);
    part(p,(Vec3){at.x-.29f*s,at.y+.25f*s,at.z-.25f*s},(Vec3){.58f*s,.095f*s,.50f*s},red ? 0xb93539 : 0x8d6344);
    part(p,(Vec3){at.x-.21f*s,at.y+.345f*s,at.z-.175f*s},(Vec3){.42f*s,.056f*s,.35f*s},red ? 0xe24942 : 0xa77a53);
    if (red) {
        part(p,(Vec3){at.x-.13f*s,at.y+.396f*s,at.z+.07f*s},(Vec3){.07f*s,.007f*s,.058f*s},0xfaf0de);
        part(p,(Vec3){at.x+.073f*s,at.y+.396f*s,at.z-.128f*s},(Vec3){.077f*s,.007f*s,.071f*s},0xfaf0de);
    }
}
static void draw_mushroom(int x,int y,int z,int red) {
    Pose p={{x+.5f,(float)y,z+.5f},1,0};
    mushroom_model(p,(Vec3){0,0,0},1,red);
}
static void draw_dye_plant(int x,int y,int z,int raw) {
    BlockBox box;
    if (!dye_plant_box(raw,x,y,z,&box)) return;
    Pose p={{box.x0,box.y0,box.z0},1,0};
    Vec3 size={box.x1-box.x0,box.y1-box.y0,box.z1-box.z0};
    if (cactus_cell(raw)) {
        part(p,(Vec3){0,0,0},size,0x508535);
        for (int i=1;i<4;++i) {
            part(p,(Vec3){size.x*i/4-.017f,0,-.004f},(Vec3){.034f,1.f,.012f},0x72a44b);
            part(p,(Vec3){-.004f,0,size.z*i/4-.017f},(Vec3){.012f,1.f,.034f},0x61993e);
        }
    } else {
        static const unsigned shades[]={0x7d9a43,0xaf8235,0xb96b2b};
        part(p,(Vec3){0,0,0},size,shades[cocoa_stage(raw)]);
        part(p,(Vec3){size.x*.21f,size.y*.12f,size.z*.21f},(Vec3){size.x*.58f,size.y*.88f+.015f,size.z*.58f},
             cocoa_stage(raw)==2 ? 0x925022 : 0x747238);
    }
}

static void draw_torch(int x,int y,int z) {
    Pose p={{x+.50f,(float)y,z+.50f},1,0};
    part(p,(Vec3){-.065f,0,-.065f},(Vec3){.13f,.53f,.13f},0xb1824a);
    part(p,(Vec3){-.088f,.46f,-.088f},(Vec3){.176f,.16f,.176f},0xef8f39);
    part(p,(Vec3){-.055f,.55f,-.055f},(Vec3){.11f,.13f,.11f},0xffd681);
}
static void draw_flame(float x,float y,float z,float size) {
    for (int i=0;i<3;++i) {
        float dx=(i==0 ? 1.f : i==1 ? .50f : -.50f)*size;
        float dz=(i==0 ? 0.f : .866f)*size;
        float bx=x+(i-1)*size*.14f,bz=z+(1-i)*size*.09f;
        Vec3 base[]={{bx-dx*.44f,y,bz-dz*.44f},{bx+dx*.44f,y,bz+dz*.44f},
            {bx+dx*.23f,y+size*.55f,bz+dz*.23f},{bx-dx*.31f,y+size*.42f,bz-dz*.31f}};
        Vec3 tip[]={base[3],base[2],{bx+dx*.06f,y+size*(.95f-.08f*i),bz+dz*.06f},
            {bx+dx*.06f,y+size*(.95f-.08f*i),bz+dz*.06f}};
        flat_quad(base,i==1 ? 0xffc047 : 0xf48224);
        flat_quad(tip,i==1 ? 0xffffbd : 0xffc65f);
    }
}

static int refresh_fishing(void) {
    FishingView view=fishing_view();
    if (view.phase==FISH_IDLE) { fishing_mesh.count=0; return 1; }
    vertexCount=meshOOM=0;
    Vec3 p=view.point,dir=forward();
    Vec3 hand={px+dir.x*.38f+cosf(yaw)*.27f,py+1.55f+dir.y*.38f-.20f,
               pz+dir.z*.38f+sinf(yaw)*.27f};
    Vec3 nl=unit(cross((Vec3){p.x-hand.x,p.y-hand.y,p.z-hand.z},dir));
    Vec3 w=scale(nl,.013f);
    Vec3 line[]={{hand.x-w.x,hand.y-w.y,hand.z-w.z},{hand.x+w.x,hand.y+w.y,hand.z+w.z},
        {p.x+w.x,p.y+w.y,p.z+w.z},{p.x-w.x,p.y-w.y,p.z-w.z}};
    flat_quad(line,0xddd9bc);
    Pose bobber={p,1,0};
    part(bobber,(Vec3){-.064f,-.026f,-.064f},(Vec3){.128f,.063f,.128f},0xdfebed);
    part(bobber,(Vec3){-.064f,.036f,-.064f},(Vec3){.128f,.078f,.128f},0xe63d43);
    if (view.ripple>0) {
        for (int i=0;i<12;++i) {
            float a=i*.5235987756f,b=(i+1)*.5235987756f;
            float ri=view.ripple,ro=ri+.035f;
            Vec3 q=view.wake;
            Vec3 ring[]={{q.x+cosf(a)*ri,q.y,q.z+sinf(a)*ri},{q.x+cosf(a)*ro,q.y,q.z+sinf(a)*ro},
                {q.x+cosf(b)*ro,q.y,q.z+sinf(b)*ro},{q.x+cosf(b)*ri,q.y,q.z+sinf(b)*ri}};
            flat_quad(ring,0xa7d2dd);
        }
    }
    return send_mesh(&fishing_mesh);
}

static Vec3 rail_offset(RailSample p,float side,float line,float above) {
    float inv=1.f/hypotf(p.dx,p.dz);
    return (Vec3){p.x+(p.dx*line-p.dz*side)*inv,p.y+above,p.z+(p.dz*line+p.dx*side)*inv};
}
static void draw_track(RailCell cell,RailPiece shape,int energized) {
    int n=shape.shape>=BEND_NE ? 6 : 1;
    for (int i=0;i<n;++i) {
        RailSample a=rail_sample(cell,shape,(float)i/n);
        RailSample b=rail_sample(cell,shape,(float)(i+1)/n);
        for (int j=-1;j<=1;j+=2) {
            float edge=j*.20f;
            Vec3 strip[]={
                rail_offset(a,edge-.028f,0,.018f),rail_offset(a,edge+.028f,0,.018f),
                rail_offset(b,edge+.028f,0,.018f),rail_offset(b,edge-.028f,0,.018f)};
            flat_quad(strip,shape.kind==RAIL_BOOSTER ? 0xe5ba45 : 0xb7bec1);
        }
        if (shape.kind==RAIL_BOOSTER) {
            Vec3 strip[]={
                rail_offset(a,-.040f,0,.010f),rail_offset(a,.040f,0,.010f),
                rail_offset(b,.040f,0,.010f),rail_offset(b,-.040f,0,.010f)};
            flat_quad(strip,energized ? 0xf94538 : 0x721e29);
        } else if (shape.kind==RAIL_DETECTOR || shape.kind==RAIL_ACTIVATOR) {
            RailSample t=rail_sample(cell,shape,.5f);
            Vec3 plate[]={
                rail_offset(t,-.16f,-.19f,.030f),rail_offset(t,.16f,-.19f,.030f),
                rail_offset(t,.16f,.19f,.030f),rail_offset(t,-.16f,.19f,.030f)};
            if (shape.kind==RAIL_DETECTOR) flat_quad(plate,energized ? 0xff6033 : 0x982e39);
            else for (int k=0;k<2;++k) {
                RailSample pad=rail_sample(cell,shape,k ? .70f : .30f);
                Vec3 bars[]={
                    rail_offset(pad,-.16f,-.060f,.030f),rail_offset(pad,.16f,-.060f,.030f),
                    rail_offset(pad,.16f,.060f,.030f),rail_offset(pad,-.16f,.060f,.030f)};
                flat_quad(bars,energized ? 0xff9d38 : 0xb3452c);
            }
        }
    }
    for (int i=0;i<6;++i) {
        RailSample t=rail_sample(cell,shape,(i+.5f)/6.f);
        Vec3 ties[]={
            rail_offset(t,-.31f,-.043f,0),rail_offset(t,.31f,-.043f,0),
            rail_offset(t,.31f,.043f,0),rail_offset(t,-.31f,.043f,0)};
        flat_quad(ties,0x77533a);
    }
}

static void ribbon(Vec3 a, Vec3 b, float width, unsigned tint) {
    float dx=b.x-a.x, dz=b.z-a.z;
    float horizontal=hypotf(dx,dz);
    Vec3 side=horizontal>.01f ? (Vec3){-dz*width/horizontal,0,dx*width/horizontal} : (Vec3){width,0,0};
    Vec3 horizontal_panel[]={{a.x-side.x,a.y,a.z-side.z},{a.x+side.x,a.y,a.z+side.z},
                             {b.x+side.x,b.y,b.z+side.z},{b.x-side.x,b.y,b.z-side.z}};
    Vec3 vertical_panel[]={{a.x,a.y-width,a.z},{a.x,a.y+width,a.z},{b.x,b.y+width,b.z},{b.x,b.y-width,b.z}};
    flat_quad(horizontal_panel,tint); flat_quad(vertical_panel,tint);
}

static void render_combat_models(void) {
    const FlyingArrow *shots = combat_arrows();
    for (int i=0;i<ARROW_CAPACITY;++i) if (shots[i].life>0) {
        Vec3 p=shots[i].position, v=unit(shots[i].velocity);
        ribbon(p,(Vec3){p.x-v.x*.47f,p.y-v.y*.47f,p.z-v.z*.47f},.045f,0xe3d4be);
    }
    size_t count=0;
    const ButtonArrow *lodged=button_arrows_in(world_dimension(),&count);
    for (size_t i=0;i<count;++i) {
        Vec3 p=lodged[i].point,v=lodged[i].direction;
        ribbon(p,(Vec3){p.x-v.x*.42f,p.y-v.y*.42f,p.z-v.z*.42f},.039f,0xddbd89);
    }
}

static void actor_armor(Pose p,const ActorRecord *a) {
    int id=armor_kit_item(a->traits,ARMOR_HEAD);
    if (id) {
        unsigned c=armor_item_def(id)->color;
        part(p,(Vec3){-.255f,1.70f,-.247f},(Vec3){.51f,.211f,.494f},c);
        part(p,(Vec3){-.261f,1.48f,-.235f},(Vec3){.068f,.224f,.47f},c);
        part(p,(Vec3){.193f,1.48f,-.235f},(Vec3){.068f,.224f,.47f},c);
    }
    id=armor_kit_item(a->traits,ARMOR_BODY);
    if (id) {
        unsigned c=armor_item_def(id)->color;
        part(p,(Vec3){-.25f,.86f,-.177f},(Vec3){.50f,.54f,.354f},c);
        for (int side=-1;side<=1;side+=2)
            part(p,(Vec3){side*.238f-.07f,1.21f,-.16f},(Vec3){.14f,.17f,.31f},c);
    }
    for (int slot=ARMOR_LEGS;slot<=ARMOR_FEET;++slot) {
        id=armor_kit_item(a->traits,slot);
        if (!id) continue;
        unsigned c=armor_item_def(id)->color;
        int boots=slot==ARMOR_FEET;
        for (int side=-1;side<=1;side+=2)
            part(p,(Vec3){side*.123f-.111f,boots ? .004f : .31f,boots ? -.16f : -.127f},
                (Vec3){.222f,boots ? .26f : .51f,boots ? .29f : .257f},c);
    }
}
static void wolf_model(Pose p,ActorRecord a,int angry) {
    int seated=(a.traits&WOLF_SITTING)!=0;
    float y=seated ? .08f : .20f;
    part(p,(Vec3){-.203f,y,-.24f},(Vec3){.406f,.30f,.62f},0xb5b5ad);
    part(p,(Vec3){-.224f,y+.092f,-.285f},(Vec3){.448f,.34f,.27f},0xcecdc3);
    for (int side=-1;side<=1;side+=2) {
        float x=side*.121f-.052f;
        part(p,(Vec3){x,.01f,-.218f},(Vec3){.104f,y+.13f,.11f},0xd2d2c8);
        part(p,(Vec3){x-(seated?.018f:0.f),.008f,.235f},
            (Vec3){seated?.14f:.104f,seated?.19f:.29f,seated?.19f:.115f},0xa8a9a1);
        part(p,(Vec3){side*.132f-.05f,y+.50f,-.386f},(Vec3){.10f,.14f,.12f},0xb4b3ad);
    }
    part(p,(Vec3){-.187f,y+.31f,-.48f},(Vec3){.374f,.257f,.213f},0xd6d5cc);
    part(p,(Vec3){-.121f,y+.319f,-.564f},(Vec3){.242f,.108f,.13f},0xe2e1da);
    part(p,(Vec3){-.048f,y+.374f,-.581f},(Vec3){.096f,.046f,.027f},0x333439);
    for (int side=-1;side<=1;side+=2)
        part(p,(Vec3){side*.10f-.025f,y+.457f,-.488f},(Vec3){.05f,.037f,.013f},angry ? 0xc82d39 : 0x27272d);
    if (a.traits&WOLF_TAMED)
        part(p,(Vec3){-.230f,y+.227f,-.291f},(Vec3){.46f,.071f,.112f},0xc43e41);
    float height=seated ? .12f : (a.traits&WOLF_TAMED) ? .18f+.27f*a.health/WOLF_TAMED_HEALTH : .21f;
    part(p,(Vec3){-.046f,height,.362f},(Vec3){.092f,.09f,seated?.23f:.30f},0x898d87);
}
static void cat_model(Pose p,ActorRecord a,int resting) {
    int coat=a.traits&CAT_COAT_MASK;
    int sitting=resting || (a.traits&PET_SITTING);
    float y=sitting ? .085f : .204f;
    unsigned fur=coat==CAT_TUXEDO ? 0x303037 : coat==CAT_TABBY ? 0xd9772c :
        coat==CAT_SIAMESE ? 0xd8c29f : 0xc49743;
    unsigned points=coat==CAT_SIAMESE ? 0x55413e : coat==CAT_TUXEDO ? 0xe4e2dc : fur;
    unsigned light=coat==CAT_SIAMESE ? 0x715b53 : coat==CAT_TUXEDO ? 0xece7df : 0xe6c28b;
    part(p,(Vec3){-.149f,y,-.23f},(Vec3){.298f,.255f,.56f},fur);
    part(p,(Vec3){-.113f,y+.043f,-.252f},(Vec3){.226f,.169f,.065f},light);
    part(p,(Vec3){-.14f,y+.24f,-.378f},(Vec3){.28f,.155f,.19f},coat==CAT_SIAMESE ? points : fur);
    part(p,(Vec3){-.084f,y+.239f,-.433f},(Vec3){.168f,.074f,.067f},light);
    part(p,(Vec3){-.027f,y+.288f,-.444f},(Vec3){.054f,.026f,.021f},0xa96a68);
    for (int side=-1;side<=1;side+=2) {
        float x=side*.09f-.037f;
        part(p,(Vec3){x,.008f,-.196f},(Vec3){.074f,y+.074f,.086f},points);
        part(p,(Vec3){x,.008f,.236f},(Vec3){.074f,y+.11f,sitting?.159f:.087f},points);
        part(p,(Vec3){side*.095f-.036f,y+.37f,-.323f},(Vec3){.072f,.099f,.076f},coat==CAT_TUXEDO ? fur : points);
        part(p,(Vec3){side*.064f-.021f,y+.324f,-.385f},(Vec3){.042f,.028f,.016f},
            coat==CAT_SIAMESE ? 0x72b7db : a.traits&PET_TAMED ? 0x8eaa74 : 0x90a65e);
    }
    part(p,(Vec3){-.042f,y+.137f,.296f},(Vec3){.084f,.075f,.30f},coat==CAT_SIAMESE ? points : fur);
    part(p,(Vec3){-.038f,y+.177f,.56f},(Vec3){.076f,sitting?.062f:.245f,.082f},coat==CAT_SIAMESE ? points : fur);
    if (a.traits&PET_TAMED)
        part(p,(Vec3){-.139f,y+.17f,-.251f},(Vec3){.278f,.042f,.077f},0xbb3436);
    if (!coat || coat==CAT_TABBY) for (int side=-1;side<=1;side+=2)
        for (int i=0;i<4;++i) {
            float stripe=coat==CAT_TABBY ? .038f : .050f;
            part(p,(Vec3){side*.148f-.006f,y+.066f+(i%2)*.084f,-.165f+i*.129f},
                (Vec3){.012f,coat==CAT_TABBY?.11f:.048f,stripe},coat==CAT_TABBY ? 0xa55327 : 0x723e27);
        }
}
static void actor_model(const ActorRecord *a,float swing,int state) {
    Pose p = {a->feet,cosf(a->heading),sinf(a->heading)};
    if (a->type==ACTOR_SEEP_BEETLE) {
        /* Broad two-valve shell, a lime ridge, and three pairs of feet. */
        part(p,(Vec3){-.205f,.064f,-.245f},(Vec3){.41f,.17f,.49f},0x213d49);
        part(p,(Vec3){-.105f,.107f,-.325f},(Vec3){.21f,.17f,.128f},0x38686f);
        for (int side=-1;side<=1;side+=2) {
            float shell=side<0 ? -.262f : .016f;
            part(p,(Vec3){shell,.19f,-.17f},(Vec3){.246f,.124f,.45f},0x297b83);
            part(p,(Vec3){shell+.032f,.31f,-.135f},(Vec3){.183f,.066f,.363f},0x48a396);
            part(p,(Vec3){shell+.07f,.376f,-.085f},(Vec3){.101f,.026f,.24f},0xc9dc84);
            part(p,(Vec3){side*.077f-.026f,.191f,-.339f},(Vec3){.052f,.042f,.022f},0xe1edb1);
            for (int i=0;i<3;++i) {
                float z=-.16f+.14f*i+.035f*sinf(swing*6.2831853f+i*2.1f+side);
                part(p,(Vec3){side<0 ? -.307f : .17f,.036f,z},(Vec3){.137f,.051f,.056f},0x3f666d);
                part(p,(Vec3){side*.28f-.026f,.006f,z+.003f},(Vec3){.052f,.045f,.049f},0x354c53);
            }
        }
    } else if (a->type==ACTOR_IRON_GOLEM) {
        for (int side=-1;side<=1;side+=2) {
            float track=side*.308f-.115f;
            part(p,(Vec3){track,.025f,-.39f},(Vec3){.23f,.26f,.78f},0x333c40);
            part(p,(Vec3){track+.028f,.16f,-.31f},(Vec3){.176f,.19f,.62f},0x626868);
            for (int t=0;t<4;++t)
                part(p,(Vec3){track-.009f,.09f,-.32f+t*.18f},(Vec3){.249f,.06f,.078f},0x4a4f4e);
            part(p,(Vec3){side*.31f-.068f,.54f,-.387f},(Vec3){.136f,.13f,.038f},0xadd982);
        }
        part(p,(Vec3){-.34f,.285f,-.34f},(Vec3){.68f,.52f,.70f},0x9b6945);
        part(p,(Vec3){-.28f,.805f,-.17f},(Vec3){.56f,.15f,.46f},0xc9a172);
        part(p,(Vec3){-.09f,.955f,.03f},(Vec3){.18f,.16f,.18f},0x454f50);
        part(p,(Vec3){-.275f,.285f,-.445f-.14f*swing},(Vec3){.55f,.14f,.16f+.14f*swing},0xbac0b6);
    } else if (a->type==ACTOR_SNOW_GOLEM) {
        part(p,(Vec3){-.365f,.02f,-.365f},(Vec3){.73f,.13f,.73f},0x314548);
        part(p,(Vec3){-.28f,.15f,-.278f},(Vec3){.56f,.29f,.556f},0x426367);
        part(p,(Vec3){-.183f,.44f,-.17f},(Vec3){.366f,.20f,.34f},swing>.02f ? 0x96f3dd : 0x57a398);
        for (int i=0;i<4;++i) {
            float side=(i&1) ? 1.f : -1.f, front=(i&2) ? 1.f : -1.f;
            float reach=.26f+.045f*swing;
            part(p,(Vec3){side*reach-.065f,.47f,front*reach-.065f},(Vec3){.13f,.26f,.13f},0x95b9a9);
        }
        float width=.46f+.24f*swing;
        part(p,(Vec3){-.5f*width,.635f,-.045f},(Vec3){width,.090f,.09f},swing>.02f ? 0xc4ffdc : 0xb3c6b6);
        part(p,(Vec3){-.044f,.653f,-.5f*width},(Vec3){.088f,.078f,width},0x79aba0);
    } else if (a->type==ACTOR_SQUID) {
        part(p,(Vec3){-.26f,.34f,-.26f},(Vec3){.52f,.45f,.52f},0x3e4867);
        part(p,(Vec3){-.215f,.74f,-.213f},(Vec3){.43f,.11f,.43f},0x526488);
        part(p,(Vec3){-.22f,.27f,-.22f},(Vec3){.44f,.12f,.44f},0x272e46);
        for (int side=-1;side<=1;side+=2) {
            part(p,(Vec3){side*.131f-.054f,.49f,-.266f},(Vec3){.108f,.087f,.018f},0xd0dbd9);
            part(p,(Vec3){side*.131f-.022f,.508f,-.274f},(Vec3){.044f,.051f,.020f},0x191925);
        }
        for (int i=0;i<8;++i) {
            float phi=i*.78539816f+a->heading;
            float x=sinf(phi),z=cosf(phi);
            float reach=.25f+.13f*swing;
            Vec3 root={a->feet.x+x*.12f,a->feet.y+.33f,a->feet.z+z*.12f};
            Vec3 bent={a->feet.x+x*.214f,a->feet.y+.15f+swing*.04f,a->feet.z+z*.214f};
            Vec3 end={a->feet.x+x*reach,a->feet.y+.014f+swing*.091f,a->feet.z+z*reach};
            ribbon(root,bent,.058f,0x4b5a76);
            ribbon(bent,end,.041f,i&1 ? 0x455475 : 0x354463);
        }
    } else if (a->type==ACTOR_WOLF) {
        wolf_model(p,*a,state);
    } else if (a->type==ACTOR_OCELOT) {
        cat_model(p,*a,state);
    } else if (a->type==ACTOR_VILLAGER) {
        static const unsigned coats[VILLAGE_HOMES]={0x876044,0xd6d3c4,0x815395,0x494344,0xb8aa92};
        unsigned role=a->traits&~ROAMING_VILLAGER;
        unsigned coat=role<VILLAGE_HOMES ? coats[role] : coats[0];
        for (int side=0;side<2;++side)
            part(p,(Vec3){side?.014f:-.214f,0.f,-.116f},(Vec3){.20f,.62f,.232f},0x493d35);
        part(p,(Vec3){-.25f,.45f,-.16f},(Vec3){.5f,.92f,.32f},coat);
        part(p,(Vec3){-.199f,1.27f,-.176f},(Vec3){.398f,.61f,.352f},0xb58466);
        part(p,(Vec3){-.058f,1.39f,-.274f},(Vec3){.116f,.205f,.109f},0x996851);
        part(p,(Vec3){-.216f,.96f,-.255f},(Vec3){.432f,.17f,.13f},coat);
        part(p,(Vec3){-.085f,.967f,-.269f},(Vec3){.17f,.15f,.040f},0xb58466);
        part(p,(Vec3){-.171f,1.66f,-.184f},(Vec3){.342f,.057f,.02f},0x523d31);
        for (int side=-1;side<=1;side+=2)
            part(p,(Vec3){side*.110f-.048f,1.584f,-.184f},(Vec3){.096f,.052f,.021f},0x4e8b4d);
    } else if (a->type==ACTOR_SILTBACK) {
        /* Flat paddle muzzle and tall split mudguard. No cubic core, face
           panel, size tiers or bouncing animation. */
        const float stroke=swing;
        part(p,(Vec3){-.285f,.15f,-.33f},(Vec3){.57f,.28f,.69f},0x636857);
        part(p,(Vec3){-.239f,.35f,-.27f},(Vec3){.478f,.21f,.60f},0x877b5d);
        part(p,(Vec3){-.205f,.17f-.103f*stroke,-.49f},(Vec3){.41f,.14f,.26f},0x504c3b);
        part(p,(Vec3){-.258f,.101f-.069f*stroke,-.54f},(Vec3){.516f,.09f,.153f},0xab9272);
        for (int side=-1;side<=1;side+=2) {
            const float x=side*.19f-.05f;
            for (int leg=0;leg<2;++leg)
                part(p,(Vec3){side*.265f-.073f,.016f,-.25f+leg*.45f},(Vec3){.145f,.235f,.174f},0x6c624e);
            part(p,(Vec3){x,.304f,-.373f},(Vec3){.10f,.070f,.065f},0xc7a564);
            part(p,(Vec3){x+.036f,.323f,-.38f},(Vec3){.028f,.038f,.025f},0x242924);
            for (int vane=0;vane<3;++vane)
                part(p,(Vec3){x,.515f,-.17f+vane*.172f},(Vec3){.11f,.124f,.085f},
                    vane==1 ? 0xa6a17b : 0x919277);
        }
        part(p,(Vec3){-.129f,.17f,.305f},(Vec3){.258f,.10f,.252f},0x534b44);
    } else if (a->type==ACTOR_SHELLBACK) {
        /* Low, six-legged body under stepped mineral armor. Amber bands
           are the harvestable shell, distinct from the slate base. */
        part(p,(Vec3){-.36f,.19f,-.39f},(Vec3){.72f,.27f,.90f},0x464852);
        for (int band=0;band<4;++band) {
            float z=-.36f+band*.204f,w=.91f-fabsf((float)band-1.45f)*.12f;
            unsigned crust=band%2 ? 0xe4af65 : 0xf5ce84;
            part(p,(Vec3){-w*.5f,.31f,z},(Vec3){w,.26f,.193f},0x637482);
            part(p,(Vec3){-w*.34f,.57f,z+.019f},(Vec3){w*.68f,.155f,.152f},
                a->traits==SHELLBACK_RIPE ? crust : band%2 ? 0x8793a0 : 0x6d7883);
        }
        for (int side=-1;side<=1;side+=2) {
            for (int leg=0;leg<3;++leg) {
                float z=-.29f+leg*.29f,x=side*.354f-.047f;
                part(p,(Vec3){x,.085f,z},(Vec3){.147f,.22f,.105f},0x6a6b75);
                part(p,(Vec3){side*.455f-.052f,.006f,z-.045f},(Vec3){.133f,.115f,.14f},0xa79478);
            }
            part(p,(Vec3){side*.178f-.053f,.331f,-.483f},(Vec3){.105f,.073f,.02f},0xe6b976);
        }
        part(p,(Vec3){-.24f,.14f,-.53f},(Vec3){.48f,.18f,.169f},0x535b65);
    } else if (a->type == ACTOR_PIG) {
        part(p,(Vec3){-.32f,.22f,-.24f},(Vec3){.64f,.48f,.68f},0xef98ab);
        part(p,(Vec3){-.24f,.40f,-.49f},(Vec3){.48f,.46f,.34f},0xffa6b7);
        part(p,(Vec3){-.14f,.48f,-.55f},(Vec3){.28f,.155f,.10f},0xd47890);
        for (int side = 0; side < 2; ++side) {
            float x = side ? .14f : -.205f;
            part(p,(Vec3){x,.67f,-.497f},(Vec3){.065f,.07f,.014f},0x312c35);
            part(p,(Vec3){x,.82f,-.39f},(Vec3){.08f,.073f,.08f},0xd97691);
            part(p,(Vec3){x,.0f,-.23f},(Vec3){.115f,.26f,.13f},0xdb849c);
            part(p,(Vec3){x,.0f,.27f},(Vec3){.115f,.26f,.13f},0xdb849c);
        }
    } else if (a->type==ACTOR_TIDE_CRAB) {
        part(p,(Vec3){-.26f,.085f,-.17f},(Vec3){.52f,.16f,.35f},0x35545b);
        part(p,(Vec3){-.23f,.244f,-.15f},(Vec3){.46f,.065f,.31f},0x63888c);
        part(p,(Vec3){-.16f,.307f,-.13f},(Vec3){.32f,.044f,.23f},0x8aa292);
        part(p,(Vec3){-.22f,.214f,-.182f},(Vec3){.44f,.047f,.035f},0xb17752);
        for (int side=-1;side<=1;side+=2) {
            float x=side*.18f;
            part(p,(Vec3){x-.025f,.271f,-.192f},(Vec3){.05f,.097f,.048f},0x9d694e);
            part(p,(Vec3){x-.023f,.333f,-.205f},(Vec3){.046f,.032f,.045f},0x272023);
            for (int n=0;n<4;++n) {
                const float z=-.124f+n*.084f;
                const float reach=side*(.329f+.034f*swing*((n&1) ? -1.f : 1.f));
                part(p,(Vec3){side*.263f-.062f,.09f,z},(Vec3){.124f,.073f,.045f},0x92765f);
                part(p,(Vec3){reach-.041f,.018f+.026f*fabsf(swing),z+.012f},
                    (Vec3){.082f,.105f,.047f},0xb37d58);
            }
            part(p,(Vec3){side*.255f-.050f,.08f,-.265f},(Vec3){.10f,.106f,.135f},0x795e53);
            part(p,(Vec3){side*.265f-.077f,.091f,-.36f},(Vec3){.154f,.108f,.134f},0xb8815f);
            part(p,(Vec3){side*.295f-.039f,.100f,-.409f},(Vec3){.078f,.084f,.077f},0xd09b76);
            part(p,(Vec3){side*.221f-.027f,.117f,-.399f},(Vec3){.054f,.069f,.067f},0x96634e);
        }
    } else if (actors_is_bovine(a->type)) {
        part(p,(Vec3){-.36f,.47f,-.39f},(Vec3){.72f,.72f,.93f},0x65493b);
        part(p,(Vec3){-.23f,.77f,-.62f},(Vec3){.46f,.49f,.38f},0x745746);
        part(p,(Vec3){-.24f,.76f,-.71f},(Vec3){.48f,.21f,.14f},0xb59b8e);
        part(p,(Vec3){-.365f,.89f,-.13f},(Vec3){.04f,.29f,.35f},0xd5c8b6);
        part(p,(Vec3){.333f,.58f,.12f},(Vec3){.033f,.36f,.31f},0xe0d1bb);
        part(p,(Vec3){-.08f,.45f,.27f},(Vec3){.16f,.13f,.17f},0xca8f84);
        for (int side=-1;side<=1;side+=2) {
            float x=side*.22f-.069f;
            for (int end=-1;end<=1;end+=2)
                part(p,(Vec3){x,0,end*.28f-.063f},(Vec3){.138f,.51f,.126f},0x563c31);
            part(p,(Vec3){side*.21f-.07f,1.22f,-.47f},(Vec3){.14f,.115f,.13f},0xddd2bc);
            part(p,(Vec3){side*.156f-.028f,1.045f,-.63f},(Vec3){.056f,.061f,.022f},0x231f20);
            part(p,(Vec3){side*.122f-.018f,.866f,-.717f},(Vec3){.036f,.026f,.017f},0x68504a);
        }
    } else if (a->type==ACTOR_STRANDED_GUIDE) {
        /* A seated traveler with a bedroll, hood and a replenished flask. */
        part(p,(Vec3){-.25f,.04f,-.25f},(Vec3){.50f,.14f,.50f},0x6f5b49);
        part(p,(Vec3){-.21f,.17f,-.14f},(Vec3){.42f,.45f,.29f},0x405f68);
        part(p,(Vec3){-.21f,.62f,-.17f},(Vec3){.42f,.36f,.36f},0x537786);
        part(p,(Vec3){-.148f,.67f,-.193f},(Vec3){.296f,.22f,.072f},0xc29876);
        part(p,(Vec3){-.212f,.60f,-.192f},(Vec3){.424f,.076f,.096f},0xd9aa57);
        part(p,(Vec3){-.212f,.20f,.13f},(Vec3){.424f,.29f,.19f},0x8c7655);
        for (int k=-1;k<=1;k+=2) {
            float x=k*.141f-.06f;
            part(p,(Vec3){x,.14f,-.36f},(Vec3){.12f,.16f,.27f},0x55575d);
            part(p,(Vec3){k*.087f-.02f,.78f,-.197f},(Vec3){.04f,.034f,.01f},0x343943);
        }
        part(p,(Vec3){.145f,.24f,-.225f},(Vec3){.113f,.18f,.11f},state ? 0x72c9c5 : 0x957958);
    } else if (actors_is_zombie(a->type)) {
        /* Keep the persisted actor slot and collision height. Its silhouette
           is a laminated seed shell with four struts and a single lit seam. */
        unsigned shell=0x827e58, rim=0x514c3e, feet=0x615a42, crest=0xa19667;
        int id=armor_kit_item(a->traits,ARMOR_BODY);
        if (id) shell=armor_item_def(id)->color;
        id=armor_kit_item(a->traits,ARMOR_HEAD);
        if (id) crest=armor_item_def(id)->color;
        id=armor_kit_item(a->traits,ARMOR_LEGS);
        if (id) rim=armor_item_def(id)->color;
        id=armor_kit_item(a->traits,ARMOR_FEET);
        if (id) feet=armor_item_def(id)->color;
        part(p,(Vec3){-.265f,.61f,-.24f},(Vec3){.53f,.84f,.52f},rim);
        part(p,(Vec3){-.332f,.79f,-.33f},(Vec3){.664f,.56f,.59f},shell);
        part(p,(Vec3){-.266f,1.31f,-.28f},(Vec3){.532f,.32f,.53f},crest);
        part(p,(Vec3){-.195f,1.56f,-.19f},(Vec3){.390f,.19f,.41f},shell);
        part(p,(Vec3){-.102f,1.71f,-.10f},(Vec3){.204f,.15f,.27f},rim);
        part(p,(Vec3){-.252f,1.18f,-.353f},(Vec3){.504f,.112f,.040f},0x34312b);
        part(p,(Vec3){-.179f,1.218f,-.368f},(Vec3){.358f,.033f,.023f},swing>.23f ? 0xffce72 : 0xd5a16a);
        for (int side=-1;side<=1;side+=2) {
            for (int rank=0;rank<2;++rank) {
                float z=rank ? .165f : -.25f-.13f*swing;
                float x=side*(rank ? .20f : .255f)-.062f;
                part(p,(Vec3){x,.08f,z},(Vec3){.124f,rank ? .65f : .76f,.137f},rim);
                part(p,(Vec3){x-.018f,.014f,z-.042f},(Vec3){.160f,.159f,.205f},feet);
                part(p,(Vec3){x-.034f,.565f,z-.017f},(Vec3){.190f,.21f,.185f},shell);
            }
        }
    } else if (a->type==ACTOR_KILN_MOLE) {
        part(p,(Vec3){-.23f,.065f,-.22f},(Vec3){.46f,.26f,.49f},0x57535f);
        part(p,(Vec3){-.197f,.250f,-.15f},(Vec3){.394f,.19f,.39f},0x797078);
        part(p,(Vec3){-.14f,.43f,-.105f},(Vec3){.28f,.067f,.28f},0x9b8077);
        for (int stripe=0;stripe<3;++stripe) {
            float z=-.085f+stripe*.089f;
            part(p,(Vec3){-.144f,.492f,z},(Vec3){.288f,.014f,.029f},state ? 0xe7a760 : 0x655965);
            part(p,(Vec3){-.202f,.352f,z},(Vec3){.024f,.083f,.036f},state ? 0xad714a : 0x595362);
        }
        part(p,(Vec3){-.168f,.125f,-.341f},(Vec3){.336f,.198f,.215f},0x9c7c77);
        part(p,(Vec3){-.074f,.143f,-.399f},(Vec3){.148f,.087f,.090f},0xcf9890);
        part(p,(Vec3){-.046f,.194f,.241f},(Vec3){.092f,.09f,.14f},0x996c63);
        for (int side=-1;side<=1;side+=2) {
            float x=side*.105f;
            part(p,(Vec3){x-.029f,.269f,-.35f},(Vec3){.058f,.038f,.019f},0x2e2630);
            part(p,(Vec3){side*.23f-.091f,.025f,-.287f},(Vec3){.182f,.114f,.194f},0xa67c6f);
            part(p,(Vec3){side*.165f-.057f,.018f,.145f},(Vec3){.114f,.13f,.142f},0x75606a);
            for (int nail=0;nail<3;++nail)
                part(p,(Vec3){side*.23f-.065f+nail*.049f,.046f,-.34f},(Vec3){.024f,.045f,.065f},0xdcc2a8);
        }
    } else if (a->type==ACTOR_FAULTBACK) {
        /* Wide rock-boring quadruped, with a mineral wedge instead of
           a humanoid skull or weapon. Raised seams telegraph its charge. */
        int hot=state==FAULTBACK_WINDUP || state==FAULTBACK_CHARGE;
        float tuck=hot ? -.10f : state==FAULTBACK_CALM ? -.04f : 0.f;
        unsigned vein=hot ? 0xffb443 : state==FAULTBACK_CALM ? 0x65cebd : 0xb98a62;
        part(p,(Vec3){-.49f,.29f+tuck,-.48f},(Vec3){.98f,.50f,1.04f},0x413436);
        part(p,(Vec3){-.60f,.53f+tuck,-.36f},(Vec3){1.20f,.25f,.83f},0x936953);
        for (int rank=0;rank<3;++rank) {
            const float z=-.39f+rank*.277f,width=.87f+(2-rank)*.078f;
            const float height=(hot ? .99f : .85f)+.036f*(2-rank);
            part(p,(Vec3){-.5f*width,.72f+tuck,z},(Vec3){width,height-.72f,.237f},0x9d765c-rank*0x0c0502u);
            part(p,(Vec3){-.29f,height+tuck-.018f,z+.015f},(Vec3){.58f,.036f,.055f},vein);
            part(p,(Vec3){-.063f,height+tuck,z+.035f},(Vec3){.126f,hot ? .102f : .045f,.176f},vein);
        }
        for (int side=-1;side<=1;side+=2) {
            float x=side*.47f-.132f;
            for (int foot=0;foot<2;++foot) {
                float z=foot ? .324f : -.44f;
                part(p,(Vec3){x,.045f,z},(Vec3){.266f,.43f,.213f},0x665249);
                part(p,(Vec3){x-.013f,.011f,z-.065f},(Vec3){.292f,.13f,.273f},0x926d58);
            }
            part(p,(Vec3){side*.177f-.061f,.451f+tuck,-.667f},(Vec3){.122f,.072f,.019f},vein);
        }
        part(p,(Vec3){-.302f,.273f+tuck,-.711f},(Vec3){.604f,.181f,.326f},0xb08d70);
        part(p,(Vec3){-.265f,.472f+tuck,-.648f},(Vec3){.53f,.147f,.296f},0x73554a);
        part(p,(Vec3){-.25f,.231f+tuck,-.725f},(Vec3){.50f,.073f,.092f},0x40373a);
    } else if (a->type==ACTOR_SKELETON) {
        part(p,(Vec3){-.19f,1.39f,-.18f},(Vec3){.38f,.46f,.36f},0xd9d7c5);
        part(p,(Vec3){-.11f,.76f,-.12f},(Vec3){.22f,.63f,.24f},0xb0ae9b);
        for (int i=0;i<3;++i)
            part(p,(Vec3){-.21f,.91f+i*.15f,-.145f},(Vec3){.42f,.075f,.19f},0xd9d7c5);
        for (int side=0;side<2;++side) {
            part(p,(Vec3){side ? .055f : -.165f,0,-.053f},(Vec3){.11f,.77f,.106f},0xc7c5b4);
            part(p,(Vec3){side ? .22f : -.31f,1.20f,-.54f},(Vec3){.09f,.10f,.62f},0xd9d7c5);
            part(p,(Vec3){side ? .042f : -.144f,1.60f,-.191f},(Vec3){.102f,.083f,.019f},0x35373d);
        }
        ribbon(local_point(p,(Vec3){.27f,.92f,-.47f}),local_point(p,(Vec3){.27f,1.53f,-.47f}),.012f,0xd6c9ac);
        ribbon(local_point(p,(Vec3){.27f,.92f,-.47f}),local_point(p,(Vec3){.27f,1.22f,-.67f}),.028f,0x9f6136);
        ribbon(local_point(p,(Vec3){.27f,1.22f,-.67f}),local_point(p,(Vec3){.27f,1.53f,-.47f}),.028f,0x9f6136);
    } else if (a->type==ACTOR_TIDERAY) {
        /* Low amber-striped shoulder, scalloped fins, and a trailing tail.
           All fin segments share their edge as the wave propagates outward. */
        part(p,(Vec3){-.187f,.092f,-.37f},(Vec3){.374f,.19f,.69f},0x356b74);
        part(p,(Vec3){-.122f,.260f,-.27f},(Vec3){.244f,.068f,.51f},0x69a8a0);
        part(p,(Vec3){-.115f,.079f,-.435f},(Vec3){.23f,.094f,.153f},0xcaa779);
        float lift=(swing-.5f)*.19f;
        for (int side=-1;side<=1;side+=2) {
            Vec3 a0=local_point(p,(Vec3){side*.12f,.25f,-.31f});
            Vec3 a1=local_point(p,(Vec3){side*.12f,.18f,.28f});
            Vec3 b0=local_point(p,(Vec3){side*.39f,.22f+.36f*lift,-.24f});
            Vec3 b1=local_point(p,(Vec3){side*.365f,.14f+.36f*lift,.25f});
            Vec3 c0=local_point(p,(Vec3){side*.60f,.18f+lift,-.055f});
            Vec3 c1=local_point(p,(Vec3){side*.475f,.10f+.76f*lift,.227f});
            const Vec3 inner[]={a0,b0,b1,a1},outer[]={b0,c0,c1,b1};
            flat_quad(inner,0x568e91);
            flat_quad(outer,0x376e83);
            ribbon(a0,b0,.017f,0xe2bb79);
            ribbon(b0,c0,.014f,0xc7a37b);
            part(p,(Vec3){side*.096f-.028f,.247f,-.384f},(Vec3){.056f,.044f,.047f},0xe8dd9a);
        }
        Vec3 tail=local_point(p,(Vec3){0,.127f,.27f});
        Vec3 bend=local_point(p,(Vec3){lift*.25f,.121f,.47f});
        ribbon(tail,bend,.044f,0x4e8794);
        ribbon(bend,local_point(p,(Vec3){lift*.55f,.153f,.59f}),.022f,0xc2ab7e);
    } else if (a->type==ACTOR_LANTERN_MOTH) {
        const float lift=.23f*(1.f-swing);
        const unsigned sheen=state ? 0xffd891 : 0xe39f56;
        part(p,(Vec3){-.087f,.22f,-.27f},(Vec3){.174f,.149f,.57f},0x2e4e5b);
        part(p,(Vec3){-.116f,.226f,-.319f},(Vec3){.232f,.118f,.164f},0x415f68);
        for (int side=-1;side<=1;side+=2) {
            part(p,(Vec3){side*.059f-.017f,.273f,-.332f},(Vec3){.034f,.042f,.02f},0xf3ddaa);
            const Vec3 a0=local_point(p,(Vec3){side*.06f,.32f,-.18f});
            const Vec3 a1=local_point(p,(Vec3){side*.06f,.27f,.22f});
            const Vec3 shoulder=local_point(p,(Vec3){side*(.43f-lift*.2f),.30f+lift,-.25f});
            const Vec3 tip=local_point(p,(Vec3){side*(.78f-lift*.53f),.34f+lift*1.8f,-.05f});
            const Vec3 heel=local_point(p,(Vec3){side*(.48f-lift*.28f),.26f+lift*1.27f,.25f});
            const Vec3 tail=local_point(p,(Vec3){side*.22f,.20f+lift*.68f,.49f});
            const Vec3 primary[]={a0,shoulder,tip,heel},inner[]={a0,heel,tail,a1};
            flat_quad(primary,0x4a8898);
            flat_quad(inner,0x326070);
            ribbon(shoulder,tip,.023f,sheen);
            ribbon(tip,heel,.021f,sheen);
            ribbon(a0,heel,.018f,0x6fb7b8);
            ribbon(local_point(p,(Vec3){side*.086f,.332f,-.258f}),
                local_point(p,(Vec3){side*.271f,.46f,-.505f}),.018f,sheen);
        }
        if (state) for (int i=0;i<5;++i) {
            float angle=1.256637f*i+swing;
            part(p,(Vec3){.49f*sinf(angle),.13f+.056f*i,.49f*cosf(angle)},(Vec3){.034f,.034f,.034f},sheen);
        }
    } else if (a->type == ACTOR_REEDLIZARD) {
        const int hot=state==REED_WARN || state==REED_VENT,cool=state==REED_COOLED;
        const unsigned crest=cool ? 0x79bbbb : hot ? 0xf3c34f : 0xb56f50;
        part(p,(Vec3){-.252f,.16f,-.27f},(Vec3){.504f,.32f,.70f},0x513c42);
        part(p,(Vec3){-.179f,.28f,-.475f},(Vec3){.358f,.22f,.24f},0x9b6651);
        part(p,(Vec3){-.132f,.285f,-.509f},(Vec3){.264f,.079f,.08f},crest);
        part(p,(Vec3){-.13f,.20f,.385f},(Vec3){.26f,.15f,.25f},0x865347);
        ribbon(local_point(p,(Vec3){0,.247f,.56f}),local_point(p,(Vec3){.22f,.108f,.88f}),.071f,0x654349);
        for (int side=-1;side<=1;side+=2) {
            float x=side*.10f-.028f;
            part(p,(Vec3){x,.432f,-.484f},(Vec3){.055f,.041f,.013f},0xece0cb);
            for (int leg=0;leg<2;++leg) {
                float z=-.20f+leg*.44f;
                float ox=side<0 ? -.422f : .224f;
                part(p,(Vec3){ox,.062f,z},(Vec3){.198f,.202f,.14f},0x714c43);
                part(p,(Vec3){ox,.014f,z-.073f},(Vec3){.20f,.076f,.155f},0x93664d);
            }
            if (side>0 && (a->traits&REED_HARVESTED)) continue;
            for (int fin=0;fin<3;++fin) {
                float z=-.20f+fin*.192f;
                float tip=(hot ? .56f : .37f)-fin*.027f;
                float raised=(hot ? .87f : cool ? .43f : .56f)-fin*.043f;
                Vec3 fins[4]={local_point(p,(Vec3){side*.18f,.429f,z-.045f}),
                    local_point(p,(Vec3){side*tip,raised,z-.113f}),
                    local_point(p,(Vec3){side*(tip-.052f),raised-.035f,z+.067f}),
                    local_point(p,(Vec3){side*.182f,.432f,z+.101f})};
                flat_quad(fins,crest);
                ribbon(fins[0],fins[1],.015f,0xe6a979);
            }
        }
        if (hot) {
            /* Sparse fins of vapor near the mouth make the locked
               direction readable without filling the entire view. */
            for (int side=-1;side<=1;side+=2)
                ribbon(local_point(p,(Vec3){side*.06f,.36f,-.49f}),
                    local_point(p,(Vec3){side*(state==REED_VENT ? .63f : .40f),.54f,
                        state==REED_VENT ? -2.17f : -1.42f}),state==REED_VENT ? .048f : .014f,
                    state==REED_VENT ? 0xefc6a1 : 0xc88e51);
        }
    } else if (a->type == ACTOR_CHICKEN) {
        part(p,(Vec3){-.21f,.16f,-.14f},(Vec3){.42f,.36f,.48f},0xefece8);
        part(p,(Vec3){-.12f,.37f,-.24f},(Vec3){.24f,.32f,.24f},0xf4f3ee);
        part(p,(Vec3){-.09f,.44f,-.33f},(Vec3){.18f,.12f,.12f},0xf4aa34);
        for (int sign=-1;sign<=1;sign+=2)
            part(p,(Vec3){sign*.10f-.029f,0,.04f},(Vec3){.058f,.18f,.07f},0xd79b3d);
    } else if (a->type==ACTOR_SHEEP) {
        int shorn=(a->traits&SHEEP_SHORN)!=0;
        unsigned coat=shorn ? 0xb6937c : 0xf1e9d9;
        part(p,(Vec3){shorn ? -.26f : -.36f,.49f,-.38f},
            (Vec3){shorn ? .52f : .72f,shorn ? .45f : .60f,.83f},coat);
        part(p,(Vec3){-.21f,.73f,-.59f},(Vec3){.42f,.43f,.36f},coat);
        part(p,(Vec3){-.17f,.72f,-.66f},(Vec3){.34f,.27f,.15f},0xc2a28b);
        for (int side=-1;side<=1;side+=2) {
            float x=side*.225f-.072f;
            for (int end=-1;end<=1;end+=2)
                part(p,(Vec3){x,0,end*.24f-.075f},(Vec3){.145f,.54f,.15f},0xa18471);
            part(p,(Vec3){side*.145f-.024f,.93f,-.669f},(Vec3){.050f,.05f,.014f},0x353130);
        }
    } else if (a->type==ACTOR_SPIDER || a->type==ACTOR_CAVE_SPIDER) {
        int cave=a->type==ACTOR_CAVE_SPIDER;
        float k=cave ? .51f : 1.f;
        unsigned leg_color=cave ? 0x234a54 : 0x27232c;
        part(p,(Vec3){-.39f*k,.26f*k,-.12f*k},(Vec3){.78f*k,.48f*k,.69f*k},cave ? 0x37727d : 0x332b33);
        part(p,(Vec3){-.26f*k,.23f*k,-.40f*k},(Vec3){.52f*k,.37f*k,.34f*k},cave ? 0x28515c : 0x29232d);
        for (int sign=-1;sign<=1;sign+=2) {
            part(p,(Vec3){(sign*.14f-.05f)*k,.43f*k,-.411f*k},(Vec3){.10f*k,.074f*k,.026f*k},0xff3232);
            for (int i=0;i<4;++i) {
                float leg=(-.30f+i*.19f)*k;
                part(p,(Vec3){(sign>0?.28f:-.70f)*k,.28f*k,leg},(Vec3){.42f*k,.09f*k,.08f*k},leg_color);
                part(p,(Vec3){(sign>0?.62f:-.70f)*k,.07f*k,leg},(Vec3){.08f*k,.24f*k,.08f*k},leg_color);
            }
        }
    }
    if (a->traits && a->type==ACTOR_SKELETON) actor_armor(p,a);
}

static void draw_heart(Vec3 origin) {
    /* Crossed silhouettes remain visible even when the mesh is reused after a camera turn. */
    for (int axis=0;axis<2;++axis) {
        Vec3 tip=origin, left=origin, right=origin,top=origin;
        if (axis) { left.z-=.13f; right.z+=.13f; }
        else { left.x-=.13f; right.x+=.13f; }
        left.y+=.12f; right.y+=.12f; top.y+=.16f;
        flat_quad((Vec3[]){tip,left,top,right},0xe74e6e);
        for (int side=-1;side<=1;side+=2) {
            Vec3 mid=top;
            if (axis) mid.z=origin.z+side*.065f;
            else mid.x=origin.x+side*.065f;
            Vec3 a=mid,b=mid,c=mid,d=mid;
            a.y=b.y=origin.y+.10f;
            c.y=d.y=origin.y+.195f;
            if (axis) { a.z-=.055f; b.z+=.055f; c.z+=.055f; d.z-=.055f; }
            else { a.x-=.055f; b.x+=.055f; c.x+=.055f; d.x-=.055f; }
            flat_quad((Vec3[]){a,b,c,d},0xe74e6e);
        }
    }
}

static Vec3 crack_vertex(BlockBox box,int flat_z,float face,float u,float v) {
    return flat_z ? (Vec3){box.x0+u,box.y0+v,face} : (Vec3){face,box.y0+v,box.z0+u};
}
static void draw_door_cracks(ActorDoorAttack attack) {
    int y=attack.y;
    int raw=door_closed_wood(attack.x,&y,attack.z);
    BlockBox boxes[BARRIER_PARTS];
    if (!raw || y!=attack.y || !barrier_world_boxes(attack.x,y,attack.z,raw,BARRIER_BODY,boxes)) return;
    BlockBox b=boxes[0];
    int flat_z=b.z1-b.z0<b.x1-b.x0;
    /* Ordered branches on the opaque lower panel; no edits to the static chunk mesh. */
    static const float cracks[][4]={
        {.52f,.63f,.44f,.48f},{.44f,.48f,.51f,.31f},
        {.52f,.63f,.63f,.73f},{.44f,.48f,.31f,.45f},{.31f,.45f,.21f,.55f},
        {.63f,.73f,.59f,.88f},{.51f,.31f,.42f,.14f},{.51f,.31f,.69f,.23f},
        {.52f,.63f,.37f,.74f},{.63f,.73f,.82f,.66f}
    };
    unsigned n=attack.stage<sizeof cracks/sizeof *cracks ? attack.stage : sizeof cracks/sizeof *cracks;
    for (int side=0;side<2;++side) {
        float face=flat_z ? (side ? b.z1+.005f : b.z0-.005f) : (side ? b.x1+.005f : b.x0-.005f);
        for (unsigned i=0;i<n;++i) {
            const float *s=cracks[i];
            float du=s[2]-s[0],dv=s[3]-s[1];
            float scale=.013f/hypotf(du,dv),u=-dv*scale,v=du*scale;
            flat_quad((Vec3[]){
                crack_vertex(b,flat_z,face,s[0]+u,s[1]+v), crack_vertex(b,flat_z,face,s[0]-u,s[1]-v),
                crack_vertex(b,flat_z,face,s[2]-u,s[3]-v), crack_vertex(b,flat_z,face,s[2]+u,s[3]+v)
            },0x261711);
        }
    }
}

/* Store sky exposure and nearby block light on the living model, leaving
   emissive effects alone. Reuse the mesh light cache without queueing work. */
static void shade_mob(Uint32 start,Vec3 point,Dimension dim) {
    if (dim!=OVERWORLD) return;
    int x=(int)floorf(point.x),y=(int)floorf(point.y+.55f),z=(int)floorf(point.z);
    int cx=x/CHUNK_SIZE-(x%CHUNK_SIZE<0),cz=z/CHUNK_SIZE-(z%CHUNK_SIZE<0);
    LightView view={block_light_previous(cx,cz),1};
    int level=block_light_level(view,x-cx*CHUNK_SIZE,y,z-cz*CHUNK_SIZE);
    Uint8 light=(Uint8)(level+(light_has_open_sky_resident(x,y,z) ? 16 : 0));
    for (Uint32 i=start;i<vertexCount;++i) vertices[i].c[3]=light;
}

static int refresh_actors(const EntityView *view) {
    static Uint64 relight_at;
    static uint64_t uploaded_visible;
    static uint32_t uploaded_visible_boats,uploaded_visible_carts;
    Uint64 now=SDL_GetTicks();
    Dimension dim=world_dimension();
    const ActorRecord *actors=actors_in(dim);
    const BoatRecord *boat=boats_in(dim);
    const CartRecord *cart=carts_in(dim);
    uint64_t visible=0;
    uint32_t visible_boats=0,visible_carts=0;
    for (int i=0;i<ACTOR_CAPACITY;++i)
        if (actors[i].type && visible_entity(view,actors[i].feet,ACTOR_MESH_RADIUS)) visible|=UINT64_C(1)<<i;
    for (int i=0;i<BOAT_CAPACITY;++i)
        if (boat[i].occupied && visible_entity(view,boat[i].position,VEHICLE_MESH_RADIUS)) visible_boats|=UINT32_C(1)<<i;
    for (int i=0;i<CART_CAPACITY;++i)
        if (cart[i].occupied && visible_entity(view,cart[i].position,VEHICLE_MESH_RADIUS)) visible_carts|=UINT32_C(1)<<i;
    unsigned version = actors_revision();
    unsigned combat_version = combat_revision();
    unsigned hulls = boats_revision(), wagons=carts_revision();
    if (version == uploaded_actors && combat_version == uploaded_combat &&
        hulls == uploaded_boats && wagons==uploaded_carts && actor_mesh.valid &&
        uploaded_visible==visible && uploaded_visible_boats==visible_boats && uploaded_visible_carts==visible_carts &&
        (dim!=OVERWORLD || !actor_mesh.count || now<relight_at)) return 1;
    const ActorCare *care=actors_care_in(dim);
    ActorDoorAttack attacks[ACTOR_CAPACITY];
    unsigned attack_count=0;
    vertexCount = meshOOM = 0;
    for (int i = 0; i < ACTOR_CAPACITY; ++i) if (actors[i].type) {
        ActorDoorAttack attack={0,0,0,0,0.f};
        if (actors_is_zombie(actors[i].type)) {
            attack=actors_door_attack(dim,i);
            /* A door impact may be visible while the attacker is not. */
            if (attack.stage) {
                unsigned j=0;
                while (j<attack_count && (attacks[j].x!=attack.x || attacks[j].y!=attack.y || attacks[j].z!=attack.z)) ++j;
                if (j==attack_count) attacks[attack_count++]=attack;
                else if (attacks[j].stage<attack.stage) attacks[j]=attack;
            }
        }
        if (!(visible&(UINT64_C(1)<<i))) continue;
        Uint32 start=vertexCount;
        Vec3 feet=actors[i].feet;
        Dimension d=dim;
        int state=actors[i].type==ACTOR_OCELOT ? actors_cat_sitting(d,i) :
            actors[i].type==ACTOR_FAULTBACK ? actors_faultback_phase(d,i) :
            actors[i].type==ACTOR_REEDLIZARD ? actors_reed_phase(d,i) :
            actors[i].type==ACTOR_KILN_MOLE ? actors_mole_warm(d,i) : actors_wolf_angry(d,i);
        float animation=actors[i].type==ACTOR_SQUID || actors[i].type==ACTOR_TIDERAY ?
            actors_squid_pulse(d,i) : actors_golem_swing(d,i);
        if (actors[i].type==ACTOR_TIDE_CRAB) animation=actors_tide_crab_stride(d,i);
        if (actors[i].type==ACTOR_SILTBACK) animation=actors_siltback_raking(d,i);
        if (actors[i].type==ACTOR_LANTERN_MOTH) {
            animation=actors_moth_wing(d,i);
            state=actors_moth_pollinating(d,i);
        }
        if (actors[i].type==ACTOR_STRANDED_GUIDE) state=actors_stranded_state(d,i).rest_ticks!=0;
        if (actors_is_zombie(actors[i].type)) animation=attack.swing;
        actor_model(&actors[i],animation,state);
        if (care[i].grow)
          for (Uint32 j=start;j<vertexCount;++j) {
            vertices[j].p[0]=(vertices[j].p[0]+feet.x)*.5f;
            vertices[j].p[1]=(vertices[j].p[1]+feet.y)*.5f;
            vertices[j].p[2]=(vertices[j].p[2]+feet.z)*.5f;
        }
        shade_mob(start,feet,dim);
        if (care[i].love) {
            float height=actors_is_bovine(actors[i].type) || actors[i].type==ACTOR_SHEEP ? 1.53f : 1.04f;
            draw_heart((Vec3){feet.x,feet.y+height,feet.z});
        }
        if (actors_on_fire(world_dimension(),i)) {
            draw_flame(feet.x,feet.y+.05f,feet.z,.84f);
            if (actors[i].type!=ACTOR_CHICKEN) draw_flame(feet.x,feet.y+.50f,feet.z,.64f);
        }
    }
    for (unsigned i=0;i<attack_count;++i) draw_door_cracks(attacks[i]);
    const ActorFireball *fire = actors_fireballs();
    for (int i = 0; i < ACTOR_FIREBALLS; ++i) if (fire[i].seconds > 0) {
        if (fire[i].kind==HOSTILE_ARROW) {
            Vec3 p=fire[i].point,v=fire[i].velocity;
            float t=.30f/fmaxf(1.f,sqrtf(v.x*v.x+v.y*v.y+v.z*v.z));
            ribbon(p,(Vec3){p.x-v.x*t,p.y-v.y*t,p.z-v.z*t},.021f,0xe5ceb0);
            continue;
        }
        Pose p = {fire[i].point,1,0};
        if (fire[i].kind==THROWN_EGG) part(p,(Vec3){-.064f,-.083f,-.064f},(Vec3){.128f,.166f,.128f},0xeee0c1);
        else if (fire[i].kind==THROWN_SNOWBALL)
            part(p,(Vec3){-.079f,-.079f,-.079f},(Vec3){.158f,.158f,.158f},0xe9f7ff);
        else if (actors_splash_item(fire[i].kind)!=AIR) {
            const ItemDef *potion=item_def(actors_splash_item(fire[i].kind));
            part(p,(Vec3){-.088f,-.10f,-.088f},(Vec3){.176f,.18f,.176f},potion ? potion->color : 0x91afd1);
            part(p,(Vec3){-.036f,.08f,-.036f},(Vec3){.072f,.062f,.072f},0xd5b18e);
        }
        else part(p,(Vec3){-.12f,-.12f,-.12f},(Vec3){.24f,.24f,.24f},0xff892b);
    }
    render_combat_models();
    for (int i=0;i<BOAT_CAPACITY;++i) if (visible_boats&(UINT32_C(1)<<i)) {
        Pose p={boat[i].position,cosf(boat[i].heading),sinf(boat[i].heading)};
        part(p,(Vec3){-.47f,0,-.78f},(Vec3){.94f,.14f,1.56f},0xb28050);
        part(p,(Vec3){-.49f,.11f,-.60f},(Vec3){.16f,.31f,1.18f},0x9f6337);
        part(p,(Vec3){.33f,.11f,-.60f},(Vec3){.16f,.31f,1.18f},0x9f6337);
        part(p,(Vec3){-.39f,.12f,-.79f},(Vec3){.78f,.27f,.19f},0xbd874f);
        part(p,(Vec3){-.39f,.12f,.57f},(Vec3){.78f,.25f,.19f},0xbd874f);
        part(p,(Vec3){-.36f,.16f,.15f},(Vec3){.72f,.12f,.22f},0xca9864);
    }
    for (int i=0;i<CART_CAPACITY;++i) if (visible_carts&(UINT32_C(1)<<i)) {
        Pose p={cart[i].position,cosf(cart[i].heading),sinf(cart[i].heading)};
        /* Inset the floor from the end plates so the overlapping faces
           don't flicker at a distance. */
        part(p,(Vec3){-.45f,.086f,-.475f},(Vec3){.9f,.15f,.95f},0x859198);
        part(p,(Vec3){-.49f,.18f,-.47f},(Vec3){.14f,.32f,.94f},0x9eaab0);
        part(p,(Vec3){.35f,.18f,-.47f},(Vec3){.14f,.32f,.94f},0x9eaab0);
        part(p,(Vec3){-.38f,.18f,-.49f},(Vec3){.76f,.33f,.14f},0xaab6ba);
        part(p,(Vec3){-.38f,.18f,.35f},(Vec3){.76f,.33f,.14f},0xaab6ba);
        for (int s=-1;s<=1;s+=2) {
            float x=s*.23f-.06f;
            part(p,(Vec3){x,0,-.32f},(Vec3){.12f,.14f,.19f},0x454d55);
            part(p,(Vec3){x,0,.15f},(Vec3){.12f,.14f,.19f},0x454d55);
        }
        if (cart[i].kind==CART_TNT) {
            int fuse=cart[i].fuse>CART_TNT_FUSE ? 1 : cart[i].fuse;
            int bright=fuse && (fuse/5)%2;
            part(p,(Vec3){-.29f,.235f,-.29f},(Vec3){.58f,.58f,.58f},bright ? 0xfde9cf : 0xd24b39);
            part(p,(Vec3){-.296f,.48f,-.296f},(Vec3){.592f,.125f,.592f},bright ? 0xffffff : 0xfbe2af);
            if (fuse) {
                float smoke=.87f+.018f*(float)((CART_TNT_IMPACT_PENDING-fuse)%12);
                part(p,(Vec3){-.056f,smoke,-.055f},(Vec3){.112f,.105f,.11f},0x777078);
            }
        } else if (cart[i].kind==CART_CHEST) {
            part(p,(Vec3){-.327f,.239f,-.33f},(Vec3){.654f,.39f,.66f},0x976134);
            part(p,(Vec3){-.336f,.626f,-.339f},(Vec3){.672f,.172f,.678f},0xc18a4e);
            part(p,(Vec3){-.051f,.580f,.331f},(Vec3){.102f,.122f,.03f},0xffce67);
        } else if (cart[i].kind==CART_HOPPER) {
            part(p,(Vec3){-.14f,.23f,-.14f},(Vec3){.28f,.20f,.28f},0x3d4452);
            part(p,(Vec3){-.24f,.399f,-.24f},(Vec3){.48f,.135f,.48f},0x515b6c);
            part(p,(Vec3){-.34f,.526f,-.34f},(Vec3){.68f,.068f,.68f},0x434a58);
            part(p,(Vec3){-.34f,.595f,-.34f},(Vec3){.11f,.158f,.68f},0x8791a1);
            part(p,(Vec3){.23f,.595f,-.34f},(Vec3){.11f,.158f,.68f},0x8791a1);
            part(p,(Vec3){-.23f,.595f,-.34f},(Vec3){.46f,.158f,.11f},0x717b8a);
            part(p,(Vec3){-.23f,.595f,.23f},(Vec3){.46f,.158f,.11f},0x717b8a);
        } else if (cart[i].kind==CART_FURNACE) {
            CartFurnaceDrive drive;
            int lit=cart_furnace_drive(world_dimension(),i,&drive) && drive.fuel;
            part(p,(Vec3){-.31f,.233f,-.31f},(Vec3){.62f,.566f,.62f},0x787d82);
            part(p,(Vec3){-.239f,.438f,-.317f},(Vec3){.478f,.226f,.64f},0x34363b);
            if (lit) {
                part(p,(Vec3){-.173f,.452f,-.324f},(Vec3){.346f,.093f,.652f},0xff7929);
                part(p,(Vec3){-.081f,.476f,-.329f},(Vec3){.162f,.097f,.662f},0xffcd60);
                part(p,(Vec3){-.070f,.885f,-.054f},(Vec3){.123f,.092f,.117f},0x73747b);
                part(p,(Vec3){-.017f,1.016f,.045f},(Vec3){.087f,.065f,.088f},0x8b8b90);
            }
        }
    }
    if (!send_mesh(&actor_mesh)) return 0;
    if (mesh_sample_active && actor_mesh.count) {
        ++entity_sample_uploads; entity_sample_vertices+=actor_mesh.count;
    }
    uploaded_visible=visible; uploaded_visible_boats=visible_boats; uploaded_visible_carts=visible_carts;
    uploaded_actors = version;
    uploaded_combat = combat_version;
    uploaded_boats = hulls;
    uploaded_carts = wagons;
    actor_mesh.valid = 1;
    relight_at=now+750;
    return 1;
}

static void horse_part(Pose p,float size,Vec3 origin,Vec3 extent,unsigned color) {
    part(p,scale(origin,size),scale(extent,size),color);
}
static void horse_armor_model(Pose p,int type) {
    const ItemDef *armor=item_def(type);
    if (!armor || !horse_armor_points(type)) return;
    const unsigned steel=armor->color;
    unsigned rim=type==GOLD_HORSE_ARMOR ? 0xffe292 : type==DIAMOND_HORSE_ARMOR ? 0xbdffec : 0xe1e7f1;
    /* The flanks leave the saddle strap visible, and the brow plate stops above the eyes. */
    for (int side=-1;side<=1;side+=2) {
        float x=side>0 ? .33f : -.391f;
        horse_part(p,1,(Vec3){x,.62f,-.56f},(Vec3){.061f,.40f,.55f},steel);
        horse_part(p,1,(Vec3){x,.63f,.15f},(Vec3){.061f,.37f,.43f},steel);
        horse_part(p,1,(Vec3){x-side*.003f,.60f,.16f},(Vec3){.066f,.08f,.40f},rim);
    }
    horse_part(p,1,(Vec3){-.27f,.74f,-.60f},(Vec3){.54f,.30f,.072f},steel);
    horse_part(p,1,(Vec3){-.228f,1.46f,-.844f},(Vec3){.456f,.104f,.32f},steel);
    horse_part(p,1,(Vec3){-.142f,1.49f,-.855f},(Vec3){.284f,.085f,.075f},rim);
}
static int horse_armor_mesh_selftest(void) {
    vertexCount=meshOOM=0;
    Pose p={(Vec3){0,0,0},1,0};
    horse_armor_model(p,SADDLE);
    int failed=vertexCount!=0;
    horse_armor_model(p,IRON_HORSE_ARMOR);
    unsigned iron=vertexCount;
    const Uint8 iron_red=iron ? vertices[0].c[0] : 0;
    int left=0,right=0,brow=0,saddle_gap=1;
    for (unsigned i=0;i<iron;++i) {
        const float *v=vertices[i].p;
        left|=v[0]<-.39f && v[1]<1.1f;
        right|=v[0]>.39f && v[1]<1.1f;
        brow|=v[1]>1.54f && v[2]<-.7f;
        saddle_gap&=v[2]<.02f || v[2]>.14f;
    }
    vertexCount=0;
    horse_armor_model(p,DIAMOND_HORSE_ARMOR);
    failed|=meshOOM || !iron || vertexCount!=iron || (vertexCount && vertices[0].c[0]==iron_red) ||
        !left || !right || !brow || !saddle_gap;
    free(vertices); vertices=NULL; vertexCount=vertexCapacity=0;
    printf("MESH TEST horse armor vertices=%u failures=%d\n",iron,failed!=0);
    return failed;
}
static int golem_mesh_selftest(void) {
    ActorRecord r={{0,0,0},0,ACTOR_IRON_GOLEM,GOLEM_HEALTH,GOLEM_PLAYER_BUILT};
    vertexCount=meshOOM=0;
    actor_model(&r,0.f,0);
    unsigned idle=vertexCount;
    float old_reach=0.f,top=0.f;
    for (unsigned i=0;i<idle;++i) {
        old_reach=fminf(old_reach,vertices[i].p[2]);
        top=fmaxf(top,vertices[i].p[1]);
    }
    vertexCount=0;
    actor_model(&r,1.f,0);
    float reach=0.f;
    for (unsigned i=0;i<vertexCount;++i) reach=fminf(reach,vertices[i].p[2]);
    int failed=meshOOM || idle==0 || idle!=vertexCount || top<1.05f || top>GOLEM_HEIGHT || reach>=old_reach-.10f;
    static const int faces[]={4,1,5,0};
    for (int dir=0;dir<4;++dir) for (int face=0;face<6;++face)
        failed|=tile_for_block(PUMPKIN_FACE_BASE+dir,face)!=(face==faces[dir] ? PUMPKIN_FACE_TILE :
            face==2 || face==3 ? PUMPKIN_TOP_TILE : PUMPKIN_SIDE_TILE);
    r.type=ACTOR_SNOW_GOLEM;
    vertexCount=0;
    actor_model(&r,0.f,0);
    float snow_top=0.f,snow_width=0.f;
    for (unsigned i=0;i<vertexCount;++i) {
        snow_top=fmaxf(snow_top,vertices[i].p[1]);
        snow_width=fmaxf(snow_width,fabsf(vertices[i].p[0]));
    }
    unsigned snow=vertexCount;
    failed|=meshOOM || !snow || snow_top<.70f || snow_top>SNOW_GOLEM_HEIGHT || snow_width<.35f ||
        snow_width>SNOW_GOLEM_RADIUS;
    free(vertices); vertices=NULL; vertexCount=vertexCapacity=0;
    printf("MESH TEST patrol crawler=%u pulse emitter=%u pumpkin faces=4 failures=%d\n",idle,snow,failed!=0);
    return failed;
}
static int wolf_mesh_selftest(void) {
    ActorRecord a={{0,0,0},0,ACTOR_WOLF,WOLF_WILD_HEALTH,0};
    vertexCount=meshOOM=0;
    actor_model(&a,0,0);
    Uint32 wild=vertexCount;
    float front=0.f,back=0.f,standing=0.f;
    for (Uint32 i=0;i<wild;++i) {
        front=fminf(front,vertices[i].p[2]);
        back=fmaxf(back,vertices[i].p[2]);
        standing=fmaxf(standing,vertices[i].p[1]);
    }
    vertexCount=0;
    a.traits=WOLF_TAMED|WOLF_SITTING;
    a.health=WOLF_TAMED_HEALTH;
    actor_model(&a,0,0);
    Uint32 seated=vertexCount;
    float height=0.f;
    for (Uint32 i=0;i<seated;++i) height=fmaxf(height,vertices[i].p[1]);
    int failed=meshOOM || !wild || seated!=wild+36 || standing-height<.11f ||
        front>-.55f || back<.60f || height>.73f;
    free(vertices); vertices=NULL; vertexCount=vertexCapacity=0;
    printf("MESH TEST wolf standing=%u seated=%u failures=%d\n",wild,seated,failed!=0);
    return failed;
}
static int cat_mesh_selftest(void) {
    ActorRecord a={{0,0,0},0,ACTOR_OCELOT,CAT_HEALTH,0};
    vertexCount=meshOOM=0;
    unsigned coats[4]={0};
    unsigned wild=0;
    int failed=0;
    for (int i=0;i<4;++i) {
        a.traits=(uint8_t)(i ? PET_TAMED|i*CAT_TUXEDO : 0);
        vertexCount=0;
        actor_model(&a,0,0);
        if (!vertexCount || meshOOM) { failed=1; break; }
        coats[i]=(unsigned)vertices[0].c[0]<<16|(unsigned)vertices[0].c[1]<<8|vertices[0].c[2];
        if (!i) wild=vertexCount;
        for (int j=0;j<i;++j) failed|=coats[i]==coats[j];
    }
    float standing=0,seat=0,tail=0;
    for (unsigned i=0;i<vertexCount;++i) standing=fmaxf(standing,vertices[i].p[1]);
    vertexCount=0;
    actor_model(&a,0,1);
    for (unsigned i=0;i<vertexCount;++i) {
        seat=fmaxf(seat,vertices[i].p[1]); tail=fmaxf(tail,vertices[i].p[2]);
    }
    failed|=meshOOM || standing-seat<.11f || tail<.60f || seat>.60f;
    free(vertices); vertices=NULL; vertexCount=vertexCapacity=0;
    printf("MESH TEST ocelot wild=%u three coats and sitting failures=%d\n",wild,failed!=0);
    return failed;
}
static int squid_glass_mesh_selftest(void) {
    ActorRecord a={{0,0,0},0,ACTOR_SQUID,SQUID_HEALTH,0};
    vertexCount=meshOOM=0;
    actor_model(&a,0,0);
    unsigned swim=vertexCount;
    float narrow=0,wide=0;
    for (unsigned i=0;i<swim;++i) narrow=fmaxf(narrow,fabsf(vertices[i].p[0]));
    vertexCount=0;
    actor_model(&a,1,0);
    for (unsigned i=0;i<vertexCount;++i) wide=fmaxf(wide,fabsf(vertices[i].p[0]));
    int failed=meshOOM || swim<400 || swim!=vertexCount || wide-narrow<.10f;
    unsigned old_light=brightness;
    brightness=255;
    unsigned colors[STAINED_GLASS_END-STAINED_GLASS_BASE];
    for (int c=0;c<STAINED_GLASS_END-STAINED_GLASS_BASE;++c) {
        vertexCount=0;
        glass_face((Vec3){0,0,0},1.f,4,STAINED_GLASS_BASE+c);
        if (meshOOM || vertexCount!=36) { failed=1; break; }
        unsigned color=(unsigned)vertices[0].c[0]<<16|(unsigned)vertices[0].c[1]<<8|vertices[0].c[2];
        for (int previous=0;previous<c;++previous) failed|=color==colors[previous];
        colors[c]=color;
        /* Each strip's bounds must leave the center of the face open. */
        for (unsigned i=0;i<vertexCount;i+=6) {
            float x0=1,x1=0,y0=1,y1=0;
            for (unsigned j=i;j<i+6;++j) {
                x0=fminf(x0,vertices[j].p[0]); x1=fmaxf(x1,vertices[j].p[0]);
                y0=fminf(y0,vertices[j].p[1]); y1=fmaxf(y1,vertices[j].p[1]);
            }
            failed|=x0<=.5f && x1>=.5f && y0<=.5f && y1>=.5f;
        }
    }
    brightness=old_light;
    free(vertices); vertices=NULL; vertexCount=vertexCapacity=0;
    printf("MESH TEST squid vertices=%u sixteen clear-centered glass tints failures=%d\n",swim,failed!=0);
    return failed;
}
static int pane_mesh_selftest(void) {
    Pose origin={{0,0,0},1,0};
    unsigned links=1u<<EAST,parts=pane_parts(links);
    int failed=0;
    for (int i=0;i<3;++i) {
        int raw=i==0 ? GLASS_PANE_CELL : i==1 ? STAINED_PANE_END-1 : IRON_BARS_CELL;
        vertexCount=meshOOM=0;
        draw_pane(origin,1,raw,parts,links,parts,0);
        failed|=!vertexCount || meshOOM || vertexCount%6;
        int touched_seam=0;
        for (unsigned k=0;k+5<vertexCount;k+=6) {
            float minx=1,maxx=0,miny=1,maxy=0,minz=1,maxz=0;
            for (unsigned j=k;j<k+6;++j) {
                Vec3 v={vertices[j].p[0],vertices[j].p[1],vertices[j].p[2]};
                minx=fminf(minx,v.x); maxx=fmaxf(maxx,v.x);
                miny=fminf(miny,v.y); maxy=fmaxf(maxy,v.y);
                minz=fminf(minz,v.z); maxz=fmaxf(maxz,v.z);
            }
            touched_seam|=maxx==1;
            failed|=minx<.4375f || minz<.4375f || maxz>.5625f || minx==1 || miny==1;
            if (raw!=IRON_BARS_CELL)
                failed|=minx<=.75f && maxx>=.75f && miny<=.5f && maxy>=.5f;
        }
        failed|=!touched_seam;
    }
    free(vertices); vertices=NULL; vertexCount=vertexCapacity=0;
    printf("MESH TEST pane bounds, open center and connected caps failures=%d\n",failed!=0);
    return failed;
}
static void horse_model(const HorseRecord *h) {
    static const unsigned coats[]={0xece4d7,0xbf966b,0x9a5539,0x71412e,0x514039,0x363538,0xb0a59c};
    Pose p={h->feet,cosf(h->heading),sinf(h->heading)};
    float k=h->age ? .62f : 1.f;
    int donkey=h->type==EQUINE_DONKEY;
    unsigned color=donkey ? 0x807b73 : h->type==EQUINE_MULE ? 0x856344 : coats[h->coat];
    unsigned mane=donkey ? 0x4b4740 : 0x443830;
    horse_part(p,k,(Vec3){-.34f,.52f,-.55f},(Vec3){.68f,.58f,1.18f},color);
    horse_part(p,k,(Vec3){-.24f,.81f,-.57f},(Vec3){.48f,.60f,.41f},color);
    horse_part(p,k,(Vec3){-.22f,1.17f,-.83f},(Vec3){.44f,.37f,.56f},color);
    horse_part(p,k,(Vec3){-.19f,1.155f,-.91f},(Vec3){.38f,.19f,.29f},donkey ? 0xbab2a7 : color);
    horse_part(p,k,(Vec3){-.10f,1.20f,-.30f},(Vec3){.20f,.29f,.15f},mane);
    horse_part(p,k,(Vec3){-.082f,.38f,.58f},(Vec3){.164f,.49f,.17f},mane);
    for (int sign=-1;sign<=1;sign+=2) {
        float x=sign*.218f-.10f;
        for (int end=-1;end<=1;end+=2) {
            float z=end*.435f-.09f;
            horse_part(p,k,(Vec3){x,.11f,z},(Vec3){.20f,.54f,.21f},color);
            horse_part(p,k,(Vec3){x,0,z-.01f},(Vec3){.20f,.16f,.22f},mane);
        }
        horse_part(p,k,(Vec3){sign*.159f-.045f,1.48f,-.52f},
            (Vec3){.09f,donkey ? .31f : h->type==EQUINE_MULE ? .24f : .15f,.11f},color);
        horse_part(p,k,(Vec3){sign*.207f-.022f,1.35f,-.71f},(Vec3){.046f,.075f,.09f},0x24211f);
    }
    if (h->armor.count) horse_armor_model(p,h->armor.type);
    if (h->saddle) {
        horse_part(p,k,(Vec3){-.365f,1.026f,-.13f},(Vec3){.73f,.15f,.54f},0x934e34);
        horse_part(p,k,(Vec3){-.369f,.48f,.04f},(Vec3){.738f,.56f,.103f},0x55392e);
    }
}
static int refresh_horses(const EntityView *view) {
    static Uint64 relight_at;
    static uint32_t uploaded_visible;
    Uint64 now=SDL_GetTicks();
    Dimension dim=world_dimension();
    const HorseRecord *list=horses_in(dim);
    uint32_t visible=0;
    for (int i=0;i<HORSE_CAPACITY;++i)
        if (list[i].type && visible_entity(view,list[i].feet,HORSE_MESH_RADIUS)) visible|=UINT32_C(1)<<i;
    unsigned version=horses_revision();
    if (horse_mesh.valid && version==uploaded_horses && uploaded_visible==visible &&
        (dim!=OVERWORLD || !horse_mesh.count || now<relight_at)) return 1;
    vertexCount=meshOOM=0;
    for (int i=0;i<HORSE_CAPACITY;++i) if (visible&(UINT32_C(1)<<i)) {
        Uint32 start=vertexCount;
        horse_model(&list[i]);
        shade_mob(start,list[i].feet,dim);
    }
    if (!send_mesh(&horse_mesh)) return 0;
    if (mesh_sample_active && horse_mesh.count) {
        ++entity_sample_uploads; entity_sample_vertices+=horse_mesh.count;
    }
    uploaded_visible=visible;
    horse_mesh.valid=1; uploaded_horses=version;
    relight_at=now+750;
    return 1;
}

#if __has_include("ccdmc_entity_view_test.h")
#include "ccdmc_entity_view_test.h"
#endif

static void draw_lead(Vec3 from,Vec3 to,int tied) {
    float sag=fminf(.36f,hypotf(from.x-to.x,from.z-to.z)*.065f);
    Vec3 previous=from;
    for (int i=1;i<=7;++i) {
        float t=i/7.f;
        Vec3 p={from.x+(to.x-from.x)*t,from.y+(to.y-from.y)*t-4*sag*t*(1-t),from.z+(to.z-from.z)*t};
        ribbon(previous,p,.012f,0xc68c58); previous=p;
    }
    if (tied) part((Pose){to,1,0},(Vec3){-.145f,-.024f,-.145f},(Vec3){.29f,.06f,.29f},0xa46536);
}
static int refresh_leads(void) {
    unsigned version=lead_revision(),av=actors_revision(),hv=horses_revision();
    if (lead_mesh.valid && !lead_mesh.count && version==lead_version) return 1;
    Vec3 f=forward();
    Vec3 hand={px+f.x*.44f+cosf(yaw)*.24f,py+1.17f+f.y*.44f,pz+f.z*.44f+sinf(yaw)*.24f};
    if (lead_mesh.valid && version==lead_version && lead_actors==av && lead_horses==hv &&
        lead_hand.x==hand.x && lead_hand.y==hand.y && lead_hand.z==hand.z) return 1;
    vertexCount=meshOOM=0;
    Dimension d=world_dimension();
    const ActorRecord *a=actors_in(d);
    const ActorCare *c=actors_care_in(d);
    const HorseRecord *h=horses_in(d);
    for (int i=0;i<ACTOR_CAPACITY;++i) if (a[i].type) {
        LeadLink l=*actors_lead_at(d,i);
        Vec3 end;
        if (!l.kind || !lead_anchor(l,&end)) continue;
        float y=actors_is_bovine(a[i].type) || a[i].type==ACTOR_SHEEP ? .85f : .51f;
        Vec3 start=a[i].feet; start.y+=y*(c[i].grow ? .5f : 1.f);
        draw_lead(start,l.kind==LEAD_PLAYER ? hand : end,l.kind==LEAD_FENCE);
    }
    for (int i=0;i<HORSE_CAPACITY;++i) if (h[i].type) {
        LeadLink l=*horses_lead_at(d,i);
        Vec3 end;
        if (!l.kind || !lead_anchor(l,&end)) continue;
        Vec3 start=h[i].feet; start.y+=h[i].age ? .68f : 1.05f;
        draw_lead(start,l.kind==LEAD_PLAYER ? hand : end,l.kind==LEAD_FENCE);
    }
    if (!send_mesh(&lead_mesh)) return 0;
    lead_mesh.valid=1; lead_version=version; lead_actors=av; lead_horses=hv; lead_hand=hand;
    return 1;
}

static int upload_geometry_page(SDL_GPUBuffer *buffer,const Uint8 *data,Uint32 offset,Uint32 bytes) {
    void *mem = SDL_MapGPUTransferBuffer(dev, transfer, true);
    if (!mem) return 0;
    memcpy(mem, data, bytes);
    SDL_UnmapGPUTransferBuffer(dev, transfer);
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd) return 0;
    SDL_GPUCopyPass *cp = SDL_BeginGPUCopyPass(cmd);
    if (!cp) { SDL_CancelGPUCommandBuffer(cmd); return 0; }
    SDL_GPUTransferBufferLocation src = {.transfer_buffer = transfer};
    SDL_GPUBufferRegion dst = {.buffer = buffer, .offset=offset, .size = bytes};
    /* Only the first page may rotate the backing resource. Subsequent
       pages must retain the bytes already written, including on native SDL. */
    SDL_UploadToGPUBuffer(cp, &src, &dst, offset==0);
    SDL_EndGPUCopyPass(cp);
    return SDL_SubmitGPUCommandBuffer(cmd);
}
static int send_geometry(Mesh *mesh,const Vertex *data,Uint32 count) {
    if (!count) { mesh->count = 0; return 1; }
    if (count>UINT32_MAX/sizeof(Vertex)) return SDL_SetError("Mesh vertex extent overflow");
    const Uint32 bytes=count*sizeof(Vertex);
    const Uint32 page=SDL_min(bytes,(Uint32)MESH_TRANSFER_PAGE);
    /* Keep growth amortized for small moving meshes. For a dense chunk,
       reserve only the bytes used instead of another power of two. */
    const Uint32 capacity=bytes>=MESH_TRANSFER_PAGE ? bytes :
        capacity_for(count)*sizeof(Vertex);
    if (page>transferCapacity) {
        const Uint32 staging=SDL_min(capacity,(Uint32)MESH_TRANSFER_PAGE);
        SDL_GPUTransferBufferCreateInfo t={.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,.size=staging};
        SDL_GPUTransferBuffer *next=SDL_CreateGPUTransferBuffer(dev,&t);
        if (!next) return 0;
        if (transfer) SDL_ReleaseGPUTransferBuffer(dev,transfer);
        transfer=next;
        transferCapacity=staging;
    }
    SDL_GPUBuffer *buffer=mesh->buffer;
    if (bytes>mesh->capacity) {
        SDL_GPUBufferCreateInfo b={.usage=SDL_GPU_BUFFERUSAGE_VERTEX,.size=capacity};
        buffer=SDL_CreateGPUBuffer(dev,&b);
        if (!buffer) return 0;
    }
    for (Uint32 offset=0;offset<bytes;) {
        const Uint32 copy=SDL_min(bytes-offset,page);
        if (!upload_geometry_page(buffer,(const Uint8 *)data+offset,offset,copy)) {
            if (buffer!=mesh->buffer) SDL_ReleaseGPUBuffer(dev,buffer);
            return 0;
        }
        offset+=copy;
    }
    if (buffer!=mesh->buffer) {
        if (mesh->buffer) SDL_ReleaseGPUBuffer(dev,mesh->buffer);
        mesh->buffer=buffer;
        mesh->capacity=capacity;
    }
    mesh->count = count;
    return 1;
}
static int send_mesh(Mesh *mesh) {
    return meshOOM ? SDL_SetError("Out of memory building mesh") : send_geometry(mesh,vertices,vertexCount);
}

enum { FRAME_RANGE=3, FRAME_ICON_EDGE=16, FRAME_MAP_UPLOAD_LIMIT=2 };
static int frame_in_view(const ItemFrame *f,int cx,int cz) {
    return f->present && !f->detached && abs((int)floorf(f->x/(float)CHUNK_SIZE)-cx)<=FRAME_RANGE &&
        abs((int)floorf(f->z/(float)CHUNK_SIZE)-cz)<=FRAME_RANGE;
}
static Vec3 frame_point(Pose p,float u,float v,float depth,unsigned rotation) {
    for (unsigned i=0;i<rotation;++i) { float old=u; u=v; v=-old; }
    return local_point(p,(Vec3){u,v,depth});
}
static void frame_quad(Pose p,float x0,float y0,float x1,float y1,float depth,unsigned rotation,unsigned rgb) {
    Vec3 v[]={frame_point(p,x0,y0,depth,rotation),frame_point(p,x1,y0,depth,rotation),
        frame_point(p,x1,y1,depth,rotation),frame_point(p,x0,y1,depth,rotation)};
    flat_quad(v,rgb);
}
static unsigned frame_hex(char c) { return c<='9' ? (unsigned)(c-'0') : (unsigned)(c-'A'+10); }
/* The existing icon definitions include concave shapes. Fill polygons using
   an even-odd rule instead of fanning triangles from a vertex. */
static void frame_raster(int item,unsigned *pixels) {
    ItemIcon icon;
    memset(pixels,0,FRAME_ICON_EDGE*FRAME_ICON_EDGE*sizeof *pixels);
    if (!item_icon(item,&icon)) return;
    for (int layer=0;layer<ITEM_ICON_LAYERS;++layer) {
        const char *start=icon.path[layer];
        while (start && *start) {
            const char *end=strchr(start,';');
            if (!end) end=start+strlen(start);
            size_t n=(size_t)(end-start)/2;
            if (n>=3 && n<=64) for (int y=0;y<FRAME_ICON_EDGE;++y) for (int x=0;x<FRAME_ICON_EDGE;++x) {
                float u=(x+.5f)*15.f/FRAME_ICON_EDGE,v=(y+.5f)*15.f/FRAME_ICON_EDGE;
                int inside=0;
                for (size_t i=0,j=n-1;i<n;j=i++) {
                    float ax=frame_hex(start[2*i]),ay=frame_hex(start[2*i+1]);
                    float bx=frame_hex(start[2*j]),by=frame_hex(start[2*j+1]);
                    if ((v<ay)!=(v<by)) inside^=u<ax+(bx-ax)*(v-ay)/(by-ay);
                }
                if (inside) pixels[y*FRAME_ICON_EDGE+x]=icon.rgb[layer]|0x1000000u;
            }
            start=*end ? end+1 : end;
        }
    }
}
static void draw_item_frame(const ItemFrame *f) {
    Vec3 c,u,n;
    item_frame_basis(f,&c,&u,&n);
    Pose p={c,u.x,u.z};
    float w=f->item.type==FILLED_MAP ? .499f : .375f;
    part(p,(Vec3){-w,-w,0.004f},(Vec3){2*w,2*w,.026f},f->item.type==FILLED_MAP ? 0xcdb98a : 0x704632);
    if (f->item.type==FILLED_MAP) return;
    const float trim=.049f;
    part(p,(Vec3){-w,-w,.030f},(Vec3){2*w,trim,.025f},0xb78248);
    part(p,(Vec3){-w,w-trim,.030f},(Vec3){2*w,trim,.025f},0xca965a);
    part(p,(Vec3){-w,-w+trim,.030f},(Vec3){trim,2*(w-trim),.025f},0xaf7944);
    part(p,(Vec3){w-trim,-w+trim,.030f},(Vec3){trim,2*(w-trim),.025f},0xaf7944);
    if (!f->item.count) return;
    typedef struct { int type; unsigned pixels[FRAME_ICON_EDGE*FRAME_ICON_EDGE]; } FrameIcon;
    static FrameIcon cache[24];
    FrameIcon *icon=&cache[f->item.type%(sizeof cache/sizeof *cache)];
    if (icon->type!=f->item.type) { frame_raster(f->item.type,icon->pixels); icon->type=f->item.type; }
    const float start=-.304f,step=-2.f*start/FRAME_ICON_EDGE;
    for (int y=0;y<FRAME_ICON_EDGE;++y) for (int x=0;x<FRAME_ICON_EDGE;) {
        unsigned rgb=icon->pixels[y*FRAME_ICON_EDGE+x];
        int end=x+1;
        while (end<FRAME_ICON_EDGE && icon->pixels[y*FRAME_ICON_EDGE+end]==rgb) ++end;
        if (rgb) frame_quad(p,start+x*step,-start-(y+1)*step,start+end*step,-start-y*step,.057f,f->rotation,rgb);
        x=end;
    }
}
static int ensure_frame_atlas(unsigned id) {
    unsigned side=frame_tiles ? frame_tiles : 1;
    while (side*side<id) side*=2;
    if (frame_map_atlas && side==frame_tiles) return 1;
    SDL_GPUTextureCreateInfo info={.type=SDL_GPU_TEXTURETYPE_2D,.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage=SDL_GPU_TEXTUREUSAGE_SAMPLER,.width=side*MAP_SIDE,.height=side*MAP_SIDE,
        .layer_count_or_depth=1,.num_levels=1,.sample_count=SDL_GPU_SAMPLECOUNT_1};
    SDL_GPUTexture *texture=SDL_CreateGPUTexture(dev,&info);
    if (!texture) return 0;
    if (frame_map_atlas) SDL_ReleaseGPUTexture(dev,frame_map_atlas);
    frame_map_atlas=texture; frame_tiles=side;
    memset(frame_map_uploaded,0,sizeof frame_map_uploaded);
    frame_maps.count=0; frame_maps.valid=0;
    return 1;
}
static int update_frame_atlas(void) {
    unsigned ids[FRAME_MAP_UPLOAD_LIMIT],count=0;
    MapView views[FRAME_MAP_UPLOAD_LIMIT];
    for (unsigned i=1;i<=MAP_LIMIT && count<FRAME_MAP_UPLOAD_LIMIT;++i) if (frame_maps_needed[i]) {
        MapView map;
        if (map_view(i,&map) && map.revision!=frame_map_uploaded[i]) {
            ids[count]=i; views[count++]=map;
        }
    }
    if (!count) return 1;
    SDL_GPUTransferBufferCreateInfo ti={.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,.size=count*MAP_PIXELS*4u};
    SDL_GPUTransferBuffer *tb=SDL_CreateGPUTransferBuffer(dev,&ti);
    if (!tb) return 0;
    Uint8 *ptr=(Uint8 *)SDL_MapGPUTransferBuffer(dev,tb,false);
    if (!ptr) { SDL_ReleaseGPUTransferBuffer(dev,tb); return 0; }
    for (unsigned i=0;i<count;++i) for (unsigned j=0;j<MAP_PIXELS;++j) {
        unsigned rgb=map_rgb(views[i].colors[j]);
        Uint8 *rgba=ptr+4*(i*MAP_PIXELS+j);
        rgba[0]=(Uint8)(rgb>>16); rgba[1]=(Uint8)(rgb>>8); rgba[2]=(Uint8)rgb; rgba[3]=255;
    }
    SDL_UnmapGPUTransferBuffer(dev,tb);
    SDL_GPUCommandBuffer *cmd=SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd) { SDL_ReleaseGPUTransferBuffer(dev,tb); return 0; }
    SDL_GPUCopyPass *pass=SDL_BeginGPUCopyPass(cmd);
    if (!pass) { SDL_CancelGPUCommandBuffer(cmd); SDL_ReleaseGPUTransferBuffer(dev,tb); return 0; }
    for (unsigned i=0;i<count;++i) {
        unsigned tile=ids[i]-1;
        SDL_GPUTextureTransferInfo src={.transfer_buffer=tb,.offset=i*MAP_PIXELS*4u,.pixels_per_row=MAP_SIDE,.rows_per_layer=MAP_SIDE};
        SDL_GPUTextureRegion dst={.texture=frame_map_atlas,.x=(tile%frame_tiles)*MAP_SIDE,.y=(tile/frame_tiles)*MAP_SIDE,
            .w=MAP_SIDE,.h=MAP_SIDE,.d=1};
        SDL_UploadToGPUTexture(pass,&src,&dst,false);
    }
    SDL_EndGPUCopyPass(pass);
    int ok=SDL_SubmitGPUCommandBuffer(cmd);
    SDL_ReleaseGPUTransferBuffer(dev,tb);
    if (!ok) return 0;
    for (unsigned i=0;i<count;++i) {
        if (!frame_map_uploaded[ids[i]]) frame_maps.valid=0;
        frame_map_uploaded[ids[i]]=views[i].revision;
    }
    return 1;
}
static void draw_frame_map(const ItemFrame *f,unsigned id) {
    Vec3 c,u,n;
    item_frame_basis(f,&c,&u,&n);
    Pose p={c,u.x,u.z};
    const float h=.499f;
    const Vec3 pts[]={frame_point(p,-h,h,.037f,f->rotation),frame_point(p,h,h,.037f,f->rotation),
        frame_point(p,h,-h,.037f,f->rotation),frame_point(p,-h,-h,.037f,f->rotation)};
    const unsigned tile=id-1;
    const float eps=.001f/MAP_SIDE,side=frame_tiles;
    const float x0=(tile%frame_tiles+eps)/side,x1=(tile%frame_tiles+1.f-eps)/side;
    const float y0=(tile/frame_tiles+eps)/side,y1=(tile/frame_tiles+1.f-eps)/side;
    static const int order[]={0,1,2,0,2,3};
    const float uv[][2]={{x0,y0},{x1,y0},{x1,y1},{x0,y1}};
    Vertex *face=reserve_face();
    if (!face) return;
    for (int i=0;i<6;++i) {
        int j=order[i];
        face[i]=(Vertex){{pts[j].x,pts[j].y,pts[j].z},{255,255,255,255},{uv[j][0],uv[j][1]}};
    }
}
static int refresh_item_frames(int cx,int cz) {
    unsigned version=item_frames_revision();
    const ItemFrame *frames=item_frames_in(world_dimension());
    if (!frame_mesh.valid || version!=frame_uploaded || cx!=frame_x || cz!=frame_z) {
        memset(frame_maps_needed,0,sizeof frame_maps_needed);
        vertexCount=meshOOM=0;
        for (int i=0;i<ITEM_FRAME_LIMIT;++i) if (frame_in_view(&frames[i],cx,cz)) {
            draw_item_frame(&frames[i]);
            unsigned id=map_item_id(frames[i].item);
            if (id && id<=MAP_LIMIT) frame_maps_needed[id]=1;
        }
        if (!send_mesh(&frame_mesh)) return 0;
        frame_mesh.valid=1; frame_maps.valid=0;
        frame_uploaded=version; frame_x=cx; frame_z=cz;
    }
    unsigned last=MAP_LIMIT;
    while (last && !frame_maps_needed[last]) --last;
    if (!last) { frame_maps.count=0; return 1; }
    if (!ensure_frame_atlas(last) || !update_frame_atlas()) return 1; /* Keep the frame visible and retry. */
    if (frame_maps.valid) return 1;
    vertexCount=meshOOM=0;
    for (int i=0;i<ITEM_FRAME_LIMIT;++i) if (frame_in_view(&frames[i],cx,cz)) {
        unsigned id=map_item_id(frames[i].item);
        if (id && id<=MAP_LIMIT && frame_map_uploaded[id]) draw_frame_map(&frames[i],id);
    }
    if (!send_mesh(&frame_maps)) return 0;
    frame_maps.valid=1;
    return 1;
}
static int item_frame_mesh_selftest(void) {
    unsigned raster[FRAME_ICON_EDGE*FRAME_ICON_EDGE];
    frame_raster(DIAMOND_SWORD,raster);
    int pixels=0,failed=0;
    for (int i=0;i<FRAME_ICON_EDGE*FRAME_ICON_EDGE;++i) pixels+=raster[i]!=0;
    failed|=pixels<15 || pixels>175;
    unsigned old_tiles=frame_tiles;
    frame_tiles=16;
    for (int dir=0;dir<4;++dir) {
        Vec3 c,right,n;
        ItemFrame f={.x=-16,.y=65,.z=16,.item={DIAMOND_SWORD,1,0,0,0},.face=(uint8_t)dir,.rotation=1,.present=1};
        item_frame_basis(&f,&c,&right,&n);
        Pose p={c,right.x,right.z};
        Vec3 a=frame_point(p,.25f,0.f,.06f,0),b=frame_point(p,.25f,0.f,.06f,1);
        failed|=fabsf(a.y-c.y)>.001f || fabsf(b.y-c.y+.25f)>.001f;
        vertexCount=meshOOM=0;
        draw_item_frame(&f);
        failed|=meshOOM || vertexCount<=5*36;
        for (unsigned j=0;j<vertexCount;++j) {
            Vertex v=vertices[j];
            float depth=(v.p[0]-c.x)*n.x+(v.p[2]-c.z)*n.z;
            failed|=depth<.003f || depth>.058f;
        }
        vertexCount=meshOOM=0;
        draw_frame_map(&f,MAP_LIMIT);
        failed|=meshOOM || vertexCount!=6;
        for (unsigned j=0;j<vertexCount;++j) {
            Vertex v=vertices[j];
            failed|=v.uv[0]<=.875f || v.uv[0]>=.9375f || v.uv[1]<=.9375f || v.uv[1]>=1.f;
        }
    }
    free(vertices); vertices=NULL; vertexCount=vertexCapacity=0;
    frame_tiles=old_tiles;
    printf("MESH TEST item frame wall normals, raster icon, quarter turn and map UV failures=%d\n",failed!=0);
    return failed;
}

static unsigned biome_tint(int type, int side, Biome b) {
    if (type==FIBROUS_CLAY) return side==2 ? 0xf4ca96 : 0xcdb3a6;
    if (type==JOINT_BASALT) return 0x7f979d;
    if (type==BASALT_PAVING) return side==2 ? 0xa0b3ba : 0x7f989c;
    if (type==STONE && b==BIOME_JOINT_COAST) return 0xc3b695;
    if (type == WATER) {
        if (b == BIOME_SWAMP) return 0xb6ba64;
        if (b == BIOME_DEEP_OCEAN) return 0x8abcdf;
        if (b == BIOME_FROZEN_RIVER) return 0xb3e5ff;
    }
    if (type == LEAF || (type == GRASS && side != 3)) {
        switch (b) {
        case BIOME_SAVANNA: return 0xffb372;
        case BIOME_TAIGA: case BIOME_COLD_TAIGA: case BIOME_MEGA_TAIGA: return 0xb8d5ff;
        case BIOME_SWAMP: return 0xe2be8c;
        case BIOME_ROOFED: return 0xa9ce75;
        case BIOME_JUNGLE: return 0xcdff89;
        case BIOME_BIRCH: return 0xe0f5a0;
        default: break;
        }
    }
    return 0xffffff;
}

/* Terrain vertex alpha is a light code, not opacity. Low four bits hold
   block light; the upper range is sealed (0), open (1), indirect within
   one block (2), or within two blocks (3). Values above 63 keep their
   direct meaning for emissive and non-Overworld surfaces.
   No time-dependent chunk rebuild or larger vertex buffer is needed. */
static unsigned lit_face(LightView light,int x,int y,int z,int highest,int emission,Dimension dim) {
    if (emission>=15) return 255;
    int level=light.levels ? block_light_level(light,x,y,z) : 0;
    if (level<emission) level=emission;
    if (dim==OVERWORLD) return (y>highest ? 16u : 0u)+(unsigned)level;
    unsigned ambient=dim==NETHER ? 79 : 112;
    return ambient+(255-ambient)*(unsigned)level/15;
}

static float terrain_day_fraction(float sun_height) {
    /* Match the atmosphere's sunset/dawn blend. */
    const float t=fmaxf(0.f,fminf(1.f,(sun_height+.16f)/.41f));
    return t*t*(3.f-2.f*t);
}

static int terrain_lighting_selftest(void) {
    const LightView dark={NULL,1};
    uint8_t levels[MESH_LIGHT_CELLS]={0};
    levels[(LIGHT_VIEW_EDGE+1)*WY+4]=12;
    const LightView torch={levels,1};
    const int failed=lit_face(dark,0,4,0,3,0,OVERWORLD)!=16 ||
        lit_face(torch,0,4,0,3,0,OVERWORLD)!=28 ||
        lit_face(torch,0,4,0,5,0,OVERWORLD)!=12 ||
        lit_face(dark,0,4,0,3,14,OVERWORLD)!=30 ||
        lit_face(dark,0,4,0,3,15,OVERWORLD)!=255 ||
        lit_face(dark,0,4,0,3,0,NETHER)!=79 || lit_face(dark,0,4,0,3,0,END)!=112 ||
        terrain_day_fraction(-.9f)!=0.f || terrain_day_fraction(.9f)!=1.f ||
        terrain_day_fraction(-.1f)>=terrain_day_fraction(0.f) ||
        terrain_day_fraction(0.f)>=terrain_day_fraction(.2f);
    printf("MESH TEST independent sky and block light, twilight failures=%d\n",failed);
    return failed;
}

static void draw_lily_pad(float x,float y,float z,float scale,unsigned rotation) {
    /* Notched leaf with a thin rim and shades between the veins. */
    static const float outline[][2]={{.23f,.08f},{.68f,.08f},{.91f,.30f},{.55f,.50f},{.93f,.65f},
        {.71f,.90f},{.26f,.91f},{.08f,.71f},{.08f,.31f}};
    const unsigned color[]={0x6da450,0x639948,0x5b943e,0x437c35,0x447f39,0x4c8a3c,0x558c3f,0x589742,0x629d44};
    enum { SIDES=sizeof outline/sizeof *outline };
    Vec3 points[SIDES+1];
    for (unsigned i=0;i<=SIDES;++i) {
        float a=i==SIDES ? .42f : outline[i][0], b=i==SIDES ? .52f : outline[i][1];
        for (unsigned r=0;r<(rotation&3u);++r) { float old=a; a=1.f-b; b=old; }
        points[i]=(Vec3){x+a*scale,y+LILY_PAD_HEIGHT*scale,z+b*scale};
    }
    for (unsigned i=0;i<SIDES;++i) {
        Vec3 a=points[i],b=points[(i+1)%SIDES],c=points[SIDES];
        flat_quad((Vec3[]){c,a,b,b},color[i]);
        flat_quad((Vec3[]){{a.x,y,a.z},{b.x,y,b.z},b,a},0x345e2b);
    }
}

static int sculpted_block(int type) {
    switch (type) {
    case RAIL: case POWERED_RAIL: case DETECTOR_RAIL: case ACTIVATOR_RAIL:
    case CHEST: case SADDLE_CHEST: case SMITH_CHEST: case TRAPPED_CHEST:
    case TEMPLE_CHEST: case DUNGEON_CHEST: case DESERT_PYRAMID_CHEST: case ENDER_CHEST: case MONSTER_SPAWNER:
    case BED: case AIR_OUTLET: case AIR_STATION:
    case WOOD_FENCE: case NETHER_FENCE: case FENCE_GATE: case WOOD_DOOR: case IRON_DOOR:
    case WOOD_TRAPDOOR: case LADDER: case CROP_WHEAT: case CROP_CARROT:
    case CROP_POTATO: case TALL_GRASS: case SAPLING: case PUMPKIN_STEM: case POPPY: case FILTER_MAT:
    case DANDELION: case COCOA: case CACTUS: case BROWN_MUSHROOM: case RED_MUSHROOM: case COBWEB: case LILY_PAD:
    case BREWING_STAND: case ENCHANTING_TABLE:
    case ANVIL: case SUGAR_CANE: case REDSTONE_WIRE: case LEVER: case REDSTONE_TORCH: case REPEATER: case COMPARATOR:
    case PRESSURE_PLATE: case BUTTON: case HOPPER: case TRIPWIRE: case TRIPWIRE_HOOK:
    case GLASS: case GLASS_PANE: case IRON_BARS: case TORCH: case FIRE_CELL:
        return 1;
    default:
        return 0;
    }
}

static const int mesh_neighbors[9][2]={{0,0},{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{-1,1},{1,-1},{1,1}};
static void mesh_revisions(int cx,int cz,unsigned long long out[9]) {
    for (int i=0;i<9;++i)
        out[i]=chunk_revision(cx+mesh_neighbors[i][0],cz+mesh_neighbors[i][1]);
}
static int remember_mesh_detail(void *context,int x,int y,int z,int raw) {
    TerrainMeshJob *job=(TerrainMeshJob *)context;
    int value=0;
    if (wire_cell(raw)) {
        WireShape shape=redstone_wire_shape(x,y,z);
        value=shape.mask|(shape.raised<<4);
    } else if (repeater_cell(raw)) value=redstone_repeater_locked(x,y,z);
    else if (comparator_cell(raw)) value=redstone_comparator_power(x,y,z);
    else if (pressure_cell(raw)) value=redstone_plate_power(x,y,z);
    else if (button_cell(raw)) value=redstone_button_power(x,y,z);
    else if (tripwire_hook_cell(raw)) value=redstone_tripwire_power(x,y,z);
    else if (detector_track(raw)) value=redstone_detector_power(x,y,z);
    else if (cell_type(raw)==POWERED_RAIL || activator_track(raw)) value=rail_is_energized((RailCell){x,y,z});
    int index=((x-job->cx*CHUNK_SIZE)*CHUNK_SIZE+z-job->cz*CHUNK_SIZE)*WY+y;
    job->circuit[index]=(uint8_t)value;
    return 1;
}
static int mesh_column_index(int x,int z) {
    return (x+MESH_HALO)*MESH_EDGE+z+MESH_HALO;
}
static int mesh_raw(const TerrainMeshJob *job,int column,int y) {
    int i=column*WY+y;
    return (int)job->cells[i] | (job->has_high ? (int)job->high[i]<<8 : 0);
}
/* Raw IDs are immutable inputs to these rules. Resolving them once avoids
   walking the block predicates again for every neighboring face and ray. */
static int mesh_neighbor_type(int neighbor,int other) {
    if (rail_type(other) || chest_shape_type(other) || other==BED ||
        barrier_type(other) || build_partial(other) || crop_type(other) || other==TALL_GRASS || other==SAPLING || other==TORCH ||
        other==PUMPKIN_STEM || other==POPPY || other==DANDELION || other==CACTUS || other==COCOA ||
        small_mushroom(other) || other==LILY_PAD || /* Thin plants leave the underlying face exposed. */
        other==GLASS || other==FIRE_CELL || other==AIR_OUTLET || other==AIR_STATION ||
        other==MONSTER_SPAWNER || other==COBWEB ||
        other==FILTER_MAT || other==BREWING_STAND ||
        other==ENCHANTING_TABLE || other==SUGAR_CANE || other==ANVIL || other==HOPPER ||
        piston_extended(neighbor) || piston_head_cell(neighbor) ||
        circuit_cell(neighbor)) return AIR;
    return other;
}

enum { MESH_MATERIALS=2048, MAT_COVER=1, MAT_FAST_CUBE=2, MAT_SKY_BLOCKER=4 };
typedef struct {
    uint16_t type,neighbor,appearance;
    uint8_t emission,flags;
} MeshMaterial;
static MeshMaterial mesh_materials[MESH_MATERIALS];
static pthread_once_t mesh_material_once=PTHREAD_ONCE_INIT;

static MeshMaterial describe_mesh_material(int raw) {
    const int type=cell_type(raw);
    const int appearance=type==SNOW_LAYER ? SNOW :
        lamp_cell(raw) || ejector_cell(raw) || pumpkin_cell(raw) || cistern_cell(raw) || heat_bench_cell(raw) ||
        air_relay_cell(raw) || cistern_probe_cell(raw) ? raw : type;
    const int cover=(unsigned)raw<256u && terrain_full_cover[raw];
    return (MeshMaterial){(uint16_t)type,(uint16_t)mesh_neighbor_type(raw,type),(uint16_t)appearance,
        (uint8_t)light_emission(raw),(uint8_t)((cover ? MAT_COVER : 0) |
            (cover || type==LEAF || type==ICE ? MAT_FAST_CUBE : 0) |
            (light_opacity(raw)>1 ? MAT_SKY_BLOCKER : 0))};
}
static void init_mesh_materials(void) {
    for (int i=0;i<MESH_MATERIALS;++i) mesh_materials[i]=describe_mesh_material(i);
}
static inline MeshMaterial mesh_material(int raw) {
    return (unsigned)raw<MESH_MATERIALS ? mesh_materials[raw] : describe_mesh_material(raw);
}

static unsigned mesh_face_light_mode(const TerrainMeshJob *job,LightView light,int x,int y,int z,
                                int highest,int emission,Dimension dim,int cached) {
    unsigned packed=lit_face(light,x,y,z,highest,emission,dim);
    if (dim!=OVERWORLD || packed>=16 || y<0 || y>=WY) return packed;
    /* One- and two-block openings add diffuse light under eaves and near
       doorways. Stop on real occluders; a sealed cave keeps its block light. */
    static const int sides[4][2]={{-1,0},{1,0},{0,-1},{0,1}};
    unsigned near=0;
    for (int dir=0;dir<4;++dir) for (int step=1;step<=2;++step) {
        int xx=x+sides[dir][0]*step,zz=z+sides[dir][1]*step;
        if ((unsigned)(xx+MESH_HALO)>=MESH_EDGE || (unsigned)(zz+MESH_HALO)>=MESH_EDGE) break;
        int col=mesh_column_index(xx,zz);
        int raw=mesh_raw(job,col,y);
        if (cached ? (mesh_material(raw).flags&MAT_SKY_BLOCKER)!=0 : light_opacity(raw)>1) break;
        if (y>job->sky[col]) {
            if (step==1) return packed+32u;
            near=48u;
            break;
        }
    }
    return packed+near;
}
static unsigned mesh_face_light(const TerrainMeshJob *job,LightView light,int x,int y,int z,
                                int highest,int emission,Dimension dim) {
    return mesh_face_light_mode(job,light,x,y,z,highest,emission,dim,0);
}
static int mesh_cell(const TerrainMeshJob *job,int x,int y,int z) {
    return mesh_raw(job,mesh_column_index(x,z),y);
}
enum { WATER_OPTICAL_CELLS=16, WATER_DEPTH_BIN=2048 };
_Static_assert(64*16+48<WATER_DEPTH_BIN && WATER_OPTICAL_CELLS*WATER_DEPTH_BIN<=32768,
    "Depth must not overlap the wave bits or lose the fractional atlas V coordinate");

/* Count continuous liquid downward from this surface. The existing
   meshing bitplanes handle this in at most two reads, not a depth scan. */
static unsigned water_optical_column(const TerrainColumnCover *cover,int y) {
    unsigned word=(unsigned)y>>6,bit=(unsigned)y&63u;
    uint64_t run=(cover->seal[word]&~cover->dry[word])<<(63u-bit);
    if (word && bit<WATER_OPTICAL_CELLS-1)
        run|=(cover->seal[word-1]&~cover->dry[word-1])>>(bit+1u);
    uint64_t dry=~run;
    unsigned count=dry ? (unsigned)__builtin_clzll(dry) : 64u;
    return count>WATER_OPTICAL_CELLS ? WATER_OPTICAL_CELLS : count;
}
/* Every top and side reads the same world corner, including across a
   negative chunk seam. The worker only reads its immutable snapshot. */
static WaterCorner mesh_water_corner_optics(const TerrainMeshJob *job,int x,int y,int z,unsigned *optical) {
    int cells[4],above[4];
    unsigned wet=0,depth_sum=0;
    for (int i=0;i<4;++i) {
        int col=mesh_column_index(x+(i&1)-1,z+(i>>1)-1);
        cells[i]=mesh_raw(job,col,y);
        above[i]=y+1<WY ? mesh_raw(job,col,y+1) : AIR;
        if (optical && water_strength(cells[i])) {
            unsigned count=water_optical_column(&job->cover[col],y);
            depth_sum+=count ? count : 1u;
            ++wet;
        }
    }
    if (optical) *optical=wet ? (depth_sum+wet/2u)/wet : 1u;
    return water_corner(cells,above);
}
static WaterCorner mesh_water_corner(const TerrainMeshJob *job,int x,int y,int z) {
    return mesh_water_corner_optics(job,x,y,z,NULL);
}

static void water_vertex_uv(Vertex *v,float dx,float dz,int wave,int side,unsigned optical) {
    /* Basic graphics still uses the fractional atlas UV. Keep the face,
       wave weight, slope, and depth bins disjoint (see water.metal). */
    v->uv[0]+=24+(int)lroundf(dx*24.f)+64*side;
    v->uv[1]+=24+(int)lroundf(dz*24.f)+64*wave+WATER_DEPTH_BIN*(optical-1u);
}

static int uniform_water_top(const TerrainMeshJob *job,int x,int y,int z,int shaded) {
    /* All four corners collectively read this 3x3 neighborhood. This only
       accepts the analytic flat case from water_corner(), plus identical
       saturated optical depth when that channel is encoded. */
    if (shaded && y<WATER_OPTICAL_CELLS-1) return 0;
    for (int dx=-1;dx<=1;++dx) for (int dz=-1;dz<=1;++dz) {
        const int col=mesh_column_index(x+dx,z+dz);
        if (mesh_raw(job,col,y)!=WATER || (y<WY-1 && mesh_raw(job,col,y+1)!=AIR) ||
            (shaded && water_optical_column(&job->cover[col],y)!=WATER_OPTICAL_CELLS)) return 0;
    }
    return 1;
}
static void water_face(const TerrainMeshJob *job,int x,int y,int z,int side,unsigned tint,int shaded,int uniform) {
    const Uint32 first=vertexCount;
    if (uniform && side==2 && uniform_water_top(job,x,y,z,shaded)) {
        face(job->cx*CHUNK_SIZE+x,y,job->cz*CHUNK_SIZE+z,1,WATER,2,0,water_surface(WATER,AIR),tint);
        if (shaded) for (Uint32 i=first;i<vertexCount;++i) {
            /* Zero gradients, full wave weight, maximum optical depth. */
            vertices[i].uv[0]+=24+64*2;
            vertices[i].uv[1]+=24+64*4+WATER_DEPTH_BIN*(WATER_OPTICAL_CELLS-1);
        }
        return;
    }
    face(job->cx*CHUNK_SIZE+x,y,job->cz*CHUNK_SIZE+z,1,WATER,side,0,0,tint);
    if (vertexCount==first) return;
    const int index[]={0,1,2,5};
    Vertex q[4];
    WaterCorner sample[4]={{0.f,0.f,0.f,0}};
    unsigned optical[4]={1,1,1,1};
    int wave=0;
    for (int i=0;i<4;++i) {
        q[i]=vertices[first+index[i]];
        const float *c=corners[side][i];
        if (c[1]==0.f) continue;
        sample[i]=mesh_water_corner_optics(job,x+(int)c[0],y,z+(int)c[2],shaded && side==2 ? &optical[i] : NULL);
        q[i].p[1]=y+sample[i].height;
        wave+=sample[i].open;
    }
    /* A center fan is needed for twisted patches and partial wave weights.
       Adding no vertices on the edges keeps the shared boundaries intact. */
    int fan=side==2 && ((shaded && wave>0 && wave<4) ||
        fabsf(sample[0].height+sample[2].height-sample[1].height-sample[3].height)>1.e-6f);
    Vertex center=q[0];
    if (fan) {
        for (int i=0;i<3;++i) center.p[i]=(q[0].p[i]+q[1].p[i]+q[2].p[i]+q[3].p[i])*.25f;
        for (int i=0;i<2;++i) center.uv[i]=(q[0].uv[i]+q[2].uv[i])*.5f;
        if (shaded) water_vertex_uv(&center,
            (sample[1].height+sample[2].height-sample[0].height-sample[3].height)*.5f,
            (sample[0].height+sample[1].height-sample[2].height-sample[3].height)*.5f,wave,side,
            (optical[0]+optical[1]+optical[2]+optical[3]+2u)/4u);
    }
    if (shaded) for (int i=0;i<4;++i)
        water_vertex_uv(&q[i],sample[i].dx,sample[i].dz,4*sample[i].open,side,optical[i]);
    if (!fan) {
        for (int i=0;i<4;++i) vertices[first+index[i]]=q[i];
        vertices[first+3]=q[0]; vertices[first+4]=q[2];
    } else if (reserve_face()) {
        for (int i=0;i<4;++i) {
            vertices[first+i*3]=q[i];
            vertices[first+i*3+1]=q[(i+1)%4];
            vertices[first+i*3+2]=center;
        }
    }
}

static int water_optics_selftest(void) {
    TerrainColumnCover columns[4]={{0}};
    int failed=0;
    for (int y=0;y<WY;++y) for (int i=0;i<4;++i) {
        const int period=9+i*7;
        int raw=y%period==period-1 ? GLASS : y%period==2 ? FLOW_BASE+1 : WATER;
        terrain_set_cover(&columns[i],y,raw);
    }
    for (int y=0;y<WY;++y) for (int i=0;i<4;++i) {
        unsigned expected=0;
        int period=9+i*7;
        for (int q=y;q>=0 && expected<WATER_OPTICAL_CELLS && q%period!=period-1;--q) ++expected;
        failed|=water_optical_column(&columns[i],y)!=expected;
    }
    /* The last bin, greatest wave weight and every half-texel V still
       round-trip exactly, including through the basic-graphics.fract. */
    for (unsigned level=1;level<=WATER_OPTICAL_CELLS;++level) for (int v=0;v<TILE_EDGE;++v) {
        float sample=(v+.5f)/TILE_EDGE;
        Vertex packed={{0,0,0},{255,255,255,29},{.25f,sample}};
        water_vertex_uv(&packed,-1.f,1.f,16,2,level);
        int bits=(int)packed.uv[1];
        failed|=bits/WATER_DEPTH_BIN!=(int)level-1 || bits%WATER_DEPTH_BIN!=64*16+48 ||
            packed.uv[1]-(float)bits!=sample || packed.uv[0]-(float)(int)packed.uv[0]!=.25f ||
            packed.c[3]!=29;
    }
    TerrainMeshJob *job=(TerrainMeshJob *)calloc(1,sizeof *job);
    if (!job) return 1;
    /* Feed the same four world columns into either side of x=0.
       Their corner depth must agree even with mixed bed heights. */
    const unsigned depths[4]={2,6,11,15};
    for (int side=0;side<2;++side) {
        int edge=side ? 0 : CHUNK_SIZE;
        for (int i=0;i<4;++i) {
            int col=mesh_column_index(edge+(i&1)-1,7+(i>>1)-1);
            job->cells[col*WY+64]=WATER;
            for (unsigned j=0;j<depths[i];++j) terrain_set_cover(&job->cover[col],64-(int)j,WATER);
        }
        unsigned optical;
        WaterCorner shared=mesh_water_corner_optics(job,edge,64,7,&optical);
        failed|=optical!=9u || !shared.open;
    }
    free(job);
    printf("MESH TEST continuous optical depth across cover words, UV pack and basic fractions failures=%d\n",failed);
    return failed;
}

static int water_corner_selftest(void) {
    TerrainMeshJob *job=(TerrainMeshJob *)calloc(1,sizeof *job);
    if (!job) return 1;
    enum { Y=120, Z=8 };
    int failed=0;
    /* World x=0 viewed from chunk -1 and from chunk 0. In both halo
       snapshots the wedge goes downhill to the east; the top and side
       vertices must touch at exactly the same world position. */
    const float high=water_surface(WATER,AIR), low=water_surface(FLOW_BASE+2,AIR);
    const float seam=(4.f*high+low)/5.f;
    Vertex *previous=vertices;
    Uint32 previousCount=vertexCount,previousCapacity=vertexCapacity;
    int previousOOM=meshOOM;
    vertices=NULL; vertexCount=vertexCapacity=0; meshOOM=0;
    init_atlas_coordinates();
    float edge[2]={0.f,0.f};
    for (int i=0;i<2;++i) {
        job->cx=i-1;
        int x=i ? 0 : CHUNK_SIZE;
        for (int dx=-1;dx<=0;++dx) for (int dz=-1;dz<=0;++dz) {
            int col=mesh_column_index(x+dx,Z+dz)*WY;
            job->cells[col+Y]=dx<0 ? WATER : FLOW_BASE+2;
        }
        WaterCorner c=mesh_water_corner(job,x,Y,Z);
        failed|=c.open || fabsf(c.height-seam)>1.e-6f || fabsf(c.dx-(low-high))>1.e-6f || c.dz!=0.f;
        vertexCount=0;
        water_face(job,x-!i,Y,Z-1,2,0xffffff,1,0);
        water_face(job,x-!i,Y,Z-1,4,0xffffff,0,0);
        int joined=0;
        for (Uint32 v=0;v<vertexCount;++v) {
            const float *p=vertices[v].p;
            if (p[0]==0.f && p[2]==Z-1.f && p[1]>Y) {
                failed|=joined && edge[i]!=p[1];
                edge[i]=p[1]; ++joined;
            }
        }
        failed|=joined<2;
        int halo=mesh_column_index(x-1,Z-1)*WY;
        job->cells[halo+Y+1]=WATER;
        c=mesh_water_corner(job,x,Y,Z);
        failed|=c.height!=1.f || c.open;
        job->cells[halo+Y+1]=AIR;
    }
    failed|=edge[0]!=edge[1] || fabsf(edge[0]-(Y+seam))>1.e-5f;
    /* A straight shoreline leaves the two wet-side corners free and the
       bank-side corners pinned. The center fan adds no shared-edge vertices. */
    for (int dx=-1;dx<=1;++dx) for (int dz=-1;dz<=1;++dz) {
        int offset=mesh_column_index(dx,Z+dz)*WY;
        job->cells[offset+Y]=dx<0 ? SAND : WATER;
        job->cells[offset+Y+1]=AIR;
    }
    vertexCount=0;
    water_face(job,0,Y,Z,2,0xffffff,1,0);
    failed|=meshOOM || vertexCount!=12;
    for (Uint32 i=0;i<vertexCount;++i) {
        const Vertex *v=&vertices[i];
        int center=i%3==2;
        int marker=(int)v->uv[1];
        float u=v->uv[0]-floorf(v->uv[0]),t=v->uv[1]-marker;
        failed|=marker!=24+64*(center ? 2 : v->p[0]==1.f ? 4 : 0);
        failed|=center && (v->p[0]!=.5f || v->p[2]!=Z+.5f);
        failed|=fabsf(v->p[1]-(Y+high))>1.e-5f || u<=0.f || u>=1.f;
        failed|=t!=(center ? .5f : t<.5f ? .5f/TILE_EDGE : 1.f-.5f/TILE_EDGE);
    }
    free(vertices); vertices=previous; vertexCount=previousCount; vertexCapacity=previousCapacity;
    meshOOM=previousOOM;
    free(job);
    printf("MESH TEST sloped water, negative chunk seam and pinned shoreline failures=%d\n",failed!=0);
    return failed;
}
static unsigned mesh_pane_cover(const TerrainMeshJob *job,int center,const int side[],int y) {
    if ((unsigned)y>=WY) return 0;
    int raw=mesh_raw(job,center,y);
    if (!pane_cell(raw)) return 0;
    const int adjacent[]={mesh_raw(job,side[4],y),mesh_raw(job,side[1],y),
        mesh_raw(job,side[5],y),mesh_raw(job,side[0],y)};
    return pane_parts(pane_connections(raw,adjacent));
}
static ChestJoint mesh_chest_joint(const TerrainMeshJob *job,int x,int y,int z) {
    /* Same reciprocal, unambiguous join rule as the storage model. */
    static const int dx[]={-1,1,0,0},dz[]={0,0,-1,1};
    int found=-1,kind=chest_pair_type(cell_type(mesh_cell(job,x,y,z)));
    for (int d=0;d<4;++d) {
        int type=cell_type(mesh_cell(job,x+dx[d],y,z+dz[d]));
        if (is_chest(type) && chest_pair_type(type)==kind) {
            if (found>=0) return CHEST_ALONE;
            found=d;
        }
    }
    if (found<0) return CHEST_ALONE;
    int qx=x+dx[found],qz=z+dz[found];
    int count=0;
    for (int d=0;d<4;++d) {
        int type=cell_type(mesh_cell(job,qx+dx[d],y,qz+dz[d]));
        count+=is_chest(type) && chest_pair_type(type)==kind;
    }
    return count==1 ? (ChestJoint)found : CHEST_ALONE;
}
static int prepare_mesh_for_pass(TerrainMeshJob *job,const Mesh *mesh,int cx,int cz,int urgent,
                                 int request_missing,int *blocked_on_terrain,int light_only) {
    unsigned long long rev[9];
    mesh_revisions(cx,cz,rev);
    const int shown=mesh->valid && mesh->x==cx && mesh->z==cz;
    const int same_geometry=shown && !memcmp(mesh->revision,rev,sizeof rev);
    if (light_only && !same_geometry) return MESH_REUSED;
    if (same_geometry && !mesh->relight)
        return MESH_REUSED;
    ChunkView chunks[3][3];
    int waiting=0;
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) {
        chunks[i][j]=request_missing ? chunk_view(cx+i-1,cz+j-1) : chunk_peek_view(cx+i-1,cz+j-1);
        waiting|=chunks[i][j].cells==NULL;
    }
    if (waiting) {
        if (blocked_on_terrain) *blocked_on_terrain=1;
        return MESH_WAITING;
    }
    LightView light=block_light_view(cx,cz);
    /* Initial and edited geometry can draw with live skylight and the last
       block-light sample. A toggling emitter must not hold terrain invisible.
       Wait for exact light only when there is no new geometry to show. */
    if (!light.ready && same_geometry) return MESH_WAITING;
    job->relight=!light.ready;
    job->urgent=urgent;
    job->work=!shown ? MESH_FIRST : same_geometry ? MESH_ONLY_LIGHT : MESH_GEOMETRY;
    if (!light.ready) light.levels=block_light_previous(cx,cz);
    job->epoch=mesh_epoch;
    job->cx=cx; job->cz=cz;
    job->dimension=world_dimension();
    memcpy(job->revision,rev,sizeof rev);
    memcpy(job->tops,chunks[1][1].tops,sizeof job->tops);
    memcpy(job->biomes,chunks[1][1].biomes,sizeof job->biomes);
    job->has_high=0;
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) job->has_high|=chunks[i][j].high!=NULL;
    if (job->has_high) memset(job->high,0,sizeof job->high);
    /* A face on the boundary reads its immediate neighbor plus two cells
       for diffuse sky. The same halo covers culling and paired chests. */
    for (int x=-MESH_HALO;x<CHUNK_SIZE+MESH_HALO;++x) {
        int i=x<0 ? 0 : x>=CHUNK_SIZE ? 2 : 1;
        int row=(x+(1-i)*CHUNK_SIZE)*CHUNK_SIZE;
        for (int j=0;j<3;++j) {
            int src=row+(j==0 ? CHUNK_SIZE-MESH_HALO : 0);
            int dst=mesh_column_index(x,j==0 ? -MESH_HALO : j==1 ? 0 : CHUNK_SIZE);
            size_t count=j==1 ? CHUNK_SIZE : MESH_HALO;
            memcpy(job->cells+dst*WY,chunks[i][j].cells+src*WY,count*WY);
            memcpy(job->sky+dst,chunks[i][j].sky_tops+src,count*sizeof *job->sky);
            memcpy(job->cover+dst,chunks[i][j].cover+src,count*sizeof *job->cover);
            const ChunkHigh *high=chunks[i][j].high;
            if (high) for (int s=0;s<CELL_SECTIONS;++s) if (high->planes[s])
                for (int c=0;c<(int)count;++c)
                    memcpy(job->high+(dst+c)*WY+s*CELL_SECTION_HEIGHT,
                        high->planes[s]+(src+c)*CELL_SECTION_HEIGHT,CELL_SECTION_HEIGHT);
        }
    }
    job->has_light=light.levels!=NULL;
    if (job->has_light) memcpy(job->light,light.levels,sizeof job->light);
    memset(job->circuit,0,sizeof job->circuit);
    (void)world_visit_chunk_mesh_details(cx,cz,remember_mesh_detail,job);
    return MESH_QUEUED;
}
static int prepare_mesh_mode(TerrainMeshJob *job,const Mesh *mesh,int cx,int cz,int urgent,
                             int request_missing,int *blocked_on_terrain) {
    return prepare_mesh_for_pass(job,mesh,cx,cz,urgent,request_missing,blocked_on_terrain,0);
}
static int prepare_mesh_with_priority(TerrainMeshJob *job,const Mesh *mesh,int cx,int cz,int urgent) {
    return prepare_mesh_mode(job,mesh,cx,cz,urgent,1,NULL);
}
static int prepare_mesh(TerrainMeshJob *job,const Mesh *mesh,int cx,int cz) {
    return prepare_mesh_with_priority(job,mesh,cx,cz,0);
}

static inline int base_mesh_type(int raw) {
    /* The five bytes immediately after WOOL encode named tree foliage. */
    _Static_assert((int)WOOL<(int)TREE_LEAF_BASE && (int)WOOL<(int)WIRE_BASE,"Base block bytes must be literal");
    return (unsigned)raw<=WOOL ? raw : cell_type(raw);
}
static int seep_mesh_selftest(void) {
    int failed=0;
    for (int raw=SEEP_BED;raw<=SEEP_TILE_BRIGHT;++raw)
        failed|=base_mesh_type(raw)!=raw || light_emission(raw)<=0 ||
            tile_for_block(raw,2)!=SEEP_BED_ATLAS+raw-SEEP_BED;
    const int mold_tops[]={SETTLING_FRAME_ATLAS,LIMESTONE_TILE,LAMINATE_TILE};
    for (int raw=SETTLING_MOLD;raw<=SETTLING_PACKED;++raw)
        failed|=!valid_cell(raw) || !solid(cell_type(raw)) ||
            tile_for_block(raw,2)!=mold_tops[raw-SETTLING_MOLD] || tile_for_block(raw,0)!=SETTLING_FRAME_ATLAS;
    for (int raw=CISTERN_EMPTY;raw<=CISTERN_FULL;++raw) {
        MeshMaterial desc=describe_mesh_material(raw);
        failed|=!valid_cell(raw) || !solid(desc.type) || desc.type!=CISTERN_EMPTY || desc.appearance!=raw ||
            tile_for_block(raw,2)!=(raw==CISTERN_EMPTY ? CISTERN_DRY_ATLAS :
                raw==CISTERN_FULL ? CISTERN_FULL_ATLAS : CISTERN_WET_ATLAS) ||
            tile_for_block(raw,0)!=CISTERN_CASING_ATLAS;
    }
    static const int probe_face[]={4,1,5,0};
    for (int raw=CISTERN_PROBE_BASE;raw<CISTERN_PROBE_END;++raw) {
        MeshMaterial desc=describe_mesh_material(raw);
        int d=cistern_probe_facing(raw);
        failed|=!valid_cell(raw) || !solid(desc.type) || desc.type!=CISTERN_PROBE_BASE || desc.appearance!=raw ||
            tile_for_block(raw,2)!=(cistern_probe_empty(raw) ? CISTERN_DRY_ATLAS : CISTERN_WET_ATLAS) ||
            tile_for_block(raw,probe_face[d])!=PRESSURE_CLOSED_TILE ||
            tile_for_block(raw,probe_face[(d+2)&3])!=CONDUIT_TILE;
    }
    unsigned old=brightness;
    brightness=255;
    vertexCount=meshOOM=0;
    ActorRecord a={{1.f,0.f,1.f},0,ACTOR_SEEP_BEETLE,SEEP_BEETLE_HEALTH,0};
    actor_model(&a,0,0);
    failed|=meshOOM || vertexCount!=22*36;
    for (unsigned i=0;i<vertexCount;++i) {
        const float *v=vertices[i].p;
        failed|=v[0]<.67f || v[0]>1.33f || v[1]<0.f || v[1]>.42f || v[2]<.65f || v[2]>1.34f;
    }
    brightness=old;
    free(vertices); vertices=NULL; vertexCount=vertexCapacity=0;
    printf("MESH TEST seep beetle valves and three ceramic surfaces failures=%d\n",failed!=0);
    return failed!=0;
}
static unsigned mesh_build_mask(const TerrainMeshJob *job,int x,int y,int z,int raw) {
    if (!build_stair(raw)) return build_slab(raw) ? build_mask(raw,NULL) :
        !sculpted_block(cell_type(raw)) && light_opacity(raw)>=16 ? 255u : 0;
    int near[4];
    for (int d=0;d<4;++d) near[d]=mesh_cell(job,x+rail_dx(d),y,z+rail_dz(d));
    return build_mask(raw,near);
}
enum { BAKE_REFERENCE, BAKE_CELL, BAKE_MASK, BAKE_MATERIAL, BAKE_WATER_MASK, BAKE_WATER_SURFACE };
_Static_assert(WY%64==0,"Complete words are required for the vertical mesh mask");
static uint64_t visible_cell_word(const TerrainColumnCover *center,const TerrainColumnCover *west,
    const TerrainColumnCover *east,const TerrainColumnCover *north,const TerrainColumnCover *south,
    int word,int cull_water) {
    const uint64_t dry=center->dry[word],seal=center->seal[word];
    if (!(dry|seal)) return 0;
    uint64_t buried=0,cubes=dry&seal,water=seal&~dry;
    /* Across a bitplane edge, the missing cell below the world is bedrock
       and the missing cell above it is air, as in the scalar face loop. */
    if (cubes) {
        uint64_t above=(cubes>>1) | (word+1<TERRAIN_COVER_WORDS ? terrain_cube_word(center,word+1)<<63 : 0);
        uint64_t below=(cubes<<1) | (word ? terrain_cube_word(center,word-1)>>63 : 1);
        buried=cubes&above&below&terrain_cube_word(west,word)&terrain_cube_word(east,word)&
            terrain_cube_word(north,word)&terrain_cube_word(south,word);
    }
    if (cull_water && water) {
        /* The water face loop never emits an interface against another
           water cell or a full opaque cube. Leave air, lava, snow and
           incomplete silhouettes for per-face processing. */
        uint64_t above=(seal>>1) | (word+1<TERRAIN_COVER_WORDS ? center->seal[word+1]<<63 : 0);
        uint64_t below=(seal<<1) | (word ? center->seal[word-1]>>63 : 1);
        buried|=water&above&below&west->seal[word]&east->seal[word]&north->seal[word]&south->seal[word];
    }
    return (dry|seal)&~buried;
}
static void opaque_geometry_extent(const Vertex *data,Uint32 count,float *bottom,float *top) {
    float min_y=(float)WY,max_y=-1.f;
    for (Uint32 i=0;i<count;++i) {
        float y=data[i].p[1];
        if (y<min_y) min_y=y;
        if (y>max_y) max_y=y;
    }
    *bottom=min_y;
    *top=max_y;
}
static int outside_capture_floor(const Mesh *mesh,float floor_y) {
    /* Match the fragment shader's strict '<'. Leave a small margin for
       interpolation, especially faces that lie directly on the plane. */
    return mesh->opaque_top+.001f<floor_y;
}
static int bake_chunk_mesh_interruptible(TerrainMeshJob *job,int mode,int split_water,const int *cancel) {
    int fast=mode!=BAKE_REFERENCE;
    int cached=mode>=BAKE_MATERIAL;
    const int water_surface_fast=mode>=BAKE_WATER_SURFACE;
    LightView light={job->has_light ? job->light : NULL,1};
    vertexCount = meshOOM = 0;
    water_count=0;
    job->waterPlane=-1.f;
    job->water_bottom=WY;
    job->water_top=0;
    vertex_light_encoding=1;
    Dimension dim=job->dimension;
    int ox=job->cx*CHUNK_SIZE,oz=job->cz*CHUNK_SIZE,highest=0;
    for (int x = 0; x < CHUNK_SIZE; x++) for (int z = 0; z < CHUNK_SIZE; z++) {
        /* A column is the largest non-interruptible unit. The worker owns
           these partial buffers; aborted jobs never reach the GPU. */
        if (cancel && __atomic_load_n(cancel,__ATOMIC_RELAXED)) {
            brightness=255;
            vertex_light_encoding=0;
            return 0;
        }
        int center=mesh_column_index(x,z);
        int side[6];
        int shade_top[6];
        for (int f=0;f<6;++f) {
            int sample=mesh_column_index(x+dirs[f][0],z+dirs[f][2]);
            side[f]=sample;
            shade_top[f]=job->sky[sample];
        }
        int i=x*CHUNK_SIZE+z,top_y=job->tops[i];
        Biome biome=(Biome)job->biomes[i];
        if (top_y > highest) highest = top_y;
        int column=center*WY,west=side[0]*WY,east=side[1]*WY,north=side[4]*WY,south=side[5]*WY;
        const uint8_t *low=job->cells,*high=job->has_high ? job->high : NULL;
        uint64_t keep[TERRAIN_COVER_WORDS];
        if (mode>=BAKE_MASK) for (int w=0;w<TERRAIN_COVER_WORDS;++w)
            keep[w]=visible_cell_word(&job->cover[center],&job->cover[side[0]],&job->cover[side[1]],
                &job->cover[side[4]],&job->cover[side[5]],w,mode>=BAKE_WATER_MASK);
        for (int y = 0; y <= top_y; y++) {
            if (mode>=BAKE_MASK) {
                uint64_t next=keep[y>>6] & (UINT64_MAX<<(y&63));
                if (!next) { y|=63; continue; }
                y=(y&~63)+(int)__builtin_ctzll(next);
                if (y>top_y) break;
            }
            int n=column+y;
            int cell=low[n] | (high ? high[n]<<8 : 0);
            if (mode==BAKE_CELL && cell<256 && terrain_full_cover[cell] && y+1<WY &&
                terrain_full_cover[low[n+1]] && (!y || terrain_full_cover[low[n-1]]) &&
                terrain_full_cover[low[west+y]] && terrain_full_cover[low[east+y]] &&
                terrain_full_cover[low[north+y]] && terrain_full_cover[low[south+y]] &&
                (!high || !(high[n+1] | (y ? high[n-1] : 0) |
                    high[west+y] | high[east+y] | high[north+y] | high[south+y]))) continue;
            MeshMaterial desc={0};
            if (cached) desc=mesh_material(cell);
            int type=cached ? desc.type : fast ? base_mesh_type(cell) : cell_type(cell);
            if (type == AIR) continue;
            if (build_partial(cell)) {
                unsigned other[6];
                for (int f=0;f<6;++f) {
                    int nx=x+dirs[f][0],ny=y+dirs[f][1],nz=z+dirs[f][2];
                    other[f]=ny<0 ? 255u : ny>=WY ? 0 : mesh_build_mask(job,nx,ny,nz,mesh_raw(job,side[f],ny));
                }
                brightness=mesh_face_light_mode(job,light,x,y,z,job->sky[center],0,dim,cached);
                shape_mesh((Vec3){(float)(ox+x),(float)y,(float)(oz+z)},1.f,
                    build_material_block(build_material(cell)),mesh_build_mask(job,x,y,z,cell),other);
                continue;
            }
            /* Common cubes and leaves have no specialised model to dispatch. */
            if (cached ? !(desc.flags&MAT_FAST_CUBE) :
                    !fast || !((cell<256 && terrain_full_cover[cell]) || type==LEAF || type==ICE)) {
                if (sculpted_block(type))
                    brightness=mesh_face_light_mode(job,light,x,y+(solid(type)!=0),z,shade_top[2],
                        cached ? desc.emission : light_emission(cell),dim,cached);
                RailPiece track;
                if (rail_decode(cell,&track)) { draw_track((RailCell){ox+x,y,oz+z},track,job->circuit[i*WY+y]); continue; }
                if (is_chest(type)) { draw_chest(ox+x,y,oz+z,type,mesh_chest_joint(job,x,y,z)); continue; }
                if (ender_chest_cell(cell)) { draw_ender_chest(ox+x,y,oz+z,cell); continue; }
                if (air_outlet_cell(cell)) { draw_air_outlet(ox+x,y,oz+z,cell); continue; }
                if (air_station_cell(cell)) { draw_air_station((Vec3){(float)(ox+x),(float)y,(float)(oz+z)},1.f,cell-AIR_STATION_BASE); continue; }
                if (cell==AIR_PIPE) { draw_air_pipe((Vec3){(float)(ox+x),(float)y,(float)(oz+z)},1.f); continue; }
                if (dungeon_spawner_cell(cell)) {
                    draw_dungeon_spawner((Vec3){(float)(ox+x),(float)y,(float)(oz+z)},cell);
                    continue;
                }
                if (is_bed_cell(cell)) { draw_bed(ox+x,y,oz+z,cell); continue; }
                if (barrier_type(type)) {
                    const int adjacent[]={mesh_raw(job,side[4],y),mesh_raw(job,side[1],y),
                        mesh_raw(job,side[5],y),mesh_raw(job,side[0],y)};
                    if (pane_cell(cell)) {
                        unsigned links=pane_connections(cell,adjacent);
                        draw_pane((Pose){{(float)(ox+x),(float)y,(float)(oz+z)},1,0},1,cell,
                            pane_parts(links),links,mesh_pane_cover(job,center,side,y+1),mesh_pane_cover(job,center,side,y-1));
                    } else draw_barrier(ox+x,y,oz+z,cell,adjacent);
                    continue;
                }
                if (crop_type(type) || type==TALL_GRASS || type==PUMPKIN_STEM) { draw_plant(ox+x,y,oz+z,cell); continue; }
                if (type==COBWEB) { draw_cobweb(ox+x,y,oz+z); continue; }
                if (type==SAPLING) { draw_sapling(ox+x,y,oz+z,cell); continue; }
                if (type==POPPY || type==DANDELION) { draw_flower(ox+x,y,oz+z,type==DANDELION); continue; }
                if (small_mushroom(type)) { draw_mushroom(ox+x,y,oz+z,type==RED_MUSHROOM); continue; }
                if (type==LILY_PAD) {
                    /* Meet the existing surface, not a one-cell raised sheet of water. */
                    draw_lily_pad(ox+x,y-1.f+water_surface(WATER,AIR)+.003f,oz+z,1.f,hash2(ox+x,oz+z));
                    continue;
                }
                if (type==CACTUS || type==COCOA) { draw_dye_plant(ox+x,y,oz+z,cell); continue; }
                if (type==FILTER_MAT) { draw_filter_mat(ox+x,y,oz+z,cell); continue; }
                if (type==BREWING_STAND) { draw_brewing_stand(ox+x,y,oz+z); continue; }
                if (type==ENCHANTING_TABLE) { draw_calibration_table(ox+x,y,oz+z); continue; }
                if (type==ANVIL) { draw_anvil(ox+x,y,oz+z,cell); continue; }
                if (type==SUGAR_CANE) { draw_cane(ox+x,y,oz+z); continue; }
                if (type==REDSTONE_WIRE) {
                    int visual=job->circuit[i*WY+y];
                    draw_wire(ox+x,y,oz+z,cell,(WireShape){visual&15,visual>>4});
                    continue;
                }
                if (type==LEVER) { draw_lever(ox+x,y,oz+z,cell); continue; }
                if (type==REDSTONE_TORCH) { draw_redstone_torch(ox+x,y,oz+z,cell); continue; }
                if (type==REPEATER) { draw_repeater(ox+x,y,oz+z,cell,job->circuit[i*WY+y]); continue; }
                if (type==COMPARATOR) { draw_comparator(ox+x,y,oz+z,cell,job->circuit[i*WY+y]); continue; }
                if (type==PRESSURE_PLATE) { draw_pressure_plate(ox+x,y,oz+z,cell,job->circuit[i*WY+y]); continue; }
                if (type==BUTTON) { draw_button(ox+x,y,oz+z,cell,job->circuit[i*WY+y]); continue; }
                if (type==TRIPWIRE || type==TRIPWIRE_HOOK) {
                    int neighbours[4];
                    for (int d=0;d<4;++d) neighbours[d]=mesh_cell(job,x+rail_dx(d),y,z+rail_dz(d));
                    int grounded=y>0 && tripwire_floor(mesh_cell(job,x,y-1,z));
                    draw_tripwire(ox+x,y,oz+z,cell,grounded,tripwire_links(neighbours),job->circuit[i*WY+y]);
                    continue;
                }
                if (type==HOPPER) { draw_hopper(ox+x,y,oz+z,cell); continue; }
                if (type==GLASS) {
                    for (int f=0;f<6;++f) {
                        int yy=y+dirs[f][1];
                        int raw=yy<0 ? BEDROCK : yy==WY ? AIR : mesh_raw(job,side[f],yy);
                        int other=fast ? base_mesh_type(raw) : cell_type(raw);
                        if (!solid(other)) glass_face((Vec3){(float)(ox+x),(float)y,(float)(oz+z)},1.f,f,cell);
                    }
                    continue;
                }
                if (type==TORCH) { draw_torch(ox+x,y,oz+z); continue; }
                if (type==FIRE_CELL) { draw_flame(ox+x+.5f,y+.015f,oz+z+.5f,1.f); continue; }
                if (piston_cell(cell)) {
                    brightness=mesh_face_light_mode(job,light,x,y+1,z,shade_top[2],0,dim,cached);
                    draw_piston(ox+x,y,oz+z,cell);
                    continue;
                }
            }
            int above=y+1<WY ? mesh_raw(job,center,y+1) : AIR;
            int appearance=cached ? desc.appearance : type==SNOW_LAYER ? SNOW :
                lamp_cell(cell)||ejector_cell(cell)||pumpkin_cell(cell)||cistern_cell(cell)||heat_bench_cell(cell)||
                air_relay_cell(cell) || cistern_probe_cell(cell) ? cell : type;
            float top = type == SNOW_LAYER ? .125f : type == FARMLAND ? FARMLAND_HEIGHT :
                type == LAVA ? lava_surface(cell,above) : 1.f;
            int emission=-1; /* Don't sample lighting for completely buried blocks. */
            for (int f = 0; f < 6; f++) {
                int yy = y + dirs[f][1];
                if (water_surface_fast && type==WATER && (yy<0 ||
                    (yy<WY && (job->cover[side[f]].seal[yy>>6]&(UINT64_C(1)<<(yy&63)))))) continue;
                int neighbor=yy<0 ? BEDROCK : yy==WY ? AIR : mesh_raw(job,side[f],yy);
                /* Lava's boundary face is deliberate even against opaque stone. */
                MeshMaterial adjacent={0};
                if (cached) adjacent=mesh_material(neighbor);
                if (fast && type!=LAVA && (cached ? adjacent.flags&MAT_COVER :
                    neighbor<256 && terrain_full_cover[neighbor])) continue;
                int other=cached ? adjacent.type : fast ? base_mesh_type(neighbor) : cell_type(neighbor);
                if (other==SNOW_LAYER) {
                    if (type==SNOW_LAYER && f!=2 && f!=3) continue;
                    if (f==2) continue; /* The layer already covers this top. */
                    other=AIR;
                }
                if (other!=AIR)
                    other=cached ? adjacent.neighbor : mesh_neighbor_type(neighbor,other);
                if (type==WATER) {
                    /* Same-fluid edges already meet, including different
                       levels and falling columns. Only draw the boundary. */
                    if (other!=AIR && other!=LAVA) continue;
                    unsigned tint=biome_tint(WATER,f,biome);
                    if (emission<0) emission=cached ? desc.emission : light_emission(cell);
                    brightness=mesh_face_light_mode(job,light,x+dirs[f][0],yy,z+dirs[f][2],shade_top[f],emission,dim,cached);
                    if (split_water && dim==OVERWORLD) {
                        Vertex *ordinary=vertices;
                        Uint32 count=vertexCount,capacity=vertexCapacity;
                        vertices=water_vertices; vertexCount=water_count; vertexCapacity=water_capacity;
                        water_face(job,x,y,z,f,tint,1,water_surface_fast);
                        if (vertexCount>water_count) {
                            /* Corners and the optional center are within
                               the block's undisplaced vertical interval. */
                            if (y<job->water_bottom) job->water_bottom=(uint16_t)y;
                            if (y+1>job->water_top) job->water_top=(uint16_t)(y+1);
                        }
                        water_vertices=vertices; water_count=vertexCount; water_capacity=vertexCapacity;
                        vertices=ordinary; vertexCount=count; vertexCapacity=capacity;
                        if ((y==OCEAN_LEVEL || job->waterPlane<0.f) && cell==WATER && above==AIR)
                            job->waterPlane=y+water_surface(WATER,AIR);
                    } else water_face(job,x,y,z,f,tint,0,water_surface_fast);
                    continue;
                }
                float bottom = 0;
                if (other != AIR) {
                    if ((type == LAVA || other == LAVA) && type != other) {
                        if (emission<0) emission=cached ? desc.emission : light_emission(cell);
                        brightness=mesh_face_light_mode(job,light,x+dirs[f][0],yy,z+dirs[f][2],shade_top[f],emission,dim,cached);
                        face(ox+x,y,oz+z,1,appearance,f,0,top,biome_tint(type,f,biome));
                        continue;
                    }
                    if (type==LAVA && other==LAVA && f!=2 && f!=3) {
                        int neighbor_above=y+1<WY ? mesh_raw(job,side[f],y+1) : AIR;
                        bottom=lava_surface(neighbor,neighbor_above);
                        if (bottom >= top) continue;
                    } else if (other==FARMLAND && f!=2 && f!=3 && top>FARMLAND_HEIGHT)
                        bottom=FARMLAND_HEIGHT;
                    else if (other!=WATER) continue;
                }
                unsigned tint=type==FARMLAND && f==2 ? (cell>FARM_BASE ? 0xa18274 : 0xe6bf9f) : biome_tint(type,f,biome);
                if (type==LEAF && leaf_species(cell)!=TREE_OAK) {
                    static const unsigned greens[]={0xffffff,0xd3ed9e,0x8cb9ba,0xa8e769,0xe2d795,0x91b267};
                    tint=greens[leaf_species(cell)];
                }
                if (emission<0) emission=cached ? desc.emission : light_emission(cell);
                brightness=mesh_face_light_mode(job,light,x+dirs[f][0],yy,z+dirs[f][2],shade_top[f],emission,dim,cached);
                face(ox + x, y, oz + z, 1, appearance, f, bottom, top, tint);
            }
        }
    }
    brightness=255;
    vertex_light_encoding=0;
    job->highest=highest;
    job->data=vertices;
    job->count=vertexCount;
    job->water=water_vertices;
    job->waterCount=water_count;
    job->failed=meshOOM;
    /* Above-water captures only consume this stream. Local water vertices
       must not inflate its bound; the water pass has its own shader. */
    job->opaque_bottom=0.f;
    job->opaque_top=highest+1.f;
    if (split_water && dim==OVERWORLD && !meshOOM)
        opaque_geometry_extent(vertices,vertexCount,&job->opaque_bottom,&job->opaque_top);
    return 1;
}
static void bake_chunk_mesh_mode(TerrainMeshJob *job,int mode,int split_water) {
    (void)bake_chunk_mesh_interruptible(job,mode,split_water,NULL);
}
static void bake_chunk_mesh(TerrainMeshJob *job) { bake_chunk_mesh_mode(job,BAKE_WATER_SURFACE,dev!=NULL); }

static int ring_index(int v) {
    int i = v % MESH_SPAN;
    return i < 0 ? i + MESH_SPAN : i;
}

void renderer_sample_forward_chunks(void) {
    if (!mesh_sample_active) return;
    /* A fixed three-wide lane from two to seven chunks in the direction
       of view. This is coverage sampling, not the full visible frustum. */
    const ChunkOffset f=view_directions[view_octant(yaw)];
    for (int depth=2;depth<=7;++depth) for (int cross=-1;cross<=1;++cross) {
        int x=meshX+depth*f.x+cross*f.z,z=meshZ+depth*f.z-cross*f.x;
        if (mesh_ring(x-meshX,z-meshZ)>RENDER_CHUNKS) continue;
        int top;
        ++mesh_sample_front_slots;
        mesh_sample_front_ready+=world_resident_top(x*CHUNK_SIZE,z*CHUNK_SIZE,&top);
        const Mesh *mesh=&terrain_mesh[ring_index(x)][ring_index(z)];
        mesh_sample_front_drawn+=mesh->valid && mesh->x==x && mesh->z==z;
    }
}

static unsigned long long player_edit_floor(int cx,int cz) {
    const MeshEditFence *f=&edit_fences[ring_index(cx)][ring_index(cz)];
    return f->x==cx && f->z==cz ? f->floor : 0;
}
void renderer_prioritize_edits_since(unsigned long long before) {
    if (world_edit_version()==before) return;
    int cx=(int)floorf(px/CHUNK_SIZE),cz=(int)floorf(pz/CHUNK_SIZE);
    for (int dx=-EDIT_RINGS;dx<=EDIT_RINGS;++dx) for (int dz=-EDIT_RINGS;dz<=EDIT_RINGS;++dz) {
        int x=cx+dx,z=cz+dz;
        unsigned long long rev=chunk_revision(x,z);
        if (rev>before) {
            edit_fences[ring_index(x)][ring_index(z)]=(MeshEditFence){x,z,rev};
            edit_hint=1;
        }
    }
}

static void *mesh_work(void *arg) {
    MeshWorker *worker=(MeshWorker *)arg;
    pthread_mutex_lock(&mesh_lock);
    while (!mesh_stop) {
        if (worker->state!=MESH_PENDING) { pthread_cond_wait(&mesh_changed,&mesh_lock); continue; }
        worker->state=MESH_BAKING;
        const unsigned sample=mesh_sample_active ? mesh_sample_generation : 0;
        pthread_mutex_unlock(&mesh_lock);
        const Uint64 started=sample ? SDL_GetTicksNS() : 0;
        const int baked=bake_chunk_mesh_interruptible(worker->job,BAKE_WATER_SURFACE,dev!=NULL,&worker->cancel);
        const Uint64 duration=sample ? SDL_GetTicksNS()-started : 0;
        pthread_mutex_lock(&mesh_lock);
        const int complete=baked && !__atomic_load_n(&worker->cancel,__ATOMIC_RELAXED);
        if (complete && sample && mesh_sample_active && sample==mesh_sample_generation) {
            ++mesh_sample_bakes;
            mesh_sample_bake_ns+=duration;
            if ((unsigned)worker->job->work<MESH_WORK_CLASSES) {
                ++mesh_sample_work[worker->job->work];
                mesh_sample_work_ns[worker->job->work]+=duration;
            }
        }
        worker->state=complete ? MESH_READY : MESH_IDLE;
    }
    pthread_mutex_unlock(&mesh_lock);
    free(vertices); vertices=NULL; vertexCapacity=0;
    free(water_vertices); water_vertices=NULL; water_capacity=0;
    return NULL;
}
static int start_mesh_workers(int requested) {
    if (mesh_running) return mesh_running;
    init_atlas_coordinates();
    pthread_once(&mesh_material_once,init_mesh_materials);
    mesh_stop=0;
    for (int i=0;i<requested;++i) {
        MeshWorker *worker=&mesh_workers[i];
        worker->state=MESH_IDLE;
        /* Keep the first worker if the host cannot allocate the second. */
        if (pthread_create(&worker->thread,NULL,mesh_work,worker)!=0) break;
        ++mesh_running;
    }
    return mesh_running;
}
int renderer_start_meshing(void) { return start_mesh_workers(MESH_WORKERS)>0; }
void renderer_stop_meshing(void) {
    if (!mesh_running) return;
    pthread_mutex_lock(&mesh_lock);
    mesh_stop=1;
    pthread_cond_broadcast(&mesh_changed);
    pthread_mutex_unlock(&mesh_lock);
    for (int i=0;i<mesh_running;++i) {
        MeshWorker *worker=&mesh_workers[i];
        pthread_join(worker->thread,NULL);
        worker->job->data=NULL;
        worker->state=MESH_IDLE;
    }
    mesh_running=0;
    free(vertices); vertices=NULL; vertexCapacity=0;
}
static int mesh_job_current(const TerrainMeshJob *job) {
    if (mesh_epoch!=job->epoch || abs(job->cx-meshX)>RENDER_CHUNKS || abs(job->cz-meshZ)>RENDER_CHUNKS)
        return 0;
    unsigned long long revision[9];
    mesh_revisions(job->cx,job->cz,revision);
    return !memcmp(revision,job->revision,sizeof revision);
}
static int mesh_job_publishable(const TerrainMeshJob *job,const Mesh *shown) {
    if (mesh_epoch!=job->epoch || abs(job->cx-meshX)>RENDER_CHUNKS || abs(job->cz-meshZ)>RENDER_CHUNKS)
        return 0;
    const int same=shown->valid && shown->x==job->cx && shown->z==job->cz;
    for (int i=0;i<9;++i) {
        if (same && job->revision[i]<shown->revision[i]) return 0;
        if (job->revision[i]<player_edit_floor(job->cx+mesh_neighbors[i][0],job->cz+mesh_neighbors[i][1]))
            return 0;
    }
    /* Water, fire or circuits may edit a chunk faster than a worker bakes
       it. Accept this immutable snapshot if it moves forward; the next
       pass will pick up remaining revisions without starving presentation. */
    return 1;
}
/* Only the render thread can submit a job or release a completed buffer.
   A worker does not reuse its TLS vertices until the next submission. */
static int claim_mesh(MeshWorker *worker) {
    if (pthread_mutex_trylock(&mesh_lock)!=0) return MESH_BAKING;
    int state=worker->state;
    if (state==MESH_READY) worker->state=MESH_IDLE;
    pthread_mutex_unlock(&mesh_lock);
    return state;
}
static int dispatch_mesh(MeshWorker *worker) {
    if (pthread_mutex_trylock(&mesh_lock)!=0) return 0;
    if (worker->state!=MESH_IDLE) { pthread_mutex_unlock(&mesh_lock); return 0; }
    __atomic_store_n(&worker->cancel,0,__ATOMIC_RELAXED);
    worker->sequence=++mesh_sequence;
    worker->state=MESH_PENDING;
    pthread_cond_broadcast(&mesh_changed);
    pthread_mutex_unlock(&mesh_lock);
    return 1;
}

static int edited_mesh_light_selftest(void) {
    const int cx=2,cz=1,x=cx*CHUNK_SIZE+5,z=cz*CHUNK_SIZE+5,y=151;
    int failed=0,steady=0;
    if (!world_begin_generated(29342,GENERATOR_BOOKS) || !light_start()) return 1;
    if (!set_block(x,y-1,z,STONE) || !set_block(x,y,z,TORCH)) { light_stop(); return 1; }
    const struct timespec delay={0,1000000};
    for (int n=0;n<3000 && !steady;++n) {
        LightView v=block_light_view(cx,cz);
        steady=v.ready && block_light_level(v,5,y,5)==14;
        if (!steady) nanosleep(&delay,NULL);
    }
    Mesh previous={.x=cx,.z=cz,.valid=1};
    mesh_revisions(cx,cz,previous.revision);
    failed=!steady || !set_block(x+1,y,z,STONE);
    if (!failed) {
        /* The current revision has never gone to the light worker. The
           first frame queues current geometry using the old light sample. */
        failed=prepare_mesh_with_priority(&mesh_job,&previous,cx,cz,1)!=MESH_QUEUED ||
            !mesh_job.relight || !mesh_job.has_light || mesh_cell(&mesh_job,6,y,5)!=STONE;
    }
    int upgraded=0;
    if (!failed) {
        memcpy(previous.revision,mesh_job.revision,sizeof previous.revision);
        previous.relight=1;
        /* Do not publish the same provisional geometry repeatedly. */
        for (int n=0;n<3000 && !upgraded;++n) {
            int result=prepare_mesh_with_priority(&mesh_job,&previous,cx,cz,1);
            if (result==MESH_QUEUED) {
                const LightView exact={mesh_job.has_light ? mesh_job.light : NULL,1};
                upgraded=!mesh_job.relight && !block_light_level(exact,6,y,5);
                failed|=!upgraded;
                break;
            }
            if (result!=MESH_WAITING) { failed=1; break; }
            nanosleep(&delay,NULL);
        }
        previous.relight=0;
        failed|=!upgraded || prepare_mesh_with_priority(&mesh_job,&previous,cx,cz,1)!=MESH_REUSED;
    }
    light_stop();
    failed|=block_light_previous(cx,cz)!=NULL;
    printf("MESH TEST edited geometry before pending light, exact relight=%d failures=%d\n",upgraded,failed!=0);
    return failed;
}

static int removed_sky_occluder_mesh_selftest(void) {
    const int cx=0,cz=-1,y=4,z=11;
    if (!world_begin_options(100,GENERATOR_CURRENT,WORLD_SUPERFLAT,1)) return 1;
    const BlockEdit placed[]={{0,y,-5,STONE},{0,y,-4,WOOD},{0,y,-3,CRAFTING_TABLE}};
    Mesh previous={.x=cx,.z=cz};
    if (!world_set_blocks(placed,3) || prepare_mesh_with_priority(&mesh_job,&previous,cx,cz,1)!=MESH_QUEUED)
        return 1;
    const LightView clear={NULL,1};
    const unsigned occluded=lit_face(clear,0,y,z+1,mesh_job.sky[mesh_column_index(0,z+1)],0,OVERWORLD);
    previous.valid=1;
    memcpy(previous.revision,mesh_job.revision,sizeof previous.revision);
    const BlockEdit removed[]={{0,y,-4,AIR},{0,y,-3,AIR}};
    if (!world_set_blocks(removed,2) ||
        prepare_mesh_with_priority(&mesh_job,&previous,cx,cz,1)!=MESH_QUEUED) return 1;
    const int new_sky=mesh_job.sky[mesh_column_index(0,z+1)];
    const unsigned exposed=lit_face(clear,0,y,z+1,new_sky,0,OVERWORLD);
    const int failed=occluded!=0 || exposed!=16 || new_sky!=SUPERFLAT_SURFACE ||
        mesh_job.relight || mesh_job.has_light || mesh_cell(&mesh_job,0,y,z)!=STONE ||
        mesh_cell(&mesh_job,0,y,z+1)!=AIR;
    printf("MESH TEST live skylight after top removal: %u -> %u, sky=%d, failures=%d\n",
        occluded,exposed,new_sky,failed);
    return failed;
}

static int diffuse_sky_mesh_selftest(void) {
    /* The receiver is already past the west edge of this mesh. The opening
       must still be visible two cells further into the neighbor's chunk. */
    const int x=-1,y=6,z=8;
    for (int i=0;i<MESH_EDGE*MESH_EDGE;++i) mesh_job.sky[i]=y+2;
    memset(mesh_job.cells,0,sizeof mesh_job.cells);
    mesh_job.has_high=0;
    const LightView clear={NULL,1};
    int col=mesh_column_index(x-1,z),opening=mesh_column_index(x-2,z);
    unsigned sealed=mesh_face_light(&mesh_job,clear,x,y,z,y+2,0,OVERWORLD);
    mesh_job.sky[opening]=y-1;
    unsigned far=mesh_face_light(&mesh_job,clear,x,y,z,y+2,0,OVERWORLD);
    mesh_job.cells[col*WY+y]=STONE;
    unsigned blocked=mesh_face_light(&mesh_job,clear,x,y,z,y+2,0,OVERWORLD);
    mesh_job.cells[col*WY+y]=AIR;
    mesh_job.sky[col]=y-1;
    unsigned near=mesh_face_light(&mesh_job,clear,x,y,z,y+2,0,OVERWORLD);
    unsigned direct=mesh_face_light(&mesh_job,clear,x,y,z,y-1,0,OVERWORLD);
    int failed=sealed!=0 || far!=48 || blocked!=0 || near!=32 || direct!=16;
    printf("MESH TEST diffuse skylight across chunk edge and opaque wall failures=%d\n",failed);
    return failed;
}

/* Count the vertices on one face of one ground or wall block. A decorative
   cap inside the adjacent cell cannot satisfy the full-square assertion. */
static unsigned support_face_vertices(const TerrainMeshJob *job,int axis,int plane,int along,int z) {
    unsigned count=0, corners_seen=0;
    for (Uint32 i=0;i+2<job->count;i+=3) {
        unsigned corners=0;
        for (unsigned j=0;j<3;++j) {
            const float *p=job->data[i+j].p;
            if (p[axis]!=plane || (p[1-axis]!=along && p[1-axis]!=along+1) ||
                (p[2]!=z && p[2]!=z+1)) break;
            corners|=1u<<((p[1-axis]==along+1)+2*(p[2]==z+1));
        }
        if (__builtin_popcount(corners)!=3) continue;
        corners_seen|=corners;
        count+=3;
    }
    /* Neighbor faces can share edge vertices; all four corners must occur. */
    return corners_seen==15 ? count : 0;
}

static int mushroom_support_mesh_selftest(void) {
    const int y=170,z=6;
    const int x[2]={5,CHUNK_SIZE-1};
    if (!world_begin_options(100,GENERATOR_CURRENT,WORLD_SUPERFLAT,1)) return 1;
    const BlockEdit placed[]={{x[0],y-1,z,GRASS},{x[0],y,z,RED_MUSHROOM},{x[0]+1,y,z,STONE},
                             {x[1],y-1,z,GRASS},{x[1],y,z,BROWN_MUSHROOM},{x[1]+1,y,z,STONE}};
    if (!world_set_blocks(placed,sizeof placed/sizeof *placed)) return 1;
    unsigned faces=0;
    for (int side=0;side<2;++side) {
        Mesh empty={0};
        if (prepare_mesh(&mesh_job,&empty,side,0)!=MESH_QUEUED) break;
        bake_chunk_mesh(&mesh_job);
        if (mesh_job.failed) break;
        /* Three of the faces belong to the near chunk, one to the far chunk. */
        unsigned current[4]={support_face_vertices(&mesh_job,1,y,x[0],z),
            support_face_vertices(&mesh_job,0,x[0]+1,y,z),support_face_vertices(&mesh_job,1,y,x[1],z),
            support_face_vertices(&mesh_job,0,x[1]+1,y,z)};
        unsigned found_models=0;
        if (!side) for (Uint32 i=0;i<mesh_job.count;++i) {
            const float *p=mesh_job.data[i].p;
            if (p[1]>y && p[1]<y+.43f && p[2]>z && p[2]<z+1) for (int k=0;k<2;++k)
                if (p[0]>x[k] && p[0]<x[k]+1) found_models|=1u<<k;
        }
        for (int i=0;i<4;++i) {
            /* Only look for the wall across the boundary in its owning chunk. */
            if ((i==3)!=(side!=0)) continue;
            if (current[i]==6 && (side || found_models==3)) ++faces;
        }
    }
    free(vertices); vertices=NULL; vertexCount=vertexCapacity=0;
    printf("MESH TEST mushroom support grass tops and adjacent walls faces=%u/4 failures=%d\n",faces,faces!=4);
    return faces!=4;
}

static int buried_cube_selftest(void) {
    int failed=0,checked=0;
    /* Compare real generated chunks, including caves and liquid faces, with
       the prior unshortened traversal. Use the same light and epoch. */
    for (int d=0;d<DIMENSION_COUNT && !failed;++d) {
        if (!world_begin_generated(992382978,GENERATOR_CURRENT) ||
            !world_select_dimension((Dimension)d)) { failed=1; break; }
        for (int k=0;k<3 && !failed;++k) {
            const int cx=k-1,cz=1-k;
            Mesh empty={0};
            if (k==0) {
                /* A wide, visually non-opaque button has low byte STONE.
                   Keep every neighbour face exposed as before. */
                const int x=cx*CHUNK_SIZE+7,z=cz*CHUNK_SIZE+7;
                const int slabs[]={10,63,64,127,128,191,192,254,255};
                for (int dx=-1;dx<=1;++dx) for (int dz=-1;dz<=1;++dz)
                    for (unsigned s=0;s<sizeof slabs/sizeof *slabs;++s)
                        for (int dy=-1;dy<=1 && slabs[s]+dy<WY;++dy)
                            failed|=!set_block(x+dx,slabs[s]+dy,z+dz,STONE);
                failed|=!world_restore_edit(x,64,z,BUTTON_BASE+STONE) ||
                    !world_restore_edit(x+1,128,z,GLASS) || !world_restore_edit(x-1,255,z,AIR);
            }
            if (failed || prepare_mesh(&mesh_job,&empty,cx,cz)!=MESH_QUEUED) { failed=1; break; }
            bake_chunk_mesh_mode(&mesh_job,BAKE_REFERENCE,0);
            size_t len=(size_t)mesh_job.count*sizeof(Vertex);
            int highest=mesh_job.highest;
            if (mesh_job.failed) { failed=1; break; }
            Vertex *original=len ? (Vertex *)malloc(len) : NULL;
            if (len && !original) { failed=1; break; }
            if (len) memcpy(original,mesh_job.data,len);
            for (int m=BAKE_CELL;m<=BAKE_WATER_SURFACE && !failed;++m) {
                bake_chunk_mesh_mode(&mesh_job,m,0);
                failed=mesh_job.failed || highest!=mesh_job.highest || len!=(size_t)mesh_job.count*sizeof(Vertex) ||
                    (len && memcmp(original,mesh_job.data,len));
            }
            free(original); ++checked;
        }
    }
    free(vertices); vertices=NULL; vertexCount=vertexCapacity=0;
    printf("MESH TEST opaque masks and scalar fast path, exact mesh comparison chunks=%d failures=%d\n",checked,failed!=0);
    return failed;
}
static int water_hull_selftest(void) {
    FlatLayout layout;
    /* The water crosses the 63/64 bitplane seam. The two compared
       chunks also share a world-coordinate seam at x=0. */
    if (flat_parse("bedrock,20*stone,46*water | coast",&layout) ||
        !world_begin_layout(10403,GENERATOR_CURRENT,WORLD_SUPERFLAT,1,&layout)) return 1;
    const BlockEdit cuts[]={
        {-1,62,34,AIR},{0,62,34,AIR},{-1,63,34,FLOW_BASE+4},{0,63,34,FLOW_BASE+1},
        {-2,63,34,SNOW_LAYER},{-1,64,35,GLASS},{0,64,35,LAVA_FLOW_BASE+5},
        {-1,65,35,LAVA},{0,66,34,AIR},{0,67,34,STONE},{-1,67,35,SAPLING_BASE},
        {1,WY-1,36,WATER},{0,WY-1,36,WATER},{-1,WY-1,36,WATER}
    };
    for (size_t i=0;i<sizeof cuts/sizeof *cuts;++i)
        if (!world_restore_edit(cuts[i].x,cuts[i].y,cuts[i].z,cuts[i].cell)) {
            printf("MESH TEST water parity fixture edit %zu (raw=%d) could not be installed\n",i,cuts[i].cell);
            return 1;
        }
    int failed=0,compared=0;
    for (int cx=-1;cx<=0 && !failed;++cx) {
        Mesh shown={0};
        if (prepare_mesh(&mesh_job,&shown,cx,2)!=MESH_QUEUED) { failed=1; break; }
        bake_chunk_mesh_mode(&mesh_job,BAKE_REFERENCE,1);
        const size_t ordinary=(size_t)mesh_job.count*sizeof(Vertex);
        const size_t water=(size_t)mesh_job.waterCount*sizeof(Vertex);
        const int highest=mesh_job.highest;
        const float plane=mesh_job.waterPlane;
        const uint16_t bottom=mesh_job.water_bottom,top=mesh_job.water_top;
        if (mesh_job.failed || !water) { failed=1; break; }
        /* The production split path must keep both vertex streams exactly
           unchanged, including packed shore flags and water-face IDs. */
        unsigned char *reference=(unsigned char *)malloc(ordinary+water);
        if (!reference) { failed=1; break; }
        if (ordinary) memcpy(reference,mesh_job.data,ordinary);
        memcpy(reference+ordinary,mesh_job.water,water);
        for (int mode=BAKE_WATER_MASK;mode<=BAKE_WATER_SURFACE && !failed;++mode) {
            bake_chunk_mesh_mode(&mesh_job,mode,1);
            failed=mesh_job.failed || highest!=mesh_job.highest || plane!=mesh_job.waterPlane ||
                bottom!=mesh_job.water_bottom || top!=mesh_job.water_top ||
                ordinary!=(size_t)mesh_job.count*sizeof(Vertex) || water!=(size_t)mesh_job.waterCount*sizeof(Vertex) ||
                (ordinary && memcmp(reference,mesh_job.data,ordinary)) || memcmp(reference+ordinary,mesh_job.water,water);
        }
        const Vertex *v=mesh_job.water;
        for (Uint32 j=0;j<mesh_job.waterCount;++j)
            failed|=v[j].p[1]<bottom || v[j].p[1]>top || v[j].p[0]<cx*CHUNK_SIZE ||
                v[j].p[0]>(cx+1)*CHUNK_SIZE || v[j].p[2]<2*CHUNK_SIZE || v[j].p[2]>3*CHUNK_SIZE;
        if (!failed) ++compared;
        free(reference);
    }
    free(vertices); vertices=NULL; vertexCount=vertexCapacity=0;
    free(water_vertices); water_vertices=NULL; water_count=water_capacity=0;
    printf("MESH TEST sealed water, packed shore and ordinary parity chunks=%d failures=%d\n",compared,failed);
    return failed;
}
static int capture_floor_selftest(void) {
    FlatLayout layout;
    if (flat_parse("bedrock,7*stone,46*water | coast",&layout) ||
        !world_begin_layout(86102,GENERATOR_CURRENT,WORLD_SUPERFLAT,1,&layout)) return 1;
    const float plane=40.5f;
    Mesh shown={0};
    /* Stay beyond the flat-water spawn platform near the origin. */
    if (prepare_mesh(&mesh_job,&shown,-2,-1)!=MESH_QUEUED) return 1;
    bake_chunk_mesh_mode(&mesh_job,BAKE_WATER_MASK,1);
    shown.opaque_top=mesh_job.opaque_top;
    const float deep_top=shown.opaque_top;
    const unsigned deep_opaque=mesh_job.count,deep_liquid=mesh_job.waterCount;
    float changed_top[2]={-1,-1};
    int failed=mesh_job.failed || !mesh_job.waterCount || !mesh_job.count || mesh_job.highest<plane ||
        shown.opaque_top!=8.f || !outside_capture_floor(&shown,plane) ||
        outside_capture_floor(&shown,8.f) || outside_capture_floor(&shown,8.0005f);
    for (Uint32 i=0;i<mesh_job.count;++i) failed|=mesh_job.data[i].p[1]>=plane;
    /* Keep a whole chunk if a scene vertex reaches above the same plane.
       Remove it again to check the new bound replaces the baked old one. */
    for (int placed=1;placed>=0;--placed) {
        if (!world_restore_edit(-20,40,-6,placed ? GLASS : WATER) ||
            prepare_mesh(&mesh_job,&shown,-2,-1)!=MESH_QUEUED) { failed=1; break; }
        bake_chunk_mesh_mode(&mesh_job,BAKE_WATER_MASK,1);
        shown.opaque_top=mesh_job.opaque_top;
        changed_top[placed]=shown.opaque_top;
        failed|=mesh_job.failed || shown.opaque_top!=(placed ? 41.f : 8.f) ||
            outside_capture_floor(&shown,plane)==placed;
    }
    free(vertices); vertices=NULL; vertexCount=vertexCapacity=0;
    free(water_vertices); water_vertices=NULL; water_count=water_capacity=0;
    printf("MESH TEST capture rejects submerged geometry, keeps intersecting glass: deep=%.2f (%u+%u), glass=%.2f, removed=%.2f failures=%d\n",
        deep_top,deep_opaque,deep_liquid,changed_top[1],changed_top[0],failed);
    return failed;
}
static int shared_vertex_selftest(void) {
    init_atlas_coordinates();
    const int types[]={STONE,SNOW_LAYER,WATER,LAVA,REDSTONE_WIRE,PRESSURE_LIFT,SINTERED_TILE};
    int failures=0,compared=0;
    for (unsigned i=0;i<sizeof types/sizeof *types;++i) for (int side=0;side<6;++side) {
        float lo=i%2 ? .125f : 0.f,hi=i%2 ? .9375f : 1.f;
        const unsigned tint=i%2 ? 0x7f9263 : 0xe0c0a0;
        vertexCount=meshOOM=0; brightness=i%2 ? 53 : 218;
        reference_face(-4295.5f,181.25f,304.03f,.825f,types[i],side,lo,hi,tint);
        face(-4295.5f,181.25f,304.03f,.825f,types[i],side,lo,hi,tint);
        if (meshOOM || vertexCount!=12 || memcmp(vertices,vertices+6,6*sizeof *vertices)) ++failures;
        ++compared;
    }
    /* Include every atlas column, not only the tiles of these blocks. */
    const volatile float width=ATLAS_UV_WIDTH;
    int atlas_failures=0;
    for (int tile=0;tile<ATLAS_TILES;++tile) for (int i=0;i<TILE_EDGE;++i) {
        const float reference=(tile*TILE_EDGE+i+.5f)/width;
        atlas_failures+=atlas_u(tile,i)!=reference;
    }
    free(vertices); vertices=NULL; vertexCount=vertexCapacity=0; brightness=255;
    printf("MESH TEST shared vertices exact comparison faces=%d failures=%d\n",compared,failures);
    printf("MESH TEST atlas texel centers columns=%d failures=%d\n",ATLAS_TILES*TILE_EDGE,atlas_failures);
    return failures || atlas_failures;
}

static int view_order_selftest(void) {
    int failures=0;
    for (int flying=0;flying<=1;++flying) for (unsigned d=0;d<VIEW_DIRECTIONS;++d) {
        const float angle=(float)d*.785398163f;
        failures+=view_octant(angle)!=d || view_octant(angle-6.283185307f)!=d;
        const ChunkOffset *order=view_order(d,flying);
        unsigned char seen[MESH_SPAN][MESH_SPAN]={0};
        int last_tier=-1,last_ring=-1,front=-1,back=-1;
        const ChunkOffset f=view_directions[d];
        const int immediate=flying ? 1 : EDIT_RINGS, ahead=flying ? FLIGHT_PREFETCH_DEPTH : 4;
        for (int i=0;i<MESH_SPAN*MESH_SPAN;++i) {
            const int x=order[i].x,z=order[i].z,ring=mesh_ring(x,z);
            if (ring>RENDER_CHUNKS) { ++failures; continue; }
            failures+=seen[x+RENDER_CHUNKS][z+RENDER_CHUNKS]++!=0;
            const int tier=stream_view_tier(x,z,ring,f,flying);
            failures+=tier<last_tier || (tier==last_tier && ring<last_ring);
            failures+=(i<(2*immediate+1)*(2*immediate+1))!=(ring<=immediate);
            if (!flying) failures+=tier<ring || tier>ring+VIEW_RING_BIAS;
            last_tier=tier; last_ring=ring;
            if (x==f.x*ahead && z==f.z*ahead) front=i;
            if (x==-f.x*3 && z==-f.z*3) back=i;
        }
        failures+=front<0 || back<0 || front>=back;
    }
    printf("MESH TEST bounded view priority directions=%d chunks=%d ground+flight failures=%d\n",
        VIEW_DIRECTIONS,MESH_SPAN*MESH_SPAN,failures);
    return failures!=0;
}

static int mesh_halo_demand_selftest(void) {
    const int cx=5,cz=-1;
    const struct timespec poll={0,2000000};
    if (!world_begin_options(4321,GENERATOR_CURRENT,WORLD_SUPERFLAT,1)) return 1;
    if (!world_stream_start()) { world_stream_stop(); return 1; }
    world_set_interactive(1);
    Mesh unseen={0};
    int missing=0;
    int failed=prepare_mesh_mode(&mesh_job,&unseen,cx,cz,0,0,&missing)!=MESH_WAITING || !missing;
    /* Let the terrain thread fill nearby work. A speculative scan far
       outside that neighborhood must not have queued its missing halo. */
    for (int i=0,ready=0;i<1000 && ready<9;++i) {
        world_stream_pump(0,0);
        ready=0;
        for (int x=-1;x<=1;++x) for (int z=-1;z<=1;++z)
            ready+=chunk_peek_view(x,z).cells!=NULL;
        if (ready<9) nanosleep(&poll,NULL);
        if (i==999 && ready<9) failed=1;
    }
    for (int x=cx-1;x<=cx+1;++x) for (int z=cz-1;z<=cz+1;++z)
        failed|=chunk_peek_view(x,z).cells!=NULL;
    int completed=0;
    for (int i=0;i<1000 && !completed && !failed;++i) {
        world_stream_pump(0,0);
        completed=prepare_mesh_mode(&mesh_job,&unseen,cx,cz,0,1,NULL)==MESH_QUEUED;
        if (!completed) nanosleep(&poll,NULL);
    }
    /* The same halo now supplies a complete job without extra requests. */
    failed|=!completed || prepare_mesh_mode(&mesh_job,&unseen,cx,cz,0,0,NULL)!=MESH_QUEUED;
    world_stream_stop();
    printf("MESH TEST speculative halo read, demand, and resident job failures=%d\n",failed!=0);
    return failed!=0;
}

static int queue_terrain_mesh(MeshWorker *,const int states[MESH_WORKERS],int *awaiting);

#if __has_include("ccdmc_mesh_publication_test.h")
static void yield_to_queued_edit(const int states[MESH_WORKERS]);
static int edit_in_flight(const int states[MESH_WORKERS],int cx,int cz,const Mesh *);
static int preempt_mesh_for_edit(int cx,int cz);
#include "ccdmc_mesh_publication_test.h"
#define CCDMC_TEST_MESH_PUBLICATION 1
#endif

static int mesh_workers_selftest(void) {
    const struct timespec delay={0,2000000};
    int failed=start_mesh_workers(MESH_WORKERS)!=MESH_WORKERS;
    int submitted[MESH_WORKERS]={0},finished[MESH_WORKERS]={0};
    for (int i=0;i<mesh_running;++i) {
        TerrainMeshJob *job=mesh_workers[i].job;
        memset(job,0,sizeof *job);
        job->cx=i;
        const int y=100+i;
        job->tops[0]=(uint8_t)y;
        const int column=mesh_column_index(0,0);
        job->cells[column*WY+y]=STONE;
        terrain_set_cover(&job->cover[column],y,STONE);
    }
    for (int tries=0;!failed && tries<2500;++tries) {
        for (int i=0;i<MESH_WORKERS;++i)
            if (!submitted[i]) submitted[i]=dispatch_mesh(&mesh_workers[i]);
        for (int i=0;i<MESH_WORKERS;++i)
            if (submitted[i] && !finished[i]) finished[i]=claim_mesh(&mesh_workers[i])==MESH_READY;
        if (finished[0] && finished[1]) break;
        nanosleep(&delay,NULL);
    }
    for (int i=0;i<MESH_WORKERS;++i) {
        const TerrainMeshJob *job=mesh_workers[i].job;
        if (!finished[i] || job->failed || !job->data || job->count!=36 || job->highest!=100+i) {
            failed=1;
            continue;
        }
        for (Uint32 k=0;k<job->count;++k)
            failed|=job->data[k].p[0]<i*CHUNK_SIZE || job->data[k].p[0]>i*CHUNK_SIZE+1 ||
                job->data[k].p[1]<100+i || job->data[k].p[1]>101+i;
    }
    failed|=mesh_job.data==mesh_aux.data;
    renderer_stop_meshing();
    failed|=mesh_running!=0 || mesh_job.data!=NULL || mesh_aux.data!=NULL;
    failed|=start_mesh_workers(1)!=1;
    renderer_stop_meshing();
    printf("MESH TEST two isolated workers, join and restart failures=%d\n",failed!=0);
    return failed!=0;
}

/* Engine regression: the same snapshot and geometry worker used by gameplay,
   with no SDL window or synthetic device input. */
int renderer_mesh_selftest(void) {
    const int cx=-2,cz=3,x=cx*CHUNK_SIZE,z=cz*CHUNK_SIZE;
    enum { Y=200 };
    int failed=0,fixtures_checked=0;
    if (!world_begin_generated(29342,GENERATOR_BOOKS) || !world_stream_start() || !renderer_start_meshing()) {
        world_stream_stop();
        return 1;
    }
    /* One join crosses into the diagonal neighbor's two-cell halo; three
       adjoining chests are invalid, while the separate pair is reciprocal. */
    const BlockEdit setup[]={{x+15,Y,z,CHEST},{x+16,Y,z,CHEST},{x+16,Y,z-1,CHEST},
        {x-1,Y,z+3,SMITH_CHEST_CELL},{x,Y,z+3,CHEST},{x+5,Y-1,z+5,REDSTONE_BLOCK},
        {x+15,Y,z+6,TRAPPED_CHEST_CELL},{x+16,Y,z+6,TRAPPED_CHEST_CELL},{x+15,Y,z+7,CHEST},
        {x+15,Y,z+8,ENDER_CHEST_BASE+EAST},{x+16,Y,z+8,ENDER_CHEST_BASE+WEST},
        {x+13,Y,z+12,STONE},{x+14,Y,z+12,TRIPWIRE_HOOK_BASE+WEST},
        {x+15,Y,z+12,TRIPWIRE_CELL},{x+16,Y,z+12,TRIPWIRE_CELL},
        {x+17,Y,z+12,TRIPWIRE_HOOK_BASE+EAST},{x+18,Y,z+12,STONE},
        {x+5,Y,z+5,TRACK_POWERED},{x+8,Y,z+5,TORCH},
        {x+15,Y,z+3,STONE},{x+15,Y,z+4,BUTTON_BASE+NORTH},
        {x+17,Y,z+4,STONE},{x+16,Y,z+4,BUTTON_BASE+EAST},{x+12,Y,z+9,HOPPER_DOWN},
        {x+9,Y,z+8,DROPPER_UP},{x+10,Y,z+8,DROPPER_BASE+SOUTH},
        {x+7,Y,z+10,DISPENSER_BASE+WEST},{x+15,Y,z+14,TNT_BLOCK},{x+3,Y,z+12,FIRE_CELL},
        {x+4,Y-1,z+13,STONE},{x+4,Y,z+13,SNOW_LAYER},
        {x+11,Y,z+5,PISTON_BASE+PISTON_STICKY+PISTON_EXTENDED+EJECTOR_UP},
        {x+11,Y+1,z+5,PISTON_HEAD_BASE+PISTON_STICKY+EJECTOR_UP},
        {x+1,Y-1,z+9,STONE},{x+1,Y,z+9,DOOR_BASE+NORTH},{x+1,Y+1,z+9,DOOR_BASE+NORTH+DOOR_UPPER},
        {x+1,Y-1,z+11,STONE},{x+1,Y,z+11,DOOR_BASE+NORTH+DOOR_OPEN+DOOR_IRON+DOOR_POWERED},
        {x+1,Y+1,z+11,DOOR_BASE+NORTH+DOOR_OPEN+DOOR_IRON+DOOR_POWERED+DOOR_UPPER},
        {x+14,Y,z+11,LAVA},{x+15,Y,z+11,LAVA_FLOW_BASE+5},{x+16,Y,z+11,LAVA_FLOW_BASE+3},
        {x+5,Y,z+14,EMERALD_ORE_CELL},{x+6,Y,z+14,EMERALD_BLOCK_CELL},
        {x+2,Y-1,z+6,DIRT},{x+2,Y,z+6,SAPLING_BASE+2*TREE_SPRUCE+1},
        {x+3,Y,z+6,TREE_LEAF_BASE+TREE_SPRUCE-1},
        {x+15,Y,z+2,COBWEB},{x+16,Y,z+2,COBWEB}};
    for (size_t i=0;i<sizeof setup/sizeof *setup;++i)
        if (!world_restore_edit(setup[i].x,setup[i].y,setup[i].z,setup[i].cell)) failed=1;
    world_set_interactive(1);
    meshZ=cz;
    const struct timespec delay={0,2000000};
    Mesh empty={0};
    for (int pass=0;pass<4 && !failed;++pass) {
        int submitted=0,finished=0,changed=0,vis_x=x+pass*CHUNK_SIZE;
        meshX=cx+pass;
        int requested=meshX+(pass==3 ? WORLD_VIEW_CHUNKS : 0);
        for (int attempts=0;attempts<7500 && !finished && !failed;++attempts) {
            world_stream_pump(vis_x,z);
            if (!submitted && (pass!=0 || block_light_view(requested,meshZ).ready) &&
                prepare_mesh(&mesh_job,&empty,requested,meshZ)==MESH_QUEUED) {
                if (pass==0 && !fixtures_checked) {
                    LightView light={mesh_job.has_light ? mesh_job.light : NULL,1};
                    failed=mesh_chest_joint(&mesh_job,15,Y,0)!=CHEST_ALONE ||
                        mesh_chest_joint(&mesh_job,0,Y,3)!=CHEST_WEST ||
                        mesh_chest_joint(&mesh_job,15,Y,6)!=CHEST_EAST ||
                        mesh_chest_joint(&mesh_job,15,Y,7)!=CHEST_ALONE ||
                        mesh_cell(&mesh_job,14,Y,12)!=TRIPWIRE_HOOK_BASE+WEST ||
                        mesh_cell(&mesh_job,15,Y,12)!=TRIPWIRE_CELL || mesh_cell(&mesh_job,16,Y,12)!=TRIPWIRE_CELL ||
                        mesh_cell(&mesh_job,15,Y,2)!=COBWEB || mesh_cell(&mesh_job,16,Y,2)!=COBWEB ||
                        cell_type(mesh_cell(&mesh_job,-1,Y,3))!=SMITH_CHEST ||
                        mesh_job.circuit[(5*CHUNK_SIZE+5)*WY+Y]!=1 ||
                        mesh_cell(&mesh_job,15,Y,4)!=BUTTON_BASE || mesh_cell(&mesh_job,16,Y,4)!=BUTTON_BASE+EAST ||
                        mesh_cell(&mesh_job,12,Y,9)!=HOPPER_DOWN || mesh_cell(&mesh_job,9,Y,8)!=DROPPER_UP ||
                        mesh_cell(&mesh_job,7,Y,10)!=DISPENSER_BASE+WEST || mesh_cell(&mesh_job,15,Y,14)!=TNT_BLOCK ||
                        mesh_cell(&mesh_job,3,Y,12)!=FIRE_CELL || mesh_cell(&mesh_job,4,Y,13)!=SNOW_LAYER ||
                        mesh_cell(&mesh_job,11,Y+1,5)!=PISTON_HEAD_BASE+PISTON_STICKY+EJECTOR_UP ||
                        mesh_cell(&mesh_job,1,Y+1,9)!=DOOR_BASE+NORTH+DOOR_UPPER ||
                        mesh_cell(&mesh_job,15,Y,11)!=LAVA_FLOW_BASE+5 || mesh_cell(&mesh_job,16,Y,11)!=LAVA_FLOW_BASE+3 ||
                        block_light_level(light,15,Y,11)!=15 ||
                        block_light_level(light,3,Y,12)!=15 ||
                        block_light_level(light,8,Y,5)!=14;
                    fixtures_checked=1;
                }
                if (failed) break;
                submitted=dispatch_mesh(&mesh_workers[0]);
                if (submitted) {
                    if (pass==0) changed=set_block(x,200,z,STONE);
                    if (pass==1) { changed=world_select_dimension(NETHER); renderer_reset_world(); }
                }
            }
            if (submitted && claim_mesh(&mesh_workers[0])==MESH_READY) {
                finished=1;
                failed=mesh_job.failed || !mesh_job.count || mesh_job.count%6 ||
                    !mesh_job.data || mesh_job.highest<=0 ||
                    (pass<2 ? !changed || mesh_job_current(&mesh_job) : !mesh_job_current(&mesh_job)) ||
                    vertexCount!=0 || vertices!=NULL || brightness!=255;
                if (pass==0 && !failed) {
                    int button_shape=0,hopper_pipe=0,dropper_up=0,dropper_south=0,dispenser_west=0,tnt_top=0,flame=0,snow_layer=0;
                    int piston_rim=0,piston_shaft=0,lava_top=0,lava_step_face=0,wood_leaf=0,iron_leaf=0,ore=0,gem=0;
                    int young_tree=0,foliage=0;
                    /* The only vertices in this narrow volume are the button's
                       outer face, not its wall or the neighboring terrain. */
                    for (Uint32 i=0;i<mesh_job.count;++i) {
                        const Vertex *v=&mesh_job.data[i];
                        const float *p=v->p;
                        button_shape|=p[0]==x+15.3125f && p[1]==Y+.375f && p[2]==z+4.125f;
                        hopper_pipe|=p[0]==x+12.375f && p[1]==Y && p[2]==z+9.375f;
                        if ((int)(v->uv[0]*ATLAS_UV_WIDTH/TILE_EDGE)==66) {
                            dropper_up|=p[0]==x+9 && p[1]==Y+1 && p[2]==z+8;
                            dropper_south|=p[0]==x+11 && p[1]==Y && p[2]==z+9;
                        }
                        if ((int)(v->uv[0]*ATLAS_UV_WIDTH/TILE_EDGE)==68)
                            dispenser_west|=p[0]==x+7 && p[1]==Y && p[2]==z+10;
                        if ((int)(v->uv[0]*ATLAS_UV_WIDTH/TILE_EDGE)==70)
                            tnt_top|=p[0]==x+16 && p[1]==Y+1 && p[2]==z+15;
                        flame|=p[0]>x+3.f && p[0]<x+4.f && p[1]>Y+.94f && p[1]<Y+1.f &&
                            p[2]>z+12.f && p[2]<z+13.f;
                        snow_layer|=p[0]==x+4 && p[1]==Y+.125f && p[2]==z+13;
                        int tile=(int)(v->uv[0]*ATLAS_UV_WIDTH/TILE_EDGE);
                        if (tile==47 && p[0]==x+16.f && p[2]==z+11.f) {
                            lava_top|=fabsf(p[1]-(Y+lava_surface(LAVA_FLOW_BASE+5,AIR)))<.0001f;
                            lava_step_face|=fabsf(p[1]-(Y+lava_surface(LAVA_FLOW_BASE+3,AIR)))<.0001f;
                        }
                        piston_rim|=tile==75 && p[0]==x+11 && p[1]==Y+2 && p[2]==z+5;
                        piston_shaft|=tile==76 && p[0]==x+11.375f && p[1]==Y+1.f && p[2]==z+5.375f;
                        wood_leaf|=p[0]==x+2.f && p[1]==Y+2.f && p[2]==z+9.1875f;
                        iron_leaf|=p[0]==x+1.1875f && p[1]==Y+2.f && p[2]==z+12.f;
                        ore|=tile==EMERALD_ORE_TILE && p[0]==x+5.f && p[1]==Y+1.f && p[2]==z+14.f;
                        gem|=tile==EMERALD_BLOCK_TILE && p[0]==x+6.f && p[1]==Y+1.f && p[2]==z+14.f;
                        young_tree|=p[0]>x+2.3f && p[0]<x+2.7f && fabsf(p[1]-(Y+.78f))<.0001f &&
                            p[2]>z+6.3f && p[2]<z+6.7f;
                        foliage|=p[0]==x+4.f && p[1]==Y+1.f && p[2]==z+6.f;
                    }
                    failed|=!(button_shape && hopper_pipe && dropper_up && dropper_south && dispenser_west && tnt_top &&
                        flame && snow_layer && piston_rim && piston_shaft && lava_top && lava_step_face && wood_leaf && iron_leaf &&
                        ore && gem && young_tree && foliage);
                }
                printf("MESH TEST chunk=(%d,%d) vertices=%u %s\n",mesh_job.cx,mesh_job.cz,
                    (unsigned)mesh_job.count,pass==0 ? "snapshot/edited rejection" : pass==1 ?
                    "dimension rejection" : pass==2 ? "movement mesh" : "new nine-chunk horizon");
            }
            if (!finished) nanosleep(&delay,NULL);
        }
        failed|=!finished;
    }
    renderer_stop_meshing();
    world_stream_stop();
    failed|=weather_mesh_selftest();
    failed|=horse_armor_mesh_selftest();
#if __has_include("ccdmc_entity_view_test.h")
    failed|=entity_view_selftest();
#endif
    failed|=golem_mesh_selftest();
    failed|=wolf_mesh_selftest();
    failed|=cat_mesh_selftest();
    failed|=squid_glass_mesh_selftest();
    failed|=pane_mesh_selftest();
    failed|=item_frame_mesh_selftest();
    failed|=dungeon_mesh_selftest();
    failed|=seep_mesh_selftest();
    failed|=edited_mesh_light_selftest();
    failed|=terrain_lighting_selftest();
    failed|=removed_sky_occluder_mesh_selftest();
    failed|=diffuse_sky_mesh_selftest();
    failed|=mushroom_support_mesh_selftest();
    failed|=buried_cube_selftest();
    failed|=water_hull_selftest();
    failed|=capture_floor_selftest();
    failed|=terrain_clip_selftest();
    failed|=water_visibility_selftest();
#if __has_include("ccdmc_shadow_receivers_test.h")
    failed|=shadow_receivers_selftest();
#endif
    failed|=shared_vertex_selftest();
    failed|=material_atlas_selftest();
    failed|=water_corner_selftest();
    failed|=water_optics_selftest();
    failed|=view_order_selftest();
    failed|=mesh_halo_demand_selftest();
#ifdef CCDMC_TEST_MESH_PUBLICATION
    failed|=mesh_build_shapes_selftest();
    failed|=mesh_wall_attachment_selftest();
    failed|=mesh_publication_progress_selftest();
    failed|=mesh_preemption_selftest();
    failed|=mesh_relight_queue_selftest();
    failed|=mesh_redstone_horizon_selftest();
#endif
    failed|=mesh_workers_selftest();
    world_reset();
    return failed;
}

/* The interaction ray is at most 6 cells. Near previously rendered edits,
   including their seam, go first even when the initial horizon is still
   filling. Prioritize the actual changed chunk before its halo consumers. */
static int mesh_in_flight(const int states[MESH_WORKERS],int cx,int cz) {
    for (int i=0;i<mesh_running;++i) {
        const TerrainMeshJob *job=mesh_workers[i].job;
        /* Only the renderer writes these identity fields. */
        if (states[i]!=MESH_IDLE && job->epoch==mesh_epoch && job->cx==cx && job->cz==cz) return 1;
    }
    return 0;
}
static int edit_in_flight(const int states[MESH_WORKERS],int cx,int cz,const Mesh *shown) {
    for (int i=0;i<mesh_running;++i) {
        const MeshWorker *worker=&mesh_workers[i];
        const TerrainMeshJob *job=worker->job;
        if (states[i]!=MESH_IDLE && job->cx==cx && job->cz==cz &&
            !__atomic_load_n(&worker->cancel,__ATOMIC_RELAXED) && mesh_job_publishable(job,shown)) return 1;
    }
    return 0;
}
static unsigned long long outstanding_edit(const Mesh *mesh,int cx,int cz) {
    const unsigned long long floor=player_edit_floor(cx,cz);
    return mesh->valid && mesh->x==cx && mesh->z==cz && mesh->revision[0]>=floor ? 0 : floor;
}
static int resident_mesh_halo(int cx,int cz) {
    for (int i=0;i<9;++i)
        if (!chunk_peek_view(cx+mesh_neighbors[i][0],cz+mesh_neighbors[i][1]).cells) return 0;
    return 1;
}
static int preempt_mesh_for_edit(int cx,int cz) {
    if (pthread_mutex_trylock(&mesh_lock)!=0) return 0;
    MeshWorker *victim=NULL;
    int best=-1;
    for (int i=0;i<mesh_running;++i) {
        MeshWorker *w=&mesh_workers[i];
        /* At most one background job yields. If a worker has completed
           or is already yielding, reuse it at the next refresh. */
        if (w->state==MESH_IDLE || w->state==MESH_READY || __atomic_load_n(&w->cancel,__ATOMIC_RELAXED)) {
            victim=NULL;
            break;
        }
        const TerrainMeshJob *job=w->job;
        if (job->urgent) continue;
        const int obsolete=job->epoch!=mesh_epoch || (job->cx==cx && job->cz==cz);
        const int score=obsolete ? 100 : mesh_ring(job->cx-meshX,job->cz-meshZ);
        if (!victim || score>best || (score==best && w->sequence>victim->sequence)) {
            best=score;
            victim=w;
        }
    }
    if (victim) {
        if (victim->state==MESH_PENDING) victim->state=MESH_IDLE;
        else __atomic_store_n(&victim->cancel,1,__ATOMIC_RELAXED);
    }
    pthread_mutex_unlock(&mesh_lock);
    return victim!=NULL;
}
static void yield_to_queued_edit(const int states[MESH_WORKERS]) {
    if (!edit_hint) return;
    int awaiting=0;
    for (int r=0;r<=EDIT_RINGS;++r) for (int dx=-r;dx<=r;++dx) for (int dz=-r;dz<=r;++dz) {
        if (abs(dx)!=r && abs(dz)!=r) continue;
        const int cx=meshX+dx,cz=meshZ+dz;
        const Mesh *shown=&terrain_mesh[ring_index(cx)][ring_index(cz)];
        if (!outstanding_edit(shown,cx,cz)) continue;
        awaiting=1;
        /* Do not shed work for an edit that still needs absent terrain, or
           that can already publish an in-flight snapshot. */
        if (!edit_in_flight(states,cx,cz,shown) && resident_mesh_halo(cx,cz)) {
            (void)preempt_mesh_for_edit(cx,cz);
            return;
        }
    }
    edit_hint=awaiting;
}
static int dispatch_scheduled_mesh(MeshWorker *worker) {
    if (!dispatch_mesh(worker)) return 0;
    if (worker->job->work==MESH_ONLY_LIGHT) geometry_since_light=0;
    else if (!worker->job->urgent && geometry_since_light<LIGHT_TURN_AFTER) ++geometry_since_light;
    return 1;
}
static int queue_near_edit(MeshWorker *worker,const int states[MESH_WORKERS],int *awaiting,int direct) {
    for (int own=1;own>=direct;--own) for (int r=0;r<=EDIT_RINGS;++r)
        for (int dx=-r;dx<=r;++dx) for (int dz=-r;dz<=r;++dz) {
            if (abs(dx)!=r && abs(dz)!=r) continue;
            int cx=meshX+dx,cz=meshZ+dz;
            Mesh *mesh=&terrain_mesh[ring_index(cx)][ring_index(cz)];
            int current=mesh->valid && mesh->x==cx && mesh->z==cz;
            if (direct) {
                if (!outstanding_edit(mesh,cx,cz) || edit_in_flight(states,cx,cz,mesh)) continue;
            } else if (mesh_in_flight(states,cx,cz)) {
                continue;
            } else if (own) {
                if (!current || mesh->revision[0]==chunk_revision(cx,cz)) continue;
            } else {
                if (!current) continue;
                unsigned long long rev[9]; mesh_revisions(cx,cz,rev);
                /* Updating light on already visible geometry is not a
                   simulation edit; it uses the separate fair queue below. */
                if (!memcmp(mesh->revision,rev,sizeof rev)) continue;
            }
            int result=prepare_mesh_with_priority(worker->job,mesh,cx,cz,direct);
            if (result==MESH_QUEUED) {
                if (dispatch_scheduled_mesh(worker)) return 1;
                *awaiting=1;
                return 0;
            }
            if (result==MESH_WAITING) *awaiting=1;
        }
    return 0;
}
enum { VIEW_FIRST, VIEW_SHOWN, VIEW_LIGHT };
static int queue_view_mesh(MeshWorker *idle,const int states[MESH_WORKERS],
                           const ChunkOffset *order,int pass,int *awaiting) {
    Uint64 started=SDL_GetTicksNS();
    int waiting=0,requested_halo=0;
    for (int i=0;i<MESH_SPAN*MESH_SPAN;++i) {
        int cx=meshX+order[i].x,cz=meshZ+order[i].z;
        Mesh *mesh=&terrain_mesh[ring_index(cx)][ring_index(cz)];
        int shown=mesh->valid && mesh->x==cx && mesh->z==cz;
        if (shown==(pass==VIEW_FIRST) || (pass==VIEW_LIGHT && !mesh->relight) ||
            mesh_in_flight(states,cx,cz)) continue;
        /* Fill the first missing halo for this work class, then look for
           already resident work. Loading every incomplete halo here keeps
           recycling terrain which neither mesh worker can use yet. */
        int missing=0;
        /* Reserve a fair turn only for resident work. A cold light halo
           can still be requested in VIEW_SHOWN when first work stalls. */
        int result=prepare_mesh_for_pass(idle->job,mesh,cx,cz,0,
            pass!=VIEW_LIGHT && !requested_halo,&missing,pass==VIEW_LIGHT);
        if (missing) requested_halo=1;
        if (result==MESH_QUEUED) {
            if (dispatch_scheduled_mesh(idle)) return 1;
            *awaiting=1;
            break;
        }
        if (result==MESH_WAITING) {
            *awaiting=1;
            if (++waiting>=24 || SDL_GetTicksNS()-started>=UINT64_C(1500000)) break;
        }
    }
    return 0;
}
static int queue_terrain_mesh(MeshWorker *idle,const int states[MESH_WORKERS],int *awaiting) {
    if (queue_near_edit(idle,states,awaiting,1)) return 1;
    const ChunkOffset *order=view_order(view_octant(yaw),ahead_of_flight());
    if (geometry_since_light>=LIGHT_TURN_AFTER &&
        queue_view_mesh(idle,states,order,VIEW_LIGHT,awaiting)) return 1;
    /* Alternate first presentation and simulation rebuilds when both have
       ready work. Neither an oscillator nor a fluid cascade can monopolize
       the next worker. Both classes share FIFO upload priority. */
    if (!prefer_initial_mesh && queue_near_edit(idle,states,awaiting,0)) {
        prefer_initial_mesh=1;
        return 1;
    }
    if (queue_view_mesh(idle,states,order,VIEW_FIRST,awaiting)) {
        prefer_initial_mesh=0;
        return 1;
    }
    if ((prefer_initial_mesh && queue_near_edit(idle,states,awaiting,0)) ||
        queue_view_mesh(idle,states,order,VIEW_SHOWN,awaiting)) {
        prefer_initial_mesh=1;
        return 1;
    }
    return 0;
}
static int refresh_terrain(void) {
    int states[MESH_WORKERS];
    if (pthread_mutex_trylock(&mesh_lock)!=0) return 1;
    for (int i=0;i<mesh_running;++i) states[i]=mesh_workers[i].state;
    pthread_mutex_unlock(&mesh_lock);

    /* Keep one GPU upload per frame. Completed player edits go first.
       Reject regressions and pre-interaction results, not every snapshot
       overtaken by a background simulation tick. */
    int publish=-1;
    for (int i=0;i<mesh_running;++i) if (states[i]==MESH_READY) {
        const TerrainMeshJob *job=mesh_workers[i].job;
        const Mesh *shown=&terrain_mesh[ring_index(job->cx)][ring_index(job->cz)];
        if (!mesh_job_publishable(job,shown)) {
            if (claim_mesh(&mesh_workers[i])==MESH_READY) states[i]=MESH_IDLE;
        } else if (publish<0 || job->urgent>mesh_workers[publish].job->urgent ||
            (job->urgent==mesh_workers[publish].job->urgent && mesh_workers[i].sequence<mesh_workers[publish].sequence)) {
            publish=i;
        }
    }
    if (publish>=0) {
        MeshWorker *worker=&mesh_workers[publish];
        if (claim_mesh(worker)!=MESH_READY) return 1;
        states[publish]=MESH_IDLE;
        const TerrainMeshJob *job=worker->job;
        if (job->failed) return SDL_SetError("Out of memory baking terrain mesh");
        Mesh *mesh=&terrain_mesh[ring_index(job->cx)][ring_index(job->cz)];
        if (!send_geometry(mesh,job->data,job->count)) return 0;
        Mesh *liquid=&water_mesh[ring_index(job->cx)][ring_index(job->cz)];
        if (!send_geometry(liquid,job->water,job->waterCount)) return 0;
        liquid->x=job->cx; liquid->z=job->cz; liquid->highest=job->highest;
        liquid->water_bottom=job->water_bottom;
        liquid->water_top=job->water_top;
        water_planes[ring_index(job->cx)][ring_index(job->cz)]=job->waterPlane;
        mesh->x=job->cx; mesh->z=job->cz;
        mesh->valid=1; mesh->highest=job->highest; mesh->relight=job->relight;
        mesh->opaque_bottom=job->opaque_bottom; mesh->opaque_top=job->opaque_top;
        memcpy(mesh->revision,job->revision,sizeof mesh->revision);
        if (mesh_sample_active) ++mesh_sample_published;
    }
    MeshWorker *idle=NULL;
    int active=0,awaiting=0;
    for (int i=0;i<mesh_running;++i) {
        if (states[i]!=MESH_IDLE) active=1;
        else if (!idle) idle=&mesh_workers[i];
    }
    if (!idle) { yield_to_queued_edit(states); return 1; }
    if (queue_terrain_mesh(idle,states,&awaiting)) return 1;
    terrain_pending = active || awaiting;
    return 1;
}

void renderer_reset_world(void) {
    /* The new seed changes untouched chunks too, not just edit revisions. */
    ++mesh_epoch;
    memset(edit_fences,0,sizeof edit_fences);
    edit_hint=0;
    for (int x = 0; x < MESH_SPAN; x++) for (int z = 0; z < MESH_SPAN; z++) {
        terrain_mesh[x][z].valid = 0;
        terrain_mesh[x][z].count = 0;
        water_mesh[x][z].count = 0;
    }
    drop_mesh.count = 0;
    xp_mesh.count=xp_mesh.valid=0;
    falling_mesh.count=falling_mesh.valid=0;
    weather_mesh.count=weather_mesh.valid=0;
    actor_mesh.count = actor_mesh.valid = 0;
    horse_mesh.count = horse_mesh.valid = 0;
    lead_mesh.count = lead_mesh.valid = 0;
    fishing_mesh.count = 0;
    frame_mesh.count=frame_mesh.valid=frame_maps.count=frame_maps.valid=0;
    memset(frame_maps_needed,0,sizeof frame_maps_needed);
    memset(frame_map_uploaded,0,sizeof frame_map_uploaded);
    terrain_pending = 1;
    prefer_initial_mesh=1;
    geometry_since_light=0;
    drops_dirty = 1;
}

static int draw_drop(const Drop *d,void *ctx) {
    (void)ctx;
    int item=d->item.type;
    const ItemDef *def=item_def(item);
    Pose p={{d->x,d->y,d->z},.92388f,.38268f};
    if (item==PUMP_GASKET) {
        for (int i=0;i<2;++i) {
            part(p,(Vec3){-.13f,.065f,-.121f+i*.188f},(Vec3){.26f,.048f,.055f},0x96705a);
            part(p,(Vec3){-.132f+i*.208f,.065f,-.067f},(Vec3){.056f,.048f,.134f},0xb39378);
        }
    } else if (item==AIR_STATION_ITEM) {
        draw_air_station((Vec3){d->x-.14f,d->y-.14f,d->z-.14f},.28f,0);
    } else if (item==AIR_PIPE_ITEM) {
        draw_air_pipe((Vec3){d->x-.14f,d->y-.14f,d->z-.14f},.28f);
    } else if (item==LILY_PAD_ITEM) {
        draw_lily_pad(d->x-.19f,d->y-.04f,d->z-.19f,.38f,0);
    } else if (def && (slab_item(item) || stair_item(item))) {
        shape_mesh((Vec3){d->x-.16f,d->y-.10f,d->z-.16f},.32f,
            build_material_block(build_material(def->placed_block)),build_mask(def->placed_block,NULL),NULL);
    } else if (item==LADDER_ITEM || item==WOOD_TRAPDOOR_ITEM) {
        BlockBox boxes[BARRIER_PARTS];
        const int ladder=item==LADDER_ITEM;
        int n=barrier_boxes(ladder ? LADDER+NORTH : WOOD_TRAPDOOR+NORTH,0,BARRIER_VISUAL,boxes);
        const float s=.35f;
        for (int i=0;i<n;++i) {
            BlockBox b=boxes[i];
            part(p,(Vec3){(b.x0-.5f)*s,(b.y0-(ladder ? .5f : .1f))*s,(b.z0-(ladder ? .06f : .5f))*s},
                (Vec3){(b.x1-b.x0)*s,(b.y1-b.y0)*s,(b.z1-b.z0)*s},
                ladder ? (i<2 ? 0xa6733f : 0xcf9a53) : (i<2 ? 0xaa733b : 0xb98048));
        }
    } else if (def && pane_cell(def->placed_block)) {
        Pose thin={{d->x-.32f*.24f,d->y-.12f,d->z-.65f*.24f},p.c,p.s};
        draw_pane(thin,.24f,def->placed_block,pane_parts((1u<<EAST)|(1u<<WEST)),0,0,0);
    } else if (def && stained_glass_cell(def->placed_block)) {
        for (int side=0;side<6;++side)
            glass_face((Vec3){d->x-.12f,d->y-.12f,d->z-.12f},.24f,side,def->placed_block);
    } else if (def && (item==INK_SAC || item==GRAY_DYE || item==LIGHT_GRAY_DYE ||
                       (item>=ROSE_RED && item<=ORANGE_DYE) || item==COCOA_BEANS || item==LAPIS_LAZULI || item==COOKIE)) {
        part(p,(Vec3){-.095f,-.073f,-.073f},(Vec3){.19f,.155f,.146f},def->color);
        part(p,(Vec3){-.043f,.076f,-.041f},(Vec3){.086f,.065f,.082f},glass_highlight(def->color));
    } else if (item==SILT_GEL) {
        part(p,(Vec3){-.124f,-.075f,-.093f},(Vec3){.248f,.15f,.186f},0x9b7356);
        part(p,(Vec3){-.094f,.063f,-.058f},(Vec3){.188f,.034f,.116f},0xd0ab78);
    } else if (item==SNOWBALL || item==LEAD) {
        if (item==SNOWBALL)
            part(p,(Vec3){-.105f,-.10f,-.105f},(Vec3){.21f,.20f,.21f},def->color);
        else part(p,(Vec3){-.14f,-.05f,-.10f},(Vec3){.28f,.10f,.20f},0xb87d4e);
    } else if (is_axe(item)) {
        part(p,(Vec3){-.027f,-.18f,-.027f},(Vec3){.054f,.33f,.054f},0x9b7146);
        part(p,(Vec3){-.054f,.047f,-.033f},(Vec3){.17f,.096f,.066f},def->color);
        part(p,(Vec3){.094f,.010f,-.039f},(Vec3){.066f,.162f,.078f},glass_highlight(def->color));
    } else if (is_shovel(item)) {
        part(p,(Vec3){-.025f,-.17f,-.026f},(Vec3){.050f,.28f,.052f},0x9b7146);
        part(p,(Vec3){-.087f,.035f,-.031f},(Vec3){.174f,.135f,.062f},def->color);
    } else if (brew_is_potion(item) || item==GLASS_BOTTLE || item==FILTER_MOSS || item==MAGMA_CREAM) {
        part(p,(Vec3){-.10f,-.094f,-.10f},(Vec3){.20f,.15f,.20f},def->color);
        if (brew_is_potion(item) || item==GLASS_BOTTLE)
            part(p,(Vec3){-.042f,.055f,-.042f},(Vec3){.084f,.10f,.084f},0xc6ddd9);
    } else if (item>=STONE_PLATE_ITEM && item<=IRON_PLATE_ITEM) {
        part(p,(Vec3){-.14f,-.12f,-.14f},(Vec3){.28f,.06f,.28f},def->color);
    } else if (item==STONE_BUTTON_ITEM || item==WOOD_BUTTON_ITEM) {
        part(p,(Vec3){-.12f,-.06f,-.08f},(Vec3){.24f,.11f,.16f},def->color);
    } else if (item==HOPPER_ITEM) {
        part(p,(Vec3){-.15f,-.012f,-.15f},(Vec3){.30f,.14f,.30f},def->color);
        part(p,(Vec3){-.05f,-.16f,-.05f},(Vec3){.10f,.15f,.10f},0x555a66);
    } else if (item==COMPARATOR_ITEM) {
        part(p,(Vec3){-.13f,-.12f,-.13f},(Vec3){.26f,.07f,.26f},0x98969d);
        for (int i=0;i<3;++i) {
            float x=i==0 ? 0.f : i==1 ? .07f : -.07f,z=i==0 ? -.072f : .065f;
            part(p,(Vec3){x-.022f,-.05f,z-.022f},(Vec3){.044f,.15f,.044f},0x9c5e36);
            part(p,(Vec3){x-.031f,.073f,z-.031f},(Vec3){.062f,.055f,.062f},0xde4b40);
        }
    } else if (item==REDSTONE_TORCH_ITEM || item==REPEATER_ITEM) {
        if (item==REPEATER_ITEM)
            part(p,(Vec3){-.13f,-.12f,-.13f},(Vec3){.26f,.07f,.26f},0x91909a);
        part(p,(Vec3){-.041f,-.09f,-.039f},(Vec3){.082f,.21f,.078f},0xa37049);
        part(p,(Vec3){-.064f,.094f,-.063f},(Vec3){.128f,.075f,.126f},0xd64037);
    } else if (item==REDSTONE_DUST || item==LEVER_ITEM) {
        part(p,(Vec3){-.12f,-.11f,-.12f},(Vec3){.24f,.08f,.24f},item==REDSTONE_DUST ? 0xa92c35 : 0x666970);
        part(p,(Vec3){-.043f,-.05f,-.043f},(Vec3){.089f,item==REDSTONE_DUST ? .05f : .20f,.089f},
            item==REDSTONE_DUST ? 0xd04546 : 0xa37241);
    } else if (item==POPPY_ITEM || item==DANDELION_ITEM || item==PUMPKIN_SEEDS) {
        if (item!=PUMPKIN_SEEDS) {
            part(p,(Vec3){-.016f,-.16f,-.018f},(Vec3){.034f,.25f,.036f},0x689449);
            part(p,(Vec3){-.095f,.058f,-.073f},(Vec3){.19f,.083f,.146f},def->color);
        } else for (int i=-1;i<=1;++i)
            part(p,(Vec3){i*.07f-.025f,-.02f,i*.045f-.041f},(Vec3){.048f,.022f,.086f},0xd2c693);
    } else if (armor_slot(item)>=0 || horse_armor_points(item)) {
        part(p,(Vec3){-.124f,-.12f,-.055f},(Vec3){.248f,.23f,.11f},def->color);
        part(p,(Vec3){-.108f,.075f,-.06f},(Vec3){.216f,.041f,.12f},0xe0cdb5);
    } else {
        int appearance=def && def->placed_block ? def->placed_block : item==COAL ? COAL_ORE :
            item==DIAMOND ? DIAMOND_ORE : item==EMERALD ? EMERALD_BLOCK : item==APPLE ? WOOD : item==IRON_INGOT ? IRON_ORE :
            item==GOLD_INGOT ? GOLD_ORE : PLANKS;
        for (int f=0;f<6;++f) face(d->x-.13f,d->y-.13f,d->z-.13f,.26f,appearance,f,0,1,0xffffff);
    }
    return !meshOOM;
}
static int refresh_experience(int cx,int cz) {
    unsigned version=experience_revision();
    if (xp_mesh.valid && version==xp_uploaded && cx==xp_mesh_x && cz==xp_mesh_z) return 1;
    size_t n=0;
    const XPOrb *list=experience_orbs(world_dimension(),&n);
    vertexCount=meshOOM=0;
    for (size_t i=0;i<n;++i) {
        Vec3 p=list[i].point;
        if (fabsf(p.x-px)>(RENDER_CHUNKS+1)*CHUNK_SIZE || fabsf(p.z-pz)>(RENDER_CHUNKS+1)*CHUNK_SIZE) continue;
        float s=list[i].value>=37 ? .19f : list[i].value>=7 ? .14f : .105f;
        unsigned tint=list[i].value>=37 ? 0xe2fa74 : 0xa6ef43;
        flat_quad((Vec3[]){{p.x,p.y+s,p.z},{p.x-s,p.y,p.z},{p.x,p.y-s,p.z},{p.x+s,p.y,p.z}},tint);
        flat_quad((Vec3[]){{p.x,p.y+s,p.z},{p.x,p.y,p.z+s},{p.x,p.y-s,p.z},{p.x,p.y,p.z-s}},0xc4ff65);
    }
    if (!send_mesh(&xp_mesh)) return 0;
    xp_mesh.valid=1; xp_uploaded=version; xp_mesh_x=cx; xp_mesh_z=cz;
    return 1;
}
static void draw_firework(const FireworkFlight *flight) {
    FireworkSpark sparks[FW_SPARKS];
    unsigned count=firework_sparks(flight,sparks),old=brightness;
    brightness=255;
    for (unsigned i=0;i<count;++i) {
        Vec3 p=sparks[i].point;
        float w=sparks[i].size;
        if (i==0 && !flight->burst) {
            part((Pose){p,1,0},(Vec3){-.06f,-.10f,-.06f},(Vec3){.12f,.21f,.12f},0xf7eae1);
            part((Pose){p,1,0},(Vec3){-.07f,.09f,-.07f},(Vec3){.14f,.085f,.14f},0xff604a);
            continue;
        }
        /* Two planes preserve visible density from all horizontal angles.
           Emission uses the flat unshadowed material, not block light. */
        flat_quad((Vec3[]){{p.x-w,p.y+w,p.z},{p.x-w,p.y-w,p.z},{p.x+w,p.y-w,p.z},{p.x+w,p.y+w,p.z}},sparks[i].color);
        flat_quad((Vec3[]){{p.x,p.y+w,p.z+w},{p.x,p.y-w,p.z+w},{p.x,p.y-w,p.z-w},{p.x,p.y+w,p.z-w}},sparks[i].color);
    }
    brightness=old;
}
static int refresh_falling(int cx,int cz) {
    size_t count=0;
    Dimension d=world_dimension();
    const FallingBlock *list=gravity_falling(d,&count);
    unsigned version=gravity_revision(),explosions=blasts_revision();
    unsigned pyrotechnics=fireworks_revision();
    size_t charges=0,effects=0;
    const PrimedBlast *armed=blasts_in(d,&charges);
    const BlastFlash *flash=blast_flashes(d,&effects);
    float frac=count||charges ? game_tick_fraction() : 0.f;
    if (falling_mesh.valid && version==falling_uploaded && explosions==blast_uploaded && pyrotechnics==fireworks_uploaded &&
        d==falling_view_dimension &&
        cx==falling_view_x && cz==falling_view_z && (!falling_mesh.count || frac==falling_fraction)) return 1;
    vertexCount=meshOOM=0;
    for (size_t i=0;i<count;++i) {
        FallingBlock e=list[i];
        if (abs(e.x-cx*CHUNK_SIZE)>RENDER_CHUNKS*CHUNK_SIZE+CHUNK_SIZE ||
            abs(e.z-cz*CHUNK_SIZE)>RENDER_CHUNKS*CHUNK_SIZE+CHUNK_SIZE) continue;
        float position=fminf((float)e.origin,e.bottom+e.speed*(1.f-frac));
        if (anvil_cell(e.cell)) draw_anvil(e.x,position,e.z,e.cell);
        else for (int side=0;side<6;++side)
            face((float)e.x,position,(float)e.z,1.f,e.cell,side,0.f,1.f,0xffffff);
    }
    float radius=(RENDER_CHUNKS+1.f)*CHUNK_SIZE;
    for (size_t i=0;i<charges;++i) {
        PrimedBlast e=armed[i];
        if (e.kind!=BLAST_TNT || fabsf(e.point.x-cx*CHUNK_SIZE)>radius || fabsf(e.point.z-cz*CHUNK_SIZE)>radius) continue;
        float size=.97f+(e.ticks<15 ? .11f*(15-e.ticks)/15.f : 0.f);
        Vec3 pos={e.point.x+e.speed.x*.05f*frac,e.point.y+e.speed.y*.05f*frac,e.point.z+e.speed.z*.05f*frac};
        if ((e.ticks/5)&1) part((Pose){pos,1,0},(Vec3){-size*.5f,0,-size*.5f},
            (Vec3){size,size,size},0xfff0da);
        else for (int side=0;side<6;++side)
            face(pos.x-.5f*size,pos.y,pos.z-.5f*size,size,TNT_BLOCK,side,0,1,0xffffff);
    }
    for (size_t i=0;i<effects;++i) {
        BlastFlash e=flash[i];
        if (!e.ticks || fabsf(e.point.x-cx*CHUNK_SIZE)>radius || fabsf(e.point.z-cz*CHUNK_SIZE)>radius) continue;
        float progress=1.f-e.ticks/(float)BLAST_FLASH_TICKS;
        for (int j=0;j<10;++j) {
            float a=j*.6283185f,r=.27f+2.75f*progress,s=.46f-.29f*progress;
            Vec3 pos={e.point.x+sinf(a)*r,e.point.y+.18f+(j%3)*r*.45f,e.point.z+cosf(a)*r};
            unsigned tint=e.underwater ? 0x8fcee6 : e.ticks<=4 ? 0x9e8e8b : j%2 ? 0xffc48b : 0xfff6df;
            part((Pose){pos,1,0},(Vec3){-.5f*s,-.5f*s,-.5f*s},(Vec3){s,s,s},tint);
        }
    }
    const FireworkFlight *fireworks=fireworks_in(d);
    for (int i=0;fireworks && i<FW_FLIGHTS;++i) if (fireworks[i].fuse &&
        fabsf(fireworks[i].point.x-cx*CHUNK_SIZE)<=radius && fabsf(fireworks[i].point.z-cz*CHUNK_SIZE)<=radius)
        draw_firework(&fireworks[i]);
    if (!send_mesh(&falling_mesh)) return 0;
    falling_mesh.valid=1; falling_uploaded=version; blast_uploaded=explosions; fireworks_uploaded=pyrotechnics;
    falling_fraction=frac;
    falling_view_x=cx; falling_view_z=cz; falling_view_dimension=d;
    return 1;
}

enum { PRECIP_VISUAL_RADIUS=8, PRECIP_VISUAL_HEIGHT=12 };
static unsigned weather_geometry(int cx,int cy,int cz,unsigned phase) {
    vertexCount=meshOOM=0;
    float intensity=weather_intensity();
    int count=intensity>0.f ? 1+(int)(2.f*intensity) : 0;
    float sun=world_daylight();
    if (count) for (int dx=-PRECIP_VISUAL_RADIUS;dx<=PRECIP_VISUAL_RADIUS;++dx)
        for (int dz=-PRECIP_VISUAL_RADIUS;dz<=PRECIP_VISUAL_RADIUS;++dz) {
            if (dx*dx+dz*dz>PRECIP_VISUAL_RADIUS*PRECIP_VISUAL_RADIUS) continue;
            int x=cx+dx,z=cz+dz,ground;
            unsigned h=hash2(x,z);
            if ((h&1u) || (!dx && !dz)) continue;
            Precipitation p=weather_column(x,z,&ground);
            if (p==PRECIP_NONE) continue;
            int snow=p==PRECIP_SNOW;
            unsigned tint=snow ? (sun<.33f ? 0xb9cde1 : 0xf6fbff) : (sun<.33f ? 0x567795 : 0x91aec6);
            for (int i=0;i<count;++i) {
                h=h*1664525u+1013904223u;
                float width=snow ? .031f : .014f;
                float qx=x+.16f+.68f*((h>>4)&255u)/255.f,qz=z+.16f+.68f*((h>>12)&255u)/255.f;
                float span=PRECIP_VISUAL_HEIGHT;
                float t=span-(float)((phase*(snow ? 3u : 24u)+(h>>20))%960u)*(span/960.f);
                /* Wrapping against an absolute height grid keeps the
                   particles steady when the camera crosses an integer Y. */
                float bottom=fmaxf(ground+1.f,cy-4.f);
                float y=floorf((cy-4.f-t)/span)*span+t;
                if (y<cy-4.f) y+=span;
                if (y<=bottom) continue;
                float end=fmaxf(bottom,y-(snow ? .07f : .72f));
                float drift=snow ? .105f*sinf((float)(phase%256u)*.0736f+i) : 0.f;
                qx+=drift; qz+=drift*.6f;
                flat_quad((Vec3[]){{qx-width,y,qz},{qx+width,y,qz},{qx+width,end,qz},{qx-width,end,qz}},tint);
                flat_quad((Vec3[]){{qx,y,qz-width},{qx,y,qz+width},{qx,end,qz+width},{qx,end,qz-width}},tint);
            }
        }
    LightningView flash=weather_lightning();
    if (flash.ticks) {
        Vec3 base=flash.point,next_point=base;
        unsigned rng=hash2((int)floorf(base.x),(int)floorf(base.z));
        for (int j=0;j<8;++j) {
            rng=rng*1664525u+1013904223u;
            Vec3 top={base.x+((rng>>24)/255.f-.5f)*3.2f,base.y+(j+1)*3.6f,
                base.z+(((rng>>16)&255u)/255.f-.5f)*3.2f};
            ribbon(next_point,top,.08f,0xe2eaff);
            next_point=top;
        }
    }
    return vertexCount;
}
static int weather_mesh_selftest(void) {
    world_begin(4322);
    int failed=!weather_restore((WeatherSave){7,1000,0,8,WEATHER_RAIN});
    enum { Y=150 };
    for (int x=-8;x<=8;++x) for (int z=-8;z<=8;++z)
        failed|=!set_block(x,Y-1,z,x>=0 ? GLASS : STONE);
    /* Build the same rainy scene two frames apart with real loaded geometry.
       Check the sheltered half after adding a roof tall enough to exclude it. */
    unsigned open=weather_geometry(0,Y+3,0,3);
    float first_y=open ? vertices[0].p[1] : 0.f;
    unsigned moved=weather_geometry(0,Y+3,0,4);
    failed|=!open || !moved || !vertices || (moved && vertices[0].p[1]==first_y) || meshOOM;
    for (int x=0;x<=8;++x) for (int z=-8;z<=8;++z) failed|=!set_block(x,Y+16,z,GLASS);
    unsigned half=weather_geometry(0,Y+3,0,3);
    failed|=!half || half>=open || meshOOM;
    for (unsigned i=0;i<half;++i) failed|=vertices[i].p[0]>=0.f || vertices[i].p[1]<Y;
    failed|=!world_select_dimension(NETHER) || weather_geometry(0,Y+3,0,3)!=0;
    free(vertices); vertices=NULL; vertexCount=vertexCapacity=0;
    printf("MESH TEST weather open=%u sheltered=%u failures=%d\n",open,half,failed!=0);
    return failed;
}
static void refresh_weather(void) {
    if (!weather_intensity() && !weather_lightning().ticks) { weather_mesh.count=0; weather_mesh.valid=0; return; }
    int x=(int)floorf(px),y=(int)floorf(py+1.55f),z=(int)floorf(pz);
    unsigned phase=weather_frame()*2u+(game_tick_fraction()>=.5f);
    if (weather_mesh.valid && weather_phase==phase && weather_x==x && weather_y==y && weather_z==z) return;
    weather_geometry(x,y,z,phase);
    if (!send_mesh(&weather_mesh)) { /* Decorative weather must not stop an otherwise playable scene. */
        weather_mesh.count=weather_mesh.valid=0;
        return;
    }
    weather_phase=phase; weather_x=x; weather_y=y; weather_z=z; weather_mesh.valid=1;
}
int upload_mesh(void) {
    int x = (int)floorf(px / CHUNK_SIZE), z = (int)floorf(pz / CHUNK_SIZE);
    unsigned long long version = world_edit_version();
    if (x != meshX || z != meshZ || version != meshWorldVersion) {
        if (x!=meshX || z!=meshZ) drops_dirty=1;
        meshX = x; meshZ = z; meshWorldVersion = version;
        terrain_pending = 1;
    }
    if (terrain_pending && !refresh_terrain()) return 0;
    EntityView actors_view=current_entity_view();
    if (!refresh_actors(&actors_view) || !refresh_horses(&actors_view) || !refresh_leads() || !refresh_fishing() || !refresh_item_frames(x,z) ||
        !refresh_experience(x,z) || !refresh_falling(x,z)) return 0;
    refresh_weather();
    if (!drops_dirty) return 1;

    vertexCount = meshOOM = 0;
    drops_visible(x,z,&draw_drop,NULL);
    if (!send_mesh(&drop_mesh)) return 0;
    drops_dirty = 0;
    return 1;
}

static SDL_GPUShader *load_shader(const char *path,const char *entry,SDL_GPUShaderStage stage,
                                  Uint32 uniforms,Uint32 samplers) {
    enum { SOURCE_LIMIT=65536 };
    Uint8 *source=(Uint8 *)malloc(SOURCE_LIMIT+1);
    if (!source) { SDL_OutOfMemory(); return NULL; }
    FILE *file=fopen(path,"rb");
    if (!file) { free(source); SDL_SetError("无法读取着色器 %s",path); return NULL; }
    size_t length=fread(source,1,SOURCE_LIMIT+1,file);
    const int failed=ferror(file);
    fclose(file);
    if (failed || !length || length>SOURCE_LIMIT) {
        free(source); SDL_SetError("着色器为空、长度超限或读取失败：%s",path); return NULL;
    }
    source[length]=0;
    SDL_GPUShaderCreateInfo info={.code_size=length,.code=source,.entrypoint=entry,
        .format=SDL_GPU_SHADERFORMAT_MSL,.stage=stage,.num_samplers=samplers,.num_uniform_buffers=uniforms};
    SDL_GPUShader *shader=SDL_CreateGPUShader(dev,&info);
    if (!shader) {
        char why[160];
        SDL_strlcpy(why,SDL_GetError(),sizeof why);
        SDL_SetError("%s (%s): %s",path,entry,why);
    }
    free(source);
    return shader;
}

enum { PIPE_VERTEX=1, PIPE_COLOR=2, PIPE_DEPTH=4, PIPE_WRITE=8, PIPE_BLEND=16, PIPE_BACKGROUND=32 };
static SDL_GPUGraphicsPipeline *make_pipeline(SDL_GPUShader *v,SDL_GPUShader *f,unsigned options) {
        Uint32 color_target=(options&PIPE_COLOR)!=0 ? 1u : 0u;
        bool depth_target=(options&PIPE_DEPTH)!=0;
        SDL_GPUVertexBufferDescription binding = {.pitch=sizeof(Vertex), .input_rate=SDL_GPU_VERTEXINPUTRATE_VERTEX};
        SDL_GPUVertexAttribute attrs[] = {
            {.location=0,.format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,.offset=0},
            {.location=1,.format=SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM,.offset=12},
            {.location=2,.format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,.offset=16}
        };
        SDL_GPUColorTargetDescription color = {.format=SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM};
        if (options&PIPE_BLEND) color.blend_state=(SDL_GPUColorTargetBlendState){
            .src_color_blendfactor=SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op=SDL_GPU_BLENDOP_ADD,.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .alpha_blend_op=SDL_GPU_BLENDOP_ADD,.enable_blend=true};
        SDL_GPUGraphicsPipelineCreateInfo cfg = {
            .vertex_shader=v, .fragment_shader=f,
            .primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .rasterizer_state={.fill_mode=SDL_GPU_FILLMODE_FILL,.cull_mode=SDL_GPU_CULLMODE_NONE,.enable_depth_clip=true},
            .multisample_state={.sample_count=SDL_GPU_SAMPLECOUNT_1},
            .depth_stencil_state={.compare_op=SDL_GPU_COMPAREOP_LESS,
                .enable_depth_test=(options&(PIPE_VERTEX|PIPE_BACKGROUND)) && depth_target,
                .enable_depth_write=(options&PIPE_WRITE)!=0},
            .target_info={.color_target_descriptions=color_target ? &color : NULL,.num_color_targets=color_target,
                          .depth_stencil_format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT,.has_depth_stencil_target=depth_target}
        };
        if (options&PIPE_VERTEX) cfg.vertex_input_state=(SDL_GPUVertexInputState){&binding,1,attrs,3};
        return SDL_CreateGPUGraphicsPipeline(dev, &cfg);
}

static int make_scene_pipelines(void) {
    const unsigned opaque=PIPE_VERTEX|PIPE_COLOR|PIPE_DEPTH|PIPE_WRITE;
    SDL_GPUShader *stages[18]={0};
    SDL_GPUShader *textureV,*textureF,*litF,*worldV,*worldF,*unshadowedF,*shadowV,*mirrorF;
    SDL_GPUShader *skyV,*skyF,*mirrorSkyF,*skyPresentF,*basicMoonF,*waterV,*waterF,*flatWaterV;
    SDL_GPUShader *underwaterF,*waterBelowF;
    unsigned count=0;
    char problem[224]={0};
    int success=0;
#define STAGE(variable,path,entry,type,uniforms,samplers) \
    variable=load_shader("Shaders/" path ".metal",entry,SDL_GPU_SHADERSTAGE_##type,uniforms,samplers); \
    if (!variable) goto done; stages[count++]=variable
#define PIPE(name,v,f,flags) do { name=make_pipeline(v,f,flags); if (!name) goto done; } while (0)
    STAGE(textureV,"textured","xcSDLTexturedVertex",VERTEX,1,0);
    STAGE(textureF,"textured","xcSDLTexturedFragment",FRAGMENT,0,1);
    STAGE(litF,"textured","xcSDLResidentFragment",FRAGMENT,1,1);
    PIPE(pipe,textureV,textureF,opaque);
    PIPE(pipe_lit,textureV,litF,opaque);
    STAGE(worldV,"world_lighting","worldVertex",VERTEX,1,0);
    STAGE(worldF,"world_lighting","worldFragment",FRAGMENT,1,2);
    STAGE(unshadowedF,"world_lighting","worldUnshadowedFragment",FRAGMENT,1,1);
    STAGE(shadowV,"world_lighting","shadowCaster",VERTEX,1,0);
    STAGE(mirrorF,"world_lighting","reflectionFragment",FRAGMENT,1,1);
    STAGE(underwaterF,"world_lighting","underwaterFragment",FRAGMENT,1,1);
    PIPE(pipe_world,worldV,worldF,opaque);
    PIPE(pipe_unshadowed,worldV,unshadowedF,opaque);
    PIPE(pipe_shadow,shadowV,NULL,PIPE_VERTEX|PIPE_DEPTH|PIPE_WRITE);
    PIPE(pipe_reflection,worldV,mirrorF,opaque);
    PIPE(pipe_underwater,worldV,underwaterF,opaque);
    STAGE(skyV,"atmosphere","skyVertex",VERTEX,0,0);
    STAGE(skyF,"atmosphere","skyFragment",FRAGMENT,1,0);
    STAGE(mirrorSkyF,"atmosphere","skyReflection",FRAGMENT,1,0);
    STAGE(skyPresentF,"atmosphere","skyComposite",FRAGMENT,1,1);
    STAGE(basicMoonF,"atmosphere","basicMoon",FRAGMENT,1,0);
    PIPE(pipe_sky,skyV,skyF,PIPE_COLOR);
    PIPE(pipe_mirror_sky,skyV,mirrorSkyF,PIPE_COLOR|PIPE_DEPTH|PIPE_BACKGROUND);
    PIPE(pipe_present,skyV,skyPresentF,PIPE_COLOR|PIPE_DEPTH|PIPE_BACKGROUND);
    PIPE(pipe_basic_moon,skyV,basicMoonF,PIPE_COLOR|PIPE_DEPTH|PIPE_BACKGROUND);
    STAGE(waterV,"water","waterVertex",VERTEX,1,0);
    STAGE(waterF,"water","waterFragment",FRAGMENT,1,1);
    STAGE(waterBelowF,"water","waterBelow",FRAGMENT,1,1);
    STAGE(flatWaterV,"water","waterFlatVertex",VERTEX,1,0);
    /* The closest crest must occlude water triangles behind it at low angles. */
    PIPE(pipe_water,waterV,waterF,opaque|PIPE_BLEND);
    PIPE(pipe_water_below,waterV,waterBelowF,opaque|PIPE_BLEND);
    PIPE(pipe_water_flat,flatWaterV,litF,opaque);
    success=1;
done:
    if (!success) SDL_strlcpy(problem,SDL_GetError(),sizeof problem);
    while (count) SDL_ReleaseGPUShader(dev,stages[--count]);
    if (!success) SDL_SetError("%s",problem);
    return success;
#undef PIPE
#undef STAGE
}

static SDL_GPUTexture *scene_target(Uint32 w,Uint32 h,int isDepth,int isSampled) {
    SDL_GPUTextureCreateInfo cfg={.type=SDL_GPU_TEXTURETYPE_2D,
        .format=isDepth ? SDL_GPU_TEXTUREFORMAT_D32_FLOAT : SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM,
        .usage=(isDepth ? SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET : SDL_GPU_TEXTUREUSAGE_COLOR_TARGET) |
            (isSampled ? SDL_GPU_TEXTUREUSAGE_SAMPLER : 0),
        .width=w,.height=h,.layer_count_or_depth=1,.num_levels=1,.sample_count=SDL_GPU_SAMPLECOUNT_1};
    return SDL_CreateGPUTexture(dev,&cfg);
}

static void drop_scene_texture(SDL_GPUTexture **texture) {
    if (*texture) { SDL_ReleaseGPUTexture(dev,*texture); *texture=NULL; }
}

static int prepare_effect_targets(Uint32 width,Uint32 height,int with_shadow) {
    /* Roughly 1/16 of the screen, with a cap for large desktops. */
    float reduction=fmaxf(4.f,fmaxf(width/384.f,height/256.f));
    Uint32 w=SDL_max(1u,(Uint32)(width/reduction)),h=SDL_max(1u,(Uint32)(height/reduction));
    if (w!=effectW || h!=effectH) {
        drop_scene_texture(&skyColor);
        effectW=w; effectH=h;
    }
    if (!skyColor) skyColor=scene_target(w,h,0,1);
    if (with_shadow && !shadowMap) shadowMap=scene_target(SHADOW_SIZE,SHADOW_SIZE,1,1);
    return skyColor && (!with_shadow || shadowMap);
}

static int prepare_reflection_targets(Uint32 width,Uint32 height) {
    /* Keep sharp geometry separate from the low-resolution cloud ray march.
       At most 6.4 MiB of color + depth; never scale this pass above the view. */
    float reduction=fmaxf(1.f,fmaxf(sqrtf((float)width*height/830000.f),fmaxf(width/1280.f,height/960.f)));
    Uint32 w=SDL_max(1u,(Uint32)(width/reduction)),h=SDL_max(1u,(Uint32)(height/reduction));
    if (w!=mirrorW || h!=mirrorH) {
        drop_scene_texture(&mirrorColor); drop_scene_texture(&mirrorDepth);
        mirrorW=w; mirrorH=h;
    }
    if (!mirrorColor) mirrorColor=scene_target(w,h,0,1);
    if (!mirrorDepth) mirrorDepth=scene_target(w,h,1,0);
    return mirrorColor && mirrorDepth;
}

typedef struct { float a,b,c,limit; } CullPlane;
typedef struct { CullPlane plane[6]; } CullFrustum;
/* Metal's homogeneous near plane is z>=0; the other planes are against w.
   Compute the common plane/extents data once, not for every ring entry. */
static CullFrustum clip_frustum(const float m[16],int cx,int cz,int radius) {
    CullFrustum out={0};
    const float max_x=(fabsf((float)cx)+radius+1.f)*CHUNK_SIZE,max_z=(fabsf((float)cz)+radius+1.f)*CHUNK_SIZE;
    const float max_y=WY+16.f;
    static const int row[] = {0,0,1,1,2,2}, sign[] = {1,-1,1,-1,1,-1};
    for (int i = 0; i < 6; i++) {
        int r = row[i], s = sign[i], w = i != 4;
        float a = w*m[3] + s*m[r], b = w*m[7] + s*m[4+r];
        float c = w*m[11] + s*m[8+r], d = w*m[15] + s*m[12+r];
        /* Bound rounding conservatively over the whole ring, including the
           GPU's separate homogeneous coordinates and positions far from zero. */
        const float numeric=.001f+1e-6f*(max_x*(w*fabsf(m[3])+fabsf(m[r]))+
            max_y*(w*fabsf(m[7])+fabsf(m[4+r]))+max_z*(w*fabsf(m[11])+fabsf(m[8+r]))+
            w*fabsf(m[15])+fabsf(m[12+r]));
        out.plane[i]=(CullPlane){a,b,c,d+CHUNK_SIZE*.5f*(fabsf(a)+fabsf(c))+numeric};
    }
    return out;
}
static int visible_chunk(const CullFrustum *f,int cx,int cz,float bottom,float top) {
    const float half=CHUNK_SIZE*.5f;
    const float x=cx*CHUNK_SIZE+half,z=cz*CHUNK_SIZE+half;
    for (int i=0;i<6;++i) {
        const CullPlane *p=&f->plane[i];
        const float y=p->b<0.f ? bottom : top;
        if (p->a*x+p->b*y+p->c*z+p->limit<0.f) return 0;
    }
    return 1;
}
/* A caster can be outside the camera and still shade something onscreen.
   For a receiver p and caster q, q = p + sun*t + filterOffset. Both depths
   must be inside the same shadow map. Sweeping the visible frustum towards
   the sun (not intersecting it) preserves those offscreen casters. */
typedef struct {
    CullPlane plane[6+3*3];
    unsigned count;
} ShadowReceivers;
static ShadowReceivers shadow_receivers(const float view[16],Vec3 sun,int centerX,int centerZ) {
    const CullFrustum camera=clip_frustum(view,centerX,centerZ,SHADOW_CHUNKS);
    CullPlane buffered[6];
    float along[6];
    ShadowReceivers out={0};
    /* PCF offsets by 1.65 texels on each axis; include the compare sampler's
       footprint, depth roundoff, and precision loss far from the origin. */
    const float coordinate=(fabsf((float)centerX)+fabsf((float)centerZ)+2*SHADOW_CHUNKS+2)*CHUNK_SIZE;
    const float guard=4.f*(2.f*shadow_radius/SHADOW_SIZE)+.10f+8.f*FLT_EPSILON*(coordinate+WY);
    for (int i=0;i<6;++i) {
        CullPlane p=camera.plane[i];
        const Vec3 n={p.a,p.b,p.c};
        along[i]=dot(n,sun);
        p.limit+=guard*sqrtf(dot(n,n))+2.f*(fabsf(p.a)+fabsf(p.c));
        buffered[i]=p;
        p.limit+=fmaxf(0.f,-along[i])*shadow_depth_range;
        out.plane[out.count++]=p;
    }
    /* Each pair of planes with opposite slopes in t gives an additional
       boundary of the sweep. Nonnegative weights cancel t. Their already
       expanded chunk extents stay conservative after addition. */
    for (int i=0;i<6;++i) if (along[i]<0.f)
        for (int j=0;j<6;++j) if (along[j]>0.f) {
            const float a=along[j]/(along[j]-along[i]),b=1.f-a;
            const CullPlane p=buffered[i],q=buffered[j];
            out.plane[out.count++]=(CullPlane){a*p.a+b*q.a,a*p.b+b*q.b,
                a*p.c+b*q.c,a*p.limit+b*q.limit};
        }
    return out;
}
static int has_shadow_receiver(const ShadowReceivers *r,const Mesh *m) {
    const float x=m->x*CHUNK_SIZE+CHUNK_SIZE*.5f,z=m->z*CHUNK_SIZE+CHUNK_SIZE*.5f;
    for (unsigned i=0;i<r->count;++i) {
        const CullPlane *p=&r->plane[i];
        const float y=p->b<0.f ? m->opaque_bottom : m->opaque_top;
        if (p->a*x+p->b*y+p->c*z+p->limit<0.f) return 0;
    }
    return 1;
}
static int visible_opaque(const CullFrustum *f,const Mesh *m,int kind,const ShadowReceivers *receivers) {
    const int sun_or_view=visible_chunk(f,m->x,m->z,m->opaque_bottom,m->opaque_top);
    const int visible=sun_or_view && (!receivers || has_shadow_receiver(receivers,m));
    if (mesh_sample_active) {
        CullSample *sample=&mesh_sample_cull[kind];
        if (visible) {
            ++sample->draws;
            sample->submitted+=m->count;
        } else if (sun_or_view) {
            ++shadow_sample_no_receiver;
            shadow_sample_no_receiver_vertices+=m->count;
        } else if (visible_chunk(f,m->x,m->z,0.f,m->highest+1.f)) {
            ++sample->skipped;
            sample->avoided+=m->count;
        }
    }
    return visible;
}

/* Independent homogeneous corner check in double precision. */
static int clip_corner_reference_pad(const float m[16],int cx,int cz,float bottom,float top,double pad) {
    unsigned outside=63;
    for (int i=0;i<8 && outside;++i) {
        double x=(cx+(i&1))*CHUNK_SIZE+(i&1 ? pad : -pad),y=(i&2) ? top : bottom,
            z=(cz+((i>>2)&1))*CHUNK_SIZE+(i&4 ? pad : -pad);
        double v[4];
        for (int k=0;k<4;++k) v[k]=m[k]*x+m[k+4]*y+m[k+8]*z+m[k+12];
        outside&=(v[0]<-v[3] ? 1u : 0u)|(v[0]>v[3] ? 2u : 0u)|(v[1]<-v[3] ? 4u : 0u)|
            (v[1]>v[3] ? 8u : 0u)|(v[2]<0 ? 16u : 0u)|(v[2]>v[3] ? 32u : 0u);
    }
    return outside==0;
}
static int clip_corner_reference(const float m[16],int cx,int cz,float bottom,float top) {
    return clip_corner_reference_pad(m,cx,cz,bottom,top,0.);
}
static int terrain_clip_selftest(void) {
    float m[16]={.0625f,0,0,0, 0,.125f,0,0, 0,0,.03125f,0, 0,-2.5f,.25f,1};
    const CullFrustum above=clip_frustum(m,0,0,2);
    int failed=!visible_chunk(&above,0,0,0,71) || visible_chunk(&above,0,0,40,70) ||
        !visible_chunk(&above,0,0,28,60) || !visible_chunk(&above,1,0,20,20) ||
        visible_chunk(&above,2,0,20,20);
    unsigned cases=0,avoid=0;
    for (int phase=0;phase<9;++phase) {
        /* Use the real perspective matrix with steep angles, negative
           chunks and a far-origin view as well as ordinary ground play. */
        const int cx=(phase%3)*2100-2100,cz=(phase/3)*2100-2100;
        Vec3 eye={cx*CHUNK_SIZE+.5f,30.f+20.f*phase,cz*CHUNK_SIZE+.7f};
        const float p=(phase-4)*.31f;
        Vec3 dir={.6f*cosf(p),sinf(p),-.8f*cosf(p)};
        Vec3 right=unit(cross(dir,(Vec3){0,1,0})),up=cross(right,dir);
        camera_matrix(m,.64f,eye,dir,right,up,1.f);
        CullFrustum f=clip_frustum(m,cx,cz,8);
        for (int x=cx-8;x<=cx+8;++x) for (int z=cz-8;z<=cz+8;++z) for (int y=4;y<180;y+=15) {
            float hi=y+4.5f;
            int hit=visible_chunk(&f,x,z,(float)y,hi);
            failed|=!hit && clip_corner_reference(m,x,z,(float)y,hi);
            avoid+=!hit && visible_chunk(&f,x,z,0.f,hi+1.f);
            ++cases;
        }
    }
    failed|=!avoid;
    printf("MESH TEST clip bounds: %u cases, %u height rejects, no rejected intersecting corners, failures=%d\n",
        cases,avoid,failed!=0);
    return failed;
}

static void draw_mesh(SDL_GPURenderPass *pass, const Mesh *mesh) {
    if (!mesh->count) return;
    SDL_GPUBufferBinding bind = {.buffer=mesh->buffer};
    SDL_BindGPUVertexBuffers(pass, 0, &bind, 1);
    for (Uint32 first=0;first<mesh->count;) {
        const Uint32 n=SDL_min(mesh->count-first,(Uint32)MESH_DRAW_PAGE);
        SDL_DrawGPUPrimitives(pass,n,1,first,0);
        first+=n;
    }
}

static void draw_terrain(SDL_GPURenderPass *pass,Mesh grid[MESH_SPAN][MESH_SPAN],const float m[16],int radius,int kind,
                         const ShadowReceivers *receivers) {
    const CullFrustum frustum=clip_frustum(m,meshX,meshZ,radius);
    for (int x=meshX-radius;x<=meshX+radius;++x) for (int z=meshZ-radius;z<=meshZ+radius;++z) {
        const Mesh *mesh=&grid[ring_index(x)][ring_index(z)];
        if (!mesh->count || mesh->x!=x || mesh->z!=z) continue;
        if (visible_opaque(&frustum,mesh,kind,receivers)) draw_mesh(pass,mesh);
    }
}

/* water.metal displaces vertices by up to (.27+.125+.11+.045)*1.57
   vertically. Each horizontal component is bounded by that sum times .78.
   Use round-up margins, including float rounding far from the origin. */
#define WATER_WAVE_PAD_Y 1.f
#define WATER_WAVE_PAD_XZ 1.25f
static CullFrustum water_clip_frustum(CullFrustum f,int animated) {
    if (animated) for (int i=0;i<6;++i)
        f.plane[i].limit+=WATER_WAVE_PAD_XZ*(fabsf(f.plane[i].a)+fabsf(f.plane[i].c));
    return f;
}
static int visible_water_chunk(const CullFrustum *f,const Mesh *mesh,int animated) {
    const float pad=animated ? WATER_WAVE_PAD_Y : 0.f;
    return visible_chunk(f,mesh->x,mesh->z,mesh->water_bottom-pad,mesh->water_top+pad);
}
static int water_visibility_selftest(void) {
    /* A chunk contains both a low lake and a tall cliff. Only the cliff
       overlaps this view. Neither its height nor old flat water bounds
       can stand in for the lake surface. */
    Mesh lake={0};
    lake.water_bottom=41; lake.water_top=42; lake.highest=137;
    float m[16]={.0625f,0,0,0, 0,.125f,0,0, 0,0,.03125f,0, -.5f,-12.5f,.25f,1};
    CullFrustum f=clip_frustum(m,0,0,9),wave=water_clip_frustum(f,1);
    int failure=!visible_chunk(&f,0,0,0.f,lake.highest+1.f) ||
        visible_water_chunk(&wave,&lake,1);
    /* Move the same lake a fraction outside the vertical frustum: the
       conservative orbital extent still catches waves at the border. */
    m[13]=-6.3125f;
    f=clip_frustum(m,0,0,9); wave=water_clip_frustum(f,1);
    failure|=visible_water_chunk(&f,&lake,0) || !visible_water_chunk(&wave,&lake,1);
    unsigned tested=0,rejected=0;
    for (int phase=0;phase<15;++phase) {
        const int width=(WORLD_SAVE_LIMIT-1600)/CHUNK_SIZE;
        const int cx=(phase%3-1)*width,cz=(phase/3%3-1)*width;
        const Vec3 eye={cx*CHUNK_SIZE+6.f,48.f+7.f*phase,cz*CHUNK_SIZE+10.f};
        const float pitch=(phase-7)*.17f;
        const Vec3 dir={.6f*cosf(pitch),sinf(pitch),-.8f*cosf(pitch)};
        const Vec3 right=unit(cross(dir,(Vec3){0,1,0})),up=cross(right,dir);
        camera_matrix(m,phase%2==0 ? .60f : 1.7f,eye,dir,right,up,1.f);
        f=clip_frustum(m,cx,cz,9); wave=water_clip_frustum(f,1);
        for (int x=cx-9;x<=cx+9;++x) for (int z=cz-9;z<=cz+9;++z) for (int y=5;y<WY-1;y+=31) {
            lake.x=x; lake.z=z; lake.water_bottom=(uint16_t)y; lake.water_top=(uint16_t)(y+2);
            const int keep=visible_water_chunk(&wave,&lake,1);
            failure|=(!keep && clip_corner_reference_pad(m,x,z,y-1.f,y+3.f,1.0)) ||
                (!visible_water_chunk(&f,&lake,0) && clip_corner_reference(m,x,z,(float)y,(float)(y+2)));
            rejected+=!keep && visible_chunk(&f,x,z,0.f,WY);
            ++tested;
        }
    }
    printf("MESH TEST water visibility flat and swollen bounds: %u cases, %u coarse-box rejects, failures=%d\n",
        tested,rejected,failure!=0);
    return failure || !rejected;
}
static unsigned collect_visible_water(const Mesh *visible[MESH_SPAN*MESH_SPAN],const float m[16],int animated) {
    const CullFrustum coarse=clip_frustum(m,meshX,meshZ,RENDER_CHUNKS);
    const CullFrustum frustum=water_clip_frustum(coarse,animated);
    unsigned count=0;
    /* Preserve the old transparent draw order; the list only removes submissions. */
    for (int x=meshX-RENDER_CHUNKS;x<=meshX+RENDER_CHUNKS;++x)
        for (int z=meshZ-RENDER_CHUNKS;z<=meshZ+RENDER_CHUNKS;++z) {
            const Mesh *mesh=&water_mesh[ring_index(x)][ring_index(z)];
            if (!mesh->count || mesh->x!=x || mesh->z!=z) continue;
            if (visible_water_chunk(&frustum,mesh,animated)) {
                visible[count++]=mesh;
                if (mesh_sample_active) {
                    ++mesh_sample_water.draws;
                    mesh_sample_water.submitted+=mesh->count;
                }
            } else if (mesh_sample_active && visible_chunk(&coarse,x,z,0.f,mesh->highest+1.f)) {
                ++mesh_sample_water.skipped;
                mesh_sample_water.avoided+=mesh->count;
            }
        }
    return count;
}
static void draw_visible_water(SDL_GPURenderPass *pass,const Mesh *const *visible,unsigned count) {
    for (unsigned i=0;i<count;++i) draw_mesh(pass,visible[i]);
}

static void draw_above_water_capture(SDL_GPURenderPass *pass,const float m[16],int radius,float floor_y,int kind) {
    CaptureSample *sample=mesh_sample_active ? &mesh_sample_capture[kind] : NULL;
    const CullFrustum frustum=clip_frustum(m,meshX,meshZ,radius);
    if (sample) ++sample->passes;
    for (int x=meshX-radius;x<=meshX+radius;++x) for (int z=meshZ-radius;z<=meshZ+radius;++z) {
        const Mesh *mesh=&terrain_mesh[ring_index(x)][ring_index(z)];
        if (!mesh->count || mesh->x!=x || mesh->z!=z) continue;
        int skipped=outside_capture_floor(mesh,floor_y);
        if (skipped && !sample) continue;
        if (skipped ? !visible_chunk(&frustum,x,z,mesh->opaque_bottom,mesh->opaque_top) :
            !visible_opaque(&frustum,mesh,kind==CAPTURE_REFLECTION ? CULL_MIRROR : CULL_SNELL,NULL)) continue;
        if (sample) {
            ++sample->chunks;
            sample->vertices+=mesh->count;
            if (skipped) {
                ++sample->skipped_chunks;
                sample->skipped_vertices+=mesh->count;
            }
        }
        if (!skipped) draw_mesh(pass,mesh);
    }
}

static float reflection_plane(const float m[16],Vec3 eye) {
    float closest=1.e20f,plane=-1.f;
    const CullFrustum frustum=clip_frustum(m,meshX,meshZ,REFLECTION_CHUNKS);
    for (int dx=-REFLECTION_CHUNKS;dx<=REFLECTION_CHUNKS;++dx)
        for (int dz=-REFLECTION_CHUNKS;dz<=REFLECTION_CHUNKS;++dz) {
            int x=meshX+dx,z=meshZ+dz,i=ring_index(x),j=ring_index(z);
            const Mesh *mesh=&water_mesh[i][j];
            float level=water_planes[i][j];
            if (!mesh->count || mesh->x!=x || mesh->z!=z || level<0 || level>=eye.y ||
                !visible_chunk(&frustum,x,z,0.f,mesh->highest+1.f)) continue;
            float distance=(float)(dx*dx+dz*dz)+.025f*fabsf(eye.y-level);
            if (distance<closest) { closest=distance; plane=level; }
        }
    return plane;
}

static void sun_matrix(float m[16],Vec3 eye,Vec3 sun) {
    Vec3 right=unit(cross((Vec3){0,1,0},sun)),up=cross(sun,right);
    float texel=2*shadow_radius/SHADOW_SIZE;
    float sx=roundf(dot(right,eye)/texel)*texel, sy=roundf(dot(up,eye)/texel)*texel, sz=dot(sun,eye);
    const float projection[16]={right.x/shadow_radius,up.x/shadow_radius,-sun.x/shadow_depth_range,0,
        right.y/shadow_radius,up.y/shadow_radius,-sun.y/shadow_depth_range,0,
        right.z/shadow_radius,up.z/shadow_radius,-sun.z/shadow_depth_range,0,
        -sx/shadow_radius,-sy/shadow_radius,.5f+sz/shadow_depth_range,1};
    memcpy(m,projection,sizeof projection);
}

#if __has_include("ccdmc_shadow_receivers_test.h")
#include "ccdmc_shadow_receivers_test.h"
#endif

static SDL_GPUDepthStencilTargetInfo depth_target(SDL_GPUTexture *t,int sampled) {
    return (SDL_GPUDepthStencilTargetInfo){.texture=t,.clear_depth=1,.load_op=SDL_GPU_LOADOP_CLEAR,
        .store_op=sampled ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE,
        .stencil_load_op=SDL_GPU_LOADOP_DONT_CARE,.stencil_store_op=SDL_GPU_STOREOP_DONT_CARE};
}

static void bind_texture(SDL_GPURenderPass *pass,Uint32 slot,SDL_GPUTexture *texture,SDL_GPUSampler *sampler) {
    SDL_GPUTextureSamplerBinding binding={.texture=texture,.sampler=sampler};
    SDL_BindGPUFragmentSamplers(pass,slot,&binding,1);
}

enum { SKY_UNIFORM_FLOATS=28 };
static void sky_uniforms(float out[SKY_UNIFORM_FLOATS],Vec3 eye,Vec3 f,Vec3 s,Vec3 u,float aspect,Vec3 sun,
                         float time,float clouds,Uint32 pixelHeight,SDL_FColor background,float zoom) {
    float v=1.f/(camera_scale()*zoom);
    const float atmosphere[SKY_UNIFORM_FLOATS]={eye.x,eye.y,eye.z,time, f.x,f.y,f.z,2.f*v/SDL_max(pixelHeight,1u),
        s.x*v*aspect,s.y*v*aspect,s.z*v*aspect,0, u.x*v,u.y*v,u.z*v,0,
        sun.x,sun.y,sun.z,weather_intensity(), 129.f,154.f,clouds,475.f,
        background.r,background.g,background.b,(float)world_moon_phase()};
    memcpy(out,atmosphere,sizeof atmosphere);
}

static void draw_sky(SDL_GPUCommandBuffer *cmd,SDL_GPURenderPass *pass,SDL_GPUGraphicsPipeline *pipeline,
                     const float atmosphere[SKY_UNIFORM_FLOATS]) {
    SDL_BindGPUGraphicsPipeline(pass,pipeline);
    SDL_PushGPUFragmentUniformData(cmd,0,atmosphere,SKY_UNIFORM_FLOATS*sizeof *atmosphere);
    SDL_DrawGPUPrimitives(pass,3,1,0,0);
}

static int cancel_draw(SDL_GPUCommandBuffer *cmd) {
    SDL_CancelGPUCommandBuffer(cmd);
    return 0;
}

static int resize_drawable(int width,int height);

int draw_scene(void) {
    if (!win) return SDL_SetError("No drawable surface");
    const int advanced=save_advanced_lighting() && world_dimension()==OVERWORLD;
    /* CCDUI gives logical points, not backing pixels. Spend the extra pixels
       on the HD path (including stars), but cap the GPU and sandbox footprint. */
    if (!resize_drawable(advanced ? hd_w : view_w,advanced ? hd_h : view_h)) {
        if (!advanced || !resize_drawable(view_w,view_h)) return 0;
        /* Don't retry a rejected high-resolution request every frame. */
        hd_w=view_w; hd_h=view_h;
    }
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(dev);
    if (!cmd) return 0;
    SDL_GPUTexture *swap = NULL;
    Uint32 w = 0, h = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, win, &swap, &w, &h)) {
        SDL_CancelGPUCommandBuffer(cmd); return 0;
    }
    if (!swap || !w || !h) { SDL_CancelGPUCommandBuffer(cmd); return 1; }
    if (!depth || w != depthW || h != depthH) {
        SDL_GPUTextureCreateInfo d = {.type=SDL_GPU_TEXTURETYPE_2D,.format=SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            .usage=SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,.width=w,.height=h,
            .layer_count_or_depth=1,.num_levels=1,.sample_count=SDL_GPU_SAMPLECOUNT_1};
        SDL_GPUTexture *next = SDL_CreateGPUTexture(dev, &d);
        if (!next) { SDL_CancelGPUCommandBuffer(cmd); return 0; }
        if (depth) SDL_ReleaseGPUTexture(dev, depth);
        depth = next; depthW = w; depthH = h;
        printf("viewport %u x %u\n", w, h); fflush(stdout);
    }
    const float aspect=(float)w/h;
    Vec3 eye={px,py+1.55f,pz},f=forward(),s=unit(cross(f,(Vec3){0,1,0})),u=cross(s,f);
    WaterView waterView=water_view_sample(eye.x,eye.y,eye.z);
    const int airEffects=advanced && !waterView.submerged;
    float transform[16];
    camera_matrix(transform,aspect,eye,f,s,u,1.f);
    float phase=(player_rules.day_tick+game_tick_fraction())*(6.283185307f/DAY_TICKS);
    Vec3 solar=unit((Vec3){cosf(phase),sinf(phase),.37f});
    const float day=terrain_day_fraction(solar.y);
    const float sun=world_daylight(),rain=weather_intensity();
    const float darkening=weather_kind()==WEATHER_THUNDER ? .48f : .31f;
    const float skyLight=(.11f+.89f*day)*(1.f-darkening*rain);
    const SDL_FColor nightHorizon={.024f,.039f,.093f,1.f};
    SDL_GPUColorTargetInfo out = {.texture=swap,.clear_color={
        nightHorizon.r+(.47f*sun-nightHorizon.r)*day,
        nightHorizon.g+(.73f*sun-nightHorizon.g)*day,
        nightHorizon.b+(.91f*sun-nightHorizon.b)*day,1.f},
        .load_op=SDL_GPU_LOADOP_CLEAR,.store_op=SDL_GPU_STOREOP_STORE};
    if (inside_pressure_works()) out.clear_color = (SDL_FColor){.047f,.059f,.056f,1};
    else if (world_dimension() == NETHER) out.clear_color = (SDL_FColor){.21f,.055f,.045f,1};
    else if (inside_aquifer()) out.clear_color = (SDL_FColor){.022f,.049f,.058f,1};
    else if (world_dimension() == END) out.clear_color = (SDL_FColor){.065f,.041f,.111f,1};
    else {
        float clouds=rain*.55f;
        float flash=weather_lightning().ticks>=3 ? .13f : 0.f;
        out.clear_color.r=out.clear_color.r*(1.f-clouds)+clouds*(.027f+(.58f*sun-.027f)*day)+flash;
        out.clear_color.g=out.clear_color.g*(1.f-clouds)+clouds*(.033f+(.64f*sun-.033f)*day)+flash;
        out.clear_color.b=out.clear_color.b*(1.f-clouds)+clouds*(.045f+(.70f*sun-.045f)*day)+flash;
    }
    const SDL_FColor airClear=out.clear_color;
    float viewAndSun[32],reflected[32],atmosphere[SKY_UNIFORM_FLOATS];
    Vec3 reflectionUp={-u.x,u.y,-u.z};
    const float mirrorZoom=.87f;
    float seconds=(float)(SDL_GetTicks()%UINT64_C(7200000))*.001f;
    float vision=player_night_vision_lift();
    float waterDay=fmaxf(0.f,solar.y)*(1.f-rain*.76f);
    float optics[20];
    const float snellZoom=.26f;
    int snell_capture=0;
    if (waterView.submerged) {
        Biome type;
        int swamp=world_precipitation_surface((int)floorf(eye.x),(int)floorf(eye.z),NULL,&type) && type==BIOME_SWAMP;
        float glow=.19f+.80f*waterDay*waterView.open_sky+.55f*vision;
        float deep=1.f-fminf(24.f,waterView.surface-eye.y)*.022f;
        out.clear_color=(SDL_FColor){(swamp ? .094f : .048f)*glow*deep,
            (swamp ? .249f : .317f)*glow*deep,(swamp ? .229f : .388f)*glow*deep,1.f};
        float density=(swamp ? .105f : .048f)*(1.f-vision*.70f)*(1.f+rain*.16f);
        const float medium[20]={eye.x,eye.y,eye.z,waterView.surface,solar.x,solar.y,solar.z,waterDay,
            out.clear_color.r,out.clear_color.g,out.clear_color.b,density,seconds,vision,(float)waterView.open_sky,(float)advanced,
            skyLight,0.f,0.f,0.f};
        memcpy(optics,medium,sizeof optics);
    }
    float shadowStrength=.37f*fmaxf(0.f,fminf(1.f,(solar.y-.06f)*5.f))*(1.f-weather_intensity()*.85f);
    /* The old no-caster path cleared depth to 1.0 and every compare was lit.
       Express the same result directly, with no dependency on a prior frame. */
    if (shadowStrength<=.001f) shadowStrength=0.f;
    const Mesh *water_visible[MESH_SPAN*MESH_SPAN];
    const unsigned water_draws=collect_visible_water(water_visible,transform,advanced);
    /* If there is a consumer, the old reflection selection is unchanged.
       Otherwise neither capture can contribute a pixel to this frame. */
    float plane=airEffects && water_draws ? reflection_plane(transform,eye) : -1.f;
    const int snell_candidate=waterView.submerged && waterView.open_sky && advanced &&
        waterView.surface-eye.y<26.f && f.y+fabsf(u.y)/camera_scale()>.28f;
    if (mesh_sample_active) {
        ++water_sample_frames;
        if (!water_draws) {
            ++water_sample_dry;
            if (snell_candidate || (airEffects && reflection_plane(transform,eye)>=0.f))
                ++water_sample_unused_capture;
        }
    }
    if (water_draws && snell_candidate && prepare_reflection_targets(w,h)) {
        /* Reuse the reflection attachment (never needed underwater). The
           overhead capture is a wide directional view, not a full camera
           render of the underwater scene. Clip out geometry below water. */
        const Vec3 upper={0,1,0},right={1,0,0},north={0,0,-1};
        Vec3 source={eye.x,waterView.surface-.48f,eye.z};
        camera_matrix(reflected,1.f,source,upper,right,north,snellZoom);
        memcpy(reflected+16,reflected,sizeof transform);
        SDL_GPUColorTargetInfo color=out;
        color.texture=mirrorColor;
        SDL_GPUDepthStencilTargetInfo captureDepth=depth_target(mirrorDepth,0);
        SDL_GPURenderPass *capture=SDL_BeginGPURenderPass(cmd,&color,1,&captureDepth);
        if (!capture) return cancel_draw(cmd);
        SDL_BindGPUGraphicsPipeline(capture,pipe_reflection);
        SDL_PushGPUVertexUniformData(cmd,0,reflected,sizeof reflected);
        const float above[16]={waterView.surface-.07f,.22f,1.f-fmaxf(0.f,solar.y),1.10f,
            source.x,source.y,source.z,1.f/95.f,0.f,1.f,0.f,vision,airClear.r,airClear.g,airClear.b,skyLight};
        SDL_PushGPUFragmentUniformData(cmd,0,above,sizeof above);
        bind_texture(capture,0,atlas,atlasSampler);
        draw_above_water_capture(capture,reflected,3,above[0],CAPTURE_ABOVE_WATER);
        float overhead[SKY_UNIFORM_FLOATS];
        sky_uniforms(overhead,source,upper,right,north,1.f,solar,seconds,.80f,mirrorH,airClear,snellZoom);
        draw_sky(cmd,capture,pipe_mirror_sky,overhead);
        SDL_EndGPURenderPass(capture);
        snell_capture=1;
    } else if (airEffects) {
        if (!prepare_effect_targets(w,h,shadowStrength>0.f)) return cancel_draw(cmd);
        memcpy(viewAndSun,transform,sizeof transform);
        sun_matrix(viewAndSun+16,eye,solar);
        if (shadowStrength>0.f) {
            SDL_GPUDepthStencilTargetInfo shadow=depth_target(shadowMap,1);
            SDL_GPURenderPass *cast=SDL_BeginGPURenderPass(cmd,NULL,0,&shadow);
            if (!cast) return cancel_draw(cmd);
            SDL_BindGPUGraphicsPipeline(cast,pipe_shadow);
            SDL_PushGPUVertexUniformData(cmd,0,viewAndSun+16,sizeof transform);
            const ShadowReceivers receivers=shadow_receivers(transform,solar,meshX,meshZ);
            draw_terrain(cast,terrain_mesh,viewAndSun+16,SHADOW_CHUNKS,CULL_SHADOW,&receivers);
            SDL_EndGPURenderPass(cast);
            if (mesh_sample_active) ++shadow_sample_passes;
        } else if (mesh_sample_active) ++shadow_sample_inactive;

        SDL_GPUColorTargetInfo effects=out;
        effects.texture=skyColor;
        SDL_GPURenderPass *sky=SDL_BeginGPURenderPass(cmd,&effects,1,NULL);
        if (!sky) return cancel_draw(cmd);
        sky_uniforms(atmosphere,eye,f,s,u,aspect,solar,seconds,.88f,h,out.clear_color,1.f);
        draw_sky(cmd,sky,pipe_sky,atmosphere);
        SDL_EndGPURenderPass(sky);

        if (plane>=0.f) {
            if (!prepare_reflection_targets(w,h)) return cancel_draw(cmd);
            Vec3 reflectedEye={eye.x,2.f*plane-eye.y,eye.z};
            Vec3 reflectedF={f.x,-f.y,f.z};
            camera_matrix(reflected,aspect,reflectedEye,reflectedF,s,reflectionUp,mirrorZoom);
            memcpy(reflected+16,viewAndSun+16,sizeof transform);
            effects.texture=mirrorColor;
            SDL_GPUDepthStencilTargetInfo mirror=depth_target(mirrorDepth,0);
            SDL_GPURenderPass *reflection=SDL_BeginGPURenderPass(cmd,&effects,1,&mirror);
            if (!reflection) return cancel_draw(cmd);
            SDL_BindGPUGraphicsPipeline(reflection,pipe_reflection);
            SDL_PushGPUVertexUniformData(cmd,0,reflected,sizeof reflected);
            const float air[16]={plane,.28f,1.f-fmaxf(0.f,solar.y),1.06f,
                reflectedEye.x,reflectedEye.y,reflectedEye.z,1.f/144.f,
                reflectedF.x,reflectedF.y,reflectedF.z,vision,
                airClear.r,airClear.g,airClear.b,skyLight};
            SDL_PushGPUFragmentUniformData(cmd,0,air,sizeof air);
            bind_texture(reflection,0,atlas,atlasSampler);
            draw_above_water_capture(reflection,reflected,REFLECTION_CHUNKS,air[0],CAPTURE_REFLECTION);
            draw_mesh(reflection,&frame_mesh);
            if (frame_maps.count && frame_map_atlas) {
                bind_texture(reflection,0,frame_map_atlas,atlasSampler);
                draw_mesh(reflection,&frame_maps);
            }
            float mirrorSky[SKY_UNIFORM_FLOATS];
            sky_uniforms(mirrorSky,reflectedEye,reflectedF,s,reflectionUp,aspect,solar,seconds,.88f,mirrorH,out.clear_color,mirrorZoom);
            draw_sky(cmd,reflection,pipe_mirror_sky,mirrorSky);
            SDL_EndGPURenderPass(reflection);
        }
    }
    SDL_GPUDepthStencilTargetInfo d=depth_target(depth,0);
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &out, 1, &d);
    if (!pass) { SDL_CancelGPUCommandBuffer(cmd); return 0; }
    SDL_GPUViewport viewport = {.x=0,.y=0,.w=(float)w,.h=(float)h,.min_depth=0,.max_depth=1};
    SDL_SetGPUViewport(pass, &viewport);
    const float lighting[4]={skyLight,vision,0.f,0.f};
    if (waterView.submerged) {
        memcpy(viewAndSun,transform,sizeof transform);
        memcpy(viewAndSun+16,transform,sizeof transform);
        SDL_BindGPUGraphicsPipeline(pass,pipe_underwater);
        SDL_PushGPUVertexUniformData(cmd,0,viewAndSun,sizeof viewAndSun);
        SDL_PushGPUFragmentUniformData(cmd,0,optics,sizeof optics);
        bind_texture(pass,0,atlas,atlasSampler);
    } else if (airEffects) {
        SDL_BindGPUGraphicsPipeline(pass,shadowStrength>0.f ? pipe_world : pipe_unshadowed);
        SDL_PushGPUVertexUniformData(cmd,0,viewAndSun,sizeof viewAndSun);
        const float scene[16]={vision,shadowStrength,1.f/SHADOW_SIZE,.0019f,
            .30f,1.f-fmaxf(0.f,solar.y),1.06f,.72f+rain*.28f,
            eye.x,eye.y,eye.z,1.f/(CHUNK_SIZE*RENDER_CHUNKS*1.29f),
            airClear.r,airClear.g,airClear.b,skyLight};
        SDL_PushGPUFragmentUniformData(cmd,0,scene,sizeof scene);
        bind_texture(pass,0,atlas,atlasSampler);
        if (shadowStrength>0.f) bind_texture(pass,1,shadowMap,shadowSampler);
    } else {
        SDL_BindGPUGraphicsPipeline(pass,pipe_lit);
        SDL_PushGPUVertexUniformData(cmd,0,transform,sizeof transform);
        SDL_PushGPUFragmentUniformData(cmd,0,lighting,sizeof lighting);
        bind_texture(pass,0,atlas,atlasSampler);
    }
    draw_terrain(pass,terrain_mesh,transform,RENDER_CHUNKS,CULL_WORLD,NULL);
    if (airEffects) {
        bind_texture(pass,0,skyColor,smoothSampler);
        draw_sky(cmd,pass,pipe_present,atmosphere);
    } else if (!waterView.submerged) {
        /* Don't add a full-screen pass to the low graphics setting unless
           the moon is above the horizon and can intersect the camera view. */
        if (world_dimension()==OVERWORLD && solar.y<0.f && world_moon_phase()!=4) {
            const float margin=.045f, forward_edge=-dot(f,solar)+margin;
            const float near=forward_edge/camera_scale();
            if (forward_edge>0.f && fabsf(dot(s,solar))<=near*aspect+margin && fabsf(dot(u,solar))<=near+margin) {
                sky_uniforms(atmosphere,eye,f,s,u,aspect,solar,seconds,0,h,out.clear_color,1.f);
                draw_sky(cmd,pass,pipe_basic_moon,atmosphere);
            }
        }
    }
    if (water_draws && !airEffects && !waterView.submerged) {
        SDL_BindGPUGraphicsPipeline(pass,pipe_water_flat);
        SDL_PushGPUVertexUniformData(cmd,0,transform,sizeof transform);
        SDL_PushGPUFragmentUniformData(cmd,0,lighting,sizeof lighting);
        bind_texture(pass,0,atlas,atlasSampler);
        draw_visible_water(pass,water_visible,water_draws);
    }
    if (!waterView.submerged) {
        /* Entity meshes share the flat lighting decoder. Only living models
           carry light codes; unlit effects keep their original alpha byte. */
        SDL_BindGPUGraphicsPipeline(pass,pipe_lit);
        SDL_PushGPUVertexUniformData(cmd,0,transform,sizeof transform);
        SDL_PushGPUFragmentUniformData(cmd,0,lighting,sizeof lighting);
        bind_texture(pass,0,atlas,atlasSampler);
    }
    draw_mesh(pass, &drop_mesh);
    draw_mesh(pass, &xp_mesh);
    draw_mesh(pass, &falling_mesh);
    draw_mesh(pass, &actor_mesh);
    draw_mesh(pass, &horse_mesh);
    draw_mesh(pass, &lead_mesh);
    draw_mesh(pass, &fishing_mesh);
    if (!waterView.submerged) draw_mesh(pass, &weather_mesh);
    draw_mesh(pass, &frame_mesh);
    if (frame_maps.count && frame_map_atlas) {
        bind_texture(pass,0,frame_map_atlas,atlasSampler);
        draw_mesh(pass,&frame_maps);
        bind_texture(pass,0,atlas,atlasSampler);
    }
    if (water_draws && waterView.submerged) {
        float waterViews[20];
        memcpy(waterViews,transform,sizeof transform);
        waterViews[16]=seconds; waterViews[17]=rain; waterViews[18]=(float)advanced; waterViews[19]=skyLight;
        float below[20];
        memcpy(below,optics,sizeof optics);
        below[16]=camera_scale()*snellZoom; below[17]=(float)snell_capture; below[18]=rain; below[19]=0.f;
        SDL_BindGPUGraphicsPipeline(pass,pipe_water_below);
        SDL_PushGPUVertexUniformData(cmd,0,waterViews,sizeof waterViews);
        SDL_PushGPUFragmentUniformData(cmd,0,below,sizeof below);
        bind_texture(pass,0,snell_capture ? mirrorColor : atlas,smoothSampler);
        draw_visible_water(pass,water_visible,water_draws);
    } else if (water_draws && airEffects) {
        float waterViews[20];
        memcpy(waterViews,transform,sizeof transform);
        waterViews[16]=seconds; waterViews[17]=rain; waterViews[18]=1.f; waterViews[19]=skyLight;
        /* Streams without a suitable planar capture still get slope lighting,
           animated ripples and the Fresnel reflection of the sky color. */
        float water[40]={eye.x,eye.y,eye.z,seconds,solar.x,solar.y,solar.z,rain,
            .18f,.44f,.50f,plane,.027f,.12f,.21f,1.f,
            airClear.r,airClear.g,airClear.b,plane>=0.f ? .94f : 0.f};
        if (plane>=0.f) {
            memcpy(water+20,reflected,sizeof transform);
            water[36]=1.f/mirrorW; water[37]=1.f/mirrorH;
        }
        water[38]=vision;
        SDL_BindGPUGraphicsPipeline(pass,pipe_water);
        SDL_PushGPUVertexUniformData(cmd,0,waterViews,sizeof waterViews);
        SDL_PushGPUFragmentUniformData(cmd,0,water,sizeof water);
        bind_texture(pass,0,plane>=0.f ? mirrorColor : atlas,smoothSampler);
        draw_visible_water(pass,water_visible,water_draws);
    }
    SDL_EndGPURenderPass(pass);
    return SDL_SubmitGPUCommandBuffer(cmd);
}

static int open_surface(int width, int height) {
    win = SDL_CreateWindow("方块沙盒", width, height, SDL_WINDOW_RESIZABLE);
    if (!win) return 0;
    if (SDL_ClaimWindowForGPUDevice(dev, win)) {
        surface_created_w=width; surface_created_h=height;
        return 1;
    }
    SDL_DestroyWindow(win);
    win = NULL;
    return 0;
}

static int resize_drawable(int width, int height) {
    static int surface_w = START_W, surface_h = START_H;
    if (width < 1 || height < 1) return 0;
    if (win && width == surface_w && height == surface_h) return 1;
    if (!win || !SDL_SetWindowSize(win, width, height)) {
        /* The CCD video driver may not resize a claimed window in place.
           Replace just the drawable, keeping the GPU device and meshes. */
        if (win) {
            SDL_ReleaseWindowFromGPUDevice(dev, win);
            SDL_DestroyWindow(win);
        }
        win = NULL;
        if (!open_surface(width, height)) {
            fprintf(stderr, "resize %d x %d: %s\n", width, height, SDL_GetError());
            return 0;
        }
    }
    surface_w = width;
    surface_h = height;
    return 1;
}

int renderer_resize(int width,int height) {
    if (width<1 || height<1 || width>4096 || height>4096) return 0;
    view_w=width; view_h=height;
    float scale=fminf(2.f,sqrtf(1120000.f/((float)width*height)));
    scale=fminf(scale,4096.f/SDL_max(width,height));
    hd_w=SDL_max(1,(int)roundf(width*scale)); hd_h=SDL_max(1,(int)roundf(height*scale));
    return 1;
}

int renderer_ready(void) { return win && dev && pipe && pipe_water && atlas && shadowSampler; }
const char *renderer_startup_error(void) { return renderer_error; }
static int renderer_startup_failed(const char *step) {
    const char *detail=SDL_GetError();
    snprintf(renderer_error,sizeof renderer_error,"%s：%s",step,detail && *detail ? detail : "设备未提供详情");
    fprintf(stderr,"CCDMC: %s\n",renderer_error);
    fflush(stderr);
    if (win) {
        if (dev) SDL_ReleaseWindowFromGPUDevice(dev,win);
        SDL_DestroyWindow(win); win=NULL;
    }
    if (dev) {
        if (smoothSampler) SDL_ReleaseGPUSampler(dev,smoothSampler);
        if (shadowSampler) SDL_ReleaseGPUSampler(dev,shadowSampler);
        smoothSampler=NULL; shadowSampler=NULL;
        drop_scene_texture(&shadowMap); drop_scene_texture(&skyColor);
        drop_scene_texture(&mirrorColor); drop_scene_texture(&mirrorDepth);
        SDL_GPUGraphicsPipeline **effects[]={&pipe_world,&pipe_unshadowed,&pipe_shadow,&pipe_reflection,&pipe_sky,
            &pipe_mirror_sky,&pipe_present,&pipe_water,&pipe_water_flat,&pipe_basic_moon,&pipe_underwater,&pipe_water_below};
        for (unsigned i=0;i<sizeof effects/sizeof *effects;++i)
            if (*effects[i]) { SDL_ReleaseGPUGraphicsPipeline(dev,*effects[i]); *effects[i]=NULL; }
        if (atlasSampler) SDL_ReleaseGPUSampler(dev,atlasSampler);
        if (atlas) SDL_ReleaseGPUTexture(dev,atlas);
        drop_scene_texture(&frame_map_atlas);
        frame_tiles=0;
        if (pipe_lit) SDL_ReleaseGPUGraphicsPipeline(dev,pipe_lit);
        if (pipe) SDL_ReleaseGPUGraphicsPipeline(dev,pipe);
        SDL_DestroyGPUDevice(dev); dev=NULL;
    }
    atlasSampler=NULL; atlas=NULL; pipe=NULL; pipe_lit=NULL;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    return 0;
}
int renderer_init(void) {
    init_atlas_coordinates();
    if (renderer_ready()) return 1;
    if (!SDL_Init(SDL_INIT_VIDEO)) return renderer_startup_failed("SDL 视频初始化失败");
    if (!ccd_sdl_use_ccdui()) return renderer_startup_failed("CCDUI 拒绝嵌入 SDL 画面");
    dev = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, false, NULL);
    if (!dev) return renderer_startup_failed("GPU 设备初始化失败");
    if (!open_surface(START_W, START_H)) return renderer_startup_failed("SDL 窗口初始化失败");
    if (!make_scene_pipelines()) return renderer_startup_failed("GPU 着色器初始化失败");
    if (!make_atlas()) return renderer_startup_failed("GPU 渲染资源初始化失败");
    SDL_GPUSamplerCreateInfo params={.min_filter=SDL_GPU_FILTER_LINEAR,.mag_filter=SDL_GPU_FILTER_LINEAR,
        .mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,.address_mode_v=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE};
    smoothSampler=SDL_CreateGPUSampler(dev,&params);
    if (!smoothSampler) return renderer_startup_failed("GPU 纹理采样器初始化失败");
    params.enable_compare=true; params.compare_op=SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
    shadowSampler=SDL_CreateGPUSampler(dev,&params);
    if (!shadowSampler) return renderer_startup_failed("GPU 深度采样器初始化失败");
    printf("Render distance: %d blocks (%d x %d chunks)\n", RENDER_CHUNKS * CHUNK_SIZE, MESH_SPAN, MESH_SPAN);
    fflush(stdout);
    return 1;
}
