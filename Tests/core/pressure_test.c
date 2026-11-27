#include "../game/actors.h"
#include "../game/boats.h"
#include "../game/drops.h"
#include "../game/minecarts.h"
#include "../world/redstone.h"
#include <stdio.h>
#include <string.h>

#define CHECK_PRESSURE(c) do { if (!(c)) { printf("PRESSURE TEST failed line %d: %s\n",__LINE__,#c); ++failures; } } while (0)
enum { Y=150 };
static void step(int n) { while (n--) redstone_step(-16,0); }
static int level(int x,int z) { return redstone_plate_power(x,Y,z); }
static Drop stack_at(float x,float y,float z) {
    return make_drop(x,y,z,(ItemStack){STONE,64,0,0,0});
}
static int aim_plate(int x,int z) {
    px=x+.5f; py=Y+.01f; pz=z+2.25f; yaw=0;
    for (int i=35;i<80;++i) {
        pitch=-i*.01f;
        Hit h=target();
        if (h.hit && h.x==x && h.y==Y && h.z==z) return 1;
    }
    return 0;
}
int pressure_selftest(void) {
    int failures=0;
    world_begin(651836);
    generate();
    const int materials[]={STONE,PLANKS,GOLD_INGOT,IRON_INGOT};
    for (int i=0;i<4;++i) {
        inventory_reset();
        inventory[0]=(ItemStack){(uint16_t)materials[i],2,0,0,0};
        CHECK_PRESSURE(craft_take((int[]){0,0,-1,-1},2) && inventory[0].type==STONE_PLATE_ITEM+i &&
            inventory[0].count==1 && redstone_plate_item(PRESSURE_BASE+i)==STONE_PLATE_ITEM+i);
    }
    for (int x=-20;x<=0;++x) for (int z=-3;z<=7;++z) CHECK_PRESSURE(set_block(x,Y-1,z,STONE));
    CHECK_PRESSURE(!redstone_place_plate(-16,Y+1,0,PRESSURE_STONE));
    CHECK_PRESSURE(redstone_place_plate(-16,Y,0,PRESSURE_STONE) &&
        redstone_place_plate(-16,Y,4,PRESSURE_WOOD) && redstone_place_plate(-12,Y,0,PRESSURE_GOLD) &&
        redstone_place_plate(-8,Y,0,PRESSURE_IRON));
    /* The lamp is across the chunk seam, one block down, alongside the
       plate's strongly powered support, not alongside the plate itself. */
    CHECK_PRESSURE(set_block(-17,Y-1,0,REDSTONE_LAMP));
    px=-15.5f; py=Y+.01f; pz=2.2f; yaw=0; grounded=0;
    drops[0]=stack_at(-15.5f,Y+.13f,.5f);
    step(1);
    CHECK_PRESSURE(!level(-16,0) && world_cell(-17,Y-1,0)==LAMP_OFF);
    memset(drops,0,sizeof dimension_drops[0]);
    for (int i=0;i<80 && !level(-16,0);++i) game_step(.025f,0,1,0,0);
    CHECK_PRESSURE(pz<1.16f && level(-16,0)==15 && redstone_plate_height(-16,Y,0)==.03125f &&
        world_cell(-17,Y-1,0)==LAMP_ON);
    pz=2.2f;
    step(19);
    CHECK_PRESSURE(level(-16,0)==15);
    step(1);
    CHECK_PRESSURE(level(-16,0)==0 && redstone_plate_height(-16,Y,0)==.0625f &&
        world_cell(-17,Y-1,0)==LAMP_ON);
    step(4);
    CHECK_PRESSURE(world_cell(-17,Y-1,0)==LAMP_OFF);
    CHECK_PRESSURE(actors_restore(OVERWORLD,(ActorRecord){{-15.5f,Y+.01f,.5f},0,ACTOR_PIG,10,0}));
    step(1);
    CHECK_PRESSURE(level(-16,0)==15);
    actors_reset();

    drops[0]=stack_at(-15.5f,Y+.55f,4.5f); /* Above the pressure volume. */
    step(1);
    CHECK_PRESSURE(!level(-16,4));
    drops[0]=stack_at(-15.5f,Y+.13f,4.5f);
    step(1);
    CHECK_PRESSURE(level(-16,4)==15);
    memset(drops,0,sizeof dimension_drops[0]);
    CHECK_PRESSURE(boat_restore(OVERWORLD,(BoatRecord){{-15.5f,Y+.01f,4.5f},0,1}));
    step(20);
    CHECK_PRESSURE(level(-16,4)==15);
    boats_reset();
    CHECK_PRESSURE(cart_restore(OVERWORLD,(CartRecord){{-15.5f,Y+.01f,4.5f},{-16,Y,4},.5f,0,0,1,CART_RIDEABLE,0,0,{0,0,0},0}));
    step(20);
    CHECK_PRESSURE(level(-16,4)==15);
    carts_reset();
    step(20);
    CHECK_PRESSURE(level(-16,4)==0);

    CHECK_PRESSURE(redstone_place_wire(-11,Y,0) && redstone_place_wire(-10,Y,0));
    drops[0]=stack_at(-11.5f,Y+.13f,.5f);
    step(1);
    CHECK_PRESSURE(level(-12,0)==1 && redstone_wire_power(-11,Y,0)==1 && !redstone_wire_power(-10,Y,0));
    for (int i=1;i<16;++i) drops[i]=drops[0];
    step(9);
    CHECK_PRESSURE(level(-12,0)==1);
    step(1);
    CHECK_PRESSURE(level(-12,0)==15 && redstone_wire_power(-10,Y,0)==14);
    CHECK_PRESSURE(world_select_dimension(NETHER) && level(-12,0)==0 &&
        world_select_dimension(OVERWORLD) && level(-12,0)==15);
    memset(drops,0,sizeof dimension_drops[0]);
    for (int i=0;i<10;++i) drops[i]=stack_at(-7.5f,Y+.13f,.5f);
    step(1);
    CHECK_PRESSURE(level(-8,0)==1);
    drops[10]=drops[0];
    step(10);
    CHECK_PRESSURE(level(-8,0)==2);
    for (int i=11;i<MAXDROP;++i) drops[i]=drops[0];
    for (int i=0;i<23;++i) CHECK_PRESSURE(death_drop_restore(OVERWORLD,drops[0]));
    step(10);
    CHECK_PRESSURE(level(-8,0)==15);
    CHECK_PRESSURE(set_block(-8,Y-1,0,AIR));
    step(1);
    CHECK_PRESSURE(block(-8,Y,0)==PRESSURE_PLATE && !level(-8,0)); /* Full visible drop pool. */
    drops[0].item=(ItemStack){0,0,0,0,0};
    step(1);
    CHECK_PRESSURE(block(-8,Y,0)==AIR && drops[0].item.type==IRON_PLATE_ITEM);
    drops_reset();
    inventory_reset();
    selected_slot=0;
    CHECK_PRESSURE(aim_plate(-16,0));
    (void)mine();
    CHECK_PRESSURE(block(-16,Y,0)==AIR && !drops[0].item.count);
    CHECK_PRESSURE(aim_plate(-16,4));
    (void)mine();
    CHECK_PRESSURE(block(-16,Y,4)==AIR && drops[0].item.type==WOOD_PLATE_ITEM);
    inventory[0]=(ItemStack){WOOD_PICKAXE,1,0,0,0};
    CHECK_PRESSURE(aim_plate(-12,0));
    (void)mine();
    CHECK_PRESSURE(block(-12,Y,0)==AIR && drops[1].item.type==GOLD_PLATE_ITEM);
    drops_reset();
    world_reset();
    printf("PRESSURE TEST four-recipes/player/mob/vehicles/stack/contact/hold/cap/seam/isolation/drop failures=%d\n",failures);
    return failures;
}
