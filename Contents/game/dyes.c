#include "dyes.h"

int glass_recipe_item(int dye) {
    static const uint16_t inputs[]={INK_SAC,GRAY_DYE,LIGHT_GRAY_DYE,BONE_MEAL,ROSE_RED,CACTUS_GREEN,
        COCOA_BEANS,LAPIS_LAZULI,PURPLE_DYE,CYAN_DYE,PINK_DYE,LIME_DYE,DANDELION_YELLOW,
        LIGHT_BLUE_DYE,MAGENTA_DYE,ORANGE_DYE};
    for (unsigned i=0;i<sizeof inputs/sizeof *inputs;++i)
        if (inputs[i]==dye) return BLACK_GLASS_ITEM+(int)i;
    return AIR;
}

ItemStack mix_dye(const uint16_t *grid,int slots) {
    static const struct { uint8_t used,count; uint16_t ingredients[4],result; } formulas[] = {
        {2,2,{INK_SAC,BONE_MEAL,0,0},GRAY_DYE},
        {2,2,{GRAY_DYE,BONE_MEAL,0,0},LIGHT_GRAY_DYE},
        {3,3,{INK_SAC,BONE_MEAL,BONE_MEAL,0},LIGHT_GRAY_DYE},
        {2,2,{ROSE_RED,LAPIS_LAZULI,0,0},PURPLE_DYE},
        {2,2,{LAPIS_LAZULI,CACTUS_GREEN,0,0},CYAN_DYE},
        {2,2,{ROSE_RED,BONE_MEAL,0,0},PINK_DYE},
        {2,2,{CACTUS_GREEN,BONE_MEAL,0,0},LIME_DYE},
        {2,2,{LAPIS_LAZULI,BONE_MEAL,0,0},LIGHT_BLUE_DYE},
        {2,2,{PURPLE_DYE,PINK_DYE,0,0},MAGENTA_DYE},
        {3,3,{LAPIS_LAZULI,ROSE_RED,PINK_DYE,0},MAGENTA_DYE},
        {4,4,{LAPIS_LAZULI,ROSE_RED,ROSE_RED,BONE_MEAL},MAGENTA_DYE},
        {2,2,{ROSE_RED,DANDELION_YELLOW,0,0},ORANGE_DYE}
    };
    ItemStack none={0};
    if (!grid || (slots!=4 && slots!=9)) return none;
    uint16_t picked[4];
    unsigned count=0;
    for (int i=0;i<slots;++i) if (grid[i]) {
        if (count==4) return none;
        picked[count++]=grid[i];
    }
    for (unsigned f=0;f<sizeof formulas/sizeof *formulas;++f) if (count==formulas[f].used) {
        unsigned used=0;
        for (unsigned i=0;i<count;++i) {
            for (unsigned j=0;j<count;++j) if (!(used&(1u<<j)) && formulas[f].ingredients[i]==picked[j]) {
                used|=1u<<j;
                break;
            }
        }
        if (used==(1u<<count)-1u) return (ItemStack){formulas[f].result,formulas[f].count,0,0,0};
    }
    return none;
}
