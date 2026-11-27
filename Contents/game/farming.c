#include "farming.h"
#include "forestry.h"
#include "actors.h"
#include "boats.h"
#include "horses.h"
#include "minecarts.h"
#include "drops.h"
#include "experience.h"
#include "enchanting.h"
#include "weather.h"
#include "sound.h"
#include "../world/light.h"
#include "../world/mushrooms.h"
#include "../world/rails.h"
#include "../world/space.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

enum { FARM_RADIUS=2, FARM_RING=5, FARM_BATCH=12 };
static float elapsed;
static unsigned cursor[DIMENSION_COUNT],farm_tick_index;
static const int crop_items[]={WHEAT_SEEDS,CARROT,POTATO};
static const int crop_bases[]={WHEAT_BASE,CARROT_BASE,POTATO_BASE};

int farm_is_hoe(int type) { return type>=WOOD_HOE && type<=DIAMOND_HOE; }
static int change(int x,int y,int z,int raw) {
    const BlockEdit e={x,y,z,raw};
    return world_set_blocks(&e,1);
}
static unsigned roll(int x,int y,int z,unsigned serial) {
    return hash2(x+y*19+(int)(serial%1048576u)*9,z-y*31);
}
static int close_water(int x,int y,int z) {
    if (weather_wets(x,y+1,z)) return 1;
    for (int a=-4;a<=4;++a) for (int b=-4;b<=4;++b)
        if (block(x+a,y,z+b)==WATER || block(x+a,y+1,z+b)==WATER) return 1;
    return 0;
}
static int enough_light(int x,int y,int z) {
    return (player_rules.day_tick<12000 && light_has_open_sky(x,y,z)) || block_light_at(x,y,z)>=9;
}
static int fruit_space(int x,int y,int z) {
    BlockBox fruit={(float)x,(float)y,(float)z,x+1.f,y+1.f,z+1.f};
    return !box_overlap(fruit,body_box(px,py,pz,.28f,1.72f)) && !actors_overlap_cell(x,y,z) &&
        !boat_overlaps_cell(x,y,z) && !cart_overlaps_cell(x,y,z) && !horse_overlaps_cell(x,y,z);
}
static int can_till(int x,int y,int z) {
    return y+1<WY && (block(x,y+1,z)==AIR || block(x,y+1,z)==TALL_GRASS);
}
int farm_can_plant(int x,int y,int z) {
    return y>0 && y<WY && block(x,y,z)==AIR && block(x,y-1,z)==FARMLAND;
}
static void use_one(ItemStack *held) {
    if (player_rules.mode==SURVIVAL && !--held->count) *held=(ItemStack){0,0,0,0,0};
}
static void wear(ItemStack *held) {
    if (player_rules.mode==SURVIVAL) enchant_wear(held,1,0);
}
static int release_slots(int *slots,int count);
static void release(int slot,int x,int y,int z,ItemStack item);
static int cane_wet_soil(int x,int y,int z) {
    int t=block(x,y,z);
    return (t==GRASS || t==DIRT || t==SAND || t==RED_SAND) &&
        (block(x-1,y,z)==WATER || block(x+1,y,z)==WATER || block(x,y,z-1)==WATER || block(x,y,z+1)==WATER);
}
enum { FILTER_UNKNOWN=-1, FILTER_IDLE, FILTER_FEED, FILTER_COVER, FILTER_OUTLET, FILTER_MULTIPLE };
static int filter_feed(int x,int y,int z) {
    int top;
    if (!world_resident_cell(x,y+1,z,&top)) return FILTER_UNKNOWN;
    if (top!=AIR) return FILTER_COVER;
    int sides[4],inlet=-1;
    for (int d=0;d<4;++d)
        if (!world_resident_cell(x+rail_dx(d),y-1,z+rail_dz(d),&sides[d])) return FILTER_UNKNOWN;
    for (int d=0;d<4;++d) if (water_strength(sides[d])) {
        if (inlet>=0) return FILTER_MULTIPLE;
        inlet=d;
    }
    if (inlet<0 || sides[inlet]==WATER || water_strength(sides[inlet])<2) return FILTER_IDLE;
    return sides[(inlet+2)&3]==AIR ? FILTER_FEED : FILTER_OUTLET;
}
static const char *filter_tap(int x,int y,int z,int raw,ItemStack *held) {
    const int sips=raw>=FILTER_MAT_ONE ? raw-FILTER_MAT_ONE+1 : 0;
    const int feed=filter_feed(x,y,z);
    if (feed==FILTER_UNKNOWN) return "滤苔床附近尚未加载";
    if (feed==FILTER_COVER) return "滤苔床顶面要留空 · 已存水量不变";
    if (held->count==1 && field_flask_item(held->type)) {
        if (held->type==FIELD_FLASK_FULL) return "水囊已满 · 滤苔床未扣水";
        if (sips) {
            if (!world_set_farm_stage(x,y,z,raw-1)) return "暂时无法从滤苔床取水";
            ++held->type;
            sound_play(SOUND_SPLASH,.28f,1.f,(Vec3){x+.5f,y+.15f,z+.5f});
            return "已装入 1 口滤水";
        }
    } else if (sips) {
        return sips==2 ? "滤苔床 · 已蓄 2/2 口 · 用水囊取水" : "滤苔床 · 已蓄 1/2 口 · 用水囊取水";
    }
    if (feed==FILTER_MULTIPLE) return "滤苔床 · 只留一个进水侧";
    if (feed==FILTER_OUTLET) return "滤苔床 · 进水对侧要留出水口";
    if (feed==FILTER_FEED) return "滤苔床 · 正在渗滤";
    return "滤苔床 · 床基一侧需接流动水";
}
static const char *break_cane(int x,int y,int z,int soil) {
    BlockEdit edit[4];
    int stems=0,n=0; ItemStack item={0,0,0,0,0};
    if (soil) {
        item=(ItemStack){(uint16_t)mining_drop(block(x,y,z),0),1,0,0,0};
        edit[n++]=(BlockEdit){x,y,z,AIR};
        ++y;
    }
    while (stems<3 && block(x,y+stems,z)==SUGAR_CANE) {
        edit[n++]=(BlockEdit){x,y+stems,z,AIR}; ++stems;
    }
    int needed=player_rules.mode==SURVIVAL ? 1+(item.type!=AIR) : 0;
    int slots[2];
    if (!release_slots(slots,needed)) return "掉落区已满，没有移除甘蔗";
    if (!stems || !world_set_blocks(edit,n)) return "无法记录甘蔗收获";
    if (needed) {
        release(slots[0],x,y,z,(ItemStack){SUGAR_CANE,(uint8_t)stems,0,0,0});
        if (needed==2) release(slots[1],x,y-1,z,item);
    }
    if (!soil && y>0 && block(x,y-1,z)==SUGAR_CANE && !world_has_edit(x,y-1,z))
        world_restore_edit(x,y-1,z,SUGAR_CANE);
    return "已收获甘蔗";
}
static int release_slots(int *slots,int count) {
    int found=0;
    for (int i=0;i<MAXDROP && found<count;++i) if (!drops[i].item.count) slots[found++]=i;
    return found==count;
}
static void release(int slot,int x,int y,int z,ItemStack item) {
    drops[slot]=make_drop(x+.46f,y+.30f,z+.49f,item);
    drops_dirty=1;
}
static ItemStack cocoa_yield(int raw) {
    return (ItemStack){COCOA_BEANS,(uint8_t)(cocoa_stage(raw)==2 ? 3 : 1),0,0,0};
}
int farm_attached_cocoa(int x,int y,int z,BlockEdit edits[4],ItemStack loot[4]) {
    int n=0;
    for (int d=0;d<4;++d) {
        int a=x+rail_dx(d),b=z+rail_dz(d),raw;
        if (world_resident_cell(a,y,b,&raw) && cocoa_cell(raw) && cocoa_support_side(raw)==((d+2)&3)) {
            edits[n]=(BlockEdit){a,y,b,AIR};
            loot[n++]=cocoa_yield(raw);
        }
    }
    return n;
}
static int cactus_site(int x,int y,int z) {
    int below;
    if (y<1 || y>=WY) return 0;
    if (!world_resident_cell(x,y-1,z,&below)) return -1;
    int soil=cell_type(below);
    if (soil!=CACTUS && soil!=SAND && soil!=RED_SAND) return 0;
    for (int d=0;d<4;++d) {
        int side;
        if (!world_resident_cell(x+rail_dx(d),y,z+rail_dz(d),&side)) return -1;
        if (solid(cell_type(side)) || lava_strength(side)) return 0;
    }
    return 1;
}
static int cactus_stack_below(int x,int y,int z) {
    int height=0,raw;
    while (height<3 && y-height>=0 &&
        world_resident_cell(x,y-height,z,&raw) && cactus_cell(raw)) ++height;
    return height;
}
static const char *break_cactus(int x,int y,int z,int soil) {
    BlockEdit e[4];
    int n=0,stem=0,base=y;
    ItemStack ground={0};
    if (soil) {
        ground=(ItemStack){(uint16_t)mining_drop(block(x,y,z),0),1,0,0,0};
        e[n++]=(BlockEdit){x,y++,z,AIR};
    }
    while (stem<3 && block(x,y+stem,z)==CACTUS) {
        e[n++]=(BlockEdit){x,y+stem,z,AIR};
        ++stem;
    }
    int loot=player_rules.mode==SURVIVAL ? 1+(ground.type!=AIR) : 0;
    int slots[2];
    if (!release_slots(slots,loot)) return "掉落区已满，没有移除仙人掌";
    if (!stem || !world_set_blocks(e,n)) return "无法记录仙人掌收获";
    if (loot) {
        release(slots[0],x,y,z,(ItemStack){CACTUS_ITEM,(uint8_t)stem,0,0,0});
        if (loot==2) release(slots[1],x,base,z,ground);
    }
    /* Activate the edit-indexed farm scheduler for a surviving wild stem. */
    int raw;
    if (!soil && world_resident_cell(x,y-1,z,&raw) && cactus_cell(raw) && !world_has_edit(x,y-1,z))
        (void)world_restore_edit(x,y-1,z,raw);
    return "已收获仙人掌";
}
static int harvest_items(int x,int y,int z,int raw,ItemStack out[2]) {
    int type=cell_type(raw),stage=(raw-FARM_BASE)&7;
    unsigned r=roll(x,y,z,farm_tick_index+player_rules.day_tick);
    if (type==CROP_WHEAT) {
        out[0]=(ItemStack){WHEAT_SEEDS,(uint8_t)(stage==7 ? 1+r%3u : 1),0,0,0};
        if (stage==7) { out[1]=(ItemStack){WHEAT,1,0,0,0}; return 2; }
        return 1;
    }
    int kind=type-CROP_WHEAT;
    out[0]=(ItemStack){(uint16_t)crop_items[kind],(uint8_t)(stage==7 ? 2+r%3u : 1),0,0,0};
    return 1;
}
const char *farm_break(Hit h) {
    if (!h.hit) return NULL;
    int x=h.x,y=h.y,z=h.z,raw=world_cell(x,y,z),type=cell_type(raw);
    if (type==SUGAR_CANE) return break_cane(x,y,z,0);
    if ((type==DIRT || type==GRASS || type==SAND || type==RED_SAND) && block(x,y+1,z)==SUGAR_CANE)
        return break_cane(x,y,z,1);
    if (type==CACTUS) return break_cactus(x,y,z,0);
    if ((type==SAND || type==RED_SAND) && block(x,y+1,z)==CACTUS) return break_cactus(x,y,z,1);
    ItemStack items[3];
    int n=0,add=0;
    if (cocoa_cell(raw)) items[n++]=cocoa_yield(raw);
    else if (crop_type(type)) n=harvest_items(x,y,z,raw,items);
    else if (type==PUMPKIN_STEM) {
        int seeds=1+(int)(roll(x,y,z,farm_tick_index)%3u)*(raw==PUMPKIN_STEM_END-1);
        items[n++]=(ItemStack){PUMPKIN_SEEDS,(uint8_t)seeds,0,0,0};
    }
    else if (type==FILTER_MAT) {
        items[n++]=(ItemStack){FILTER_MOSS,1,0,0,0};
    } else if (type==ASH_SAND && filter_mat_cell(world_cell(x,y+1,z))) {
        items[n++]=(ItemStack){ASH_SAND,1,0,0,0};
        items[n++]=(ItemStack){FILTER_MOSS,1,0,0,0};
        add=1;
    }
    else if (type==TALL_GRASS) {
        if (roll(x,y,z,farm_tick_index+player_rules.day_tick)%8u==0)
            items[n++]=(ItemStack){WHEAT_SEEDS,1,0,0,0};
    } else if (type==FARMLAND) {
        items[n++]=(ItemStack){DIRT,1,0,0,0};
        int above=world_cell(x,y+1,z);
        if (crop_type(cell_type(above))) { n+=harvest_items(x,y+1,z,above,items+n); add=1; }
        else if (pumpkin_stem_cell(above)) { items[n++]=(ItemStack){PUMPKIN_SEEDS,1,0,0,0}; add=1; }
        else if (sapling_cell(above)) {
            items[n++]=(ItemStack){(uint16_t)tree_sapling_drop(above),1,0,0,0};
            add=1;
        }
    } else return NULL;
    if (player_rules.mode==CREATIVE) n=0;
    int slots[3];
    if (!release_slots(slots,n)) return "掉落区已满，没有移除作物";
    const BlockEdit e[]={{x,y,z,AIR},{x,y+1,z,AIR}};
    if (!world_set_blocks(e,1+add)) return "无法记录收获";
    for (int i=0;i<n;++i) release(slots[i],x,y,z,items[i]);
    return n ? "已收获，走近拾取" : "已清理";
}
int mushroom_can_stay(int x,int y,int z) {
    int raw;
    if (!world_resident_cell(x,y-1,z,&raw)) return 0;
    int below=cell_type(raw);
    return below==MYCELIUM || below==PODZOL ||
        (solid(below) && below!=GLASS && !stained_glass_cell(raw) && below!=FARMLAND &&
            !light_has_open_sky(x,y,z) && block_light_at_resident(x,y,z)<13);
}

