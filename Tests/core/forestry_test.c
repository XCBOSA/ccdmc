#include "world_test.h"
#include "../game/chests.h"
#include "../game/dispenser_actions.h"
#include "../game/drops.h"
#include "../game/forestry.h"
#include "../game/interaction.h"
#include "../game/item_icons.h"
#include "../world/internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { printf("FORESTRY TEST failed line %d: %s\n",__LINE__,#c); ++bad; } } while (0)
enum { Y=189 };
static void aim(float x,float y,float z) {
    float dx=x-px,dz=z-pz;
    yaw=atan2f(dx,-dz); pitch=atan2f(y-py-1.55f,hypotf(dx,dz));
}
static int plant(int x,int z,int species,int stage) {
    BlockEdit e[]={{x,Y-1,z,DIRT},{x,Y,z,SAPLING_BASE+2*species+stage}};
    return world_set_blocks(e,2);
}
static int generation_test(void) {
    const uint32_t seed=98213;
    int bad=0,covered=1,biomes=0,chunks=0,tagged=0;
    static TerrainResult old,current;
    if (!world_begin_generated(seed,GENERATOR_FORESTRY)) return 1;
    for (int x=-3600;x<=3600 && covered!=63;x+=45) for (int z=-3600;z<=3600 && covered!=63;z+=51) {
        Biome b=terrain_biome(x,z);
        int species=b==BIOME_BIRCH ? TREE_BIRCH : b==BIOME_TAIGA || b==BIOME_COLD_TAIGA || b==BIOME_MEGA_TAIGA ? TREE_SPRUCE :
            b==BIOME_JUNGLE ? TREE_JUNGLE : b==BIOME_SAVANNA ? TREE_ACACIA : b==BIOME_ROOFED ? TREE_DARK_OAK : 0;
        if (!species || (covered&(1<<species))) continue;
        int cx=(int)floorf((float)x/CHUNK_SIZE),cz=(int)floorf((float)z/CHUNK_SIZE);
        terrain_prepare(TERRAIN_KEY(seed,GENERATOR_HORSE_ARMOR,OVERWORLD,WORLD_STANDARD,0),cx,cz,&old);
        terrain_prepare(TERRAIN_KEY(seed,GENERATOR_FORESTRY,OVERWORLD,WORLD_STANDARD,0),cx,cz,&current);
        CHECK(!memcmp(old.tops,current.tops,sizeof old.tops) && !memcmp(old.sky_tops,current.sky_tops,sizeof old.sky_tops) &&
            !memcmp(old.biomes,current.biomes,sizeof old.biomes) && current.wide.count==old.wide.count);
        for (int i=0;i<CHUNK_CELLS;++i) if (old.cells[i]!=current.cells[i]) {
            CHECK(old.cells[i]==LEAF && cell_type(current.cells[i])==LEAF);
            covered|=1<<leaf_species(current.cells[i]);
            ++tagged;
        }
        biomes|=1<<species;
        ++chunks;
    }
    CHECK(covered==63 && tagged>0 && chunks>=3 && (biomes&2));
    printf("FORESTRY GENERATION species-mask=%d chunks=%d typed=%d\n",covered,chunks,tagged);
    return bad;
}
int forestry_selftest(void) {
    int bad=generation_test();
    world_begin(77529); generate();
    player_reset_rules(SURVIVAL);
    memset(inventory,0,sizeof inventory); selected_slot=0;
    px=5.5f; py=Y+.01f; pz=9.5f;
    CHECK(set_block(5,Y-1,7,DIRT));
    inventory[0]=(ItemStack){BIRCH_SAPLING,1,0,0,0};
    aim(5.5f,Y-.15f,7.5f);
    CHECK(game_tap().message && world_cell(5,Y,7)==SAPLING_BASE+2*TREE_BIRCH && !inventory[0].count);
    CHECK(!solid(SAPLING) && plant_height(SAPLING_BASE+2*TREE_BIRCH)>0);
    inventory[0]=(ItemStack){BONE_MEAL,64,0,0,0};
    aim(5.5f,Y+.35f,7.5f);
    for (int tries=0;tries<60 && block(5,Y,7)==SAPLING;++tries) CHECK(game_tap().message);
    CHECK(block(5,Y,7)==BIRCH_LOG && inventory[0].count<64 && inventory[0].count>0);
    int leaf_y=Y;
    while (leaf_y<Y+14 && block(5,leaf_y,7)==BIRCH_LOG) ++leaf_y;
    CHECK(world_cell(5,leaf_y,7)==natural_leaf(TREE_BIRCH));
    CHECK(tree_leaf_drop(world_cell(5,leaf_y,7),5,leaf_y,7,(ItemStack){SHEARS,1,0,0,0})==BIRCH_LEAF_ITEM);
    CHECK(set_block(6,Y+4,4,SPRUCE_LOG) && set_block(6,Y+4,5,LEAF));
    CHECK(tree_leaf_drop(LEAF,6,Y+4,5,(ItemStack){SHEARS,1,0,0,0})==SPRUCE_LEAF_ITEM);
    for (int i=0;i<TREE_SPECIES;++i) {
        ItemIcon icon;
        CHECK(item_icon(OAK_SAPLING+i,&icon) && icon.path[0]);
        if (i) CHECK(item_icon(BIRCH_LEAF_ITEM+i-1,&icon) && icon.path[0]);
    }

    CHECK(world_begin_generated(999,GENERATOR_FORESTRY));
    generate(); player_reset_rules(CREATIVE); px=-7.5f; py=Y+.01f; pz=8.5f;
    CHECK(plant(15,7,TREE_DARK_OAK,1));
    CHECK(tree_bonemeal(15,Y,7)<0 && block(15,Y,7)==SAPLING);
    CHECK(plant(15,8,TREE_DARK_OAK,1) && plant(16,7,TREE_DARK_OAK,1) && plant(16,8,TREE_DARK_OAK,1));
    CHECK(set_block(15,Y+3,7,STONE));
    unsigned long long revision=world_edit_version();
    CHECK(tree_bonemeal(16,Y,8)<0 && world_edit_version()==revision && block(15,Y,7)==SAPLING);
    CHECK(set_block(15,Y+3,7,AIR));
    player_rules.day_tick=18000;
    CHECK(tree_bonemeal(16,Y,8)<0 && block(16,Y,8)==SAPLING);
    player_rules.day_tick=0;
    CHECK(tree_bonemeal(16,Y,8)==TREE_BONE_GROWN && block(15,Y,7)==DARK_OAK_LOG &&
        block(16,Y,7)==DARK_OAK_LOG && block(15,Y,8)==DARK_OAK_LOG && block(16,Y,8)==DARK_OAK_LOG);

    CHECK(plant(159,7,TREE_OAK,1));
    int raw;
    CHECK(!world_resident_cell(160,Y+5,7,&raw));
    revision=world_edit_version();
    CHECK(tree_bonemeal(159,Y,7)<0 && world_edit_version()==revision && !world_resident_cell(160,Y+5,7,&raw));
    (void)chunk_view(10,0);
    CHECK(tree_bonemeal(159,Y,7)==TREE_BONE_GROWN && block(159,Y,7)==WOOD);
    CHECK(world_restore_edit(511,Y,7,SAPLING_BASE+2*TREE_SPRUCE));
    CHECK(world_farm_batch(31,0,(BlockEdit[1]){{0}},1)==0 && !world_resident_cell(511,Y,7,&raw));

    for (int x=-16;x<=-15;++x) for (int z=7;z<=8;++z) CHECK(plant(x,z,TREE_SPRUCE,1));
    (void)chunk_view(-2,0);
    CHECK(tree_bonemeal(-15,Y,8)==TREE_BONE_GROWN && block(-16,Y+11,7)==SPRUCE_LOG &&
        block(-15,Y+11,8)==SPRUCE_LOG);

    const BlockEdit prune={-9,Y,5,TREE_LEAF_BASE+TREE_JUNGLE-1};
    CHECK(world_set_blocks(&prune,1));
    player_reset_rules(SURVIVAL); px=-8.5f; py=Y+.01f; pz=8.5f;
    inventory[0]=(ItemStack){SHEARS,1,0,0,0};
    aim(-8.5f,Y+.5f,5.5f);
    drops_reset();
    CHECK(mine() && block(-9,Y,5)==AIR && drops[0].item.type==JUNGLE_LEAF_ITEM && inventory[0].damage==1);
    inventory[0]=drops[0].item;
    CHECK(set_block(-9,Y-1,5,STONE));
    aim(-8.5f,Y-.15f,5.5f);
    CHECK(game_tap().message && world_cell(-9,Y,5)==PLACED_LEAF_BASE+TREE_JUNGLE && !inventory[0].count);

    player_reset_rules(CREATIVE);
    CHECK(plant(7,7,TREE_ACACIA,0));
    DispenseOutlet out={7,Y,7,0,0,1,{7.5f,Y+.5f,7.5f}};
    ItemStack meal={BONE_MEAL,2,0,0,0};
    CHECK(dispenser_action(&meal,1,0,out)==DISPENSE_USED && meal.count==1 && world_cell(7,Y,7)==SAPLING_BASE+2*TREE_ACACIA+1);
    CHECK(dispenser_action(&meal,1,0,out)==DISPENSE_USED && !meal.count && block(7,Y,7)==ACACIA_LOG);

    CHECK(plant(7,12,TREE_JUNGLE,0));
    for (unsigned t=1;t<=5000 && block(7,Y,12)==SAPLING;++t) tree_tick((BlockEdit){7,Y,12,0},t);
    CHECK(block(7,Y,12)==JUNGLE_LOG);
    printf("FORESTRY TEST harvest/plant/fertilize/darkness/seams/natural failures=%d\n",bad);
    return bad;
}
