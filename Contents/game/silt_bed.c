#include "silt_bed.h"
#include "enchanting.h"
#include "sound.h"
#include <string.h>

int silt_bed_water(int x,int y,int z) {
    int raw,above;
    if (world_dimension()!=OVERWORLD || y<1 || y+2>=WY ||
        !world_resident_cell(x,y,z,&raw) || raw!=CLAY ||
        !world_resident_cell(x,y+1,z,&above)) return 0;
    if (water_strength(above)) {
        return world_resident_cell(x,y+2,z,&raw) && raw==AIR;
    }
    if (above!=AIR) return 0;
    const int sides[][2]={{1,0},{0,1},{-1,0},{0,-1}};
    for (unsigned i=0;i<4;++i) if (world_resident_cell(x+sides[i][0],y,z+sides[i][1],&raw) &&
        water_strength(raw)>=4) return 1;
    return 0;
}

const char *silt_bed_tap(Hit hit) {
    int raw;
    if (!hit.hit || !world_resident_cell(hit.x,hit.y,hit.z,&raw) || (raw!=FIBROUS_CLAY && raw!=CLAY) ||
        (unsigned)selected_slot>=HOTBAR_SIZE) return NULL;
    ItemStack *tool=&inventory[selected_slot];
    if (raw==CLAY) {
        if ((!tool->count || is_shovel(tool->type)) && silt_bed_water(hit.x,hit.y,hit.z))
            return "接水泥床 · 等泥脊兽翻出纤纹";
        return NULL;
    }
    if (!tool->count) return "纤纹已经形成 · 用锹轻点采泥纤胶";
    if (!is_shovel(tool->type)) return NULL;
    ItemStack backup[SLOT_COUNT];
    memcpy(backup,inventory,sizeof backup);
    if (!inventory_add_stack((ItemStack){SILT_GEL,1,0,0,0})) return "背包已满 · 泥纤留在原处";
    const BlockEdit scrape={hit.x,hit.y,hit.z,CLAY};
    if (!world_set_blocks(&scrape,1)) {
        memcpy(inventory,backup,sizeof backup);
        return "暂时无法采泥纤 · 泥床和装备未变";
    }
    if (player_rules.mode==SURVIVAL) enchant_wear(tool,1,0);
    sound_play(SOUND_SILTBACK,.67f,2,(Vec3){hit.x+.5f,hit.y+.85f,hit.z+.5f});
    return !tool->count ? "已收集泥纤胶 · 泥床保留，锹已磨损" : "已收集泥纤胶 · 泥床留待再次翻泥";
}
