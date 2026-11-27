#include "world_test.h"
#include "../game/blast.h"
#include "../game/drops.h"
#include "../game/forestry.h"
#include "../game/leaf_decay.h"
#include "../game/pistons.h"
#include "../world/rails.h"
#include <stdio.h>

enum { Y=196 };
#define CHECK(c) do { if (!(c)) { printf("LEAF DECAY TEST failed line %d: %s\n",__LINE__,#c); ++bad; } } while (0)
static void step(int x,int z,int n) { for (int i=0;i<n;++i) leaves_step(x,z); }
static int leaf(int x,int y,int z,int species,int placed) {
    BlockEdit e={x,y,z,placed ? PLACED_LEAF_BASE+species : natural_leaf(species)};
    return world_set_blocks(&e,1);
}
static void clear_blocks(unsigned seed) {
    world_begin_generated(seed,GENERATOR_FORESTRY);
    generate();
    player_reset_rules(SURVIVAL);
    px=8.5f; py=Y+.01f; pz=6.5f;
}
int leaf_decay_selftest(void) {
    int bad=0;
    clear_blocks(6199);
    CHECK(set_block(4,Y,7,SPRUCE_LOG));
    for (int x=5;x<=9;++x) CHECK(leaf(x,Y,7,TREE_SPRUCE,0));
    CHECK(leaf(8,Y,8,TREE_BIRCH,1));
    CHECK(set_block(7,Y-1,7,WOOD) && set_block(7,Y-1,7,AIR));
    /* Four leaf edges are enough; five and a diagonal alone are not. */
    CHECK(leaf(4,Y,5,TREE_BIRCH,0));
    step(6,7,180);
    CHECK(block(5,Y,7)==LEAF && block(8,Y,7)==LEAF && block(9,Y,7)==AIR &&
        world_cell(8,Y,8)==PLACED_LEAF_BASE+TREE_BIRCH && block(4,Y,5)==AIR);
    CHECK(set_block(5,Y,7,AIR));
    step(6,7,180);
    CHECK(block(6,Y,7)==AIR && block(7,Y,7)==AIR && block(8,Y,7)==AIR &&
        block(4,Y,7)==SPRUCE_LOG && world_cell(8,Y,8)==PLACED_LEAF_BASE+TREE_BIRCH);

    clear_blocks(8180);
    /* Force a cross-chunk case far from the spawn cache. The save overlay
       can contain support without the host having loaded that column. */
    CHECK(leaf(-145,Y,7,TREE_JUNGLE,0) && world_restore_edit(-144,Y,7,JUNGLE_LOG));
    CHECK(set_block(-146,Y,7,WOOD) && set_block(-146,Y,7,AIR));
    int raw;
    CHECK(!world_resident_cell(-144,Y,7,&raw));
    step(-145,7,160);
    CHECK(block(-145,Y,7)==LEAF && !world_resident_cell(-144,Y,7,&raw) && leaves_pending(OVERWORLD)>0);
    (void)chunk_view(-9,0);
    step(-145,7,160);
    CHECK(block(-145,Y,7)==LEAF && block(-144,Y,7)==JUNGLE_LOG);
    CHECK(set_block(-144,Y,7,AIR));
    step(-145,7,160);
    CHECK(block(-145,Y,7)==AIR);

    clear_blocks(9831);
    int drop_x=0,found=0;
    for (int x=6;x<1300;++x)
        if (tree_leaf_drop(natural_leaf(TREE_ACACIA),x,Y,7,(ItemStack){0,0,0,0,0})==ACACIA_SAPLING) {
            drop_x=x; found=1; break;
        }
    CHECK(found && leaf(drop_x,Y,7,TREE_ACACIA,0));
    CHECK(set_block(drop_x,Y-1,7,ACACIA_LOG) && set_block(drop_x,Y-1,7,AIR));
    for (int i=0;i<MAXDROP;++i) drops[i].item=(ItemStack){DIRT,1,0,0,0};
    step(drop_x,7,180);
    CHECK(block(drop_x,Y,7)==LEAF);
    drops_reset();
    step(drop_x,7,240);
    int seeds=0;
    for (int i=0;i<MAXDROP;++i) if (drops[i].item.type==ACACIA_SAPLING) seeds+=drops[i].item.count;
    CHECK(block(drop_x,Y,7)==AIR && seeds==1);

    clear_blocks(3255);
    /* A piston removes the support on the same grouped world edit used
       by blast and fire: horizontal movement by one tile breaks this arm. */
    CHECK(piston_place(7,Y,4,EAST,0) && set_block(8,Y,4,ACACIA_LOG));
    CHECK(leaf(8,Y+1,4,TREE_ACACIA,0) && leaf(8,Y+2,4,TREE_ACACIA,0));
    CHECK(set_block(6,Y,4,REDSTONE_BLOCK));
    for (int i=0;i<12;++i) redstone_step(8,4);
    CHECK(block(9,Y,4)==ACACIA_LOG && piston_head_cell(world_cell(8,Y,4)));
    step(8,4,200);
    CHECK(block(8,Y+1,4)==AIR && block(8,Y+2,4)==AIR && block(9,Y,4)==ACACIA_LOG);
    CHECK(set_block(12,Y,4,WOOD) && leaf(12,Y+1,4,TREE_OAK,0) &&
        blast_queue_cart((Vec3){12.5f,Y+.1f,4.5f},0.f,0));
    blasts_step(1);
    CHECK(block(12,Y,4)==AIR && leaves_pending(OVERWORLD)>0);

    LeafWork invalid={WORLD_SAVE_LIMIT,96,0,0,0,{256u}};
    CHECK(!leaves_restore(END,&invalid));
    invalid.layers[0]=1u;
    CHECK(leaves_restore(END,&invalid) && !leaves_restore(END,&invalid));
    invalid.x-=1;
    CHECK(!leaves_restore(END,&invalid));
    world_reset();
    CHECK(!leaves_pending(OVERWORLD) && !leaves_pending(END));
    printf("LEAF DECAY TEST paths/protection/unknown/retry/drops/piston/blast failures=%d\n",bad);
    return bad;
}
