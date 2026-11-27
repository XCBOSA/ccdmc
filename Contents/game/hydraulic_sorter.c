#include "hydraulic_sorter.h"
#include "chests.h"
#include "../world/rails.h"

enum { SORTER_CYCLE=16, SORTERS_PER_CHUNK_PULSE=8 };
enum { LIGHT_LANE, HEAVY_LANE };
enum { SORTER_DRY, SORTER_STILL, SORTER_FLOODED, SORTER_COLD, SORTER_READY };
typedef struct { int state,side,units; } FeedWater;

static FeedWater feed_water(int x,int y,int z) {
    int wet=0,side=-1,strength=0,raw=0;
    for (int d=0;d<4;++d) {
        if (!world_resident_cell(x+rail_dx(d),y,z+rail_dz(d),&raw))
            return (FeedWater){SORTER_COLD,-1,0};
        const int depth=water_strength(raw);
        if (!depth) continue;
        ++wet;
        if (raw!=WATER) { side=d; strength=depth; }
    }
    if (wet>1) return (FeedWater){SORTER_FLOODED,-1,0};
    if (side<0) return (FeedWater){wet ? SORTER_STILL : SORTER_DRY,-1,0};
    return (FeedWater){SORTER_READY,side,(strength+1)/2};
}
static int cargo_lane(int type) {
    switch (type) {
    case WOOD: case BIRCH_LOG: case SPRUCE_LOG: case JUNGLE_LOG: case ACACIA_LOG: case DARK_OAK_LOG:
    case PLANKS: case BIRCH_PLANK_ITEM: case SPRUCE_PLANK_ITEM: case JUNGLE_PLANK_ITEM:
    case ACACIA_PLANK_ITEM: case DARK_OAK_PLANK_ITEM:
    case STICK: case STRING: case LEATHER: case WOOL: case PAPER: case FEATHER:
    case WHEAT_SEEDS: case WHEAT: case CARROT: case POTATO: case PUMPKIN_SEEDS:
    case SUGAR_CANE: case APPLE: case PUMPKIN_ITEM:
        return LIGHT_LANE;
    case STONE: case COBBLESTONE: case SLATE_ITEM: case LIMESTONE_ITEM: case LAMINATE_ITEM:
    case JOINT_BASALT_ITEM: case BASALT_PAVING_ITEM: case TIDE_GRIT:
    case GRAVEL: case SAND: case RED_SAND: case SANDSTONE: case CLAY: case CLAY_BALL:
    case IRON_ORE: case GOLD_ORE: case COAL_ORE: case DIAMOND_ORE: case EMERALD_ORE_ITEM:
    case IRON_INGOT: case GOLD_INGOT: case QUARTZ: case LAPIS_ORE_ITEM:
    case GLASS: case STRIATED_GLASS: case BRICK_ITEM: case SINTERED_TILE_ITEM:
    case LANTERN_SCALES: case REED_FIN: case REED_GRIT:
        return HEAVY_LANE;
    default: return -1;
    }
}
static int next_cargo(const ChestView *view,int lane,int first) {
    for (int i=first;i<view->count*CHEST_SLOTS;++i) {
        const ItemStack item=chest_slot(view,i);
        if (item.count && cargo_lane(item.type)==lane) return i;
    }
    return -1;
}
static void sorter_pulse(BlockEdit e,unsigned tick) {
    if (e.cell!=HYDRAULIC_SORTER || e.y<=0 || e.y>=WY-1) return;
    const FeedWater feed=feed_water(e.x,e.y,e.z);
    if (feed.state!=SORTER_READY) return;
    ChestView source,out[2];
    if (!chest_automation_open_resident((ChestPos){e.x,e.y+1,e.z},&source)) return;
    const int exit=(feed.side+2)&3;
    const int opened[]={chest_automation_open_resident((ChestPos){e.x+rail_dx(exit),e.y,e.z+rail_dz(exit)},&out[0]),
        feed.units>=3 && chest_automation_open_resident((ChestPos){e.x,e.y-1,e.z},&out[1])};
    int slot[2]={opened[0] ? next_cargo(&source,LIGHT_LANE,0) : -1,
        opened[1] ? next_cargo(&source,HEAVY_LANE,0) : -1};
    const int cost[]={1,3};
    int budget=feed.units;
    const int first=(int)((tick/SORTER_CYCLE)&1u);
    while (budget>0) {
        int lane=-1;
        for (int n=0;n<2;++n) {
            int k=(first+n)&1;
            if (slot[k]>=0 && budget>=cost[k]) { lane=k; break; }
        }
        if (lane<0) break;
        const int i=slot[lane];
        const ItemStack item=chest_slot(&source,i);
        /* Commit destination before deducting the source. Each lane is
           FIFO; a jammed lane must not starve or reroute the other lane. */
        if (!chest_automation_insert(&out[lane],item)) { slot[lane]=-1; continue; }
        chest_automation_remove(&source,i);
        budget-=cost[lane];
        if (item.count==1) slot[lane]=next_cargo(&source,lane,i+1);
    }
}
static int chunk_of(int p) { return p/CHUNK_SIZE-(p%CHUNK_SIZE<0); }
static int residue(int p) { int r=p%3; return r<0 ? r+3 : r; }
static int batch_axis(int center,int desired) {
    return center+(residue(center-1)==desired ? -1 : residue(center)==desired ? 0 : 1);
}
void hydraulic_sorters_step(int x,int z,unsigned tick) {
    unsigned phase=tick%SORTER_CYCLE;
    if (phase>=9u || !world_has_sorters()) return;
    /* One resident chunk and a bounded sparse batch per tick. Each nearby
       chunk has one absolute time slot, even if the camera crosses a seam. */
    const int cx=batch_axis(chunk_of(x),(int)(phase/3u)),cz=batch_axis(chunk_of(z),(int)(phase%3u));
    BlockEdit sites[SORTERS_PER_CHUNK_PULSE];
    const int n=world_sorter_batch(cx,cz,sites,SORTERS_PER_CHUNK_PULSE);
    for (int i=0;i<n;++i) sorter_pulse(sites[i],tick);
}
const char *hydraulic_sorter_use(Hit h) {
    int raw;
    if (!h.hit || (unsigned)selected_slot>=HOTBAR_SIZE || inventory[selected_slot].count ||
        !world_resident_cell(h.x,h.y,h.z,&raw) || raw!=HYDRAULIC_SORTER) return NULL;
    const FeedWater f=feed_water(h.x,h.y,h.z);
    switch (f.state) {
    case SORTER_DRY: return "洗选槽停机 · 需要单侧流动水";
    case SORTER_STILL: return "洗选槽停机 · 静水没有动力";
    case SORTER_FLOODED: return "洗选槽停机 · 只保留一个入水面";
    case SORTER_COLD: return "洗选槽停机 · 等待邻区载入";
    default: break;
    }
    const int exit=(f.side+2)&3;
    if (h.y<=0 || h.y>=WY-1 || !world_resident_cell(h.x,h.y+1,h.z,&raw) || raw!=CHEST)
        return "洗选槽 · 顶部放普通进料箱";
    int below,side;
    if (!world_resident_cell(h.x,h.y-1,h.z,&below) ||
        !world_resident_cell(h.x+rail_dx(exit),h.y,h.z+rail_dz(exit),&side)) return "洗选槽 · 等待邻区载入";
    if (below!=CHEST || side!=CHEST) return "洗选槽 · 水流对面放轻料箱，下方放重料箱";
    return f.units<3 ? "洗选槽运转 · 水流弱，只搬轻料" : "洗选槽运转 · 轻料走侧面，重料沉到下方";
}
