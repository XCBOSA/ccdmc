#include "seep.h"
#include "../world/light.h"

int seep_wet_faces(int x,int y,int z) {
    int cover;
    if (y<0 || y>=WY-1 || !inworld(x,y,z)) return -1;
    if (!world_resident_cell(x,y+1,z,&cover)) return -2;
    if (cover!=AIR) return -1;
    const int faces[4][2]={{1,0},{-1,0},{0,1},{0,-1}};
    int wet=0;
    for (int i=0;i<4;++i) {
        int raw;
        if (!world_resident_cell(x+faces[i][0],y,z+faces[i][1],&raw)) return -2;
        wet+=water_strength(raw)>0;
    }
    return wet;
}

const char *seep_use(Hit hit) {
    if (!hit.hit || (unsigned)selected_slot>=HOTBAR_SIZE ||
        !inworld(hit.x,hit.y,hit.z)) return NULL;
    int raw;
    if (!world_resident_cell(hit.x,hit.y,hit.z,&raw) || raw!=SEEP_BED) return NULL;
    ItemStack held=inventory[selected_slot];
    /* A placeable item should still be able to touch down on the bed. */
    if (held.count && held.type!=CLAY_BALL) return NULL;
    int wet=seep_wet_faces(hit.x,hit.y,hit.z);
    if (wet==-2) return "渗光床 · 等附近水道加载";
    if (wet<0) return "渗光床 · 顶部留空，不能盖住或浸没";
    if (!wet) return "渗光床 · 让水贴着侧面流过";
    if (!held.count) return wet==1 ? "渗光床 · 黏土可制柔光砖；引水至另一侧可制高亮砖" :
        "渗光床 · 双面供水充足，拿黏土做高亮砖";
    int made=wet==1 ? SEEP_TILE_ITEM : SEEP_BRIGHT_ITEM;
    if (!inventory_fill_container(selected_slot,(ItemStack){(uint16_t)made,2,0,0,0}))
        return "背包空间不足，黏土未消耗";
    return wet==1 ? "已制成 2 块柔光陶砖" : "已制成 2 块高亮陶砖";
}

const char *settling_mold_use(Hit hit) {
    if (!hit.hit || (unsigned)selected_slot>=HOTBAR_SIZE) return NULL;
    int raw;
    if (!world_resident_cell(hit.x,hit.y,hit.z,&raw) || !settling_cell(raw)) return NULL;
    ItemStack *slot=&inventory[selected_slot];
    int held=slot->count ? slot->type : 0;
    /* Other materials, buckets and tools keep their normal use. */
    if (held && held!=LIMESTONE_ITEM && held!=SLATE_ITEM) return NULL;
    if (world_dimension()!=OVERWORLD) return "石浆模床 · 请在地表世界布置水岸";
    int wet=seep_wet_faces(hit.x,hit.y,hit.z);
    if (wet==-2) return "石浆模床 · 等附近水道加载";
    if (wet<0) return "石浆模床 · 顶部要留空";
    if (raw==SETTLING_PACKED) {
        if (wet) return "板坯已压实 · 请先排掉侧面的水";
        if (!light_has_open_sky_resident(hit.x,hit.y+1,hit.z))
            return "板坯已排水 · 移除上方遮挡，让模床露天";
        if (held) return "板坯已可脱模 · 空手轻点取板";
        if (!set_block(hit.x,hit.y,hit.z,SETTLING_MOLD)) return "暂时无法脱模，板坯仍在";
        *slot=(ItemStack){LAMINATE_ITEM,3,0,0,0};
        return "已脱模 · 取出 3 块层压砌板";
    }
    if (!wet) return "石浆模床 · 让水贴着侧面再投料";
    if (raw==SETTLING_MOLD && held!=LIMESTONE_ITEM) return "石浆模床 · 拿孔隙石灰岩轻点调浆";
    if (raw==SETTLING_LIME && held!=SLATE_ITEM) return "石灰浆已就绪 · 拿层理页岩轻点压入";
    int next=raw==SETTLING_MOLD ? SETTLING_LIME : SETTLING_PACKED;
    if (!set_block(hit.x,hit.y,hit.z,next)) return "暂时无法投料，材料未消耗";
    if (player_rules.mode==SURVIVAL && !--slot->count) *slot=(ItemStack){0,0,0,0,0};
    return next==SETTLING_LIME ? "石灰浆已就绪 · 接下来压入层理页岩" :
        "板坯已压实 · 排水并在露天脱模";
}
