#include "../game/seep.h"
#include "../game/actors.h"
#include "../game/interaction.h"
#include "../game/item_icons.h"
#include "../game/spawn_eggs.h"
#include "../world/aquifer_layout.h"
#include "../world/light.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"SEEP TEST line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
static int said(const char *msg,const char *part) { return msg && strstr(msg,part); }
static int beetles(void) {
    const ActorRecord *a=actors_in(world_dimension());
    int n=0;
    for (int i=0;i<ACTOR_CAPACITY;++i) n+=a[i].type==ACTOR_SEEP_BEETLE;
    return n;
}
int seep_selftest(void) {
    int failures=0;
    CHECK(world_begin_options(1712,GENERATOR_CURRENT,WORLD_SUPERFLAT,1));
    generate(); player_reset_rules(SURVIVAL);
    memset(inventory,0,sizeof inventory);
    selected_slot=0;
    inventory[0]=(ItemStack){CLAY_BALL,2,0,0,0};
    inventory[1]=(ItemStack){SLATE_ITEM,1,0,0,0};
    inventory[2]=(ItemStack){LIMESTONE_ITEM,1,0,0,0};
    const int recipe[]={0,1,2,-1};
    CHECK(craft_preview(recipe,2).type==SEEP_BED_ITEM && craft_take(recipe,2));
    CHECK(inventory[0].type==CLAY_BALL && inventory[0].count==1 && inventory[1].type==SEEP_BED_ITEM);
    const Hit bed={1,4,4,4,0,0,0};
    CHECK(set_block(4,4,4,SEEP_BED));
    px=4.5f; py=4.01f; pz=8.f; yaw=0.f; pitch=-.34f;
    CHECK(target().hit && target().z==4 && said(game_tap().message,"贴着侧面"));
    CHECK(inventory[0].type==CLAY_BALL && inventory[0].count==1);
    CHECK(set_block(3,4,4,WATER) && said(game_tap().message,"柔光陶砖"));
    CHECK(inventory[0].type==SEEP_TILE_ITEM && inventory[0].count==2 && light_emission(SEEP_TILE)==7);
    inventory[0]=(ItemStack){CLAY_BALL,2,0,0,0};
    CHECK(set_block(5,4,4,WATER) && set_block(4,5,4,WATER));
    CHECK(said(seep_use(bed),"顶部留空") && inventory[0].count==2);
    CHECK(set_block(4,5,4,AIR) && seep_wet_faces(4,4,4)==2);
    for (int i=1;i<SLOT_COUNT;++i) inventory[i]=(ItemStack){DIRT,STACK_MAX,0,0,0};
    CHECK(said(seep_use(bed),"背包空间不足") && inventory[0].count==2);
    inventory[1]=(ItemStack){0};
    CHECK(said(game_tap().message,"高亮陶砖") && inventory[0].count==1 &&
        inventory[1].type==SEEP_BRIGHT_ITEM && inventory[1].count==2 && light_emission(SEEP_TILE_BRIGHT)==12);
    ItemIcon icon;
    CHECK(item_icon(SEEP_BED_ITEM,&icon) && item_icon(SEEP_TILE_ITEM,&icon) && item_icon(SEEP_BRIGHT_ITEM,&icon));
    CHECK(game_set_difficulty(PEACEFUL) && spawn_egg_at(EGG_SEEP_BEETLE,(Vec3){4.5f,5.01f,4.5f}));
    for (int i=0;i<42;++i) actors_step(.05f);
    CHECK(beetles()==1 && world_cell(4,4,4)==SEEP_BED && player_rules.health==20);

    int x=0,z=0;
    CHECK(world_begin_generated(1712,GENERATOR_PRE_GEOLOGY) && nearest_stronghold(0,0,&x,&z));
    CHECK(world_cell(x-4,STRONGHOLD_FLOOR+1,z+3)==AIR);
    for (int dx=-7;dx<=7;++dx) CHECK(world_cell(x+dx,STRONGHOLD_FLOOR+2,z+6)==STONE_BRICKS);
    CHECK(world_begin_generated(1712,GENERATOR_CURRENT) && world_select_dimension(END));
    CHECK(world_cell(SEEP_GARDEN_X,SEEP_GARDEN_Y,SEEP_GARDEN_Z)==SEEP_BED &&
        seep_wet_faces(SEEP_GARDEN_X,SEEP_GARDEN_Y,SEEP_GARDEN_Z)==2);
    actors_reset(); player_reset_rules(SURVIVAL);
    px=-8.5f; py=54.05f; pz=1.5f;
    for (int i=0;i<140;++i) actors_step(.05f);
    CHECK(beetles()==1);
    actors_reset(); world_reset();
    printf("SEEP TEST shared wet faces, processing, inventory safety, legacy terrain, natural and peaceful habitat failures=%d\n",failures);
    return failures;
}