enum { MUSHROOM_BONE_GROWN=6, MUSHROOM_BONE_TRIED=7 };
static int mushroom_bonemeal(int x,int y,int z,int raw) {
    int support;
    if (!world_resident_cell(x,y-1,z,&support)) return -1;
    int soil=cell_type(support);
    if (soil!=DIRT && soil!=GRASS && soil!=PODZOL && soil!=MYCELIUM) return -1;
    int height=4+(int)(hash2(x+y*17,z)%3u);
    if (y+height>=WY) return -1;
    BlockEdit edits[80];
    BlockBox player=body_box(px,py,pz,.28f,1.72f);
    int count=0;
    for (int h=0;h<=height;++h) for (int dx=-MUSHROOM_RADIUS;dx<=MUSHROOM_RADIUS;++dx)
        for (int dz=-MUSHROOM_RADIUS;dz<=MUSHROOM_RADIUS;++dz) {
            int part=huge_mushroom_part(raw==RED_MUSHROOM,height,dx,h,dz);
            if (part==AIR) continue;
            int bx=x+dx,by=y+h,bz=z+dz,previous;
            if (!inworld(bx,by,bz) || !world_resident_cell(bx,by,bz,&previous) ||
                ((!h && !dx && !dz) ? previous!=raw : previous!=AIR && previous!=TALL_GRASS &&
                    previous!=SNOW_LAYER && cell_type(previous)!=LEAF) ||
                placed_leaf_cell(previous) ||
                box_overlap(player,(BlockBox){(float)bx,(float)by,(float)bz,bx+1.f,by+1.f,bz+1.f}) ||
                actors_overlap_cell(bx,by,bz) || horse_overlaps_cell(bx,by,bz) ||
                boat_overlaps_cell(bx,by,bz) || cart_overlaps_cell(bx,by,bz) ||
                count==(int)(sizeof edits/sizeof *edits)) return -1;
            edits[count++]=(BlockEdit){bx,by,bz,part};
        }
    if (player_rules.mode==SURVIVAL && experience_roll(0,99)>=40u) return MUSHROOM_BONE_TRIED;
    return world_set_blocks(edits,count) ? MUSHROOM_BONE_GROWN : -1;
}
int farm_bonemeal(int x,int y,int z) {
    if (!inworld(x,y,z) || !world_chunk_ready(x,z)) return -1;
    int raw=world_cell(x,y,z),type=cell_type(raw);
    if (small_mushroom(raw)) return mushroom_bonemeal(x,y,z,raw);
    if (type==SAPLING) return tree_bonemeal(x,y,z);
    if (cocoa_cell(raw) && cocoa_stage(raw)<2) {
        int d=cocoa_support_side(raw),log;
        if (!world_resident_cell(x+rail_dx(d),y,z+rail_dz(d),&log) || log!=JUNGLE_LOG) return 0;
        return world_set_farm_stage(x,y,z,raw+4) ? 1 : -1;
    }
    if (type==PUMPKIN_STEM && raw<PUMPKIN_STEM_END-1) {
        int next=raw+2+(int)(roll(x,y,z,farm_tick_index+player_rules.day_tick)%4u);
        return world_set_farm_stage(x,y,z,next<PUMPKIN_STEM_END ? next : PUMPKIN_STEM_END-1) ? 1 : -1;
    }
    if (crop_type(type) && ((raw-FARM_BASE)&7)<7) {
        int advance=2+(int)(roll(x,y,z,farm_tick_index+player_rules.day_tick)%4u);
        int stage=(raw-FARM_BASE)&7,base=raw-stage;
        int next=stage+advance>7 ? 7 : stage+advance;
        return world_set_farm_stage(x,y,z,base+next) ? 1 : -1;
    }
    if (type!=GRASS || y+1>=WY) return 0;
    BlockEdit growth[25];
    int n=0;
    for (int a=-2;a<=2;++a) for (int b=-2;b<=2;++b)
        if (a*a+b*b<=5 && world_chunk_ready(x+a,z+b) && block(x+a,y,z+b)==GRASS &&
            block(x+a,y+1,z+b)==AIR && ((!a && !b) || roll(x+a,y,z+b,farm_tick_index)%3u!=0)) {
            unsigned r=roll(x+a,y,z+b,farm_tick_index+331u);
            int flower=(a || b) && r%5u==0;
            growth[n++]=(BlockEdit){x+a,y+1,z+b,flower ? ((r>>9)&1u ? DANDELION_CELL : POPPY_CELL) : TALL_GRASS};
        }
    return n ? world_set_blocks(growth,n) ? 2 : -1 : -1;
}
const char *farm_use(Hit h) {
    if (!h.hit || selected_slot<0 || selected_slot>=HOTBAR_SIZE || !player_rules.health) return NULL;
    ItemStack *held=&inventory[selected_slot];
    int x=h.x,y=h.y,z=h.z,raw=world_cell(x,y,z),type=cell_type(raw);
    if (type==FILTER_MAT) return filter_tap(x,y,z,raw,held);
    if (held->count && held->type==FILTER_MOSS && type==ASH_SAND) {
        int surface,top;
        if (h.ay!=y+1 || y+2>=WY || !world_resident_cell(x,y+1,z,&surface) ||
            !world_resident_cell(x,y+2,z,&top) || surface!=AIR || top!=AIR)
            return "滤苔要铺在灰砂床基顶面，上方留空";
        if (!change(x,y+1,z,FILTER_MAT_SEEDED)) return "暂时无法铺滤苔";
        use_one(held);
        return "已铺滤苔 · 给床基侧面接单侧流动水";
    }
    if (held->count && held->type==CACTUS_ITEM) {
        if (h.ay!=y+1 || block(x,y+1,z)!=AIR || cactus_site(x,y+1,z)!=1 ||
            cactus_stack_below(x,y,z)>=3 || !fruit_space(x,y+1,z))
            return "仙人掌需种在沙或仙人掌顶面，四周留空";
        if (!change(x,y+1,z,CACTUS_BASE)) return "暂时无法种植仙人掌";
        use_one(held);
        return "已种仙人掌";
    }
    if (held->count && held->type==COCOA_BEANS && type==JUNGLE_LOG) {
        int dir=-1;
        for (int d=0;d<4;++d) if (x==h.ax+rail_dx(d) && z==h.az+rail_dz(d)) dir=d;
        if (dir<0 || h.ay!=y || block(h.ax,y,h.az)!=AIR) return "对准空着的丛林原木侧面种可可";
        if (!change(h.ax,y,h.az,COCOA_BASE+dir)) return "暂时无法种植可可";
        use_one(held);
        return "已种可可";
    }
    if (held->count && held->type==SUGAR_CANE) {
        if (h.ay!=y+1 || y+1>=WY || block(x,y+1,z)!=AIR || !cane_wet_soil(x,y,z))
            return "甘蔗要种在挨着水的泥土、草地或沙上";
        if (!set_block(x,y+1,z,SUGAR_CANE)) return "暂时无法种植甘蔗";
        use_one(held);
        return "已种甘蔗";
    }
    if (held->count && farm_is_hoe(held->type) && type==TALL_GRASS && block(x,y-1,z)==GRASS) {
        --y;
        h.ay=y+1;
        type=GRASS;
    }
    if (held->count && farm_is_hoe(held->type) && (type==GRASS || type==DIRT)) {
        if (!can_till(x,y,z) || h.ay<=y) return "从上方对准无遮挡的泥土或草地";
        BlockEdit e[]={{x,y,z,FARM_BASE+(close_water(x,y,z) ? 7 : 0)},{x,y+1,z,AIR}};
        if (!world_set_blocks(e,2)) return "无法开垦";
        wear(held);
        return "已开垦耕地";
    }
    if (held->count && held->type==BONE_MEAL) {
        int applied=farm_bonemeal(x,y,z);
        if (applied<0) return "暂时无法施肥";
        if (applied) {
            use_one(held);
            if (applied==MUSHROOM_BONE_GROWN) return "蘑菇长成了巨型蘑菇";
            if (applied==MUSHROOM_BONE_TRIED) return "施过骨粉，蘑菇暂未长大";
            if (applied==TREE_BONE_STAGE) return "树苗再长大了一些";
            if (applied==TREE_BONE_GROWN) return "树苗长成了树";
            if (applied==TREE_BONE_TRIED) return "施过骨粉，树苗暂未长大";
            return applied==1 ? "骨粉加速了作物生长" : "草地上长出了高草和花";
        }
    }
    if (held->count && type==FARMLAND) {
        if (held->type==PUMPKIN_SEEDS) {
            if (!farm_can_plant(x,y+1,z) || h.ay!=y+1) return "南瓜种子要种在耕地顶面";
            if (!change(x,y+1,z,PUMPKIN_STEM_BASE)) return "暂时无法播种";
            use_one(held);
            return "已种下南瓜种子";
        }
        for (int i=0;i<3;++i) if (held->type==crop_items[i]) {
            if (!farm_can_plant(x,y+1,z)) return "耕地上方放不下种子";
            if (!change(x,y+1,z,crop_bases[i])) return "暂时无法播种";
            use_one(held);
            return "播种成功";
        }
    }
    if (crop_type(type)) return ((raw-FARM_BASE)&7)==7 ? farm_break(h) : "作物还没有成熟";
    return NULL;
}

