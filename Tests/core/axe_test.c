#include "../game/anvil.h"
#include "../game/enchanting.h"
#include "../game/furnace.h"
#include "../game/item_icons.h"
#include <stdio.h>
#include <string.h>

int axe_selftest(void) {
    int failures=0;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"axe test:%d %s\n",__LINE__,#c); ++failures; } } while (0)
    static const int materials[]={PLANKS,COBBLESTONE,IRON_INGOT,GOLD_INGOT,DIAMOND};
    static const int durability[]={59,131,250,32,1561},damage[]={4,5,6,4,7};
    static const int left[]={0,0,-1,0,1,-1,-1,1,-1},right[]={-1,0,0,-1,1,0,-1,1,-1};
    CHECK(world_begin_options(100,GENERATOR_CURRENT,WORLD_SUPERFLAT,1));
    generate();
    ItemIcon sword;
    CHECK(item_icon(WOOD_SWORD,&sword));
    for (int i=0;i<5;++i) {
        int tool=WOOD_AXE+i;
        const ItemDef *def=item_def(tool);
        ItemIcon icon;
        CHECK(def && def->durability==durability[i] && def->melee==damage[i] &&
            def->max_stack==1 && !def->placed_block && !def->pick_tier);
        CHECK(item_icon(tool,&icon) && icon.path[2] && sword.path[2] && strcmp(icon.path[2],sword.path[2]));
        inventory_reset();
        inventory[0]=(ItemStack){(uint16_t)materials[i],3,0,0,0};
        inventory[1]=(ItemStack){STICK,2,0,0,0};
        CHECK(craft_preview(left,3).type==tool && craft_preview(right,3).type==tool && craft_take(left,3) &&
            inventory[0].type==tool && !inventory[1].count);
        inventory[0].damage=20;
        inventory[1]=(ItemStack){(uint16_t)materials[i],1,0,0,0};
        AnvilPlan plan;
        CHECK(anvil_preview(0,1,NULL,&plan)==ANVIL_READY && plan.item.damage<20);
        CHECK(item_stack_valid((ItemStack){(uint16_t)tool,1,0,enchant_code(ENCH_SILK_TOUCH,1),0}));
        CHECK(!item_stack_valid((ItemStack){(uint16_t)tool,2,0,0,0}));
    }
    CHECK(axe_chop_milliseconds(WOOD_AXE)>axe_chop_milliseconds(STONE_AXE) &&
        axe_chop_milliseconds(STONE_AXE)>axe_chop_milliseconds(IRON_AXE) &&
        axe_chop_milliseconds(IRON_AXE)>axe_chop_milliseconds(DIAMOND_AXE) &&
        axe_chop_milliseconds(DIAMOND_AXE)>axe_chop_milliseconds(GOLD_AXE));
    inventory[0]=(ItemStack){WOOD_AXE,1,0,0,0};
    CHECK(set_block(0,4,-3,WOOD) && target().hit && target().z==-3);
    CHECK(!mine_quick_chop(350) && !inventory[0].damage && block(0,4,-3)==WOOD);
    CHECK(mine_quick_chop(365) && inventory[0].damage==1 && block(0,4,-3)==AIR && drops[0].item.type==WOOD);
    CHECK(set_block(0,4,-3,STONE) && !mine_quick_chop(365) && inventory[0].damage==1 && block(0,4,-3)==STONE);
    CHECK(mine() && block(0,4,-3)==AIR && !drops[1].item.count && inventory[0].damage==2);
    CHECK(set_block(0,4,-3,BOOKSHELF));
    inventory[0]=(ItemStack){GOLD_AXE,1,31,enchant_code(ENCH_SILK_TOUCH,1),0};
    const char *broke=mine_quick_chop(185);
    CHECK(broke && !strcmp(broke,"斧已损坏") && !inventory[0].count && drops[1].item.type==BOOKSHELF);
    CHECK(set_block(2,4,0,FURNACE));
    CHECK(furnace_automation_insert(2,4,0,FURNACE_FUEL,(ItemStack){WOOD_AXE,1,0,0,0}));
    printf("AXE TEST material/mirror/icon/repair/hold/wear/silk/fuel failures=%d\n",failures);
#undef CHECK
    return failures!=0;
}
