#include "../game/actors.h"
#include "../game/blast.h"
#include "../game/chests.h"
#include "../game/droppers.h"
#include "../game/drops.h"
#include "../game/interaction.h"
#include "../world/redstone.h"
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { printf("BLAST TEST failed line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
enum { Y=150 };
static int dropped(int type) {
    int n=0;
    for (int i=0;i<MAXDROP;++i) if (drops[i].item.type==type) n+=drops[i].item.count;
    return n;
}
static int count_charges(void) {
    size_t n=0; blasts_in(world_dimension(),&n); return (int)n;
}
int blast_selftest(void) {
    int failures=0;
    world_begin(94731); generate(); player_reset_rules(SURVIVAL); inventory_reset();
    inventory[0]=(ItemStack){GUNPOWDER,5,0,0,0}; inventory[1]=(ItemStack){SAND,4,0,0,0};
    CHECK(craft_take((int[]){0,1,0,1,0,1,0,1,0},3) && inventory[0].type==TNT_ITEM);
    CHECK(set_block(-17,Y,-1,STONE));
    px=-16.5f; py=149.01f; pz=2.6f; yaw=0; pitch=0; selected_slot=0;
    CHECK(game_tap().message && block(-17,Y,0)==TNT_BLOCK && !inventory[0].count);
    inventory[0]=(ItemStack){FLINT_AND_STEEL,1,0,0,0};
    CHECK(game_tap().message && block(-17,Y,0)==AIR && inventory[0].damage==1);
    size_t n=0;
    const PrimedBlast *list=blasts_in(OVERWORLD,&n);
    CHECK(n==1 && list[0].ticks==TNT_FUSE);
    blasts_step(3);
    CHECK(list[0].ticks==TNT_FUSE-3 && list[0].point.y>Y);

    /* Center on the west side of a chunk seam; only weak or reachable
       blocks change, containers release contents, and TNT chains. */
    blasts_reset(); px=-27.f; py=Y; pz=4.f;
    CHECK(set_block(-16,Y,0,TNT_BLOCK) && set_block(-17,Y,1,OBSIDIAN) && set_block(-17,Y,-1,CHEST));
    ChestView chest;
    inventory[0]=(ItemStack){DIAMOND,7,0,0,0};
    CHECK(chest_open((ChestPos){-17,Y,-1},&chest)==CHEST_OK &&
        chest_move(&chest,DOUBLE_CHEST_SLOTS,0,0)==CHEST_OK);
    CHECK(blast_restore(OVERWORLD,(PrimedBlast){{-16.5f,Y,.5f},{0,0,0},0,BLAST_TNT}));
    blasts_step(1);
    CHECK(block(-16,Y,0)==AIR && block(-17,Y,1)==OBSIDIAN &&
        block(-17,Y,-1)==AIR && dropped(DIAMOND)==7 && chests_count_in(OVERWORLD)==0);
    list=blasts_in(OVERWORLD,&n);
    CHECK(n==1 && list[0].ticks>=CHAIN_FUSE_MIN && list[0].ticks<=CHAIN_FUSE_MAX && list[0].point.x==-15.5f);
    CHECK(set_block(-16,Y-1,0,OBSIDIAN));
    blasts_step(30);
    CHECK(count_charges()==0);
    CHECK(blast_restore(OVERWORLD,(PrimedBlast){{46.5f,Y,2.5f},{0,0,0},TNT_FUSE,BLAST_TNT}) &&
        blast_restore(OVERWORLD,(PrimedBlast){{44.5f,Y,2.5f},{0,0,0},0,BLAST_TNT}));
    blasts_step(1);
    list=blasts_in(OVERWORLD,&n);
    CHECK(n==1 && list[0].point.x>46.5f && list[0].speed.x>10.f);
    blasts_reset();
    CHECK(set_block(-18,Y,8,CHEST));
    inventory[0]=(ItemStack){DIAMOND,2,0,0,0};
    CHECK(chest_open((ChestPos){-18,Y,8},&chest)==CHEST_OK &&
        chest_move(&chest,DOUBLE_CHEST_SLOTS,0,0)==CHEST_OK);
    for (int i=0;i<MAXDROP;++i) drops[i]=make_drop(-17.5f,Y+.5f,8.5f,(ItemStack){STONE,1,0,0,0});
    CHECK(blast_restore(OVERWORLD,(PrimedBlast){{-16.5f,Y,8.5f},{0,0,0},0,BLAST_TNT}));
    blasts_step(1);
    CHECK(block(-18,Y,8)==CHEST && chest_slot(&chest,0).count==2 && !dropped(DIAMOND));
    drops_reset();

    /* Opening in water still produces an effect but cannot break blocks. */
    CHECK(set_block(4,Y,6,WATER) && set_block(5,Y,6,DIRT));
    CHECK(blast_restore(OVERWORLD,(PrimedBlast){{4.5f,Y,6.5f},{0,0,0},0,BLAST_TNT}));
    blasts_step(1);
    CHECK(block(4,Y,6)==WATER && block(5,Y,6)==DIRT);

    for (int y=Y;y<Y+2;++y) for (int z=4;z<=6;++z) CHECK(set_block(12,y,z,OBSIDIAN));
    px=13.5f; py=Y; pz=5.5f;
    CHECK(blast_restore(OVERWORLD,(PrimedBlast){{10.5f,Y,5.5f},{0,0,0},0,BLAST_TNT}));
    blasts_step(1);
    CHECK(player_rules.health==MAX_HEALTH);
    px=28.5f; py=Y; pz=5.5f;
    CHECK(blast_restore(OVERWORLD,(PrimedBlast){{25.5f,Y,5.5f},{0,0,0},0,BLAST_TNT}));
    blasts_step(1);
    CHECK(player_rules.health<MAX_HEALTH);
    player_reset_rules(SURVIVAL);

    CHECK(set_block(5,Y,1,TNT_BLOCK) && set_block(4,Y,1,REDSTONE_BLOCK));
    redstone_step(5,1);
    CHECK(block(5,Y,1)==AIR && count_charges()==1);
    blasts_reset();
    CHECK(set_block(7,Y,1,TNT_BLOCK) && set_block(8,Y,1,LAVA));
    redstone_step(7,1);
    CHECK(block(7,Y,1)==AIR && count_charges()==1);
    blasts_reset();

    CHECK(ejector_place(10,Y,1,EAST,DISPENSER));
    inventory[0]=(ItemStack){TNT_ITEM,1,0,0,0};
    CHECK(dropper_move(10,Y,1,DROPPER_SLOTS,0,0) && set_block(9,Y,1,REDSTONE_BLOCK));
    for (int i=0;i<5;++i) redstone_step(10,1);
    CHECK(count_charges()==1 && dropper_at(10,Y,1) && !dropper_at(10,Y,1)->slots[0].count);
    blasts_reset();

    px=35.5f; py=Y; pz=2.5f;
    CHECK(set_block(33,Y-1,2,LIMESTONE_ROCK));
    CHECK(actors_restore(OVERWORLD,(ActorRecord){{33.5f,Y+.01f,2.5f},1.57f,ACTOR_SHELLBACK,SHELLBACK_HEALTH,0}));
    CHECK(!blast_restore(OVERWORLD,(PrimedBlast){{33.5f,Y,2.5f},{0,0,0},1,BLAST_ARCHIVE_SMALL}));
    for (int i=0;i<70;++i) actors_step(.05f);
    CHECK(count_charges()==0 && actors_in(OVERWORLD)[0].type==ACTOR_SHELLBACK);
    world_reset(); drops_reset();
    printf("BLAST TEST craft/flint/fuse/trajectory/seam/chain/container/water/shield/injury/redstone/lava/dispenser/retired blast failures=%d\n",failures);
    return failures;
}
