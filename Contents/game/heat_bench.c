#include "heat_bench.h"
#include "actors.h"
#include "furnace.h"
#include "sound.h"
#include "../world/rails.h"
#include <stdio.h>
#include <string.h>

static int repairable(int type) {
    return type==IRON_PICKAXE || type==IRON_SHOVEL || type==IRON_AXE ||
        type==IRON_HOE || type==IRON_SWORD || type==SHEARS;
}
static void use_sound(Hit h) {
    sound_material(SOUND_PLACE,HEAT_BENCH_EMPTY,(Vec3){h.x+.5f,h.y+.7f,h.z+.5f});
}
static const char *collect(Hit h,int charge,char *text,size_t size) {
    if (charge==HEAT_BENCH_CAP) return "蓄热 4/4 · 可修铁工具或熔回玻璃板";
    int found=-1;
    for (int i=0;i<4;++i) {
        int cell;
        if (!world_resident_cell(h.x+rail_dx(i),h.y,h.z+rail_dz(i),&cell))
            return "等待邻区载入 · 未取余火";
        if (cell!=FURNACE) continue;
        if (found>=0) return "同一高度只接一座熔炉 · 未取余火";
        found=i;
    }
    if (found<0) {
        snprintf(text,size,"蓄热 %d/4 · 可接同一高度的熔炉取余火",charge);
        return text;
    }
    const int gained=furnace_store_idle_heat(h.x+rail_dx(found),h.y,h.z+rail_dz(found),h.x,h.z);
    if (gained<0) return "无法记录蓄热 · 余火未扣";
    if (gained) {
        use_sound(h);
        int visitor=actors_visit_heat_bench(h);
        snprintf(text,size,"回收余火 +%d · 蓄热 %d/4%s",gained,charge+gained,visitor ? " · 灰窑鼹来访" : "");
    } else snprintf(text,size,"蓄热 %d/4 · 炉在烧制或余火不足 6 秒",charge);
    return text;
}

const char *heat_bench_core(Hit h,int input) {
    int raw;
    if (!h.hit || (unsigned)selected_slot>=HOTBAR_SIZE || !inventory[selected_slot].count ||
        inventory[selected_slot].type!=input || (input!=CLAY_BALL && input!=CERAMIC_CORE && input!=WARM_CORE) ||
        !world_resident_cell(h.x,h.y,h.z,&raw) || !heat_bench_cell(raw)) return NULL;
    const int charge=raw-HEAT_BENCH_EMPTY;
    const int store=input!=WARM_CORE;
    if (store && !charge) return "回温台没有蓄热 · 材料未改变";
    if (!store && charge==HEAT_BENCH_CAP) return "回温台已满 · 暖陶芯未消耗";
    ItemStack after[SLOT_COUNT];
    if (!inventory_plan_exchange(after,selected_slot,1,(ItemStack){(uint16_t)(store ? WARM_CORE : CERAMIC_CORE),1,0,0,0}))
        return "背包没有剩余空间 · 材料和蓄热未改变";
    if (!world_set_heat_bench_level(h.x,h.y,h.z,raw,raw+(store ? -1 : 1)))
        return "无法记录蓄热 · 材料未改变";
    memcpy(inventory,after,sizeof inventory);
    sound_play(SOUND_KILN_MOLE,.68f,store ? 1 : 2,(Vec3){h.x+.5f,h.y+.7f,h.z+.5f});
    if (input==CLAY_BALL) return "灰窑鼹压制了暖陶芯 · 取用了 1 格蓄热";
    return store ? "陶芯已充热 · 取用了 1 格蓄热" : "暖陶芯已放热 · 回温台 +1 格";
}

const char *heat_bench_use(Hit h) {
    int raw;
    if (!h.hit || h.x<-WORLD_SAVE_LIMIT || h.x>WORLD_SAVE_LIMIT ||
        h.z<-WORLD_SAVE_LIMIT || h.z>WORLD_SAVE_LIMIT || (unsigned)selected_slot>=HOTBAR_SIZE ||
        !world_resident_cell(h.x,h.y,h.z,&raw) || !heat_bench_cell(raw)) return NULL;
    const int charge=raw-HEAT_BENCH_EMPTY;
    ItemStack *held=&inventory[selected_slot];
    static char text[100];
    if (!held->count) return collect(h,charge,text,sizeof text);
    if (held->type==CERAMIC_CORE || held->type==WARM_CORE) return heat_bench_core(h,held->type);
    if (repairable(held->type)) {
        if (!item_stack_valid(*held)) return "工具状态异常 · 未修复";
        if (!held->damage) return "工具无需修复 · 蓄热未扣";
        if (!charge) return "需要 1 格蓄热 · 工具未改变";
        const unsigned restore=held->damage<HEAT_BENCH_REPAIR ? held->damage : HEAT_BENCH_REPAIR;
        if (!world_set_heat_bench_level(h.x,h.y,h.z,raw,raw-1)) return "无法记录蓄热 · 工具未改变";
        held->damage-=(uint16_t)restore;
        use_sound(h);
        snprintf(text,sizeof text,"回温修复 %u 点 · 蓄热 %d/4",restore,charge-1);
        return text;
    }
    if (held->type==GLASS_PANE_ITEM) {
        if (held->count<HEAT_BENCH_PANES) return "每次至少需要 4 片普通玻璃板 · 未扣材料";
        if (charge<HEAT_BENCH_RECYCLE_COST) return "熔回玻璃需要 2 格蓄热 · 未扣材料";
        ItemStack after[SLOT_COUNT];
        if (!inventory_plan_exchange(after,selected_slot,HEAT_BENCH_PANES,(ItemStack){GLASS,1,0,0,0}))
            return "背包无法装入回收玻璃 · 材料和蓄热未扣";
        if (!world_set_heat_bench_level(h.x,h.y,h.z,raw,raw-HEAT_BENCH_RECYCLE_COST))
            return "无法记录蓄热 · 玻璃板未改变";
        memcpy(inventory,after,sizeof inventory);
        use_sound(h);
        snprintf(text,sizeof text,"4 片玻璃板熔回 1 块玻璃 · 蓄热 %d/4",charge-HEAT_BENCH_RECYCLE_COST);
        return text;
    }
    return NULL;
}