int settling_mold_selftest(void) {
    int failures=0;
    CHECK(world_begin_options(56182,GENERATOR_CURRENT,WORLD_SUPERFLAT,1));
    player_reset_rules(SURVIVAL);
    memset(inventory,0,sizeof inventory);
    inventory[10]=(ItemStack){LIMESTONE_ITEM,1,0,0,0};
    inventory[11]=(ItemStack){PLANKS,1,0,0,0};
    inventory[12]=(ItemStack){CLAY_BALL,1,0,0,0};
    const int ingredients[]={10,11,12,-1};
    const int x=6,y=12,z=-6;
    const Hit h={1,x,y,z,0,0,0};
    CHECK(craft_preview(ingredients,2).type==SETTLING_MOLD_ITEM && craft_take(ingredients,2) &&
        inventory[0].type==SETTLING_MOLD_ITEM && inventory[0].count==1);
    CHECK(set_block(x,y,z,SETTLING_MOLD));
    selected_slot=0;
    inventory[0]=(ItemStack){LIMESTONE_ITEM,2,0,0,0};
    CHECK(settling_mold_use(h) && inventory[0].count==2 && world_cell(x,y,z)==SETTLING_MOLD);
    CHECK(set_block(x+1,y,z,WATER) && set_block(x,y+1,z,STONE));
    CHECK(settling_mold_use(h) && inventory[0].count==2 && world_cell(x,y,z)==SETTLING_MOLD);
    CHECK(set_block(x,y+1,z,AIR) && settling_mold_use(h) && inventory[0].count==1 &&
        world_cell(x,y,z)==SETTLING_LIME);
    CHECK(settling_mold_use(h) && inventory[0].count==1 && world_cell(x,y,z)==SETTLING_LIME);
    inventory[0]=(ItemStack){SLATE_ITEM,2,0,0,0};
    CHECK(settling_mold_use(h) && inventory[0].count==1 && world_cell(x,y,z)==SETTLING_PACKED);
    selected_slot=1; /* An empty hand is also a reserved slot for the output. */
    CHECK(settling_mold_use(h) && !inventory[1].count && world_cell(x,y,z)==SETTLING_PACKED);
    CHECK(set_block(x+1,y,z,AIR) && set_block(x,y+2,z,STONE));
    CHECK(settling_mold_use(h) && !inventory[1].count && world_cell(x,y,z)==SETTLING_PACKED);
    CHECK(set_block(x,y+2,z,AIR) && settling_mold_use(h) && world_cell(x,y,z)==SETTLING_MOLD &&
        inventory[1].type==LAMINATE_ITEM && inventory[1].count==3);
    selected_slot=2;
    CHECK(settling_mold_use(h) && !inventory[2].count && world_cell(x,y,z)==SETTLING_MOLD);
    CHECK(mining_drop(SETTLING_MOLD,0)==SETTLING_MOLD_ITEM);
    ItemIcon icon;
    CHECK(item_icon(SETTLING_MOLD_ITEM,&icon));
    world_reset();
    printf("SETTLING TEST recipe, water, open sky, consumption, mold reset and icon failures=%d\n",failures);
    return failures;
}
