#include "fall_sieve.h"
#include "sound.h"
#include <string.h>

enum { SIEVE_TAP_DRY, SIEVE_TAP_READY, SIEVE_TAP_LOW_HEAD, SIEVE_TAP_FLOODED, SIEVE_TAP_COLD };

static int fall_at(int x,int y,int z) {
    int top;
    if (!world_resident_cell(x,y+1,z,&top)) return SIEVE_TAP_COLD;
    if (top!=AIR) return SIEVE_TAP_FLOODED;
    int wet=0,shaft=0;
    static const int sides[][2]={{1,0},{-1,0},{0,1},{0,-1}};
    for (unsigned i=0;i<sizeof sides/sizeof sides[0];++i) {
        const int a=x+sides[i][0],b=z+sides[i][1];
        int mid;
        if (!world_resident_cell(a,y,b,&mid)) return SIEVE_TAP_COLD;
        if (!water_strength(mid)) continue;
        if (++wet>1) return SIEVE_TAP_FLOODED;
        int upper,lower;
        if (!world_resident_cell(a,y+1,b,&upper) || !world_resident_cell(a,y-1,b,&lower))
            return SIEVE_TAP_COLD;
        /* Supported, lateral flow decays. A real drop has three adjacent
           vertical cells of equal strength, with moving water at the tap. */
        const int strength=water_strength(mid);
        shaft=mid!=WATER && lower!=WATER && strength>=3 &&
              water_strength(upper)==strength && water_strength(lower)==strength;
    }
    return shaft ? SIEVE_TAP_READY : wet ? SIEVE_TAP_LOW_HEAD : SIEVE_TAP_DRY;
}

const char *fall_sieve_use(Hit h) {
    int raw;
    if (!h.hit || (unsigned)selected_slot>=HOTBAR_SIZE ||
        h.x< -WORLD_SAVE_LIMIT || h.x>WORLD_SAVE_LIMIT || h.z< -WORLD_SAVE_LIMIT || h.z>WORLD_SAVE_LIMIT ||
        !world_resident_cell(h.x,h.y,h.z,&raw) || raw!=FALL_SIEVE) return NULL;
    const ItemStack held=inventory[selected_slot];
    if (held.count && held.type!=GRAVEL) return NULL;
    if (h.y<=0 || h.y>=WY-1) return "落水筛 · 需要三格高的水路";
    switch (fall_at(h.x,h.y,h.z)) {
    case SIEVE_TAP_COLD: return "落水筛 · 等相邻区块载入";
    case SIEVE_TAP_FLOODED: return "落水筛 · 顶部留空，只能从一侧落水";
    case SIEVE_TAP_DRY: return "落水筛 · 先引水跌落至筛子侧面";
    case SIEVE_TAP_LOW_HEAD: return "落水筛 · 静水或平流无法洗砂，需要三格高的落水";
    default: break;
    }
    if (held.count<3) return "落水筛已就绪 · 拿至少 3 份砂砾轻点";
    ItemStack after[SLOT_COUNT];
    if (!inventory_plan_exchange(after,selected_slot,3,(ItemStack){TIDE_GRIT,2,0,0,0}))
        return "背包装不下两份潮磨砂 · 未消耗砂砾";
    memcpy(inventory,after,sizeof inventory);
    sound_material(SOUND_PLACE,WATER,(Vec3){h.x+.5f,h.y+.5f,h.z+.5f});
    return "已淘洗 · 3 份砂砾换成 2 份潮磨砂";
}