void farm_trample(int x,int y,int z,float distance) {
    if (distance<.55f || block(x,y,z)!=FARMLAND) return;
    int crop=world_cell(x,y+1,z);
    ItemStack items[2];
    int has_crop=crop_type(cell_type(crop));
    int n=has_crop && player_rules.mode==SURVIVAL ? harvest_items(x,y+1,z,crop,items) : 0;
    int slots[2];
    if (!release_slots(slots,n)) return;
    BlockEdit e[]={{x,y,z,DIRT},{x,y+1,z,AIR}};
    if (!world_set_blocks(e,1+has_crop)) return;
    for (int i=0;i<n;++i) release(slots[i],x,y+1,z,items[i]);
}
static void tick(BlockEdit e,unsigned now) {
    int raw=world_cell(e.x,e.y,e.z),type=cell_type(raw);
    if (small_mushroom(raw)) {
        if (!mushroom_can_stay(e.x,e.y,e.z)) {
            int slots[1];
            if (release_slots(slots,1) && change(e.x,e.y,e.z,AIR))
                release(slots[0],e.x,e.y,e.z,(ItemStack){(uint16_t)(raw==RED_MUSHROOM ?
                    RED_MUSHROOM_ITEM : BROWN_MUSHROOM_ITEM),1,0,0,0});
        } else {
            unsigned r=roll(e.x,e.y,e.z,now);
            if (r%25u) return;
            /* Radius four and a five-mushroom cap prevent unbounded spread. */
            int nearby=0;
            for (int a=-4;a<=4;++a) for (int b=-4;b<=4;++b) for (int h=-1;h<=1;++h) {
                int prior;
                if (world_resident_cell(e.x+a,e.y+h,e.z+b,&prior) && prior==raw && ++nearby>=5) return;
            }
            for (unsigned i=0;i<4;++i) {
                unsigned bits=roll(e.x,e.y,e.z,now+i*37u+83u);
                int x=e.x+(int)(bits%3u)-1,y=e.y+(int)((bits>>6)%3u)-1,z=e.z+(int)((bits>>9)%3u)-1,there;
                if (world_resident_cell(x,y,z,&there) && there==AIR && mushroom_can_stay(x,y,z)) {
                    (void)change(x,y,z,raw);
                    break;
                }
            }
        }
        return;
    }
    if (type==SAPLING) { tree_tick(e,now); return; }
    if (type==CACTUS) {
        int top=e.y+1,above,site=cactus_site(e.x,e.y,e.z);
        if (site==0) { (void)break_cactus(e.x,e.y,e.z,0); return; }
        if (site<0 || !world_resident_cell(e.x,top,e.z,&above) || above!=AIR || top>=WY ||
            cactus_stack_below(e.x,e.y,e.z)>=3 || cactus_site(e.x,top,e.z)!=1 ||
            roll(e.x,e.y,e.z,now)%12u) return;
        if (raw<CACTUS_END-1) (void)world_set_farm_stage(e.x,e.y,e.z,raw+1);
        else if (fruit_space(e.x,top,e.z)) {
            BlockEdit plant[]={{e.x,e.y,e.z,CACTUS_BASE},{e.x,top,e.z,CACTUS_BASE}};
            (void)world_set_blocks(plant,2);
        }
        return;
    }
    if (type==COCOA) {
        int d=cocoa_support_side(raw),log;
        if (!world_resident_cell(e.x+rail_dx(d),e.y,e.z+rail_dz(d),&log)) return;
        if (log!=JUNGLE_LOG) { (void)farm_break((Hit){1,e.x,e.y,e.z,0,0,0}); return; }
        if (cocoa_stage(raw)<2 && roll(e.x,e.y,e.z,now)%48u==0)
            (void)world_set_farm_stage(e.x,e.y,e.z,raw+4);
        return;
    }
    if (type==PUMPKIN_STEM) {
        int x=e.x,y=e.y,z=e.z;
        if (block(x,y-1,z)!=FARMLAND) {
            (void)farm_break((Hit){1,x,y,z,0,0,0});
            return;
        }
        unsigned r=roll(x,y,z,now);
        if (r%64u || !((player_rules.day_tick<12000 && light_has_open_sky(x,y,z)) ||
            block_light_at_resident(x,y,z)>=9)) return;
        if (raw<PUMPKIN_STEM_END-1) {
            (void)world_set_farm_stage(x,y,z,raw+1);
            return;
        }
        int options=0,dir=(int)((r>>12)&3u);
        for (int d=0;d<4;++d) {
            int a=x+rail_dx(d),b=z+rail_dz(d),above,below;
            if (!world_resident_cell(a,y,b,&above)) return;
            if (pumpkin_cell(above)) return;
            if (above==AIR && world_resident_cell(a,y-1,b,&below) && tree_soil(cell_type(below)) &&
                fruit_space(a,y,b)) options|=1<<d;
        }
        for (int k=0;k<4;++k) {
            int d=(dir+k)&3;
            if (options&(1<<d)) {
                (void)change(x+rail_dx(d),y,z+rail_dz(d),PUMPKIN_FACE_BASE+((d+2)&3));
                return;
            }
        }
        return;
    }
    if (type==SUGAR_CANE) {
        int base=e.y;
        while (base>0 && e.y-base<2 && block(e.x,base-1,e.z)==SUGAR_CANE) --base;
        if (!cane_wet_soil(e.x,base-1,e.z)) { break_cane(e.x,base,e.z,0); return; }
        int height=0;
        while (height<3 && block(e.x,base+height,e.z)==SUGAR_CANE) ++height;
        if (height<3 && block(e.x,base+height,e.z)==AIR &&
            roll(e.x,e.y,e.z,now)%80u==0) set_block(e.x,base+height,e.z,SUGAR_CANE);
        return;
    }
    if (type==FILTER_MAT) {
        int soil;
        if (!world_resident_cell(e.x,e.y-1,e.z,&soil)) return;
        if (soil!=ASH_SAND) { farm_break((Hit){1,e.x,e.y,e.z,0,0,0}); return; }
        /* Bounded by the existing nearby-farm round-robin. No requests for
           offscreen chunks and no repeated writes when full or disconnected. */
        if (raw<FILTER_MAT_TWO && filter_feed(e.x,e.y,e.z)==FILTER_FEED)
            (void)world_set_farm_stage(e.x,e.y,e.z,raw+1);
        return;
    }
    if (type==FARMLAND) {
        int wet=raw-FARM_BASE,next=wet;
        /* Full hydration is rechecked every fourth sample, not every frame. */
        if (wet==7 && (roll(e.x,e.y,e.z,now)&3u)!=0) return;
        if (close_water(e.x,e.y,e.z)) next=7;
        else if (wet) next=wet-1;
        else if (!crop_type(block(e.x,e.y+1,e.z)) && block(e.x,e.y+1,e.z)!=PUMPKIN_STEM && (roll(e.x,e.y,e.z,now)&7u)==0) {
            change(e.x,e.y,e.z,DIRT); return;
        }
        if (next!=wet) world_set_farm_stage(e.x,e.y,e.z,FARM_BASE+next);
    } else if (crop_type(type)) {
        int soil=world_cell(e.x,e.y-1,e.z);
        if (cell_type(soil)!=FARMLAND) {
            farm_break((Hit){1,e.x,e.y,e.z,0,0,0});
            return;
        }
        unsigned chance=soil>FARM_BASE ? 56u : 140u;
        if (((raw-FARM_BASE)&7)<7 && roll(e.x,e.y,e.z,now)%chance==0 &&
            enough_light(e.x,e.y,e.z)) world_set_farm_stage(e.x,e.y,e.z,raw+1);
    }
}
void farm_reset(void) {
    elapsed=0; farm_tick_index=0;
    for (int d=0;d<DIMENSION_COUNT;++d) cursor[d]=0;
}
void farm_step(float dt) {
    if (!(dt>0.f && dt<=.1f)) return;
    elapsed+=dt;
    if (elapsed<.25f) return;
    elapsed-=.25f;
    int cx=(int)floorf(px/CHUNK_SIZE),cz=(int)floorf(pz/CHUNK_SIZE);
    unsigned *slot=&cursor[world_dimension()];
    BlockEdit batch[FARM_BATCH];
    int remaining=FARM_BATCH;
    ++farm_tick_index;
    for (int i=0;i<FARM_RING*FARM_RING && remaining;++i) {
        unsigned key=(*slot)++%(FARM_RING*FARM_RING);
        int x=cx+(int)(key%FARM_RING)-FARM_RADIUS,z=cz+(int)(key/FARM_RING)-FARM_RADIUS;
        int n=world_farm_batch(x,z,batch,remaining);
        remaining-=n;
        for (int j=0;j<n;++j) tick(batch[j],farm_tick_index);
    }
}
