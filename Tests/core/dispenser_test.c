#include "../game/actors.h"
#include "../game/armor.h"
#include "../game/boats.h"
#include "../game/chests.h"
#include "../game/droppers.h"
#include "../game/drops.h"
#include "../game/combat.h"
#include "../game/interaction.h"
#include "../game/minecarts.h"
#include "../world/redstone.h"
#include <stdio.h>

#define CHECK(c) do { if (!(c)) { printf("DISPENSER TEST failed line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
enum { Y=150 };
static ItemStack stack(int type,int size) { return (ItemStack){(uint16_t)type,(uint8_t)size,0,0,0}; }
static ItemStack slot(int x,int z,int i) {
    const Dropper *d=dropper_at(x,Y,z);
    return d ? d->slots[i] : stack(AIR,0);
}
static int arm(int x,int z,int type,int num) {
    inventory[0]=stack(type,num);
    return dropper_move(x,Y,z,DROPPER_SLOTS,0,0) && !inventory[0].count;
}
static int pulse(int x,int z) {
    if (!set_block(x-1,Y,z,AIR)) return 0;
    redstone_step(x,z);
    if (!set_block(x-1,Y,z,REDSTONE_BLOCK)) return 0;
    for (int i=0;i<5;++i) redstone_step(x,z);
    return 1;
}
static int dropped(int type) {
    int count=0;
    for (int i=0;i<MAXDROP;++i) if (drops[i].item.type==type) count+=drops[i].item.count;
    return count;
}
int dispenser_selftest(void) {
    int failures=0;
    world_begin(310581); generate(); inventory_reset(); player_reset_rules(SURVIVAL);
    inventory[0]=stack(COBBLESTONE,7); inventory[1]=stack(BOW,1); inventory[2]=stack(REDSTONE_DUST,1);
    CHECK(craft_take((int[]){0,0,0,0,1,0,0,2,0},3) && inventory[0].type==DISPENSER_ITEM);
    CHECK(set_block(-17,Y,-1,STONE));
    px=-16.5f; py=149.01f; pz=2.55f; yaw=0; pitch=0; selected_slot=0;
    CHECK(game_tap().panel==INTERACTION_NONE && world_cell(-17,Y,0)==DISPENSER_BASE+SOUTH);
    CHECK(game_tap().panel==INTERACTION_EJECTOR);
    CHECK(arm(-17,0,ARROW,2) && pulse(-17,0));
    const FlyingArrow *arrows=combat_arrows();
    CHECK(arrows[0].machine && arrows[0].recoverable && arrows[0].velocity.z>20.f &&
        slot(-17,0,0).count==1 && !dropped(ARROW));
    for (int i=0;i<4;++i) combat_step(.025f);
    CHECK(player_rules.health==MAX_HEALTH-4);
    for (int i=0;i<18;++i) redstone_step(-17,0);
    CHECK(slot(-17,0,0).count==1);

    /* Firing into a full projectile pool may not subtract a second arrow. */
    combat_reset();
    for (int i=0;i<ARROW_CAPACITY;++i)
        CHECK(combat_dispenser_arrow((Vec3){8,155,8},(Vec3){0,0,1}));
    CHECK(pulse(-17,0) && slot(-17,0,0).count==1);
    combat_reset();

    CHECK(ejector_place(-17,Y,7,EAST,DISPENSER) && arm(-17,7,EGG,2) && pulse(-17,7));
    const ActorFireball *f=actors_fireballs();
    CHECK(f[0].kind==THROWN_EGG && f[0].seconds>0 && f[0].velocity.x>12.f &&
        slot(-17,7,0).count==1 && !dropped(EGG));
    CHECK(ejector_place(-17,Y,11,EAST,DISPENSER) && arm(-17,11,SNOWBALL,2) && pulse(-17,11));
    CHECK(f[1].kind==THROWN_SNOWBALL && f[1].seconds>0 && f[1].velocity.x>17.f &&
        slot(-17,11,0).count==1 && !dropped(SNOWBALL));

    CHECK(ejector_place(0,Y,3,EAST,DISPENSER) && arm(0,3,WATER_BUCKET,1) && pulse(0,3));
    CHECK(world_cell(1,Y,3)==WATER && slot(0,3,0).type==BUCKET);
    CHECK(pulse(0,3) && world_cell(1,Y,3)==AIR && slot(0,3,0).type==WATER_BUCKET);
    CHECK(world_select_dimension(NETHER) && ejector_place(0,Y,3,EAST,DISPENSER) &&
        arm(0,3,WATER_BUCKET,1) && pulse(0,3));
    CHECK(world_cell(1,Y,3)==AIR && slot(0,3,0).type==BUCKET);
    CHECK(world_select_dimension(OVERWORLD) && slot(0,3,0).type==WATER_BUCKET);

    /* A stack of empty buckets must leave room for the filled bucket,
       or reserve a drop before changing the world. */
    CHECK(ejector_place(8,Y,8,EAST,DISPENSER) && set_block(9,Y,8,LAVA));
    for (int i=0;i<DROPPER_SLOTS;++i) {
        inventory[0]=stack(BUCKET,2);
        CHECK(dropper_move(8,Y,8,DROPPER_SLOTS,i,0));
    }
    for (int i=0;i<MAXDROP;++i) drops[i]=make_drop(8.5f,Y+.5f,8.5f,stack(STONE,1));
    CHECK(pulse(8,8) && world_cell(9,Y,8)==LAVA && !dropped(LAVA_BUCKET));
    int total=0;
    for (int i=0;i<DROPPER_SLOTS;++i) total+=slot(8,8,i).count;
    CHECK(total==2*DROPPER_SLOTS);
    drops[0]=(Drop){0};
    CHECK(pulse(8,8) && world_cell(9,Y,8)==AIR && dropped(LAVA_BUCKET)==1);
    total=0;
    for (int i=0;i<DROPPER_SLOTS;++i) total+=slot(8,8,i).count;
    CHECK(total==2*DROPPER_SLOTS-1);
    drops_reset();

    CHECK(ejector_place(5,Y,3,EAST,DISPENSER) && set_block(6,Y,3,WATER) &&
        arm(5,3,BOAT,1) && pulse(5,3));
    CHECK(boats_in(OVERWORLD)[0].occupied && boats_in(OVERWORLD)[0].position.x>6.5f &&
        !slot(5,3,0).count && !dropped(BOAT));
    CHECK(arm(5,3,BOAT,1) && pulse(5,3) && slot(5,3,0).count==1);

    CHECK(ejector_place(9,Y,3,EAST,DISPENSER) && set_block(10,Y-1,3,STONE) &&
        rail_place((RailCell){10,Y,3},RAIL_NORMAL,1) && arm(9,3,MINECART,1) && pulse(9,3));
    CHECK(carts_in(OVERWORLD)[0].occupied && carts_in(OVERWORLD)[0].track.x==10 && !slot(9,3,0).count);
    CHECK(arm(9,3,MINECART,1) && pulse(9,3) && slot(9,3,0).count==1);

    const BlockEdit crop[]={{13,Y-1,7,FARM_BASE+7},{13,Y,7,WHEAT_BASE}};
    CHECK(ejector_place(12,Y,7,EAST,DISPENSER) && world_set_blocks(crop,2) &&
        arm(12,7,BONE_MEAL,2) && pulse(12,7));
    CHECK(world_cell(13,Y,7)>=WHEAT_BASE+2 && slot(12,7,0).count==1);

    CHECK(ejector_place(16,Y,3,EAST,DISPENSER) && arm(16,3,IRON_HELMET,1));
    px=17.5f; py=149.01f; pz=3.5f;
    CHECK(pulse(16,3) && armor_worn(ARMOR_HEAD).type==IRON_HELMET && !slot(16,3,0).count);
    CHECK(set_block(17,Y,3,CHEST) && arm(16,3,DIAMOND,1) && pulse(16,3));
    ChestView c;
    CHECK(chest_open((ChestPos){17,Y,3},&c)==CHEST_OK && !chest_slot(&c,0).count && dropped(DIAMOND)==1);

    CHECK(ejector_place(22,Y,0,SOUTH,DISPENSER) && arm(22,0,FLINT_AND_STEEL,1));
    for (int i=0;i<4;++i) for (int j=-1;j<4;++j)
        if (i==0 || i==3 || j==-1 || j==3) CHECK(set_block(21+i,Y+j,1,OBSIDIAN));
    CHECK(pulse(22,0) && block(22,Y,1)==FIRE_CELL && slot(22,0,0).damage==1);
    world_reset(); drops_reset();
    printf("DISPENSER TEST recipe/aim/arrows/damage/capacity/egg/buckets/heat/boat/cart/fertilizer/armor/ignition/ejection failures=%d\n",failures);
    return failures;
}
